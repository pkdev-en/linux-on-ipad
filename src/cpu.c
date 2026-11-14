#include "cpu.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* System register id = instruction bits [19:5]: o0:op1:CRn:CRm:op2 */
#define SR(op0, op1, crn, crm, op2) \
    ((((op0) & 1u) << 14) | ((op1) << 11) | ((crn) << 7) | ((crm) << 3) | (op2))
enum {
    SR_MIDR_EL1 = SR(3, 0, 0, 0, 0),
    SR_MPIDR_EL1 = SR(3, 0, 0, 0, 5),
    SR_SCTLR_EL1 = SR(3, 0, 1, 0, 0),
    SR_CPACR_EL1 = SR(3, 0, 1, 0, 2),
    SR_CURRENTEL = SR(3, 0, 4, 2, 2),
    SR_VBAR_EL1 = SR(3, 0, 12, 0, 0),
    SR_TPIDR_EL1 = SR(3, 0, 13, 0, 4),
    SR_DAIF = SR(3, 3, 4, 2, 1),
    SR_TPIDR_EL0 = SR(3, 3, 13, 0, 2),
};

void lx_cpu_reset(LxCpu *c, LxMachine *m)
{
    memset(c, 0, sizeof *c);
    c->m = m;
    c->sctlr = 0x00c50838; /* reset value, MMU and caches off */
    c->daif = 0x3c0;       /* all exceptions masked */
}

static void fault(LxCpu *c, uint32_t insn, const char *fmt, ...)
{
    va_list ap;
    int n = snprintf(c->why, sizeof c->why, "pc=0x%" PRIx64 " insn=0x%08x: ", c->pc, insn);
    va_start(ap, fmt);
    vsnprintf(c->why + n, sizeof c->why - (size_t)n, fmt, ap);
    va_end(ap);
    c->stop = LX_FAULT;
}

/* ---- helpers --------------------------------------------------------------- */

static inline int64_t sext(uint64_t v, unsigned bits)
{
    return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

static inline uint64_t rdr(const LxCpu *c, unsigned r, bool sp_ok)
{
    return r != 31 ? c->x[r] : (sp_ok ? c->sp : 0);
}

static inline void wrr(LxCpu *c, unsigned r, uint64_t v, bool sp_ok)
{
    if (r != 31)
        c->x[r] = v;
    else if (sp_ok)
        c->sp = v;
}

static uint64_t awc(uint64_t a, uint64_t b, unsigned cin, bool is64, uint32_t *nzcv)
{
    const unsigned bits = is64 ? 64 : 32;
    const uint64_t mask = is64 ? ~0ull : 0xffffffffull;
    a &= mask;
    b &= mask;
    uint64_t r;
    bool carry;
    if (is64) {
        unsigned __int128 s = (unsigned __int128)a + b + cin;
        r = (uint64_t)s;
        carry = (s >> 64) & 1;
    } else {
        uint64_t s = a + b + cin;
        r = s & mask;
        carry = (s >> 32) & 1;
    }
    bool n = (r >> (bits - 1)) & 1;
    bool z = r == 0;
    bool v = (((a ^ r) & (b ^ r)) >> (bits - 1)) & 1;
    *nzcv = ((uint32_t)n << 31) | ((uint32_t)z << 30) | ((uint32_t)carry << 29) | ((uint32_t)v << 28);
    return r;
}

static uint64_t ror_e(uint64_t x, unsigned r, unsigned esize)
{
    uint64_t m = esize == 64 ? ~0ull : ((1ull << esize) - 1);
    x &= m;
    r %= esize;
    if (!r)
        return x;
    return ((x >> r) | (x << (esize - r))) & m;
}

/* shift types: 0 LSL, 1 LSR, 2 ASR, 3 ROR */
static uint64_t shift_val(uint64_t v, unsigned type, unsigned amt, bool is64)
{
    const unsigned bits = is64 ? 64 : 32;
    v &= is64 ? ~0ull : 0xffffffffull;
    switch (type) {
    case 0: return amt ? v << amt : v;
    case 1: return v >> amt;
    case 2: return (uint64_t)(sext(v, bits) >> amt);
    default: return ror_e(v, amt, bits);
    }
}

/* ARM ARM DecodeBitMasks(). Returns false for reserved encodings. */
static bool decode_bitmask(unsigned N, unsigned imms, unsigned immr, unsigned datasize,
                           bool immediate, uint64_t *wmask, uint64_t *tmask)
{
    unsigned x = (N << 6) | (~imms & 0x3f);
    if (x == 0)
        return false;
    unsigned len = 31 - (unsigned)__builtin_clz(x);
    if (len < 1)
        return false;
    unsigned esize = 1u << len;
    if (esize > datasize)
        return false;
    unsigned levels = esize - 1;
    unsigned S = imms & levels, R = immr & levels;
    if (immediate && S == levels)
        return false;
    unsigned d = (S - R) & levels;
    uint64_t welem = (S + 1 == 64) ? ~0ull : ((1ull << (S + 1)) - 1);
    uint64_t telem = (d + 1 == 64) ? ~0ull : ((1ull << (d + 1)) - 1);
    uint64_t w = ror_e(welem, R, esize), W = 0, T = 0;
    for (unsigned i = 0; i < datasize; i += esize) {
        W |= w << i;
        T |= telem << i;
    }
    *wmask = W;
    if (tmask)
        *tmask = T;
    return true;
}

static bool cond_holds(uint32_t nzcv, unsigned cond)
{
    bool n = (nzcv >> 31) & 1, z = (nzcv >> 30) & 1, cf = (nzcv >> 29) & 1, v = (nzcv >> 28) & 1, r;
    switch (cond >> 1) {
    case 0: r = z; break;
    case 1: r = cf; break;
    case 2: r = n; break;
    case 3: r = v; break;
    case 4: r = cf && !z; break;
    case 5: r = n == v; break;
    case 6: r = (n == v) && !z; break;
    default: return true; /* AL / NV */
    }
    return (cond & 1) ? !r : r;
}

/* ---- system registers ------------------------------------------------------- */

static bool sysreg_read(LxCpu *c, unsigned id, uint64_t *v)
{
    switch (id) {
    case SR_MIDR_EL1: *v = 0x410fd083; return true;
    case SR_MPIDR_EL1: *v = 0x80000000ull; return true;
    case SR_CURRENTEL: *v = 4; return true; /* EL1 */
    case SR_SCTLR_EL1: *v = c->sctlr; return true;
    case SR_CPACR_EL1: *v = c->cpacr; return true;
    case SR_VBAR_EL1: *v = c->vbar; return true;
    case SR_TPIDR_EL0: *v = c->tpidr0; return true;
    case SR_TPIDR_EL1: *v = c->tpidr1; return true;
    case SR_DAIF: *v = c->daif; return true;
    default: return false;
    }
}

static int sysreg_write(LxCpu *c, unsigned id, uint64_t v)
{
    switch (id) {
    case SR_SCTLR_EL1:
        if (v & 1)
            return -2; /* MMU enable */
        c->sctlr = v;
        return 0;
    case SR_CPACR_EL1: c->cpacr = v; return 0;
    case SR_VBAR_EL1: c->vbar = v; return 0;
    case SR_TPIDR_EL0: c->tpidr0 = v; return 0;
    case SR_TPIDR_EL1: c->tpidr1 = v; return 0;
    case SR_DAIF: c->daif = v & 0x3c0; return 0;
    default: return -1;
    }
}

static void psci(LxCpu *c)
{
    switch ((uint32_t)c->x[0]) {
    case 0x84000000: c->x[0] = 2; break; /* PSCI_VERSION -> 0.2 */
    case 0x84000008: c->stop = LX_POWEROFF; break;
    case 0x84000009: c->stop = LX_RESET; break;
    default: c->x[0] = (uint64_t)-1; break; /* NOT_SUPPORTED */
    }
}

/* ---- loads and stores --------------------------------------------------------- */

/* opc: 0 store, 1 load zero-extend, 2 load sign-extend to 64, 3 load sign-extend to 32 */
static bool do_ldst(LxCpu *c, uint32_t insn, uint64_t addr, unsigned size, unsigned opc, unsigned rt)
{
    const unsigned bytes = 1u << size;
    uint64_t v;
    if (opc == 0) {
        v = rt == 31 ? 0 : c->x[rt];
        if (!lx_bus_write(c->m, addr, bytes, v)) {
            fault(c, insn, "bus error writing %u bytes at 0x%" PRIx64, bytes, addr);
            return false;
        }
        return true;
    }
    if (opc == 2 && size == 3)
        return true; /* PRFM: no architectural effect */
    if (opc == 3 && size >= 2) {
        fault(c, insn, "reserved load encoding");
        return false;
    }
    if (!lx_bus_read(c->m, addr, bytes, &v)) {
        fault(c, insn, "bus error reading %u bytes at 0x%" PRIx64, bytes, addr);
        return false;
    }
    if (opc == 2)
        v = (uint64_t)sext(v, bytes * 8);
    else if (opc == 3)
        v = (uint64_t)sext(v, bytes * 8) & 0xffffffffull;
    if (rt != 31)
        c->x[rt] = v;
    return true;
}

/* ---- one instruction ------------------------------------------------------------ */

#define BAD(...)                         \
    do {                                 \
        fault(c, insn, __VA_ARGS__);     \
        return;                          \
    } while (0)

static void step(LxCpu *c)
{
    uint32_t insn;
    uint8_t *p = (c->pc & 3) ? NULL : lx_ram_ptr(c->m, c->pc, 4);
    if (!p) {
        fault(c, 0, "instruction fetch from 0x%" PRIx64, c->pc);
        return;
    }
    memcpy(&insn, p, 4);
    if (c->trace)
        fprintf(stderr, "%08" PRIx64 ": %08x\n", c->pc, insn);

    uint64_t next = c->pc + 4;
    const unsigned rd = insn & 31, rn = (insn >> 5) & 31, rm = (insn >> 16) & 31;
    const bool sf = insn >> 31;
    const unsigned bits = sf ? 64 : 32;
    const uint64_t mask = sf ? ~0ull : 0xffffffffull;

    if ((insn & 0x1F800000) == 0x12800000) { /* move wide immediate */
        unsigned opc = (insn >> 29) & 3, hw = (insn >> 21) & 3;
        uint64_t imm = (insn >> 5) & 0xffff;
        if (opc == 1 || (!sf && hw > 1))
            BAD("reserved move-wide encoding");
        unsigned sh = hw * 16;
        uint64_t v;
        if (opc == 0)
            v = ~(imm << sh);
        else if (opc == 2)
            v = imm << sh;
        else
            v = (rdr(c, rd, false) & ~(0xffffull << sh)) | (imm << sh);
        wrr(c, rd, v & mask, false);
    } else if ((insn & 0x1F000000) == 0x10000000) { /* ADR / ADRP */
        uint64_t imm = (((insn >> 5) & 0x7ffff) << 2) | ((insn >> 29) & 3);
        int64_t off = sext(imm, 21);
        uint64_t v = (insn >> 31) ? (c->pc & ~0xfffull) + ((uint64_t)off << 12) : c->pc + (uint64_t)off;
        wrr(c, rd, v, false);
    } else if ((insn & 0x1F800000) == 0x11000000) { /* add/sub immediate */
        bool sub = (insn >> 30) & 1, S = (insn >> 29) & 1;
        uint64_t imm = (uint64_t)((insn >> 10) & 0xfff) << (((insn >> 22) & 1) ? 12 : 0);
        uint64_t a = rdr(c, rn, true);
        if (S) {
            uint32_t f;
            uint64_t r = sub ? awc(a, ~imm, 1, sf, &f) : awc(a, imm, 0, sf, &f);
            c->nzcv = f;
            wrr(c, rd, r, false);
        } else {
            wrr(c, rd, (sub ? a - imm : a + imm) & mask, true);
        }
    } else if ((insn & 0x1F800000) == 0x12000000) { /* logical immediate */
        unsigned N = (insn >> 22) & 1, immr = (insn >> 16) & 63, imms = (insn >> 10) & 63;
        uint64_t w;
        if ((!sf && N) || !decode_bitmask(N, imms, immr, bits, true, &w, NULL))
            BAD("reserved logical-immediate encoding");
        uint64_t a = rdr(c, rn, false) & mask, r;
        unsigned opc = (insn >> 29) & 3;
        r = opc == 1 ? (a | w) : opc == 2 ? (a ^ w) : (a & w);
        r &= mask;
        if (opc == 3) {
            c->nzcv = ((uint32_t)((r >> (bits - 1)) & 1) << 31) | ((uint32_t)(r == 0) << 30);
            wrr(c, rd, r, false);
        } else {
            wrr(c, rd, r, true);
        }
    } else if ((insn & 0x1F800000) == 0x13000000) { /* SBFM / BFM / UBFM */
        unsigned opc = (insn >> 29) & 3, N = (insn >> 22) & 1;
        unsigned immr = (insn >> 16) & 63, imms = (insn >> 10) & 63;
        uint64_t wm, tm;
        if (opc == 3 || N != (unsigned)sf || (!sf && ((immr | imms) & 0x20)) ||
            !decode_bitmask(N, imms, immr, bits, false, &wm, &tm))
            BAD("reserved bitfield encoding");
        uint64_t src = rdr(c, rn, false) & mask, dst = rdr(c, rd, false) & mask;
        uint64_t bot = ror_e(src, immr, bits) & wm, res;
        if (opc == 0) {
            uint64_t top = ((src >> imms) & 1) ? mask : 0;
            res = (top & ~tm) | (bot & tm);
        } else if (opc == 1) {
            bot = (dst & ~wm) | bot;
            res = (dst & ~tm) | (bot & tm);
        } else {
            res = bot & tm;
        }
        wrr(c, rd, res & mask, false);
    } else if ((insn & 0x7C000000) == 0x14000000) { /* B / BL */
        if (insn >> 31)
            c->x[30] = c->pc + 4;
        next = c->pc + (uint64_t)(sext(insn & 0x3ffffff, 26) * 4);
    } else if ((insn & 0xFF000010) == 0x54000000) { /* B.cond */
        if (cond_holds(c->nzcv, insn & 15))
            next = c->pc + (uint64_t)(sext((insn >> 5) & 0x7ffff, 19) * 4);
    } else if ((insn & 0x7E000000) == 0x34000000) { /* CBZ / CBNZ */
        uint64_t v = rdr(c, rd, false) & mask;
        if ((v == 0) != (bool)((insn >> 24) & 1))
            next = c->pc + (uint64_t)(sext((insn >> 5) & 0x7ffff, 19) * 4);
    } else if ((insn & 0x7E000000) == 0x36000000) { /* TBZ / TBNZ */
        unsigned bit = ((insn >> 31) << 5) | ((insn >> 19) & 31);
        bool set = (rdr(c, rd, false) >> bit) & 1;
        if (set == (bool)((insn >> 24) & 1))
            next = c->pc + (uint64_t)(sext((insn >> 5) & 0x3fff, 14) * 4);
    } else if ((insn & 0xFFFFFC1F) == 0xD61F0000 || (insn & 0xFFFFFC1F) == 0xD63F0000 ||
               (insn & 0xFFFFFC1F) == 0xD65F0000) { /* BR / BLR / RET */
        if (rn == 31)
            BAD("branch to register 31");
        uint64_t t = c->x[rn];
        if ((insn & 0xFFFFFC1F) == 0xD63F0000)
            c->x[30] = c->pc + 4;
        next = t;
    } else if ((insn & 0xFFE0001F) == 0xD4400000) { /* HLT */
        c->stop = LX_HALT;
        snprintf(c->why, sizeof c->why, "hlt #0x%x", (insn >> 5) & 0xffff);
        return;
    } else if ((insn & 0xFFE0001F) == 0xD4000002) { /* HVC */
        psci(c);
    } else if ((insn & 0xFFE0001F) == 0xD4000001 || (insn & 0xFFE0001F) == 0xD4000003) {
        BAD("SVC/SMC: exception model not implemented yet");
    } else if (insn == 0xD503207F) { /* WFI */
        c->stop = LX_HALT;
        snprintf(c->why, sizeof c->why, "wfi");
        c->pc = next;
        return;
    } else if ((insn & 0xFFFFF01F) == 0xD503201F || (insn & 0xFFFFF01F) == 0xD503301F) {
        /* NOP/YIELD/hints, and DSB/DMB/ISB/CLREX: nothing to do in an interpreter */
    } else if ((insn & 0xFFD00000) == 0xD5100000) { /* MRS / MSR (register) */
        unsigned id = (insn >> 5) & 0x7fff;
        if ((insn >> 21) & 1) {
            uint64_t v;
            if (!sysreg_read(c, id, &v))
                BAD("read of unimplemented system register id=0x%04x", id);
            wrr(c, rd, v, false);
        } else {
            int r = sysreg_write(c, id, rdr(c, rd, false));
            if (r == -2)
                BAD("SCTLR_EL1.M set: MMU not implemented yet");
            if (r)
                BAD("write to unimplemented system register id=0x%04x", id);
        }
    } else if ((insn & 0x3E000000) == 0x28000000) { /* LDP / STP (general registers) */
        unsigned opc = (insn >> 30) & 3, type = (insn >> 23) & 3, rt2 = (insn >> 10) & 31;
        bool load = (insn >> 22) & 1;
        if (opc == 1 || opc == 3 || type == 0)
            BAD("unsupported load/store pair variant");
        unsigned scale = opc == 2 ? 3 : 2, bytes = 1u << scale;
        int64_t off = sext((insn >> 15) & 0x7f, 7) * (int64_t)bytes;
        uint64_t base = rdr(c, rn, true);
        uint64_t addr = type == 1 ? base : base + (uint64_t)off;
        uint64_t v1, v2;
        if (load) {
            if (!lx_bus_read(c->m, addr, bytes, &v1) || !lx_bus_read(c->m, addr + bytes, bytes, &v2))
                BAD("bus error on load pair at 0x%" PRIx64, addr);
            if (rd != 31)
                c->x[rd] = v1;
            if (rt2 != 31)
                c->x[rt2] = v2;
        } else {
            v1 = rd == 31 ? 0 : c->x[rd];
            v2 = rt2 == 31 ? 0 : c->x[rt2];
            if (!lx_bus_write(c->m, addr, bytes, v1) || !lx_bus_write(c->m, addr + bytes, bytes, v2))
                BAD("bus error on store pair at 0x%" PRIx64, addr);
        }
        if (type == 1 || type == 3)
            wrr(c, rn, base + (uint64_t)off, true);
    } else if ((insn & 0x3B000000) == 0x39000000) { /* LDR/STR unsigned offset */
        if (insn & 0x04000000)
            BAD("SIMD/FP load/store not implemented yet");
        unsigned size = insn >> 30, opc = (insn >> 22) & 3;
        uint64_t addr = rdr(c, rn, true) + ((uint64_t)((insn >> 10) & 0xfff) << size);
        if (!do_ldst(c, insn, addr, size, opc, rd))
            return;
    } else if ((insn & 0x3B200000) == 0x38000000) { /* LDUR/STUR, pre/post-index */
        if (insn & 0x04000000)
            BAD("SIMD/FP load/store not implemented yet");
        unsigned size = insn >> 30, opc = (insn >> 22) & 3, idx = (insn >> 10) & 3;
        int64_t imm = sext((insn >> 12) & 0x1ff, 9);
        if (idx == 2)
            BAD("unprivileged load/store not implemented");
        uint64_t base = rdr(c, rn, true);
        uint64_t addr = idx == 1 ? base : base + (uint64_t)imm;
        if (!do_ldst(c, insn, addr, size, opc, rd))
            return;
        if (idx == 1 || idx == 3)
            wrr(c, rn, base + (uint64_t)imm, true);
    } else if ((insn & 0x1FE00800) == 0x1A800000 && !((insn >> 29) & 1)) { /* CSEL/CSINC/CSINV/CSNEG */
        unsigned op = (insn >> 30) & 1, o2 = (insn >> 10) & 1;
        uint64_t a = rdr(c, rn, false), b = rdr(c, rm, false), r;
        if (cond_holds(c->nzcv, (insn >> 12) & 15))
            r = a;
        else if (!op)
            r = o2 ? b + 1 : b;
        else
            r = o2 ? (uint64_t)-(int64_t)b : ~b;
        wrr(c, rd, r & mask, false);
    } else if ((insn & 0x7FE00000) == 0x1B000000) { /* MADD / MSUB */
        unsigned ra = (insn >> 10) & 31;
        uint64_t prod = (rdr(c, rn, false) & mask) * (rdr(c, rm, false) & mask);
        uint64_t acc = rdr(c, ra, false);
        wrr(c, rd, ((insn >> 15) & 1 ? acc - prod : acc + prod) & mask, false);
    } else if ((insn & 0x1F200000) == 0x0B000000) { /* add/sub shifted register */
        unsigned shift = (insn >> 22) & 3, amt = (insn >> 10) & 63;
        bool sub = (insn >> 30) & 1, S = (insn >> 29) & 1;
        if (shift == 3 || (!sf && amt >= 32))
            BAD("reserved add/sub shifted-register encoding");
        uint64_t a = rdr(c, rn, false), b = shift_val(rdr(c, rm, false), shift, amt, sf);
        if (S) {
            uint32_t f;
            uint64_t r = sub ? awc(a, ~b, 1, sf, &f) : awc(a, b, 0, sf, &f);
            c->nzcv = f;
            wrr(c, rd, r, false);
        } else {
            wrr(c, rd, (sub ? a - b : a + b) & mask, false);
        }
    } else if ((insn & 0x1F000000) == 0x0A000000) { /* logical shifted register */
        unsigned opc = (insn >> 29) & 3, shift = (insn >> 22) & 3, amt = (insn >> 10) & 63;
        if (!sf && amt >= 32)
            BAD("reserved logical shifted-register encoding");
        uint64_t b = shift_val(rdr(c, rm, false), shift, amt, sf);
        if ((insn >> 21) & 1)
            b = ~b;
        b &= mask;
        uint64_t a = rdr(c, rn, false) & mask;
        uint64_t r = opc == 1 ? (a | b) : opc == 2 ? (a ^ b) : (a & b);
        if (opc == 3)
            c->nzcv = ((uint32_t)((r >> (bits - 1)) & 1) << 31) | ((uint32_t)(r == 0) << 30);
        wrr(c, rd, r & mask, false);
    } else {
        BAD("unimplemented instruction");
    }

    if (c->stop == LX_RUN)
        c->pc = next;
}

void lx_cpu_run(LxCpu *c, uint64_t max_insns)
{
    /* max_insns is a budget for *this call*, so callers can run in slices. */
    const uint64_t end = max_insns ? c->icount + max_insns : UINT64_MAX;
    while (c->stop == LX_RUN && c->icount < end) {
        step(c);
        c->icount++;
    }
}
