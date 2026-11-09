// EngineApi.cpp -- C ABI glue for the SwiftUI app, see EngineApi.h.
#include "EngineApi.h"

#include <cstdio>
#include <string>
#include <cstring>

#include "Machine.h"
#include "SelfTest.h"

int lop_selftest(char *out, size_t cap) {
    if (cap == 0) return 1;
    out[0] = '\0';

    lop::MachineConfig cfg;
    cfg.ramSize = 4ull << 20; // the selftest payload is tiny
    lop::Machine m(cfg);

    std::string uart;
    m.uart().setTxSink([&uart](uint8_t b) {
        if (uart.size() < 4096) uart.push_back(char(b));
    });

    size_t len = 0;
    const uint8_t *prog = lopSelfTestProgram(&len);
    std::string err;
    if (!m.loadFlat(prog, len, cfg.ramBase, err)) {
        std::snprintf(out, cap, "selftest load failed: %s", err.c_str());
        return 1;
    }

    m.run(100000000); // hlt stops well before the budget

    size_t n = uart.size() < cap - 1 ? uart.size() : cap - 1;
    std::memcpy(out, uart.data(), n);
    out[n] = '\0';
    return uart == lopSelfTestExpected() ? 0 : 1;
}
