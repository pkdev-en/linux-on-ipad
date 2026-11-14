#!/bin/sh
# Builds liblxemu.a for iOS (arm64) and type-checks the Swift front end against the iOS SDK.
# Needs Xcode (run on macOS).
set -eu
cd "$(dirname "$0")/.."

SDK=$(xcrun --sdk iphoneos --show-sdk-path)
MIN=17.0
OUT=build/ios
rm -rf "$OUT"
mkdir -p "$OUT"

for f in src/machine.c src/fdt.c src/boot.c src/cpu.c src/display.c src/input.c src/vm.c; do
    xcrun --sdk iphoneos clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN \
        -O2 -std=c11 -Wall -Wextra -c "$f" -o "$OUT/$(basename "$f" .c).o"
done
ar rcs "$OUT/liblxemu.a" "$OUT"/*.o
echo "built $OUT/liblxemu.a"

xcrun --sdk iphoneos swiftc -swift-version 5 -target arm64-apple-ios$MIN -sdk "$SDK" -typecheck \
    -import-objc-header ios/lxemu-Bridging-Header.h ios/*.swift
echo "Swift front end type-checks against the iOS $MIN SDK"
