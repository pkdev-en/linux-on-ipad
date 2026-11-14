#!/bin/sh
# Optional: run the bare-metal CPU test on real QEMU as an independent reference.
# QEMU is the oracle; if lxemu and QEMU ever disagree, lxemu is wrong.
set -eu
cd "$(dirname "$0")/.."
command -v qemu-system-aarch64 >/dev/null || { echo "qemu-system-aarch64 not installed, skipping"; exit 0; }
rm -f build/tests/qemu.out
timeout 6 qemu-system-aarch64 -M virt,virtualization=off -cpu cortex-a57 -m 256 \
    -display none -monitor none -serial file:build/tests/qemu.out \
    -device loader,file=build/tests/cpu_basic.bin,addr=0x40200000,cpu-num=0 >/dev/null 2>&1 || true
./lxemu --raw build/tests/cpu_basic.bin > build/tests/lxemu.out 2>/dev/null
if cmp -s build/tests/qemu.out build/tests/lxemu.out; then echo "QEMU and lxemu agree: $(tr '\n' ' ' < build/tests/lxemu.out)"
else echo "MISMATCH"; echo "qemu:";  cat build/tests/qemu.out; echo "lxemu:"; cat build/tests/lxemu.out; exit 1; fi
