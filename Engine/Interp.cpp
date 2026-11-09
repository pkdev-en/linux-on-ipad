// Interp.cpp -- AArch64 interpreter core, see Interp.h.
//
// Semantics follow the ARM ARM; the trickier ones (BFM, CCMP, conditional
// select, extended register arithmetic) were cross-checked against QEMU's
// translate-a64.c and validated by the unicorn oracle tests in tools/.
#include "Interp.h"

#include <cstdio>

namespace lop {

namespace {

inline uint32_t bits(uint32_t v, unsigned hi, unsigned lo) {
    return (v >> lo) & ((hi >= 31) ? 0xFFFFFFFFu : ((1u << (hi - lo + 1)) - 1));
}
inline uint64_t sext(uint64_t v, unsigned width) {
    if (width == 0 || width >= 64) return v;
    uint64_t m = 1ull << (width - 1);
    return (v ^ m) - m;
}

// Register 31 means ZR or SP depending on the instruction context.
inline uint64_t rZ(const Cpu &c, unsigned i, bool sf) {
    uint64_t v = (i == 31) ? 0 : c.x[i];
    return sf ? v : (v & 0xFFFFFFFFull);
}
inline uint64_t rSP(const Cpu &c, unsigned i, bool sf) {
    uint64_t v = (i == 31) ? c.sp : c.x[i];
    return sf ? v : (v & 0xFFFFFFFFull);
}
inline void wZ(Cpu &c, unsigned i, bool sf, uint64_t v) {
    if (i == 31) return; // ZR: discard
    c.x[i] = sf ? v : (v & 0xFFFFFFFFull);
}
inline void wSP(Cpu &c, unsigned i, bool sf, uint64_t v) {
    if (i == 31) c.sp = v;
    else c.x[i] = sf ? v : (v & 0xFFFFFFFFull);
}

inline void setNZ(Cpu &c, uint64_t res, bool sf) {
    c.n = sf ? (res >> 63) & 1 : (res >> 31) & 1;
    c.z = (sf ? res : (res & 0xFFFFFFFFull)) == 0;
}

// ---- add/subtract with carry/overflow -----------------------------------
inline uint64_t flagsAdd(Cpu &c, uint64_t a, uint64_t b, uint64_t carryIn, bool sf) {
    uint64_t res;
    if (sf) {
        __uint128_t sum = (__uint128_t)a + b + carryIn;
        res = (uint64_t)sum;
        c.c = sum >> 64;
        c.v = ((a ^ res) & (b ^ res)) >> 63;
    } else {
        a &= 0xFFFFFFFFull; b &= 0xFFFFFFFFull;
        uint64_t sum = a + b + carryIn;
        res = sum & 0xFFFFFFFFull;
        c.c = sum >> 32;
        c.v = ((a ^ res) & (b ^ res)) >> 31;
    }
    setNZ(c, res, sf);
    return res;
}
inline uint64_t flagsSub(Cpu &c, uint64_t a, uint64_t b, uint64_t borrowIn, bool sf) {
    uint64_t res;
    bool noBorrow;
    if (sf) {
        res = a - b - borrowIn;
        noBorrow = (a >= b) && (a - b >= borrowIn);
        c.v = ((a ^ b) & (a ^ res)) >> 63;
    } else {
        a &= 0xFFFFFFFFull; b &= 0xFFFFFFFFull;
        res = (a - b - borrowIn) & 0xFFFFFFFFull;
        noBorrow = (a >= b) && (a - b >= borrowIn);
        c.v = ((a ^ b) & (a ^ res)) >> 31;
    }
    c.c = noBorrow;
    setNZ(c, res, sf);
    return res;
}
inline void flagsLogical(Cpu &c, uint64_t res, bool sf) {
    c.c = false;
    c.v = false;
    setNZ(c, res, sf);
}

// ---- shifts / extends -----------------------------------------------------
inline uint64_t shiftValue(uint64_t val, unsigned type, unsigned amt, bool sf) {
    unsigned width = sf ? 64 : 32;
    switch (type) {
    case 0: return amt < width ? val << amt : 0;                              // lsl
    case 1: return amt < width ? val >> amt : 0;                              // lsr
    case 2: {                                                                 // asr
        if (amt >= width) {
            bool sign = sf ? (val >> 63) & 1 : (val >> 31) & 1;
            return sign ? ~0ull : 0;
        }
        return sf ? (uint64_t)((int64_t)val >> amt) : (uint64_t)((int32_t)val >> amt);
    }
    default: {                                                                // ror
        amt %= width;
        if (amt == 0) return val;
        return (val >> amt) | (val << (width - amt));
    }
    }
}

// extend type: 000 uxtb, 001 uxth, 010 uxtw, 011 uxtx, 100 sxtb, 101 sxth, 110 sxtw, 111 sxtx
inline uint64_t extendValue(uint64_t rm, unsigned extend, bool sf) {
    switch (extend) {
    case 0: return rm & 0xFFull;
    case 1: return rm & 0xFFFFull;
    case 2: return rm & 0xFFFFFFFFull;
    case 3: return sf ? rm : (rm & 0xFFFFFFFFull);
    case 4: return (uint64_t)(int64_t)(int8_t)(rm & 0xFFull);
    case 5: return (uint64_t)(int64_t)(int16_t)(rm & 0xFFFFull);
    case 6: return (uint64_t)(int64_t)(int32_t)(rm & 0xFFFFFFFFull);
    default: return sf ? rm : (uint64_t)(int64_t)(int32_t)(rm & 0xFFFFFFFFull);
    }
}

inline bool condHolds(const Cpu &c, unsigned cond) {
    // Base truth for the even condition, inverted by the low bit (ne/lt/...).
    bool result;
    switch (cond >> 1) {
    case 0: result = c.z; break;                                  // eq
    case 1: result = c.c; break;                                  // cs/hs
    case 2: result = c.n; break;                                  // mi
    case 3: result = c.v; break;                                  // vs
    case 4: result = c.c && !c.z; break;                          // hi
    case 5: result = c.n == c.v; break;                           // ge
    case 6: result = !c.z && c.n == c.v; break;                   // gt
    default: result = true; break;                                // al/nv
    }
    return result ^ bool(cond & 1);
}

inline uint64_t rotl(uint64_t v, unsigned n) { return (v << n) | (v >> (64 - n)); }

inline uint64_t countLeading(bool ones, uint64_t v, unsigned width, bool sf) {
    uint64_t count = 0;
    uint64_t mask = 1ull << (width - 1);
    for (unsigned i = 0; i < width; ++i, mask >>= 1) {
        bool bit = (v & mask) != 0;
        if (bit != ones) break;
        ++count;
    }
    (void)sf;
    return count;
}

// BFM family, mirroring QEMU's trans_SBFM/trans_UBFM/trans_BFM exactly.
void doBitfield(Cpu &cpu, const Decoded &d, unsigned kind /*0 sbfm, 1 bfm, 2 ubfm*/) {
    unsigned bitsize = d.sf ? 64 : 32;
    unsigned ri = d.shiftAmt;   // immr
    unsigned si = d.shiftType;  // imms
    uint64_t src = rZ(cpu, d.rn, true);
    uint64_t result;

    if (si >= ri) {
        unsigned len = (si - ri) + 1;
        uint64_t field = (src >> ri) & ((len == 64) ? ~0ull : ((1ull << len) - 1));
        if (kind == 0 && len < 64 && ((field >> (len - 1)) & 1)) {
            field |= ~0ull << len; // sign-extend
        }
        if (kind == 1) {
            uint64_t dst = rZ(cpu, d.rd, true);
            uint64_t keep = (len == 64) ? 0 : (~0ull << len);
            if (!d.sf) keep |= ~0xFFFFFFFFull;
            result = (dst & keep) | field;
        } else {
            result = field;
        }
    } else {
        unsigned len = si + 1;
        unsigned pos = (bitsize - ri) & (bitsize - 1);
        uint64_t field = src;
        if (kind == 0 && len < ri) {
            // sign-fill up to ri bits before depositing
            uint64_t low = src & ((1ull << len) - 1);
            if ((low >> (len - 1)) & 1) low |= (ri == 64) ? (~0ull << len) : ((1ull << ri) - 1) & ~((1ull << len) - 1);
            field = low;
            len = ri;
        }
        uint64_t mask = (len == 64) ? ~0ull : ((1ull << len) - 1);
        uint64_t payload = (field & mask) << pos;
        if (kind == 1) {
            uint64_t keep = ~(mask << pos);
            if (!d.sf) keep |= ~0xFFFFFFFFull;
            result = (rZ(cpu, d.rd, true) & keep) | payload;
        } else {
            result = payload;
        }
    }
    wZ(cpu, d.rd, d.sf, result);
}

} // namespace

Step step(Cpu &cpu, Bus &bus, uint64_t pc, const Decoded &d) {
    bool sf = d.sf;
    uint64_t nextPc = pc + 4;

    switch (d.op) {
    case Op::Unimplemented:
        cpu.badInsn = 0; // caller fills from the raw word
        return Step::Unimplemented;

    case Op::Nop:
    case Op::Barrier:
        break;

    // ---- move wide --------------------------------------------------------
    case Op::MovZ: {
        uint64_t v = (uint64_t)d.imm << d.shiftAmt;
        wZ(cpu, d.rd, sf, v);
        break;
    }
    case Op::MovN: {
        uint64_t v = ~(uint64_t(d.imm) << d.shiftAmt);
        wZ(cpu, d.rd, sf, v);
        break;
    }
    case Op::MovK: {
        uint64_t v = rZ(cpu, d.rd, true);
        uint64_t shift = d.shiftAmt;
        uint64_t mask = 0xFFFFull << shift;
        v = (v & ~mask) | ((uint64_t)d.imm << shift);
        if (sf) wZ(cpu, d.rd, true, v);
        else wZ(cpu, d.rd, false, (v & 0xFFFFFFFFull));
        break;
    }

    // ---- add/subtract immediate -------------------------------------------
    case Op::AddImm:  wSP(cpu, d.rd, sf, rSP(cpu, d.rn, sf) + d.imm); break;
    case Op::SubImm:  wSP(cpu, d.rd, sf, rSP(cpu, d.rn, sf) - d.imm); break;
    case Op::AddsImm: wZ(cpu, d.rd, sf, flagsAdd(cpu, rSP(cpu, d.rn, sf), d.imm, 0, sf)); break;
    case Op::SubsImm: wZ(cpu, d.rd, sf, flagsSub(cpu, rSP(cpu, d.rn, sf), d.imm, 0, sf)); break;

    // ---- add/subtract shifted register ------------------------------------
    case Op::AddReg: case Op::AddsReg: case Op::SubReg: case Op::SubsReg: {
        uint64_t a = rZ(cpu, d.rn, sf); // ZR context: Rn=31 -> ZR here
        uint64_t b = shiftValue(rZ(cpu, d.rm, sf), d.shiftType, d.shiftAmt, sf);
        switch (d.op) {
        case Op::AddReg:  wZ(cpu, d.rd, sf, a + b); break;
        case Op::SubReg:  wZ(cpu, d.rd, sf, a - b); break;
        case Op::AddsReg: wZ(cpu, d.rd, sf, flagsAdd(cpu, a, b, 0, sf)); break;
        default:          wZ(cpu, d.rd, sf, flagsSub(cpu, a, b, 0, sf)); break;
        }
        break;
    }

    // ---- add/subtract extended register -----------------------------------
    case Op::AddExt: case Op::AddsExt: case Op::SubExt: case Op::SubsExt: {
        uint64_t a = rSP(cpu, d.rn, sf); // Rn=31 -> SP in this group
        uint64_t b = extendValue(rZ(cpu, d.rm, true), d.extend, sf) << d.shiftAmt;
        switch (d.op) {
        case Op::AddExt:  wZ(cpu, d.rd, sf, a + b); break;
        case Op::SubExt:  wZ(cpu, d.rd, sf, a - b); break;
        case Op::AddsExt: wZ(cpu, d.rd, sf, flagsAdd(cpu, a, b, 0, sf)); break;
        default:          wZ(cpu, d.rd, sf, flagsSub(cpu, a, b, 0, sf)); break;
        }
        break;
    }

    // ---- logical (register / immediate) -----------------------------------
    case Op::AndReg: case Op::OrrReg: case Op::EorReg: case Op::AndsReg: {
        uint64_t a = rZ(cpu, d.rn, sf);
        uint64_t b = shiftValue(rZ(cpu, d.rm, sf), d.shiftType, d.shiftAmt, sf);
        if (d.invertRm) b = ~b;
        if (!sf) { a &= 0xFFFFFFFFull; b &= 0xFFFFFFFFull; }
        uint64_t r;
        switch (d.op) {
        case Op::AndReg: r = a & b; break;
        case Op::OrrReg: r = a | b; break;
        case Op::EorReg: r = a ^ b; break;
        default: r = a & b; break;
        }
        if (d.op == Op::AndsReg) flagsLogical(cpu, r, sf);
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::AndImm: case Op::OrrImm: case Op::EorImm: case Op::AndsImm: {
        uint64_t a = rZ(cpu, d.rn, sf);
        uint64_t r;
        switch (d.op) {
        case Op::AndImm: r = a & d.imm; break;
        case Op::OrrImm: r = a | d.imm; break;
        case Op::EorImm: r = a ^ d.imm; break;
        default: r = a & d.imm; break;
        }
        if (d.op == Op::AndsImm) flagsLogical(cpu, r, sf);
        wZ(cpu, d.rd, sf, r);
        break;
    }

    // ---- carry --------------------------------------------------------------
    case Op::Adc:  wZ(cpu, d.rd, sf, rZ(cpu, d.rn, sf) + rZ(cpu, d.rm, sf) + (cpu.c ? 1 : 0)); break;
    case Op::Adcs: wZ(cpu, d.rd, sf, flagsAdd(cpu, rZ(cpu, d.rn, sf), rZ(cpu, d.rm, sf), cpu.c ? 1 : 0, sf)); break;
    case Op::Sbc:  wZ(cpu, d.rd, sf, rZ(cpu, d.rn, sf) - rZ(cpu, d.rm, sf) - (cpu.c ? 0 : 1)); break;
    case Op::Sbcs: wZ(cpu, d.rd, sf, flagsSub(cpu, rZ(cpu, d.rn, sf), rZ(cpu, d.rm, sf), cpu.c ? 0 : 1, sf)); break;

    // ---- conditional select / compare --------------------------------------
    case Op::Csel: {
        uint64_t r = condHolds(cpu, d.cond) ? rZ(cpu, d.rn, sf) : rZ(cpu, d.rm, sf);
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Csinc: {
        uint64_t r = condHolds(cpu, d.cond) ? rZ(cpu, d.rn, sf) : rZ(cpu, d.rm, sf) + 1;
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Csinv: {
        uint64_t r = condHolds(cpu, d.cond) ? rZ(cpu, d.rn, sf) : ~rZ(cpu, d.rm, sf);
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Csneg: {
        uint64_t r = condHolds(cpu, d.cond) ? rZ(cpu, d.rn, sf) : 0 - rZ(cpu, d.rm, sf);
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::CcmpReg: case Op::CcmpImm: case Op::CcmnReg: case Op::CcmnImm: {
        if (condHolds(cpu, d.cond)) {
            bool isSub = (d.op == Op::CcmpReg || d.op == Op::CcmpImm);
            uint64_t b = (d.op == Op::CcmpReg || d.op == Op::CcmnReg)
                             ? rZ(cpu, d.rm, sf) : d.imm;
            if (isSub) flagsSub(cpu, rZ(cpu, d.rn, sf), b, 0, sf);
            else flagsAdd(cpu, rZ(cpu, d.rn, sf), b, 0, sf);
        } else {
            cpu.setNzcvNibble(d.nzcv);
        }
        break;
    }

    // ---- multiply family -----------------------------------------------------
    case Op::Madd: {
        uint64_t r = (uint64_t)((__int128_t)(__int64_t)rZ(cpu, d.rn, sf) *
                                (__int128_t)(__int64_t)rZ(cpu, d.rm, sf) +
                                (__int128_t)rZ(cpu, d.ra, sf));
        if (!sf) {
            uint32_t r32 = uint32_t(rZ(cpu, d.rn, sf) & 0xFFFFFFFF) *
                           uint32_t(rZ(cpu, d.rm, sf) & 0xFFFFFFFF) +
                           uint32_t(rZ(cpu, d.ra, sf) & 0xFFFFFFFF);
            wZ(cpu, d.rd, false, r32);
        } else {
            wZ(cpu, d.rd, true, r);
        }
        break;
    }
    case Op::Msub: {
        if (!sf) {
            uint32_t r32 = uint32_t(rZ(cpu, d.ra, sf) & 0xFFFFFFFF) -
                           uint32_t(rZ(cpu, d.rn, sf) & 0xFFFFFFFF) *
                               uint32_t(rZ(cpu, d.rm, sf) & 0xFFFFFFFF);
            wZ(cpu, d.rd, false, r32);
        } else {
            wZ(cpu, d.rd, true, rZ(cpu, d.ra, sf) - rZ(cpu, d.rn, sf) * rZ(cpu, d.rm, sf));
        }
        break;
    }
    case Op::Smulh: {
        __int128_t p = (__int128_t)(__int64_t)rZ(cpu, d.rn, true) *
                       (__int128_t)(__int64_t)rZ(cpu, d.rm, true);
        wZ(cpu, d.rd, true, (uint64_t)(p >> 64));
        break;
    }
    case Op::Umulh: {
        unsigned __int128 p = (unsigned __int128)rZ(cpu, d.rn, true) *
                              (unsigned __int128)rZ(cpu, d.rm, true);
        wZ(cpu, d.rd, true, (uint64_t)(p >> 64));
        break;
    }
    case Op::Smaddl: wZ(cpu, d.rd, true, rZ(cpu, d.ra, true) +
                        (uint64_t)(int64_t)(int32_t)rZ(cpu, d.rn, false) *
                            (int64_t)(int32_t)rZ(cpu, d.rm, false)); break;
    case Op::Umsubl: wZ(cpu, d.rd, true, rZ(cpu, d.ra, true) -
                        (uint64_t)(uint32_t)rZ(cpu, d.rn, false) *
                            (uint64_t)(uint32_t)rZ(cpu, d.rm, false)); break;
    case Op::Smsubl: wZ(cpu, d.rd, true, rZ(cpu, d.ra, true) -
                        (uint64_t)((int64_t)(int32_t)rZ(cpu, d.rn, false) *
                                   (int64_t)(int32_t)rZ(cpu, d.rm, false))); break;
    case Op::Umaddl: wZ(cpu, d.rd, true, rZ(cpu, d.ra, true) +
                        (uint64_t)(uint32_t)rZ(cpu, d.rn, false) *
                            (uint64_t)(uint32_t)rZ(cpu, d.rm, false)); break;

    // ---- divide / variable shifts --------------------------------------------
    case Op::Udiv: {
        uint64_t a = rZ(cpu, d.rn, sf), b = rZ(cpu, d.rm, sf);
        uint64_t r = 0;
        if (!sf) {
            uint32_t bb = uint32_t(b);
            r = bb ? uint32_t(a) / bb : 0;
        } else {
            r = b ? a / b : 0;
        }
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Sdiv: {
        uint64_t r = 0;
        if (!sf) {
            int32_t bb = (int32_t)rZ(cpu, d.rm, sf);
            r = bb ? (uint64_t)(uint32_t)((int32_t)rZ(cpu, d.rn, sf) / bb) : 0;
        } else {
            int64_t aa = (int64_t)rZ(cpu, d.rn, sf), bb = (int64_t)rZ(cpu, d.rm, sf);
            r = bb ? (uint64_t)(aa / bb) : 0;
            if (bb == -1) r = (uint64_t)-aa; // INT64_MIN / -1 wraps
        }
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Lslv: case Op::Lsrv: case Op::Asrv: case Op::Rorv: {
        // variable shifts take the amount modulo the datasize
        unsigned amt = unsigned(rZ(cpu, d.rm, sf)) & (sf ? 63u : 31u);
        unsigned type = d.op == Op::Lslv ? 0u : d.op == Op::Lsrv ? 1u
                      : d.op == Op::Asrv ? 2u : 3u;
        wZ(cpu, d.rd, sf, shiftValue(rZ(cpu, d.rn, sf), type, amt, sf));
        break;
    }

    // ---- 1-source ---------------------------------------------------------------
    case Op::Rbit: {
        uint64_t v = rZ(cpu, d.rn, sf), r = 0;
        unsigned width = sf ? 64u : 32u;
        for (unsigned i = 0; i < width; ++i)
            if ((v >> i) & 1) r |= 1ull << (width - 1 - i);
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Rev: {
        uint64_t v = rZ(cpu, d.rn, sf), r = 0;
        unsigned width = sf ? 8 : 4;
        for (unsigned i = 0; i < width; ++i) {
            uint64_t byte = (v >> (8 * i)) & 0xFF;
            r |= byte << (8 * (width - 1 - i));
        }
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Rev16: {
        uint64_t v = rZ(cpu, d.rn, sf), r = 0;
        for (unsigned i = 0; i < (sf ? 4u : 2u); ++i) {
            uint64_t h = (v >> (16 * i)) & 0xFFFF;
            r |= uint64_t(((h >> 8) | ((h & 0xFF) << 8)) & 0xFFFF) << (16 * i);
        }
        wZ(cpu, d.rd, sf, r);
        break;
    }
    case Op::Rev32: {
        uint64_t v = rZ(cpu, d.rn, true), r = 0;
        for (unsigned i = 0; i < 2; ++i) {
            uint64_t w = (v >> (32 * i)) & 0xFFFFFFFF;
            w = ((w & 0xFF) << 24) | ((w & 0xFF00) << 8) | ((w >> 8) & 0xFF00) | ((w >> 24) & 0xFF);
            r |= w << (32 * i);
        }
        wZ(cpu, d.rd, sf, r & (sf ? ~0ull : 0xFFFFFFFFull));
        break;
    }
    case Op::Clz: {
        uint64_t v = rZ(cpu, d.rn, sf);
        unsigned width = sf ? 64 : 32;
        wZ(cpu, d.rd, sf, countLeading(false, v, width, sf));
        break;
    }
    case Op::Cls: {
        // CLS does not count the sign bit itself (max = width-1)
        uint64_t v = rZ(cpu, d.rn, sf);
        unsigned width = sf ? 64 : 32;
        bool sign = (v >> (width - 1)) & 1;
        uint64_t count = 0;
        uint64_t mask = 1ull << (width - 2);
        for (unsigned i = 0; i + 1 < width; ++i, mask >>= 1) {
            if (((v & mask) != 0) != sign) break;
            ++count;
        }
        wZ(cpu, d.rd, sf, count);
        break;
    }

    // ---- pc-relative -----------------------------------------------------------
    case Op::Adr:  wZ(cpu, d.rd, sf, pc + d.imm); break;
    case Op::Adrp: wZ(cpu, d.rd, sf, (pc & ~0xFFFull) + d.imm); break;

    // ---- bitfield / extract ------------------------------------------------------
    case Op::Sbfm: doBitfield(cpu, d, 0); break;
    case Op::Bfm:  doBitfield(cpu, d, 1); break;
    case Op::Ubfm: doBitfield(cpu, d, 2); break;
    case Op::Extr: {
        unsigned lsb = d.shiftAmt;
        uint64_t hi = rZ(cpu, d.rn, sf), lo = rZ(cpu, d.rm, sf);
        unsigned width = sf ? 64 : 32;
        uint64_t r = lsb == 0 ? lo : ((hi << (width - lsb)) | (lo >> lsb));
        if (!sf) r &= 0xFFFFFFFFull;
        wZ(cpu, d.rd, sf, r);
        break;
    }

    // ---- branches -------------------------------------------------------------
    case Op::B:  nextPc = pc + d.imm; break;
    case Op::Bl:
        wZ(cpu, 30, true, pc + 4);
        nextPc = pc + d.imm;
        break;
    case Op::BCond:
        if (condHolds(cpu, d.cond)) nextPc = pc + d.imm;
        break;
    case Op::Cbz:
        if ((sf ? rZ(cpu, d.rd, true) : (rZ(cpu, d.rd, false))) == 0) nextPc = pc + d.imm;
        break;
    case Op::Cbnz:
        if ((sf ? rZ(cpu, d.rd, true) : (rZ(cpu, d.rd, false))) != 0) nextPc = pc + d.imm;
        break;
    case Op::Tbz:
        if (((rZ(cpu, d.rd, sf) >> d.shiftAmt) & 1) == 0) nextPc = pc + d.imm;
        break;
    case Op::Tbnz:
        if (((rZ(cpu, d.rd, sf) >> d.shiftAmt) & 1) == 1) nextPc = pc + d.imm;
        break;
    case Op::Br:  nextPc = rZ(cpu, d.rn, true); break;
    case Op::Blr:
        wZ(cpu, 30, true, pc + 4);
        nextPc = rZ(cpu, d.rn, true);
        break;
    case Op::Ret: nextPc = rZ(cpu, d.rn, true); break;
    case Op::Hlt:
        cpu.stopImm = uint32_t(d.imm);
        cpu.pc = nextPc;
        return Step::Halt;
    case Op::Svc:
        cpu.stopImm = uint32_t(d.imm);
        cpu.pc = nextPc;
        return Step::Svc;

    // ---- system registers --------------------------------------------------------
    case Op::Mrs: {
        uint32_t id = d.sysregId;
        uint64_t v;
        if (id == sysregId(3, 3, 4, 2, 0)) v = uint64_t(cpu.nzcvNibble()) << 28; // NZCV
        else if (id == sysregId(3, 0, 4, 2, 2)) v = 4;                           // CurrentEL (EL1)
        else if (id == sysregId(3, 0, 0, 0, 0)) v = 0x410FD083ull;               // MIDR_EL1
        else if (id == sysregId(3, 0, 0, 0, 5)) v = 0x80000000ull;               // MPIDR_EL1
        else if (id == sysregId(3, 3, 14, 0, 0)) v = 24000000;                   // CNTFRQ_EL0
        else if (id == sysregId(3, 3, 14, 0, 1) || id == sysregId(3, 3, 14, 0, 2))
            v = cpu.insnCount * 24;                                              // CNT[P/V]CT_EL0
        else if (id == sysregId(3, 3, 4, 2, 1)) v = cpu.daif;                    // DAIF
        else v = cpu.readSysreg(id);
        wZ(cpu, d.rd, sf, v);
        break;
    }
    case Op::Msr: {
        uint32_t id = d.sysregId;
        if (d.msrImm) {
            unsigned op1 = (id >> 17) & 7, crn = (id >> 12) & 15;
            unsigned crm = (id >> 8) & 15, op2 = (id >> 5) & 7;
            if (op1 == 3 && crn == 4 && op2 == 6) cpu.daif |= (crm & 0xF) << 6;       // DAIFSET
            else if (op1 == 3 && crn == 4 && op2 == 7) cpu.daif &= ~((crm & 0xF) << 6); // DAIFCLR
            else if (op1 == 0 && crn == 4 && crm == 0 && op2 == 5) {}                  // SPSel: ignore
            else cpu.writeSysreg(id, d.imm);
        } else {
            uint64_t v = rZ(cpu, d.rd, sf);
            if (id == sysregId(3, 3, 4, 2, 0)) cpu.setNzcvNibble(uint32_t(v >> 28));  // NZCV
            else if (id == sysregId(3, 0, 4, 2, 1)) cpu.daif = uint32_t(v & 0x3C0);   // DAIF
            else cpu.writeSysreg(id, v);
        }
        break;
    }

    // ---- loads/stores -----------------------------------------------------------
    default: {
        uint64_t addr = 0;
        switch (d.addrMode) {
        case AddrMode::Literal:   addr = pc + d.imm; break;
        case AddrMode::Offset:    addr = rSP(cpu, d.rn, true) + d.imm; break;
        case AddrMode::PreIndex:  addr = rSP(cpu, d.rn, true) + d.imm; break;
        case AddrMode::PostIndex: addr = rSP(cpu, d.rn, true); break;
        case AddrMode::RegOffset: {
            uint64_t base = rSP(cpu, d.rn, true);
            uint64_t off;
            switch (d.extend) {
            case 0b010: off = rZ(cpu, d.rm, true) & 0xFFFFFFFFull; break;
            case 0b011: off = (sf ? rZ(cpu, d.rm, true) : (rZ(cpu, d.rm, true) & 0xFFFFFFFFull)) << d.shiftAmt; break;
            case 0b110: off = (uint64_t)(int64_t)(int32_t)(rZ(cpu, d.rm, true) & 0xFFFFFFFFull); break;
            default: {
                uint64_t v = d.extend == 0b111 ? (uint64_t)(int64_t)rZ(cpu, d.rm, true)
                                               : ((rZ(cpu, d.rm, true) & 0xFFFFFFFFull));
                if (d.extend == 0b111 && !sf) v = (uint64_t)(int64_t)(int32_t)v;
                off = v << d.shiftAmt;
                break;
            }
            }
            addr = base + off;
            break;
        }
        }

        uint64_t fault = 0;
        switch (d.op) {
        case Op::Str: case Op::Strb: case Op::Strh: {
            uint64_t v = rZ(cpu, d.rd, sf);
            if (!bus.store(addr, 1u << d.size, v, &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            break;
        }
        case Op::Stp: case Op::Stxp: {
            uint64_t step = 1ull << d.size;
            if (!bus.store(addr, 1u << d.size, rZ(cpu, d.rd, sf), &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            if (!bus.store(addr + step, 1u << d.size, rZ(cpu, d.ra, sf), &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            if (d.op == Op::Stxp) wZ(cpu, d.rm, false, 0); // STXP status: always success
            break;
        }
        case Op::Stxr: {
            if (!bus.store(addr, 1u << d.size, rZ(cpu, d.rd, sf), &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            wZ(cpu, d.rm, false, 0); // STXR status register: success
            break;
        }
        case Op::Ldr: case Op::Ldrb: case Op::Ldrh: {
            uint64_t v;
            if (!bus.load(addr, 1u << d.size, &v, &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            wZ(cpu, d.rd, sf, v);
            break;
        }
        case Op::Ldrsb: case Op::Ldrsh: case Op::Ldrsw: {
            uint64_t v;
            if (!bus.load(addr, 1u << d.size, &v, &fault)) {
                cpu.faultAddr = fault;
                return Step::DataAbort;
            }
            unsigned bits = 8u << d.size;
            v = uint64_t(sext(v, bits));
            wZ(cpu, d.rd, sf, v);
            break;
        }
        case Op::Ldp: case Op::Ldxp: {
            uint64_t step = 1ull << d.size, a, b;
            if (!bus.load(addr, 1u << d.size, &a, &fault)) { cpu.faultAddr = fault; return Step::DataAbort; }
            if (!bus.load(addr + step, 1u << d.size, &b, &fault)) { cpu.faultAddr = fault; return Step::DataAbort; }
            wZ(cpu, d.rd, sf, a);
            wZ(cpu, d.ra, sf, b);
            break;
        }
        case Op::LdpSw: {
            uint64_t step = 4, a, b;
            if (!bus.load(addr, 4, &a, &fault)) { cpu.faultAddr = fault; return Step::DataAbort; }
            if (!bus.load(addr + step, 4, &b, &fault)) { cpu.faultAddr = fault; return Step::DataAbort; }
            wZ(cpu, d.rd, sf, sext(a, 32));
            wZ(cpu, d.ra, sf, sext(b, 32));
            break;
        }
        case Op::Ldxr: {
            uint64_t v;
            if (!bus.load(addr, 1u << d.size, &v, &fault)) { cpu.faultAddr = fault; return Step::DataAbort; }
            wZ(cpu, d.rd, sf, v);
            break;
        }
        default:
            cpu.badInsn = 0;
            return Step::Unimplemented;
        }

        if (d.writeback) {
            uint64_t wb = (d.addrMode == AddrMode::PreIndex) ? addr
                                                             : addr + d.imm;
            wSP(cpu, d.rn, true, wb);
        }
        break;
    }
    }

    cpu.pc = nextPc;
    return Step::Continue;
}

} // namespace lop
