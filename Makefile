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

# Regenerate Engine/SelfTest.cpp from tests/selftest.S.
#   tools/gen_selftest.sh    needs aarch64-linux-gnu-{as,ld,objcopy} (Ubuntu: binutils-aarch64-linux-gnu)
#   tools/gen_selftest_py.py needs only: pip3 install keystone-engine
selftest-gen:
	tools/gen_selftest_py.py

# Differential semantics test: 24k+ instruction cases with expected results
# recorded from Unicorn (QEMU's engine). Needs: pip3 install keystone-engine unicorn capstone
semtest: build/lopvm build/semtest_cases.cpp
	$(CXX) $(CXXFLAGS) -Ibuild -o build/semtest $(ENGINE_ALL) tools/semtest_main.cpp
	./build/semtest

build/semtest_cases.cpp: tools/gen_oracle.py tools/pyasm.py
	@mkdir -p build
	python3 tools/gen_oracle.py build/semtest_cases.cpp

# Differential tests against qemu-user. Needs: aarch64-linux-gnu-{as,ld,objcopy}, qemu-aarch64, python3.
#   Ubuntu/Debian: sudo apt install binutils-aarch64-linux-gnu qemu-user
test: build/lopvm
	./tests/run_tests.sh

# Device-tree for the virtual board (needs `dtc`; or use tools/gen_dtb.py without it).
dtb: build/lop-virt.dtb
build/lop-virt.dtb: dts/lop-virt.dts
	@mkdir -p build
	dtc -I dts -O dtb -o $@ $<
dtb-py:
	@mkdir -p build
	python3 tools/gen_dtb.py build/lop-virt.dtb

clean:
	rm -rf build

.PHONY: all selftest selftest-gen semtest test dtb dtb-py clean
