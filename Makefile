# Host build of the engine (Mac or Linux). The iOS app is built by XcodeGen / Xcode, not by this file.
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
ENGINE_ALL  := $(wildcard Engine/*.cpp)

all: build/lopvm

build/lopvm: $(ENGINE_ALL) $(wildcard Engine/*.h) tools/lopvm.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(ENGINE_ALL) tools/lopvm.cpp

# Built-in smoke test: runs the guest program embedded in Engine/SelfTest.cpp (no extra tools needed).
selftest: build/lopvm tools/selftest_main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/selftest $(ENGINE_ALL) tools/selftest_main.cpp
	./build/selftest

# Differential tests against qemu-user. Needs: aarch64-linux-gnu-{as,ld,objcopy}, qemu-aarch64, python3.
#   Ubuntu/Debian: sudo apt install binutils-aarch64-linux-gnu qemu-user
test: build/lopvm
	./tests/run_tests.sh

# Device-tree for the virtual board (needs `dtc`).
dtb: build/lop-virt.dtb
build/lop-virt.dtb: dts/lop-virt.dts
	@mkdir -p build
	dtc -I dts -O dtb -o $@ $<

clean:
	rm -rf build

.PHONY: all selftest test dtb clean
