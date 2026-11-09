// EngineApi.h -- small C ABI for the SwiftUI app (bridging header).
// Linux on iPad -- Apache-2.0.
#ifndef LOP_ENGINE_API_H
#define LOP_ENGINE_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runs the embedded bare-metal selftest (tests/selftest.S) on a fresh
// Machine, collects the PL011 output into `out` (NUL-terminated, truncated
// to `cap` bytes) and returns 0 when the output matches the expected text.
int lop_selftest(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // LOP_ENGINE_API_H
