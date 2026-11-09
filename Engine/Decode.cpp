// Decode.cpp -- AArch64 decoder implementation, see Decode.h.
//
// Structure follows the ARM ARM encoding groups. Field extraction uses the
// numeric layouts verified against keystone/LLVM ground truth:
//   - data-processing (register) group: bits28-24 = 11010, sub-op = (op>>21)&0x1FF
//   - load/store single: bits29-27 = 111 (unsigned: bits25-24=01, advanced: 00)
//   - load/store pair:   bits29-27 = 101
//   - literal:           bits29-27 = 011
//   - exclusive/ordered: bits28-24 = 01000
//   - branches/EXTR:     mixed, matched by explicit masks
#include "Decode.h"
#include "Cpu.h"

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

// ARM ARM DecodeBitMasks(): expand (N, immr, imms) into a logical-immediate.
bool decodeBitmasks(bool is64, uint32_t N, uint32_t immr, uint32_t imms, uint64_t *out) {
    uint32_t inv = (~imms) & 0x3F;
    uint32_t cat = (N << 6) | inv;
    if (cat == 0) return false;
    uint32_t len = 31u - __builtin_clz(cat);
    if (!is64 && N) return false; // 32-bit form requires N == 0
    if (!is64 && len > 5) return false;
    uint32_t levels = (1u << len) - 1;
    uint32_t S = imms & levels;
    uint32_t R = immr & levels;
    if (S == levels) return false;
    uint32_t esize = 1u << len;
    uint64_t welem = (S + 1 == esize) ? ~0ull : ((1ull << (S + 1)) - 1);
    uint64_t elem;
    if (R == 0) {
        elem = welem;
    } else {
        elem = (welem >> R) | ((welem & ((1ull << R) - 1)) << (esize - R));
    }
    uint64_t imm = 0;
    for (uint32_t i = 0; i < 64 / esize; ++i) imm |= elem << (i * esize);
    *out = is64 ? imm : (imm & 0xFFFFFFFFull);
    return true;
}

// Map opc/size to a single-register load/store op.
bool classifySingle(uint32_t size, uint32_t opc, Op *out) {
    switch (opc) {
    case 0: // store
        *out = size == 0 ? Op::Strb : size == 1 ? Op::Strh : Op::Str;
        return true;
    case 1: // load
        *out = size == 0 ? Op::Ldrb : size == 1 ? Op::Ldrh : Op::Ldr;
        return true;
    case 2: // sign-extending load
        if (size == 2) { *out = Op::Ldrsw; return true; }
        if (size == 0) { *out = Op::Ldrsb; return true; }
        if (size == 1) { *out = Op::Ldrsh; return true; }
        return false;
    default:
        return false; // opc 11 = PRFM (unsigned) or SIMD
    }
}

Decoded finishSingle(Decoded d, uint32_t size, Op op, bool signedLoad) {
    d.op = op;
    d.size = uint8_t(size);
    d.signedLoad = signedLoad;
    return d;
}

} // namespace

Decoded decode(uint32_t op) {
    Decoded d;
    d.sf = op >> 31;
    d.rd = uint8_t(bits(op, 4, 0));
    d.rn = uint8_t(bits(op, 9, 5));
    d.rm = uint8_t(bits(op, 20, 16));

    // ---- system / hints / barriers --------------------------------------
    if (op == 0xD503201F) { d.op = Op::Nop; return d; }
    if ((op >> 12) == 0xD50330) { // DSB/DMB/ISB/CLREX/...
        d.op = Op::Barrier;
        return d;
    }
    if ((op & 0xFFFFF01F) == 0xD503201F) { // remaining hints (YIELD/WFE/WFI/SEV...)
        d.op = Op::Nop;
        return d;
    }

    // ---- pc-relative ADR / ADRP -------------------------------------------
    if ((op & 0x9F000000) == 0x90000000) { // ADRP
        d.op = Op::Adrp;
        uint64_t imm = sext((bits(op, 23, 5) << 2) | bits(op, 30, 29), 21);
        d.imm = imm << 12;
        return d;
    }
    if ((op & 0x9F000000) == 0x10000000) { // ADR
        d.op = Op::Adr;
        d.imm = sext((bits(op, 23, 5) << 2) | bits(op, 30, 29), 21);
        return d;
    }

    // ---- move wide -------------------------------------------------------
    if ((op & 0x1F800000) == 0x12800000) {
        uint32_t opc = (op >> 29) & 3;
        if (opc == 1) { d.op = Op::Unimplemented; return d; }
        d.op = opc == 0 ? Op::MovN : opc == 2 ? Op::MovZ : Op::MovK;
        d.shiftAmt = uint8_t(bits(op, 22, 21) * 16); // hw << 4
        d.imm = opc == 0 ? sext(bits(op, 20, 5), 16) : uint64_t(bits(op, 20, 5));
        return d;
    }

    // ---- add/subtract immediate -----------------------------------------
    if ((op & 0x1F800000) == 0x11000000) {
        switch ((op >> 29) & 3) {
        case 0: d.op = Op::AddImm; break;
        case 1: d.op = Op::AddsImm; break;
        case 2: d.op = Op::SubImm; break;
        default: d.op = Op::SubsImm; break;
        }
        d.imm = bits(op, 21, 10);
        if (bits(op, 22, 22)) d.imm <<= 12;
        return d;
    }

    // ---- logical immediate (bits28-23 = 100100, opc in bits30-29) ---------
    if ((op & 0x1F800000) == 0x12000000) {
        uint64_t imm;
        if (!decodeBitmasks(d.sf, bits(op, 22, 22), bits(op, 21, 16), bits(op, 15, 10), &imm)) {
            d.op = Op::Unimplemented;
            return d;
        }
        switch ((op >> 29) & 3) {
        case 0: d.op = Op::AndImm; break;
        case 1: d.op = Op::OrrImm; break;
        case 2: d.op = Op::EorImm; break;
        default: d.op = Op::AndsImm; break;
        }
        d.imm = imm;
        return d;
    }

    // ---- bitfield move ----------------------------------------------------
    if ((op & 0x1F800000) == 0x13000000) {
        switch ((op >> 29) & 3) {
        case 0: d.op = Op::Sbfm; break;
        case 1: d.op = Op::Bfm; break;
        case 2: d.op = Op::Ubfm; break;
        default: d.op = Op::Unimplemented; return d;
        }
        d.shiftAmt = uint8_t(bits(op, 21, 16));  // immr
        d.shiftType = uint8_t(bits(op, 15, 10)); // imms
        return d;
    }

    // ---- EXTR -------------------------------------------------------------
    if ((op & 0x7F800000) == 0x13800000) {
        d.op = Op::Extr;
        d.shiftAmt = uint8_t(bits(op, 15, 10)); // lsb
        return d;
    }

    // ---- data-processing (register), bits28-24 = 11010 --------------------
    // key = bits30-21 (verified against keystone ground truth)
    if ((op & 0x1F000000) == 0x1A000000) {
        uint32_t key = (op >> 21) & 0x3FF;
        switch (key) {
        case 0x0D0: d.op = Op::Adc;  return d;
        case 0x1D0: d.op = Op::Adcs; return d;
        case 0x2D0: d.op = Op::Sbc;  return d;
        case 0x3D0: d.op = Op::Sbcs; return d;
        case 0x0D4: // CSEL (bit10=0) / CSINC (bit10=1), cond in bits15-12
            d.op = bits(op, 10, 10) ? Op::Csinc : Op::Csel;
            d.cond = uint8_t(bits(op, 15, 12));
            return d;
        case 0x2D4: // CSINV (bit10=0) / CSNEG (bit10=1)
            d.op = bits(op, 10, 10) ? Op::Csneg : Op::Csinv;
            d.cond = uint8_t(bits(op, 15, 12));
            return d;
        case 0x1D2: // CCMN register (bit11=0) / immediate (bit11=1)
            d.op = bits(op, 11, 11) ? Op::CcmnImm : Op::CcmnReg;
            d.cond = uint8_t(bits(op, 15, 12));
            d.nzcv = uint8_t(bits(op, 3, 0));
            d.imm = bits(op, 20, 16);
            return d;
        case 0x3D2: // CCMP register / immediate
            d.op = bits(op, 11, 11) ? Op::CcmpImm : Op::CcmpReg;
            d.cond = uint8_t(bits(op, 15, 12));
            d.nzcv = uint8_t(bits(op, 3, 0));
            d.imm = bits(op, 20, 16);
            return d;
        case 0x0D6: // 2-source: opcode in bits15-10
            switch (bits(op, 15, 10)) {
            case 0b000010: d.op = Op::Udiv; return d;
            case 0b000011: d.op = Op::Sdiv; return d;
            case 0b001000: d.op = Op::Lslv; return d;
            case 0b001001: d.op = Op::Lsrv; return d;
            case 0b001010: d.op = Op::Asrv; return d;
            case 0b001011: d.op = Op::Rorv; return d;
            default: d.op = Op::Unimplemented; return d;
            }
        case 0x2D6: // 1-source: opcode in bits15-10
            switch (bits(op, 15, 10)) {
            case 0b000000: d.op = Op::Rbit;  return d;
            case 0b000001: d.op = Op::Rev16; return d;
            case 0b000010: d.op = d.sf ? Op::Rev32 : Op::Rev; return d;
            case 0b000011:
                if (!d.sf) break;
                d.op = Op::Rev; // REV on 64-bit registers
                return d;
            case 0b000100: d.op = Op::Clz; return d;
            case 0b000101: d.op = Op::Cls; return d;
            default: break;
            }
            d.op = Op::Unimplemented;
            return d;
        default:
            d.op = Op::Unimplemented;
            return d;
        }
    }

    // ---- 3-source (multiply family), bits28-24 = 10011 --------------------
    if ((op & 0x1F000000) == 0x1B000000) {
        uint32_t op31 = bits(op, 23, 21);
        bool o0 = bits(op, 15, 15);
        d.ra = uint8_t(bits(op, 14, 10));
        if (op31 == 0) {
            d.op = o0 ? Op::Msub : Op::Madd;
        } else if (op31 == 2 && d.sf) {
            d.op = Op::Smulh;
        } else if (op31 == 6 && d.sf) {
            d.op = Op::Umulh;
        } else if (op31 == 1 && d.sf) {
            // SMADDL / SMSUBL (o0 selects; Ra=31 => SMULL / SMNEGL)
            d.op = o0 ? Op::Smsubl : Op::Smaddl;
        } else if (op31 == 5 && d.sf) {
            // UMADDL / UMSUBL
            d.op = o0 ? Op::Umsubl : Op::Umaddl;
        } else {
            d.op = Op::Unimplemented; // halfword variants
        }
        return d;
    }

    // ---- add/subtract (register): bits28-24 = 01011 -----------------------
    if ((op & 0x1F000000) == 0x0B000000) {
        switch ((op >> 29) & 3) {
        case 0: d.op = Op::AddReg; break;
        case 1: d.op = Op::AddsReg; break;
        case 2: d.op = Op::SubReg; break;
        default: d.op = Op::SubsReg; break;
        }
        if (bits(op, 21, 21) == 0) { // shifted register
            d.shiftType = uint8_t(bits(op, 23, 22));
            d.shiftAmt = uint8_t(bits(op, 15, 10));
        } else { // extended register
            if (bits(op, 23, 22) != 0) { d.op = Op::Unimplemented; return d; }
            d.extend = uint8_t(bits(op, 15, 13));
            d.shiftAmt = uint8_t(bits(op, 12, 10));
            switch (d.op) {
            case Op::AddReg:  d.op = Op::AddExt; break;
            case Op::AddsReg: d.op = Op::AddsExt; break;
            case Op::SubReg:  d.op = Op::SubExt; break;
            case Op::SubsReg: d.op = Op::SubsExt; break;
            default: break;
            }
        }
        return d;
    }

    // ---- logical (register): bits28-24 = 01010 ----------------------------
    if ((op & 0x1F000000) == 0x0A000000) {
        switch ((op >> 29) & 3) {
        case 0: d.op = Op::AndReg; break;
        case 1: d.op = Op::OrrReg; break;
        case 2: d.op = Op::EorReg; break;
        default: d.op = Op::AndsReg; break;
        }
        d.invertRm = bits(op, 21, 21) != 0;
        d.shiftType = uint8_t(bits(op, 23, 22));
        d.shiftAmt = uint8_t(bits(op, 15, 10));
        return d;
    }

    // ---- exclusive / ordered: bits29-24 = 001000 ---------------------------
    if ((op & 0x3F000000) == 0x08000000) {
        uint32_t size = bits(op, 31, 30);
        uint32_t sel = bits(op, 15, 10);
        d.size = uint8_t(size);
        if (sel == 0b011111 || sel == 0b111111) {
            // single: LDXR/STXR (+A variants, + ordered LDAR/STLR with R bit)
            bool L = bits(op, 22, 22);
            d.op = L ? Op::Ldxr : Op::Stxr; // Stxr: data in rd, status reg in rm
            return d;
        }
        if (bits(op, 10, 10) && (bits(op, 15, 11) & 0b01110) == 0) {
            // pair: LDXP/LDAXP (bit11=0) / STXP/STLXP (bit11=1)
            d.ra = uint8_t(bits(op, 14, 10)); // Rt2
            d.op = bits(op, 11, 11) ? Op::Stxp : Op::Ldxp;
            return d;
        }
        d.op = Op::Unimplemented;
        return d;
    }

    // ---- branches / exceptions --------------------------------------------
    if ((op & 0x7C000000) == 0x14000000) { // B / BL
        d.op = (op >> 31) ? Op::Bl : Op::B;
        d.imm = uint64_t(sext(bits(op, 25, 0), 26) * 4);
        return d;
    }
    if ((op & 0xFF000010) == 0x54000000) { // B.cond
        d.op = Op::BCond;
        d.cond = uint8_t(bits(op, 3, 0));
        d.imm = uint64_t(sext(bits(op, 23, 5), 19) * 4);
        return d;
    }
    if ((op & 0x7E000000) == 0x34000000) { // CBZ / CBNZ
        d.op = bits(op, 24, 24) ? Op::Cbnz : Op::Cbz;
        d.imm = uint64_t(sext(bits(op, 23, 5), 19) * 4);
        return d;
    }
    if ((op & 0x7E000000) == 0x36000000) { // TBZ / TBNZ
        d.op = bits(op, 24, 24) ? Op::Tbnz : Op::Tbz;
        d.imm = uint64_t(sext(bits(op, 18, 5), 14) * 4);
        d.shiftAmt = uint8_t(((op >> 31) << 5) | bits(op, 23, 19)); // bit number
        return d;
    }
    if ((op & 0xFFFFFC1F) == 0xD61F0000) { d.op = Op::Br;  d.rn = uint8_t(bits(op, 9, 5)); return d; }
    if ((op & 0xFFFFFC1F) == 0xD63F0000) { d.op = Op::Blr; d.rn = uint8_t(bits(op, 9, 5)); return d; }
    if ((op & 0xFFFFFC1F) == 0xD65F0000) { d.op = Op::Ret; d.rn = uint8_t(bits(op, 9, 5)); return d; }
    if ((op & 0xFFE0001F) == 0xD4400000) { // HLT
        d.op = Op::Hlt;
        d.imm = bits(op, 20, 5);
        return d;
    }
    if ((op & 0xFFE0001F) == 0xD4000001) { // SVC
        d.op = Op::Svc;
        d.imm = bits(op, 20, 5);
        return d;
    }

    // ---- MSR / MRS ----------------------------------------------------------
    if ((op & 0xFFF00000) == 0xD5300000) { // MRS
        d.op = Op::Mrs;
        unsigned op0 = bits(op, 19, 19) ? 3 : 2;
        d.sysregId = uint32_t(lop::sysregId(op0, bits(op, 18, 16), bits(op, 15, 12),
                                            bits(op, 11, 8), bits(op, 7, 5)));
        return d;
    }
    if ((op & 0xFFF00000) == 0xD5100000) { // MSR (register), Rt = source register
        d.op = Op::Msr;
        d.sysregId = uint32_t(lop::sysregId(3, bits(op, 18, 16), bits(op, 15, 12),
                                            bits(op, 11, 8), bits(op, 7, 5)));
        return d;
    }
    if ((op & 0xFFF00000) == 0xD5000000) { // MSR (immediate): op1 0100 CRm op2, Rt = 11111
        if (bits(op, 15, 12) != 0b0100 || bits(op, 4, 0) != 0x1F) {
            d.op = Op::Unimplemented;
            return d;
        }
        d.op = Op::Msr;
        d.sysregId = uint32_t(lop::sysregId(0, bits(op, 18, 16), 4,
                                            bits(op, 11, 8), bits(op, 7, 5)));
        d.msrImm = true;
        d.imm = bits(op, 11, 8); // the immediate lives in CRm
        return d;
    }

    // ---- load/store, single register: bits29-27 = 111 ----------------------
    if ((op & 0x38000000) == 0x38000000) {
        if (bits(op, 26, 26)) { d.op = Op::Unimplemented; return d; } // SIMD/FP
        uint32_t size = bits(op, 31, 30);
        uint32_t opc = bits(op, 23, 22);
        Op o;
        if (bits(op, 25, 24) == 0b01) { // unsigned scaled immediate
            if (!classifySingle(size, opc, &o)) { d.op = Op::Unimplemented; return d; }
            if (o == Op::Str || o == Op::Ldr) { /* keep size 2/3 */ }
            bool signedLd = (o == Op::Ldrsw);
            d = finishSingle(d, size, o, signedLd);
            d.addrMode = AddrMode::Offset;
            d.imm = uint64_t(bits(op, 21, 10)) << size;
            return d;
        }
        if (bits(op, 25, 24) == 0b00) {
            if (bits(op, 21, 21)) { // register offset
                if (bits(op, 11, 10) != 0b10) { d.op = Op::Unimplemented; return d; }
                if (!classifySingle(size, opc, &o)) { d.op = Op::Unimplemented; return d; }
                bool signedLd = (o == Op::Ldrsw || o == Op::Ldrsb || o == Op::Ldrsh);
                d = finishSingle(d, size, o, signedLd);
                d.addrMode = AddrMode::RegOffset;
                d.extend = uint8_t(bits(op, 15, 13)); // 010 uxtw, 011 lsl, 110 sxtw, 111 lsl(64)
                d.shiftAmt = uint8_t(bits(op, 12, 12));
                return d;
            }
            uint32_t type = bits(op, 11, 10);
            if (type == 0b10) { d.op = Op::Unimplemented; return d; } // LDAPR family
            if (!classifySingle(size, opc, &o)) { d.op = Op::Unimplemented; return d; }
            bool signedLd = (o == Op::Ldrsw || o == Op::Ldrsb || o == Op::Ldrsh);
            d = finishSingle(d, size, o, signedLd);
            d.imm = uint64_t(int64_t(sext(bits(op, 20, 12), 9)));
            if (type == 0b00) d.addrMode = AddrMode::Offset;
            else if (type == 0b01) { d.addrMode = AddrMode::PostIndex; d.writeback = true; }
            else { d.addrMode = AddrMode::PreIndex; d.writeback = true; }
            return d;
        }
        d.op = Op::Unimplemented;
        return d;
    }

    // ---- load/store pair: bits29-27 = 101 ----------------------------------
    if ((op & 0x38000000) == 0x28000000) {
        if (bits(op, 26, 26)) { d.op = Op::Unimplemented; return d; }
        uint32_t opc = bits(op, 31, 30);
        bool L = bits(op, 22, 22);
        uint32_t mode = bits(op, 25, 23);
        int32_t simm = int32_t(sext(bits(op, 21, 15), 7));
        d.ra = uint8_t(bits(op, 14, 10)); // Rt2
        switch (mode) {
        case 0b010: d.addrMode = AddrMode::Offset; break;
        case 0b011: d.addrMode = AddrMode::PreIndex; d.writeback = true; break;
        case 0b001: d.addrMode = AddrMode::PostIndex; d.writeback = true; break;
        case 0b101: d.addrMode = AddrMode::Offset; break; // non-temporal
        default: d.op = Op::Unimplemented; return d;
        }
        if (opc == 0b00) {
            d.op = L ? Op::Ldp : Op::Stp;
            d.size = 2;
            d.imm = uint64_t(simm) * 4;
        } else if (opc == 0b10) {
            d.op = L ? Op::Ldp : Op::Stp;
            d.size = 3;
            d.imm = uint64_t(simm) * 8;
        } else if (opc == 0b01 && L) {
            d.op = Op::LdpSw;
            d.size = 2;
            d.signedLoad = true;
            d.imm = uint64_t(simm) * 4;
        } else {
            d.op = Op::Unimplemented;
        }
        return d;
    }

    // ---- load/store literal: bits29-27 = 011 -------------------------------
    if ((op & 0x38000000) == 0x18000000) {
        uint32_t opc = bits(op, 31, 30);
        d.addrMode = AddrMode::Literal;
        d.imm = uint64_t(sext(bits(op, 23, 5), 19) * 4);
        d.sf = (opc == 1 || opc == 2); // 64-bit result for LDR X / LDRSW
        if (opc == 0) return finishSingle(d, 2, Op::Ldr, false);
        if (opc == 1) return finishSingle(d, 3, Op::Ldr, false);
        if (opc == 2) return finishSingle(d, 2, Op::Ldrsw, true);
        d.op = Op::Nop; // PRFM (literal)
        return d;
    }

    d.op = Op::Unimplemented;
    return d;
}

const char *opName(Op op) {
    switch (op) {
    case Op::Unimplemented: return "unimplemented";
    case Op::AddImm: return "add"; case Op::AddsImm: return "adds";
    case Op::SubImm: return "sub"; case Op::SubsImm: return "subs";
    case Op::AddReg: return "add"; case Op::AddsReg: return "adds";
    case Op::SubReg: return "sub"; case Op::SubsReg: return "subs";
    case Op::AddExt: return "add"; case Op::AddsExt: return "adds";
    case Op::SubExt: return "sub"; case Op::SubsExt: return "subs";
    case Op::AndReg: return "and"; case Op::OrrReg: return "orr";
    case Op::EorReg: return "eor"; case Op::AndsReg: return "ands";
    case Op::AndImm: return "and"; case Op::OrrImm: return "orr";
    case Op::EorImm: return "eor"; case Op::AndsImm: return "ands";
    case Op::MovN: return "movn"; case Op::MovZ: return "movz"; case Op::MovK: return "movk";
    case Op::Adc: return "adc"; case Op::Adcs: return "adcs";
    case Op::Sbc: return "sbc"; case Op::Sbcs: return "sbcs";
    case Op::Csel: return "csel"; case Op::Csinc: return "csinc";
    case Op::Csinv: return "csinv"; case Op::Csneg: return "csneg";
    case Op::CcmpReg: case Op::CcmpImm: return "ccmp";
    case Op::CcmnReg: case Op::CcmnImm: return "ccmn";
    case Op::Madd: return "madd"; case Op::Msub: return "msub";
    case Op::Smulh: return "smulh"; case Op::Umulh: return "umulh";
    case Op::Smaddl: return "smaddl"; case Op::Smsubl: return "smsubl";
    case Op::Umaddl: return "umaddl"; case Op::Umsubl: return "umsubl";
    case Op::Udiv: return "udiv"; case Op::Sdiv: return "sdiv";
    case Op::Lslv: return "lslv"; case Op::Lsrv: return "lsrv";
    case Op::Asrv: return "asrv"; case Op::Rorv: return "rorv";
    case Op::Rbit: return "rbit"; case Op::Rev: return "rev";
    case Op::Rev16: return "rev16"; case Op::Rev32: return "rev32";
    case Op::Clz: return "clz"; case Op::Cls: return "cls";
    case Op::Adr: return "adr"; case Op::Adrp: return "adrp";
    case Op::Sbfm: return "sbfm"; case Op::Bfm: return "bfm";
    case Op::Ubfm: return "ubfm"; case Op::Extr: return "extr";
    case Op::B: return "b"; case Op::Bl: return "bl"; case Op::BCond: return "b.cond";
    case Op::Cbz: return "cbz"; case Op::Cbnz: return "cbnz";
    case Op::Tbz: return "tbz"; case Op::Tbnz: return "tbnz";
    case Op::Br: return "br"; case Op::Blr: return "blr"; case Op::Ret: return "ret";
    case Op::Hlt: return "hlt"; case Op::Svc: return "svc";
    case Op::Str: return "str"; case Op::Ldr: return "ldr";
    case Op::Strb: return "strb"; case Op::Ldrb: return "ldrb";
    case Op::Strh: return "strh"; case Op::Ldrh: return "ldrh";
    case Op::Ldrsb: return "ldrsb"; case Op::Ldrsh: return "ldrsh"; case Op::Ldrsw: return "ldrsw";
    case Op::Ldxr: return "ldxr"; case Op::Stxr: return "stxr";
    case Op::Ldp: return "ldp"; case Op::Stp: return "stp"; case Op::LdpSw: return "ldpsw";
    case Op::Ldxp: return "ldxp"; case Op::Stxp: return "stxp";
    case Op::Nop: return "nop"; case Op::Barrier: return "barrier";
    case Op::Msr: return "msr"; case Op::Mrs: return "mrs";
    }
    return "?";
}

} // namespace lop
