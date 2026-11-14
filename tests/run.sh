#!/bin/sh
# Self-tests. Needs: aarch64-linux-gnu binutils, dtc (device-tree-compiler), python3.
set -eu
cd "$(dirname "$0")/.."
T=build/tests
mkdir -p "$T"
AS=${CROSS:-aarch64-linux-gnu}

echo "== cpu_basic (bare metal)"
$AS-as -o $T/cpu_basic.o tests/cpu_basic.S
$AS-ld -Ttext=0x40000000 -o $T/cpu_basic.elf $T/cpu_basic.o
$AS-objcopy -O binary $T/cpu_basic.elf $T/cpu_basic.bin
./lxemu --raw $T/cpu_basic.bin --max-insns 100000 > $T/cpu_basic.out
[ "$(cat $T/cpu_basic.out)" = "$(printf 'hello\nPASS')" ] || { echo "unexpected output:"; cat $T/cpu_basic.out; exit 1; }
echo "ok"

echo "== device tree"
./lxemu --raw $T/cpu_basic.bin --dump-dtb $T/virt.dtb >/dev/null 2>&1
dtc -I dtb -O dts $T/virt.dtb > $T/virt.dts 2>$T/dtc.err
[ ! -s $T/dtc.err ] || { cat $T/dtc.err; exit 1; }
# dtc 1.7 prints string lists with an escaped NUL ("a\0b"), also for its own output
for want in 'compatible = "arm,pl011\0arm,primecell"' 'method = "hvc"' \
            'arm,cortex-a15-gic' 'device_type = "memory"' 'stdout-path = "/pl011@9000000"'; do
    grep -qF "$want" $T/virt.dts || { echo "missing in DTS: $want"; exit 1; }
done
echo "ok"

echo "== kernel loader + boot protocol"
python3 - <<'PY'
import struct
img = bytearray(0x1000)
struct.pack_into("<I", img, 0, 0xD4400000)           # first insn: hlt #0
struct.pack_into("<Q", img, 8, 0)                    # text_offset
struct.pack_into("<Q", img, 16, 0x200000)            # image_size (incl. BSS)
struct.pack_into("<Q", img, 24, (1 << 1) | (1 << 3)) # 4K pages, any placement
struct.pack_into("<I", img, 56, 0x644d5241)          # "ARM\x64"
open("build/tests/fake.Image", "wb").write(img)
open("build/tests/fake.initrd", "wb").write(b"initrd!")
PY
set +e
./lxemu --kernel $T/fake.Image --initrd $T/fake.initrd --dump-dtb $T/k.dtb 2> $T/k.err
rc=$?
set -e
[ $rc -eq 0 ] || { echo "rc=$rc"; cat $T/k.err; exit 1; }
grep -q '4 KiB pages' $T/k.err || { cat $T/k.err; exit 1; }
# the kernel is 2 MiB incl. BSS, so DTB lands at 0x40200000 and the initrd 2 MiB after it
grep -q 'x0=0x40200000' $T/k.err || { cat $T/k.err; exit 1; }
dtc -I dtb -O dts $T/k.dtb | grep -q 'linux,initrd-start = <0x00 0x40400000>' || { echo "initrd props wrong"; exit 1; }
echo "ok"

echo "== framebuffer: guest pixels reach a screenshot"
$AS-as -o $T/fb_test.o tests/fb_test.S
$AS-ld -Ttext=0x40000000 -o $T/fb_test.elf $T/fb_test.o
$AS-objcopy -O binary $T/fb_test.elf $T/fb_test.bin
./lxemu --raw $T/fb_test.bin --fb 64x32 --screenshot $T/fb.ppm --dump-dtb $T/fb.dtb 2>/dev/null
python3 - <<'PY'
d = open("build/tests/fb.ppm", "rb").read()
hdr = b"P6\n64 32\n255\n"
assert d.startswith(hdr), d[:20]
px = d[len(hdr):]
assert len(px) == 64 * 32 * 3, len(px)
at = lambda i: tuple(px[i * 3:i * 3 + 3])
assert at(0) == (255, 0, 0) and at(63) == (255, 0, 0), "first row must be red"
assert at(64) == (0, 0, 0), "second row must be black"
assert at(64 * 32 - 1) == (0, 0, 255), "last pixel must be blue"
PY
dtc -I dtb -O dts $T/fb.dtb > $T/fb.dts
for want in 'compatible = "simple-framebuffer"' 'format = "x8r8g8b8"' 'width = <0x40>' 'no-map' \
            'reserved-memory'; do
    grep -qF "$want" $T/fb.dts || { echo "missing in DTS: $want"; exit 1; }
done
echo "ok"

echo "== input queue (incl. two-thread stress)"
${CC:-cc} ${CFLAGS:--O2 -g -std=c11 -Wall -Wextra} -pthread -o $T/unit_input tests/unit_input.c src/input.c
$T/unit_input

echo "== public API: sliced run, framebuffer accessor"
${CC:-cc} ${CFLAGS:--O2 -g -std=c11 -Wall -Wextra} -pthread -o $T/unit_vm tests/unit_vm.c build/liblxemu.a
$T/unit_vm $T/cpu_basic.bin > $T/unit_vm.out 2> $T/unit_vm.err || { cat $T/unit_vm.err; exit 1; }
[ "$(cat $T/unit_vm.out)" = "$(printf 'hello\nPASS')" ] || { echo "unexpected output"; cat $T/unit_vm.out; exit 1; }
echo "ok"

echo "== loader rejects non-kernels"
printf 'not a kernel' > $T/junk
if ./lxemu --kernel $T/junk 2>$T/j.err; then echo "accepted junk"; exit 1; fi
grep -q 'missing ARM' $T/j.err
echo "ok"

echo "ALL PASSED"
