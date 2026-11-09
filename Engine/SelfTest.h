// SelfTest.h -- embedded bare-metal smoke test, see Engine/SelfTest.cpp.
// Linux on iPad -- Apache-2.0.
#ifndef LOP_SELFTEST_H
#define LOP_SELFTEST_H

#include <cstddef>
#include <cstdint>

// Returns the assembled selftest program (assembled from tests/selftest.S)
// and its size in bytes.
const uint8_t *lopSelfTestProgram(size_t *len);

// Exact UART output the program must produce (checked by lop_selftest()).
const char *lopSelfTestExpected();

#endif // LOP_SELFTEST_H
