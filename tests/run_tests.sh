#!/bin/sh
# Differential test: run the same AArch64 program under qemu-user and under lopvm, compare output.
set -e
cd "$(dirname "$0")/.."
B=build/tests
mkdir -p "$B"

for tool in aarch64-linux-gnu-as aarch64-linux-gnu-ld aarch64-linux-gnu-objcopy qemu-aarch64 python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: '$tool' not found (see Makefile for what to install)"; exit 0; }
done

echo "== selftest"
c++ -std=c++17 -O2 -IEngine -o "$B/selftest" Engine/*.cpp Engine/*/*.cpp tools/selftest_main.cpp
"$B/selftest"

echo "== privileged system registers"
aarch64-linux-gnu-as -o "$B/sysreg.o" tests/sysreg.S
aarch64-linux-gnu-ld -Ttext=0x40000000 -o "$B/sysreg.elf" "$B/sysreg.o" 2>/dev/null
aarch64-linux-gnu-objcopy -O binary "$B/sysreg.elf" "$B/sysreg.bin"
set +e
build/lopvm --flat "$B/sysreg.bin" --semihost 2> "$B/sysreg.err"
RC=$?
set -e
[ $RC -eq 0 ] && echo "PASS: sysreg" || { echo "FAIL: sysreg test number $RC"; head -c 1500 "$B/sysreg.err"; exit 1; }

if command -v dtc >/dev/null 2>&1; then
    echo "== device tree + Linux boot protocol (fake kernel)"
    dtc -I dts -O dtb -o "$B/lop-virt.dtb" dts/lop-virt.dts 2>/dev/null
    c++ -std=c++17 -O2 -o "$B/test_fdt" tests/test_fdt.cpp Engine/Fdt.cpp
    "$B/test_fdt" "$B/lop-virt.dtb" "$B/patched.dtb"
    aarch64-linux-gnu-as -o "$B/fk.o" tests/fake_kernel.S
    aarch64-linux-gnu-ld -Ttext=0x40000000 -o "$B/fk.elf" "$B/fk.o" 2>/dev/null
    aarch64-linux-gnu-objcopy -O binary "$B/fk.elf" "$B/Image"
    printf 'INITRD!!' > "$B/initrd.bin"
    OUT=$(build/lopvm --kernel "$B/Image" --dtb "$B/lop-virt.dtb" --initrd "$B/initrd.bin" 2>"$B/boot.err" || true)
    EXPECT=$(printf 'dtb ok\ninitrd ok')
    [ "$OUT" = "$EXPECT" ] && echo "PASS: boot protocol" || { echo "FAIL: boot protocol, got: $OUT"; head -c 1500 "$B/boot.err"; exit 1; }
else
    echo "(dtc not found: skipping device tree / boot-protocol test)"
fi

echo "== differential test vs qemu-user"
python3 tests/gen_diff.py "$B/diff.S" "$B/diff.lst"
aarch64-linux-gnu-as -o "$B/diff.o" "$B/diff.S"
aarch64-linux-gnu-ld --section-start=.rwtext=0x40000000 -e _start -o "$B/diff.elf" "$B/diff.o" 2>/dev/null
aarch64-linux-gnu-objcopy -O binary "$B/diff.elf" "$B/diff.bin"
qemu-aarch64 "$B/diff.elf" > "$B/expected.txt"
set +e
build/lopvm --flat "$B/diff.bin" --semihost > "$B/actual.txt" 2> "$B/actual.err"
RC=$?
set -e
if cmp -s "$B/expected.txt" "$B/actual.txt" && [ $RC -eq 0 ]; then
    echo "PASS: $(wc -l < "$B/expected.txt") result lines identical to qemu"
    exit 0
fi
echo "FAIL (lopvm exit code $RC)"
head -c 2000 "$B/actual.err" || true
# first mismatching output line -> which test produced it (a few lines may map to one test)
LINE=$(cmp "$B/expected.txt" "$B/actual.txt" 2>&1 | sed -n 's/.*line \([0-9]*\).*/\1/p')
if [ -n "$LINE" ]; then
    echo "first difference at output line $LINE:"
    echo "  qemu : $(sed -n "${LINE}p" "$B/expected.txt")"
    echo "  lopvm: $(sed -n "${LINE}p" "$B/actual.txt")"
    echo "(output lines map roughly 1:1 to tests in $B/diff.lst; some tests print twice)"
fi
exit 1
