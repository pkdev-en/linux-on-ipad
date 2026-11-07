#!/usr/bin/env python3
"""Generates an AArch64 assembly program that runs many instructions on many operand
pairs and prints x0 + NZCV after each one. Run it under qemu-user and under lopvm
(--semihost) and diff the output: any difference is a bug in the engine.

usage: gen_diff.py out.S listing.txt
"""
import sys

VALUES = [
    0x0, 0x1, 0x7F, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFFFFFFFFFF,
    0x8000000000000000, 0xFFFFFFFFFFFFFFFF, 0x123456789ABCDEF0, 0xDEADBEEFCAFEBABE,
]
CONDS = ["eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al"]

# Each template is a list of lines; x1/x2 (w1/w2) are the operands, results go to x0.
T = []

def t(*lines):
    T.append(list(lines))

# ---- add / sub ----
for op in ["add", "adds", "sub", "subs"]:
    t(f"{op} x0, x1, x2")
    t(f"{op} w0, w1, w2")
    t(f"{op} x0, x1, x2, lsl #3")
    t(f"{op} x0, x1, x2, lsr #7")
    t(f"{op} x0, x1, x2, asr #13")
    t(f"{op} w0, w1, w2, lsl #5")
    t(f"{op} w0, w1, w2, asr #31")
    t(f"{op} x0, x1, #0xabc")
    t(f"{op} x0, x1, #0xabc, lsl #12")
    t(f"{op} w0, w1, #255")
    t(f"{op} w0, w1, #1, lsl #12")
    for ext in ["uxtb", "sxtb", "uxth", "sxth", "uxtw", "sxtw"]:
        t(f"{op} x0, x1, w2, {ext}")
        t(f"{op} x0, x1, w2, {ext} #2")
    t(f"{op} x0, x1, x2, uxtx #4")
    t(f"{op} x0, x1, x2, sxtx")
    t(f"{op} w0, w1, w2, uxtb #1")
    t(f"{op} w0, w1, w2, sxth #3")
t("cmp x1, x2")
t("cmn x1, x2")
t("cmp w1, w2")
t("cmn w1, #5")
t("cmp x1, #0")
t("neg x0, x2")
t("negs x0, x2")
t("negs w0, w2")
t("mov x0, #0", "mov x5, sp", "add sp, x5, #16", "sub sp, sp, #16", "mov x0, sp", "sub x0, x0, x5")

# ---- logical ----
for op in ["and", "orr", "eor", "ands", "bic", "orn", "eon", "bics"]:
    t(f"{op} x0, x1, x2")
    t(f"{op} w0, w1, w2")
    t(f"{op} x0, x1, x2, lsl #9")
    t(f"{op} x0, x1, x2, lsr #33")
    t(f"{op} x0, x1, x2, asr #5")
    t(f"{op} x0, x1, x2, ror #17")
    t(f"{op} w0, w1, w2, ror #3")
for op in ["and", "orr", "eor", "ands"]:
    for imm in ["0xff00ff00ff00ff00", "0x5555555555555555", "0x3", "0x8000000000000001",
                "0xfffffffffffffffe", "0x0000ffff0000ffff", "0x1"]:
        t(f"{op} x0, x1, #{imm}")
    for imm in ["0xf0f0f0f0", "0xffff", "0x7ffffffe", "0x80000001", "0x55555555"]:
        t(f"{op} w0, w1, #{imm}")
t("mvn x0, x1")
t("mvn w0, w2")
t("tst x1, x2")
t("tst w1, #0x80000000")
t("mov x0, x1")
t("mov w0, w1")

# ---- move wide ----
t("movz x0, #0x1234, lsl #16")
t("movz x0, #0xffff, lsl #48")
t("movn x0, #0x1234, lsl #32")
t("movn w0, #0x1234")
t("movz w0, #0xabcd, lsl #16")
t("mov x0, x1", "movk x0, #0xbeef, lsl #32")
t("mov x0, x2", "movk x0, #0x1, lsl #48")
t("mov w0, w2", "movk w0, #0x7fff, lsl #16")
t("mov x0, #-1")
t("mov x0, #0x1000")

# ---- carry ops ----
for pre in ["cmn x1, x2", "cmp x1, x2"]:
    for op in ["adc", "adcs", "sbc", "sbcs"]:
        t(pre, f"{op} x0, x1, x2")
        t(pre, f"{op} w0, w1, w2")
    t(pre, "ngc x0, x2")
    t(pre, "ngcs w0, w2")

# ---- conditional select / compare ----
for cond in CONDS:
    for op in ["csel", "csinc", "csinv", "csneg"]:
        t("cmp x1, x2", f"{op} x0, x1, x2, {cond}")
    t("cmp w1, w2", f"csel w0, w1, w2, {cond}")
    t("cmp w1, w2", f"csneg w0, w1, w2, {cond}")
    if cond != "al":
        t("cmp x1, x2", f"cset x0, {cond}")
        t("cmp x1, x2", f"csetm x0, {cond}")
    t("cmp x1, x2", f"ccmp x1, x2, #5, {cond}")
    t("cmp x1, x2", f"ccmn x1, x2, #10, {cond}")
    t("cmp x1, x2", f"ccmp x1, #7, #3, {cond}")
    t("cmp x1, x2", f"ccmn w1, #31, #0, {cond}")
    t("cmp w1, w2", f"ccmp w1, w2, #8, {cond}")

# ---- multiply / divide ----
for line in [
    "mul x0, x1, x2", "mul w0, w1, w2", "madd x0, x1, x2, x1", "msub x0, x1, x2, x2",
    "mneg x0, x1, x2", "madd w0, w1, w2, w1", "msub w0, w1, w2, w2", "smulh x0, x1, x2",
    "umulh x0, x1, x2", "smaddl x0, w1, w2, x1", "umaddl x0, w1, w2, x2", "smsubl x0, w1, w2, x1",
    "umsubl x0, w1, w2, x2", "smull x0, w1, w2", "umull x0, w1, w2", "smnegl x0, w1, w2",
    "umnegl x0, w1, w2", "udiv x0, x1, x2", "sdiv x0, x1, x2", "udiv w0, w1, w2", "sdiv w0, w1, w2",
    "lsl x0, x1, x2", "lsr x0, x1, x2", "asr x0, x1, x2", "ror x0, x1, x2",
    "lsl w0, w1, w2", "lsr w0, w1, w2", "asr w0, w1, w2", "ror w0, w1, w2",
]:
    t(line)

# ---- 1-source ----
for op in ["rbit", "rev", "rev16", "clz", "cls"]:
    t(f"{op} x0, x1")
    t(f"{op} w0, w1")
t("rev32 x0, x1")

# ---- bitfield / extract ----
for (lsb, w) in [(0, 8), (4, 12), (32, 32), (60, 4), (1, 63), (0, 64), (13, 1), (7, 20)]:
    t(f"ubfx x0, x1, #{lsb}, #{w}")
    t(f"sbfx x0, x1, #{lsb}, #{w}")
    if lsb + w <= 64 and lsb != 0:
        t(f"ubfiz x0, x1, #{lsb}, #{min(w, 64 - lsb)}")
        t(f"sbfiz x0, x1, #{lsb}, #{min(w, 64 - lsb)}")
        t("mov x0, x2", f"bfi x0, x1, #{lsb}, #{min(w, 64 - lsb)}")
    t("mov x0, x2", f"bfxil x0, x1, #{lsb}, #{w}")
for (lsb, w) in [(0, 8), (4, 12), (16, 16), (31, 1), (5, 27)]:
    t(f"ubfx w0, w1, #{lsb}, #{w}")
    t(f"sbfx w0, w1, #{lsb}, #{w}")
    t("mov w0, w2", f"bfxil w0, w1, #{lsb}, #{w}")
    if lsb:
        t(f"ubfiz w0, w1, #{lsb}, #{min(w, 32 - lsb)}")
        t("mov w0, w2", f"bfi w0, w1, #{lsb}, #{min(w, 32 - lsb)}")
for line in [
    "lsl x0, x1, #13", "lsr x0, x1, #7", "asr x0, x1, #63", "lsl x0, x1, #63", "lsl w0, w1, #31",
    "lsr w0, w1, #7", "asr w0, w1, #31", "sxtb x0, w1", "sxth x0, w1", "sxtw x0, w1", "sxtb w0, w1",
    "sxth w0, w1", "uxtb w0, w1", "uxth w0, w1", "extr x0, x1, x2, #17", "extr w0, w1, w2, #9",
    "ror x0, x1, #9", "ror w0, w1, #4", "extr x0, x1, x2, #0",
]:
    t(line)

# ---- branches (each prints 1/0 depending on taken) ----
for cond in CONDS:
    t("cmp x1, x2", "mov x0, #0", f"b.{cond} 1f", "mov x0, #7", "1:")
    t("cmp w1, w2", "mov x0, #0", f"b.{cond} 1f", "mov x0, #7", "1:")
for line in ["cbz x1, 1f", "cbnz x1, 1f", "cbz w1, 1f", "cbnz w1, 1f", "tbz x1, #0, 1f", "tbnz x1, #63, 1f",
             "tbz w1, #31, 1f", "tbnz x1, #5, 1f", "tbz x1, #32, 1f", "cbnz x2, 1f"]:
    t("mov x0, #0", line, "mov x0, #9", "1:")
t("adr x9, 1f", "mov x0, #5", "br x9", "mov x0, #6", "1:")
t("adr x9, 1f", "mov x0, #5", "mov x19, x30", "blr x9", "mov x0, #6", "b 2f", "1: mov x0, #8", "ret", "2: mov x30, x19")

# ---- memory ----
MEM_SETUP = ["adr x3, buf", "add x4, x3, #64", "mov x5, #3", "stp x2, x1, [x3]", "stp x1, x2, [x3, #16]",
             "stp x2, x2, [x4]", "stp x1, x1, [x4, #16]"]
mem = [
    ["str x1, [x3]", "ldr x0, [x3]"],
    ["strb w1, [x3, #9]", "ldrb w0, [x3, #9]"],
    ["strh w1, [x3, #18]", "ldrh w0, [x3, #18]"],
    ["str w1, [x3, #28]", "ldr w0, [x3, #28]"],
    ["strb w1, [x3, #9]", "ldrsb x0, [x3, #9]"],
    ["strb w1, [x3, #9]", "ldrsb w0, [x3, #9]"],
    ["strh w1, [x3, #18]", "ldrsh x0, [x3, #18]"],
    ["strh w1, [x3, #18]", "ldrsh w0, [x3, #18]"],
    ["str w1, [x3, #28]", "ldrsw x0, [x3, #28]"],
    ["stur x1, [x4, #-13]", "ldur x0, [x4, #-13]"],
    ["sturb w1, [x4, #-3]", "ldursb x0, [x4, #-3]"],
    ["sturh w1, [x4, #-5]", "ldursh w0, [x4, #-5]"],
    ["stur w1, [x4, #-9]", "ldursw x0, [x4, #-9]"],
    ["str x1, [x4, #8]!", "sub x0, x4, x3", "bl rep", "ldr x0, [x4, #-8]"],
    ["str w1, [x4], #12", "sub x0, x4, x3", "bl rep", "ldr w0, [x4, #-12]"],
    ["ldr x0, [x4, #16]!", "bl rep", "sub x0, x4, x3"],
    ["ldrh w0, [x4], #-6", "bl rep", "sub x0, x4, x3"],
    ["ldp x6, x7, [x3, #16]", "eor x0, x6, x7", "bl rep", "mov x0, x6"],
    ["ldp w6, w7, [x3, #8]", "mov x0, x7", "bl rep", "mov x0, x6"],
    ["ldpsw x6, x7, [x3, #8]", "mov x0, x7", "bl rep", "mov x0, x6"],
    ["ldp x6, x7, [x4, #16]!", "sub x0, x4, x3", "bl rep", "mov x0, x7"],
    ["ldp x6, x7, [x4], #-16", "sub x0, x4, x3", "bl rep", "mov x0, x6"],
    ["stp w1, w2, [x4, #-16]!", "ldp w6, w7, [x4]", "mov x0, x6", "bl rep", "mov x0, x7"],
    ["stp x1, x2, [x4], #32", "ldp x6, x7, [x4, #-32]", "mov x0, x7", "bl rep", "mov x0, x6"],
    ["stnp x1, x2, [x3, #32]", "ldnp x6, x7, [x3, #32]", "mov x0, x7", "bl rep", "mov x0, x6"],
    ["str x1, [x3, x5, lsl #3]", "ldr x0, [x3, x5, lsl #3]"],
    ["strb w1, [x3, x5]", "ldrb w0, [x3, w5, uxtw]"],
    ["str w1, [x3, w5, sxtw #2]", "ldrsw x0, [x3, w5, sxtw #2]"],
    ["mov x6, #-2", "str x1, [x4, x6, lsl #3]", "ldr x0, [x4, x6, lsl #3]"],
    ["mov x6, #-2", "strh w1, [x4, w6, sxtw #1]", "ldrsh x0, [x4, w6, sxtw #1]"],
    ["ldr x0, [x3, #8]", "bl rep", "ldr w0, [x3, #12]"],
    # exclusives / acquire / release
    ["str x1, [x3]", "ldxr x0, [x3]", "bl rep", "stxr w6, x2, [x3]", "mov x0, x6", "bl rep", "ldr x0, [x3]"],
    ["clrex", "stxr w6, x2, [x3]", "mov x0, x6"],
    ["str x1, [x3]", "ldaxr x0, [x3]", "stlxr w6, x2, [x3]", "bl rep", "mov x0, x6", "bl rep", "ldr x0, [x3]"],
    ["strb w1, [x3]", "ldaxrb w0, [x3]", "stlxrb w6, w2, [x3]", "bl rep", "ldrb w0, [x3]"],
    ["strh w1, [x3]", "ldxrh w0, [x3]", "stxrh w6, w2, [x3]", "bl rep", "ldrh w0, [x3]"],
    ["stlr x1, [x3]", "ldar x0, [x3]"],
    ["stlrb w1, [x3]", "ldarb w0, [x3]"],
    ["stlrh w1, [x3]", "ldarh w0, [x3]"],
    ["stlr w1, [x3]", "ldar w0, [x3]"],
    ["ldxr x0, [x3]", "stxr w6, x2, [x4]", "mov x0, x6"],
    # system registers (only EL0-accessible ones; privileged ones are in tests/sysreg.S)
    ["mrs x0, nzcv"],
    ["and x6, x1, #0xf0000000", "msr nzcv, x6", "mrs x0, nzcv"],
    ["dmb ish", "dsb sy", "isb", "nop", "yield", "mov x0, #77"],
]
for lines in mem:
    t(*(MEM_SETUP + lines))

out = []
lst = []
out.append("    .section .rwtext,\"awx\",@progbits\n    .globl _start\n_start:\n    msr nzcv, xzr\n")
tests = 0
pool = 0
for a in VALUES:
    for b in VALUES:
        out.append(f"    ldr x1, =0x{a:x}\n    ldr x2, =0x{b:x}\n")
        for lines in T:
            body = []
            for ln in lines:
                body.append(("    " + ln) if not ln[0].isdigit() else ln)
            out.append("\n".join(body) + "\n    bl rep\n")
            lst.append(f"x1=0x{a:x} x2=0x{b:x}: " + " ; ".join(lines))
            tests += 1
        pool += 1
        out.append(f"    b .Lskip{pool}\n    .ltorg\n.Lskip{pool}:\n")

out.append("""    mov x0, #0
    mov x8, #93
    svc #0

rep:                                    // print x0 (16 hex digits), NZCV nibble, newline
    mrs x10, nzcv
    lsr x10, x10, #28
    stp x29, x30, [sp, #-16]!
    adr x11, outbuf
    mov x12, #60
1:  lsr x13, x0, x12
    and x13, x13, #0xf
    cmp x13, #10
    add x14, x13, #'0'
    add x15, x13, #('a' - 10)
    csel x13, x15, x14, hs
    strb w13, [x11], #1
    subs x12, x12, #4
    b.ge 1b
    mov w13, #' '
    strb w13, [x11], #1
    cmp x10, #10
    add x14, x10, #'0'
    add x15, x10, #('a' - 10)
    csel x13, x15, x14, hs
    strb w13, [x11], #1
    mov w13, #10
    strb w13, [x11], #1
    adr x1, outbuf
    sub x2, x11, x1
    mov x0, #1
    mov x8, #64
    svc #0
    ldp x29, x30, [sp], #16
    ret

    .balign 16
outbuf: .skip 64
    .balign 16
buf:    .skip 256
""")
open(sys.argv[1], "w").write("".join(out))
open(sys.argv[2], "w").write("\n".join(lst) + "\n")
print(f"{tests} tests generated", file=sys.stderr)
