#!/usr/bin/env python3
"""Tiny AArch64 assembler used to build guest payloads without a cross toolchain.

Instructions are encoded with keystone (LLVM assembler -- ground truth).
Control flow with labels (adr/adrp/b/bl/b.cond/cbz/cbnz/tbz/tbnz) is encoded
manually after a two-pass layout, since keystone cannot resolve arbitrary
labels. Directives supported: .text/.globl/.asciz/.quad/.long/.word/.balign/.ltorg.
Every assembled binary can be verified with capstone (see verify()).
"""
import re

from keystone import *
from keystone import KsError

ks = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN)

CONDS = {"eq": 0, "ne": 1, "cs": 2, "hs": 2, "cc": 3, "lo": 3, "mi": 4, "pl": 5,
         "vs": 6, "vc": 7, "hi": 8, "ls": 9, "ge": 10, "lt": 11, "gt": 12, "le": 13,
         "al": 14, "nv": 15}

def sext(v, bits):
    m = 1 << (bits - 1)
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & m else v

def parse_value(tok):
    tok = tok.strip()
    if tok.startswith("'") and tok.endswith("'") and len(tok) == 3:
        return ord(tok[1])
    return int(tok, 0)

class AsmError(Exception):
    pass

class Program:
    def __init__(self):
        self.items = []       # list of ("insn", int) / ("bytes", bytes)
        self.labels = {}      # name -> byte offset
        self.pending = []     # (offset, kind, args, line_no)
        self.fixups = []      # (item_index, size, expr) label expressions

    def add_insn(self, word):
        self.items.append(("insn", word & 0xFFFFFFFF))

    def add_bytes(self, b):
        self.items.append(("bytes", b))

    @property
    def offset(self):
        off = 0
        for kind, payload in self.items:
            off += 4 if kind == "insn" else len(payload)
        return off

    def bytes(self):
        out = bytearray()
        for kind, payload in self.items:
            if kind == "insn":
                out += int(payload).to_bytes(4, "little")
            else:
                out += payload
        return bytes(out)

def _resolve_label(labels, numeric, name, here):
    if name in labels:
        return labels[name]
    m = re.fullmatch(r"(\d+)([bf])", name)
    if m:
        num, d = m.group(1), m.group(2)
        cands = [a for n, a in numeric if n == num]
        if d == "b":
            best = [a for a in cands if a <= here]
            return max(best) if best else None
        best = [a for a in cands if a > here]
        return min(best) if best else None
    return None

def preprocess_literals(src):
    """Rewrites `ldr xN, =imm` GNU-as literal form into an explicit pool:
       ldr xN, .LpoolK ; b .LendK ; .LpoolK: .quad imm ; .LendK:"""
    out = []
    k = 0
    for raw in src.splitlines():
        m = re.match(r"^\s*ldr\s+([xw]\d+),\s*=(.+?)\s*(?://.*)?$", raw)
        if m:
            reg, val = m.group(1), m.group(2).strip()
            word = ".quad" if reg.startswith("x") else ".word"
            out.append(f"    ldr {reg}, .Lpool{k}")
            out.append(f"    b .Lend{k}")
            out.append(f".Lpool{k}: {word} {val}")
            out.append(f".Lend{k}:")
            k += 1
        else:
            out.append(raw)
    return "\n".join(out)

def assemble(src, base=0x40000000):
    """Assembles source text into (bytes, labels). Two passes."""
    src = preprocess_literals(src)
    lines = src.splitlines()
    # ---- pass 1: layout -----------------------------------------------------
    prog = Program()
    labels = {}
    numeric = []  # numeric labels may repeat
    parse_queue = []  # (item_index_in_text, line_no, text, offset)
    for ln_no, raw in enumerate(lines, 1):
        line = raw.split("//")[0].split(";")[0].strip()
        if not line:
            continue
        # labels (possibly several on one line)
        while True:
            m = re.match(r"^([A-Za-z_.$][\w.$]*|\d+):", line)
            if not m:
                break
            name = m.group(1)
            if re.fullmatch(r"\d+", name):
                numeric.append((name, prog.offset + base))
            else:
                labels[name] = prog.offset + base
            line = line[m.end():].strip()
        if not line:
            continue
        if line.startswith("."):
            handle_directive(prog, line, ln_no)
            continue
        parse_queue.append((ln_no, line, prog.offset + base))
        prog.add_insn(0)  # placeholder, filled in pass 2

    # ---- pass 2: encode -----------------------------------------------------
    item_idx = 0
    for ln_no, text, addr in parse_queue:
        word = encode_line(text, addr, labels, ln_no, numeric)
        while prog.items[item_idx][0] != "insn":
            item_idx += 1
        prog.items[item_idx] = ("insn", word)
        item_idx += 1
    # resolve label-expression fixups ("_end - _start", "A + B", ...)
    for (idx, size, expr) in prog.fixups:
        val = eval_label_expr(expr, labels, ln_no)
        prog.items[idx] = ("bytes", val.to_bytes(size, "little"))
    return prog.bytes(), labels

def eval_label_expr(expr, labels, ln_no):
    m = re.fullmatch(r"\s*([A-Za-z_.$][\w.$]*)\s*([-+])\s*([A-Za-z_.$][\w.$]*)\s*", expr)
    if m and m.group(2) == "-":
        a = labels.get(m.group(1))
        b = labels.get(m.group(3))
        if a is None or b is None:
            raise AsmError(f"line {ln_no}: unknown label in '{expr}'")
        return a - b
    if m and m.group(2) == "+":
        a = labels.get(m.group(1))
        if a is None:
            raise AsmError(f"line {ln_no}: unknown label in '{expr}'")
        return a + parse_value(m.group(3))
    if expr in labels:
        return labels[expr]
    raise AsmError(f"line {ln_no}: cannot evaluate '{expr}'")

def handle_directive(prog, line, ln_no):
    m = re.match(r"^\.(\w+)\s*(.*)$", line)
    if not m:
        raise AsmError(f"line {ln_no}: bad directive {line}")
    name, rest = m.group(1), m.group(2).strip()
    if name in ("text", "globl", "global", "ltorg", "section", "type", "size", "align"):
        return
    if name == "asciz" or name == "string":
        s = re.match(r'^"(.*)"$', rest)
        if not s:
            raise AsmError(f"line {ln_no}: bad .asciz")
        data = s.group(1).encode().decode("unicode_escape").encode() + b"\0"
        prog.add_bytes(data)
        return
    if name in ("quad", "8byte"):
        for v in rest.split(","):
            v = v.strip()
            try:
                prog.add_bytes(parse_value(v).to_bytes(8, "little"))
            except ValueError:
                idx = len(prog.items)
                prog.add_bytes(b"\0" * 8)
                prog.fixups.append((idx, 8, v))
        return
    if name in ("long", "word", "4byte"):
        for v in rest.split(","):
            v = v.strip()
            try:
                prog.add_bytes(parse_value(v).to_bytes(4, "little"))
            except ValueError:
                idx = len(prog.items)
                prog.add_bytes(b"\0" * 4)
                prog.fixups.append((idx, 4, v))
        return
    if name == "balign" or name == "align":
        want = parse_value(rest)
        while prog.offset % want:
            prog.add_bytes(b"\0")
        return
    if name == "skip" or name == "space":
        n = parse_value(rest.split(",")[0])
        prog.add_bytes(b"\0" * n)
        return
    raise AsmError(f"line {ln_no}: unsupported directive .{name}")

def encode_line(text, addr, labels, ln_no, numeric=()):
    # strip comments already done; normalize commas
    text = text.strip()
    low = text
    # ---- branch-ish instructions need label resolution ----
    m = re.match(r"^(adr|adrp)\s+(x\d+|w\d+|sp),\s*(.+)$", text)
    if m:
        op, rd, target = m.group(1), m.group(2), m.group(3).strip()
        rd_num = int(re.sub(r"\D", "", rd)) if rd != "sp" else 31
        if target.startswith("#"):
            imm = parse_value(target)
        else:
            t = _resolve_label(labels, numeric, target, addr)
            if t is None:
                raise AsmError(f"line {ln_no}: unknown label {target}")
            imm = (t - addr) if op == "adr" else ((t & ~0xFFF) - (addr & ~0xFFF))
        imm = sext(imm, 21)
        assert -(1 << 20) <= imm < (1 << 20), f"adr out of range at line {ln_no}"
        immlo = imm & 3
        immhi = (imm >> 2) & 0x1FFFFF
        base = 0x90000000 if op == "adrp" else 0x10000000
        return base | (immlo << 29) | (immhi << 5) | rd_num
    m = re.match(r"^b\.(\w+)\s+(.+)$", text)
    if m:
        cond = CONDS[m.group(1)]
        t = _resolve_label(labels, numeric, m.group(2).strip(), addr)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {m.group(2)}")
        off = sext(t - addr, 21) >> 2
        return 0x54000000 | ((off & 0x7FFFF) << 5) | cond
    m = re.match(r"^b\s+(.+)$", text)
    if m:
        t = _resolve_label(labels, numeric, m.group(1).strip(), addr)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {m.group(1)}")
        off = sext(t - addr, 28) >> 2
        return 0x14000000 | (off & 0x3FFFFFF)
    m = re.match(r"^bl\s+(.+)$", text)
    if m:
        t = _resolve_label(labels, numeric, m.group(1).strip(), addr)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {m.group(1)}")
        off = sext(t - addr, 28) >> 2
        return 0x94000000 | (off & 0x3FFFFFF)
    m = re.match(r"^(cbz|cbnz)\s+([xw]\d+|sp),\s*(.+)$", text)
    if m:
        op, rt, target = m.group(1), m.group(2), m.group(3).strip()
        sf = 1 if rt.startswith("x") else 0
        rt_num = int(re.sub(r"\D", "", rt)) if rt != "sp" else 31
        t = _resolve_label(labels, numeric, target, addr)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {target}")
        off = sext(t - addr, 21) >> 2
        base = 0x35000000 if op == "cbnz" else 0x34000000
        return base | (sf << 31) | ((off & 0x7FFFF) << 5) | rt_num
    m = re.match(r"^ldr\s+([xw]\d+),\s*([A-Za-z_.$][\w.$]*)$", text)
    if m:  # LDR (literal)
        rd, target = m.group(1), m.group(2)
        rd_num = int(re.sub(r"\D", "", rd))
        t = _resolve_label(labels, numeric, target, addr)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {target}")
        off = sext(t - addr, 21) >> 2
        base = 0x58000000 if rd.startswith("x") else 0x18000000
        return base | ((off & 0x7FFFF) << 5) | rd_num
    m = re.match(r"^(tbz|tbnz)\s+([xw]\d+),\s*#(\d+),\s*(.+)$", text)
    if m:
        op, rt, bit, target = m.groups()
        sf = 1 if rt.startswith("x") else 0
        rt_num = int(re.sub(r"\D", "", rt))
        b5 = (int(bit) >> 5) & 1
        b40 = int(bit) & 0x1F
        t = _resolve_label(labels, target.strip(), addr, None)
        if t is None:
            raise AsmError(f"line {ln_no}: unknown label {target}")
        off = sext(t - addr, 16) >> 2
        base = 0x37000000 if op == "tbnz" else 0x36000000
        return base | (b5 << 31) | (b40 << 19) | ((off & 0x3FFF) << 5) | rt_num
    # ---- everything else goes through keystone ----
    try:
        enc, _ = ks.asm(text)  # keystone accepts any case
    except KsError as e:
        raise AsmError(f"line {ln_no}: keystone failed on '{text}': {e}")
    if len(enc) != 4:
        raise AsmError(f"line {ln_no}: expected 4 bytes, got {len(enc)} for '{text}'")
    return int.from_bytes(bytes(enc), "little")

def verify(data, base=0x40000000):
    """Disassemble with capstone and return list of (addr, size, mnemonic, op_str)."""
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    out = []
    for i in md.disasm(data, base):
        out.append((i.address, i.size, i.mnemonic, i.op_str))
    return out

if __name__ == "__main__":
    import sys
    src = open(sys.argv[1]).read()
    data, labels = assemble(src)
    sys.stdout.write(data.hex())
