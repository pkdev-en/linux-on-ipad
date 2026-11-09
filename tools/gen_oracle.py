#!/usr/bin/env python3
"""Generates build/semtest_cases.cpp: a table of instruction-semantics test
cases with expected results recorded from Unicorn (QEMU's engine), used by
tools/semtest_main.cpp to differential-test the LinuxOnPad interpreter.

Templates mirror tests/gen_diff.py (which run_tests.sh diffs against qemu-user
on CI); this file provides the same coverage on any machine with pip.

usage: gen_oracle.py out_cases.cpp
requires: pip3 install keystone-engine unicorn capstone
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pyasm

from unicorn import *
from unicorn.arm64_const import *

REPO = os.environ.get("LOP_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = 0x40000000
BUF = BASE + 0x1000
STACK_TOP = BASE + 0x10000 - 16

# ---------------------------------------------------------------- templates
# Same templates as tests/gen_diff.py (kept in sync).
VALUES = [
    0x0, 0x1, 0x7F, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFFFFFFFFFF,
    0x8000000000000000, 0xFFFFFFFFFFFFFFFF, 0x123456789ABCDEF0, 0xDEADBEEFCAFEBABE,
]
CONDS = ["eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al"]

T = []
def t(*lines):
    T.append(list(lines))

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

for pre in ["cmn x1, x2", "cmp x1, x2"]:
    for op in ["adc", "adcs", "sbc", "sbcs"]:
        t(pre, f"{op} x0, x1, x2")
        t(pre, f"{op} w0, w1, w2")
    t(pre, "ngc x0, x2")
    t(pre, "ngcs w0, w2")

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

for op in ["rbit", "rev", "rev16", "clz", "cls"]:
    t(f"{op} x0, x1")
    t(f"{op} w0, w1")
t("rev32 x0, x1")

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

for cond in CONDS:
    t("cmp x1, x2", "mov x0, #0", f"b.{cond} 1f", "mov x0, #7", "1:")
    t("cmp w1, w2", "mov x0, #0", f"b.{cond} 1f", "mov x0, #7", "1:")
for line in ["cbz x1, 1f", "cbnz x1, 1f", "cbz w1, 1f", "cbnz w1, 1f", "tbz x1, #0, 1f",
             "tbnz x1, #63, 1f", "tbz w1, #31, 1f", "tbnz x1, #5, 1f", "tbz x1, #32, 1f",
             "cbnz x2, 1f"]:
    t("mov x0, #0", line, "mov x0, #9", "1:")
t("adr x9, 1f", "mov x0, #5", "br x9", "mov x0, #6", "1:")
t("adr x9, 1f", "mov x0, #5", "mov x19, x30", "blr x9", "mov x0, #6", "b 2f", "1: mov x0, #8", "ret", "2: mov x30, x19")

MEM_SETUP = ["adr x3, buf", "add x4, x3, #64", "mov x5, #3", "stp x2, x1, [x3]",
             "stp x1, x2, [x3, #16]", "stp x2, x2, [x4]", "stp x1, x1, [x4, #16]"]
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
    ["str x1, [x4, #8]!", "sub x0, x4, x3", "ldr x0, [x4, #-8]"],
    ["str w1, [x4], #12", "sub x0, x4, x3", "ldr w0, [x4, #-12]"],
    ["ldr x0, [x4, #16]!", "sub x0, x4, x3"],
    ["ldrh w0, [x4], #-6", "sub x0, x4, x3"],
    ["ldp x6, x7, [x3, #16]", "eor x0, x6, x7", "mov x0, x6"],
    ["ldp w6, w7, [x3, #8]", "mov x0, x7", "mov x0, x6"],
    ["ldpsw x6, x7, [x3, #8]", "mov x0, x7", "mov x0, x6"],
    ["ldp x6, x7, [x4, #16]!", "sub x0, x4, x3", "mov x0, x7"],
    ["ldp x6, x7, [x4], #-16", "sub x0, x4, x3", "mov x0, x6"],
    ["stp w1, w2, [x4, #-16]!", "ldp w6, w7, [x4]", "mov x0, x6", "mov x0, x7"],
    ["stp x1, x2, [x4], #32", "ldp x6, x7, [x4, #-32]", "mov x0, x7", "mov x0, x6"],
    ["stnp x1, x2, [x3, #32]", "ldnp x6, x7, [x3, #32]", "mov x0, x7", "mov x0, x6"],
    ["str x1, [x3, x5, lsl #3]", "ldr x0, [x3, x5, lsl #3]"],
    ["strb w1, [x3, x5]", "ldrb w0, [x3, w5, uxtw]"],
    ["str w1, [x3, w5, sxtw #2]", "ldrsw x0, [x3, w5, sxtw #2]"],
    ["mov x6, #-2", "str x1, [x4, x6, lsl #3]", "ldr x0, [x4, x6, lsl #3]"],
    ["mov x6, #-2", "strh w1, [x4, w6, sxtw #1]", "ldrsh x0, [x4, w6, sxtw #1]"],
    ["ldr x0, [x3, #8]", "ldr w0, [x3, #12]"],
    ["str x1, [x3]", "ldxr x0, [x3]", "stxr w6, x2, [x3]", "mov x0, x6", "ldr x0, [x3]"],
    ["clrex", "stxr w6, x2, [x3]", "mov x0, x6"],
    ["str x1, [x3]", "ldaxr x0, [x3]", "stlxr w6, x2, [x3]", "mov x0, x6", "ldr x0, [x3]"],
    ["strb w1, [x3]", "ldaxrb w0, [x3]", "stlxrb w6, w2, [x3]", "ldrb w0, [x3]"],
    ["strh w1, [x3]", "ldxrh w0, [x3]", "stxrh w6, w2, [x3]", "ldrh w0, [x3]"],
    ["stlr x1, [x3]", "ldar x0, [x3]"],
    ["stlrb w1, [x3]", "ldarb w0, [x3]"],
    ["stlrh w1, [x3]", "ldarh w0, [x3]"],
    ["stlr w1, [x3]", "ldar w0, [x3]"],
    ["ldxr x0, [x3]", "stxr w6, x2, [x4]", "mov x0, x6"],
    ["ldxp x0, x1, [x3]", "mov x0, x1"],
    ["stxp w6, x1, x2, [x3]", "mov x0, x6", "ldp x4, x5, [x3]", "mov x0, x5"],
    ["mrs x0, nzcv"],
    ["and x6, x1, #0xf0000000", "msr nzcv, x6", "mrs x0, nzcv"],
    ["dmb ish", "dsb sy", "isb", "nop", "yield", "mov x0, #77"],
    ["adrp x0, buf"],
    ["adr x0, buf"],
]
for lines in mem:
    t(*(MEM_SETUP + lines))

# ---------------------------------------------------------------- oracle

def load_reg(val, reg="x9"):
    insns = []
    for shift in (0, 16, 32, 48):
        part = (val >> shift) & 0xFFFF
        if shift == 0:
            insns.append(f"movz {reg}, #{part}")
        else:
            insns.append(f"movk {reg}, #{part}, lsl #{shift}")
    return insns

def build_code(template_lines, a, b):
    """Prelude (x1=a, x2=b) + branch + buffer + template + hlt.
    Returns (bytes, prelude_insn_count)."""
    prelude = load_reg(a, "x1") + load_reg(b, "x2")
    src = "\n".join("    " + ln for ln in prelude)
    src += "\n    b .Ltemplate\n"
    src += "buf:\n    .skip 96\n"
    src += ".Ltemplate:\n"
    for ln in template_lines:
        if ln.strip() == "bl rep":
            continue
        body = ln if ln[0].isdigit() else ("    " + ln)
        src += body + "\n"
    src += "    hlt #0\n"
    data, labels = pyasm.assemble(src)
    return data, len(prelude) + 1  # + the b .Ltemplate

def count_template_insns(template_lines):
    n = 0
    for ln in template_lines:
        s = ln.split("//")[0].strip()
        if not s or s == "bl rep":
            continue
        if ":" in s:
            head, rest = s.split(":", 1)
            if head.isdigit():
                n += 1 if rest.strip() else 0
                continue
        n += 1
    return n

HLT = 0xD4400000

def run_unicorn(data, n_prelude, want):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 0x10000)
    uc.mem_write(BASE, data)
    uc.reg_write(UC_ARM64_REG_SP, STACK_TOP)
    hlt_addr = BASE + len(data) - 4
    pc = BASE
    for _ in range(n_prelude):
        uc.emu_start(pc, 0, 0, 1)
        pc = uc.reg_read(UC_ARM64_REG_PC)
    checkpoints = []
    for _ in range(want + 8):
        if pc == hlt_addr:
            break
        uc.emu_start(pc, 0, 0, 1)
        pc = uc.reg_read(UC_ARM64_REG_PC)
        x0 = uc.reg_read(UC_ARM64_REG_X0)
        x30 = uc.reg_read(UC_ARM64_REG_X30)
        sp = uc.reg_read(UC_ARM64_REG_SP)
        nzcv = (uc.reg_read(UC_ARM64_REG_NZCV) >> 28) & 0xF
        buf = bytes(uc.mem_read(BUF, 32))
        checkpoints.append((x0, x30, sp, nzcv, buf))
    return checkpoints

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "build/semtest_cases.cpp"
    cases = []
    skipped = []

    for ti, template in enumerate(T):
        pairs = ([(a, b) for a in VALUES for b in VALUES] if ti % 3 == 0
                 else list(zip(VALUES, VALUES[::-1])))
        for (a, b) in pairs:
            want = count_template_insns(template)
            try:
                data, npre = build_code(template, a, b)
                cps = run_unicorn(data, npre, want)
                if not (1 <= len(cps) <= want + 8):
                    raise RuntimeError(f"checkpoints {len(cps)} vs {want}")
            except Exception as e:
                skipped.append((" ".join(template)[:50], str(e)[:70]))
                break
            name = (" | ".join(s.strip() for s in template))[:180]
            cases.append((name, data, npre, cps))

    print(f"cases kept: {len(cases)}, templates skipped: {len(skipped)}", file=sys.stderr)
    for s in skipped[:12]:
        print(f"  skip: {s}", file=sys.stderr)

    allcode = bytearray()
    cps_blob = []
    case_rows = []
    for (name, data, npre, cps) in cases:
        cstart = len(allcode)
        allcode += data
        cpstart = len(cps_blob)
        cps_blob.extend(cps)
        case_rows.append((name, cstart, len(data), npre, len(cps), cpstart))

    with open(out_path, "w") as f:
        f.write("// GENERATED by tools/gen_oracle.py -- do not edit.\n")
        f.write("// Semantics oracle: expected values recorded from Unicorn (QEMU engine).\n")
        f.write("#include <cstdint>\n#include <cstddef>\n\n")
        f.write("namespace lopsem {\n\n")
        f.write("struct Checkpoint { uint64_t x0, x30, sp; uint32_t nzcv; uint8_t buf[32]; };\n")
        f.write("struct Case { const char *name; const uint8_t *code; uint32_t codeLen;\n"
                "             int preludeInsns; int nCheckpoints; const Checkpoint *cps; };\n\n")
        f.write("static const int kBufOffset = 0x1000;\n\n")
        f.write("static const Checkpoint kCps[] = {\n")
        for (x0, x30, sp, nzcv, buf) in cps_blob:
            f.write("    {%d,%d,%d,%d,{%s}},\n" % (x0, x30, sp, nzcv,
                                                   ",".join(str(v) for v in buf)))
        f.write("};\n\nstatic const uint8_t kCode[] = {\n")
        for i in range(0, len(allcode), 16):
            f.write("    " + ",".join("0x%02x" % v for v in allcode[i:i+16]) + ",\n")
        f.write("};\n\n")
        f.write("static const int kNumCases = %d;\n" % len(case_rows))
        f.write("static const Case kCases[] = {\n")
        for (name, cstart, clen, npre, ncps, cpstart) in case_rows:
            esc = name.replace("\\", "\\\\").replace('"', '\\"')
            f.write('    {"%s", kCode+%d, %d, %d, %d, kCps+%d},\n'
                    % (esc, cstart, clen, npre, ncps, cpstart))
        f.write("};\n\n} // namespace lopsem\n")
    print(f"wrote {out_path}: {len(case_rows)} cases, {len(allcode)} code bytes, "
          f"{len(cps_blob)} checkpoints", file=sys.stderr)

if __name__ == "__main__":
    main()
