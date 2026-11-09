#!/bin/sh
# Regenerates Engine/SelfTest.cpp from tests/selftest.S WITHOUT a cross toolchain.
# Assembles with keystone (pip3 install keystone-engine) and validates the
# instruction stream with capstone.
set -e
cd "$(dirname "$0")"
python3 gen_selftest_py.py
