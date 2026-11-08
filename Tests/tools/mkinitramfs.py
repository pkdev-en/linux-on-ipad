#!/usr/bin/env python3
"""Build a minimal arm64 initramfs (cpio newc, gzipped) with busybox + /init."""
import gzip, io, os, struct, sys

def cpio_entry(name, mode, data=b"", nlink=1, rdev=(0, 0), ino=1):
    name_b = name.encode()
    fields = [
        ino, mode, 0, 0, nlink, 0,
        len(data), 0, 0, rdev[0], rdev[1], len(name_b) + 1, 0,
    ]
    hdr = b"070701" + b"".join(b"%08X" % f for f in fields)
    out = hdr + name_b + b"\0"
    out += b"\0" * ((4 - len(out) % 4) % 4)
    out += data
    out += b"\0" * ((4 - len(data) % 4) % 4) if len(data) % 4 else b""
    return out

busybox_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/linuxboot/usr/bin/busybox"
out_path = sys.argv[2] if len(sys.argv) > 2 else "/tmp/linuxboot/initramfs-busybox.cpio.gz"

init_sh = b"""#!/bin/busybox sh
/bin/busybox echo "LinuxOnPad: initramfs /init running (busybox shell OK)"
/bin/busybox echo "LinuxOnPad: if you can read this, M2 boot path works"
/bin/busybox mkdir -p /proc /sys /dev
/bin/busybox mount -t proc proc /proc 2>/dev/null
/bin/busybox mount -t sysfs sysfs /sys 2>/dev/null
/bin/busybox echo
/bin/busybox echo "Type commands; this shell talks over the emulated PL011 UART."
exec /bin/busybox sh
"""

with open(busybox_path, "rb") as f:
    busybox = f.read()

entries = [
    cpio_entry(".", 0o40755, nlink=3, ino=1),
    cpio_entry("dev", 0o40755, nlink=2, ino=2),
    cpio_entry("dev/console", 0o020600, rdev=(5, 1), ino=3),
    cpio_entry("bin", 0o40755, nlink=2, ino=4),
    cpio_entry("bin/busybox", 0o755, busybox, ino=5),
    cpio_entry("bin/sh", 0o120777, b"busybox\0", ino=6),
    cpio_entry("init", 0o755, init_sh, ino=7),
    cpio_entry("TRAILER!!!", 0, b"", ino=8),
]

blob = b"".join(entries)
blob += b"\0" * ((512 - len(blob) % 512) % 512)
with gzip.open(out_path, "wb", compresslevel=6) as f:
    f.write(blob)
print(f"wrote {out_path}: {len(blob)} bytes cpio, {os.path.getsize(out_path)} gz")
