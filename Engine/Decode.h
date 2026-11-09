// Decode.h -- AArch64 (ARMv8-A) instruction decoder.
// Linux on iPad -- Apache-2.0.
//
// decode() turns a 32-bit instruction word into a small struct the
// interpreter can switch on. Unknown encodings decode to Op::Unimplemented
// instead of guessing: booting real kernels will tell us what to add next.
// Every mask in Decode.cpp is validated by DecodeSelfTest.cpp (generated
// from keystone ground truth) and by the unicorn-oracle semantic tests.
#ifndef LOP_DECODE_H
#define LOP_DECODE_H

#include <cstdint>

namespace lop {

enum class Op : uint8_t {
    Unimplemented = 0,
    // add/subtract immediate
    AddImm, AddsImm, SubImm, SubsImm,
    // add/subtract shifted register
    AddReg, AddsReg, SubReg, SubsReg,
    // add/subtract extended register
    AddExt, AddsExt, SubExt, SubsExt,
    // logical shifted register (invertRm covers BIC/ORN/EON/BICS)
    AndReg, OrrReg, EorReg, AndsReg,
    // logical immediate
    AndImm, OrrImm, EorImm, AndsImm,
    // move wide
    MovN, MovZ, MovK,
    // add/subtract carry
    Adc, Adcs, Sbc, Sbcs,
    // conditional select / compare
    Csel, Csinc, Csinv, Csneg,
    CcmpReg, CcmpImm, CcmnReg, CcmnImm,
    // data-processing (3 source)
    Madd, Msub, Smulh, Umulh,
    Smaddl, Smsubl, Umaddl, Umsubl,
    // data-processing (2 source)
    Udiv, Sdiv, Lslv, Lsrv, Asrv, Rorv,
    // data-processing (1 source)
    Rbit, Rev, Rev16, Rev32, Clz, Cls,
    // pc-relative
    Adr, Adrp,
    // bitfield / extract
    Sbfm, Bfm, Ubfm, Extr,
    // branches and exceptions
    B, Bl, BCond, Cbz, Cbnz, Tbz, Tbnz, Br, Blr, Ret, Hlt, Svc,
    // loads/stores, single register
    Str, Ldr, Strb, Ldrb, Strh, Ldrh, Ldrsb, Ldrsh, Ldrsw,
    // exclusive / ordered (single) -- Stxr writes success (0) to ra
    Ldxr, Stxr,
    // pair loads/stores (ra = Rt2)
    Ldp, Stp, LdpSw, Ldxp, Stxp,
    // system
    Nop, Barrier, Msr, Mrs,
};

enum class AddrMode : uint8_t {
    Offset,    // unsigned scaled imm12, unscaled imm9, or pair without wb
    PreIndex,  // [Xn, #imm]!
    PostIndex, // [Xn], #imm
    RegOffset, // [Xn, Xm{, extend {, shift}}]
    Literal,   // LDR (literal): pc-relative
};

struct Decoded {
    Op op = Op::Unimplemented;

    bool sf = false;             // false = 32-bit (W), true = 64-bit (X)
    uint8_t rd = 0, rn = 0, rm = 0, ra = 0; // 31 = SP/ZR per instruction context

    uint64_t imm = 0;            // immediate / address offset (sign-extended)
    uint8_t shiftType = 0;       // 0 lsl, 1 lsr, 2 asr, 3 ror
    uint8_t shiftAmt = 0;        // imm6 / imm3 / hw<<4 / bitfield immr / lsb
    uint8_t extend = 0;          // AddExt..: uxtb..sxtx; reg-offset: opt field
    bool invertRm = false;       // BIC/ORN/EON/BICS
    bool writeback = false;      // pre/post-index writeback

    uint8_t cond = 0;            // condition code
    uint8_t nzcv = 0;            // CCMP/CCMN else-value

    uint8_t size = 3;            // 0 byte, 1 half, 2 word, 3 double
    bool signedLoad = false;     // LDRSB/LDRSH/LDRSW/LDPSW
    AddrMode addrMode = AddrMode::Offset;

    uint32_t sysregId = 0;       // MRS/MSR: packed (op0,op1,CRn,CRm,op2)
    bool msrImm = false;         // MSR (immediate) form
};

Decoded decode(uint32_t insn);

const char *opName(Op op);

} // namespace lop

#endif // LOP_DECODE_H
