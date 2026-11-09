#!/usr/bin/env python3
"""Generates a flattened device tree blob equivalent to `dtc -I dts -O dtb
dts/lop-virt.dts`, so the boot-protocol test can run on machines without dtc.
usage: gen_dtb.py out.dtb"""
import struct
import sys

def align(n, a=4):
    return (n + a - 1) & ~(a - 1)

class FDT:
    def __init__(self):
        self.strings = bytearray()
        self.struct = bytearray()
        self.stack = []

    def _str(self, s):
        off = self.strings.find(s.encode() + b"\0")
        if off < 0:
            off = len(self.strings)
            self.strings += s.encode() + b"\0"
        return off

    def begin_node(self, name):
        self.struct += struct.pack(">I", 1) + name.encode() + b"\0"
        self.struct += b"\0" * (align(len(self.struct)) - len(self.struct))

    def end_node(self):
        self.struct += struct.pack(">I", 2)

    def prop(self, name, data):
        self.struct += struct.pack(">III", 3, len(data), self._str(name))
        self.struct += data
        self.struct += b"\0" * (align(len(self.struct)) - len(self.struct))

    def prop_u32(self, name, values):
        self.prop(name, b"".join(struct.pack(">I", v) for v in values))

    def prop_u64(self, name, value):
        self.prop(name, struct.pack(">Q", value))

    def prop_str(self, name, value):
        self.prop(name, value.encode() + b"\0")

    def prop_empty(self, name):
        self.prop(name, b"")

    def build(self, boot_cpuid=0):
        rsv_off = 40                       # reservation block right after the header
        struct_off = rsv_off + 16          # one empty reservation entry
        strings_off = struct_off + align(len(self.struct) + 4)  # + END token
        total = align(strings_off + len(self.strings))
        header = struct.pack(">10I",
                             0xD00DFEED, total, struct_off, strings_off, rsv_off,
                             17, 16, boot_cpuid, len(self.strings), align(len(self.struct) + 4))
        blob = header + b"\0" * 16
        blob += self.struct + struct.pack(">I", 9)
        blob += self.strings
        blob += b"\0" * (total - len(blob))
        return blob

def main():
    f = FDT()
    f.begin_node("")
    f.prop_str("compatible", "linuxonpad,virt")
    f.prop_str("model", "LinuxOnPad virt")
    f.prop_u32("#address-cells", [2])
    f.prop_u32("#size-cells", [2])
    f.prop_str("interrupt-parent", "gic")  # phandle-less reference; dtc uses a phandle

    f.begin_node("chosen")
    f.prop_str("bootargs", "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/init")
    f.prop_str("stdout-path", "/pl011@9000000")
    f.prop_u64("linux,initrd-start", 0x4A000000)
    f.prop_u64("linux,initrd-end", 0x4A000000)
    f.end_node()

    f.begin_node("memory@40000000")
    f.prop_str("device_type", "memory")
    f.prop_u32("reg", [0, 0x40000000, 0, 0x20000000])
    f.end_node()

    f.begin_node("cpus")
    f.prop_u32("#address-cells", [1])
    f.prop_u32("#size-cells", [0])
    f.begin_node("cpu@0")
    f.prop_str("device_type", "cpu")
    f.prop_str("compatible", "arm,arm-v8")
    f.prop_u32("reg", [0])
    f.end_node()
    f.end_node()

    f.begin_node("timer")
    f.prop_str("compatible", "arm,armv8-timer")
    f.prop_empty("always-on")
    f.prop_u32("interrupts", [1, 13, 0x104, 1, 14, 0x104, 1, 11, 0x104, 1, 10, 0x104])
    f.end_node()

    f.begin_node("apb-pclk")
    f.prop_str("compatible", "fixed-clock")
    f.prop_u32("#clock-cells", [0])
    f.prop_u32("clock-frequency", [24000000])
    f.prop_str("clock-output-names", "clk24mhz")
    f.end_node()

    f.begin_node("interrupt-controller@8000000")
    f.prop_str("compatible", "arm,cortex-a15-gic")
    f.prop_u32("#interrupt-cells", [3])
    f.prop_empty("interrupt-controller")
    f.prop_u32("reg", [0, 0x08000000, 0, 0x10000, 0, 0x08010000, 0, 0x10000])
    f.end_node()

    f.begin_node("pl011@9000000")
    f.prop_str("compatible", "arm,pl011")
    f.prop_u32("reg", [0, 0x09000000, 0, 0x1000])
    f.prop_u32("interrupts", [0, 1, 4])
    f.end_node()

    f.end_node()

    out = sys.argv[1] if len(sys.argv) > 1 else "build/lop-virt.dtb"
    blob = f.build()
    open(out, "wb").write(blob)
    print(f"wrote {out} ({len(blob)} bytes)")

if __name__ == "__main__":
    main()
