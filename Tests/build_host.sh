#!/usr/bin/env bash
# LinuxOnPad — build & run the engine natively on your Mac/Linux.
# This is the fastest dev loop: fix bugs on the host, then build the IPA.
set -e
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
OUT=build/host_test
mkdir -p build

$CXX -O2 -std=c++17 -Wall -o "$OUT" \
    Tests/host_test.cpp \
    Engine/GuestBus.cpp \
    Engine/Mmu.cpp \
    Engine/Dtb.cpp \
    Engine/Interpreter.cpp \
    Engine/Engine.cpp \
    Engine/EngineInfo.cpp

echo "== M1: bare-metal payload test =="
"$OUT"

echo
echo "== M2: real Linux boot (needs kernel Image + initramfs) =="
#   Kernel: any arm64 Image (Alpine netboot / Debian / your own build)
#   Alpine: https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/netboot/
#   (vmlinuz-lts is an EFI zboot wrapper — extract the gzip payload, then gunzip)
#   Initramfs: Tests/tools/mkinitramfs.py builds a busybox shell initramfs.
#
# ./build/host_test --kernel Image --initramfs initramfs-busybox.cpio.gz \
#     --bootargs "console=ttyAMA0,115200n8 earlycon=pl011,mmio32,0x09000000" --ram 256
