// CPU/ARM64CPU.cpp -- stop reporting, sysreg naming and the basic
// fetch-decode-execute loop, see CPU/ARM64CPU.hpp.
// Linux on iPad -- Apache-2.0.
#include "CPU/ARM64CPU.hpp"

#include <cstdio>

#include "Decode.h"
#include "Interp.h"

namespace lop {

std::string ARM64CPU::describeStop() const {
    char buf[160];
    switch (stopReason) {
    case StopReason::Running:
        std::snprintf(buf, sizeof buf, "running (pc=0x%llx)", (unsigned long long)pc);
        return buf;
    case StopReason::Halt:
        std::snprintf(buf, sizeof buf, "halt: hlt #%u at pc=0x%llx", stopImm,
                      (unsigned long long)pc);
        return buf;
    case StopReason::Svc:
        std::snprintf(buf, sizeof buf, "svc #%u at pc=0x%llx", stopImm,
                      (unsigned long long)pc);
        return buf;
    case StopReason::Unimplemented:
        std::snprintf(buf, sizeof buf,
                      "unimplemented instruction 0x%08llx at pc=0x%llx",
                      (unsigned long long)badInsn, (unsigned long long)pc);
        return buf;
    case StopReason::DataAbort:
        std::snprintf(buf, sizeof buf, "data abort: access to 0x%llx at pc=0x%llx",
                      (unsigned long long)faultAddr, (unsigned long long)pc);
        return buf;
    case StopReason::FetchAbort:
        std::snprintf(buf, sizeof buf, "fetch abort: pc=0x%llx",
                      (unsigned long long)faultAddr);
        return buf;
    }
    return "stopped";
}

std::string ARM64CPU::sysregName(uint64_t id) {
    unsigned op0 = (id >> 20) & 3, op1 = (id >> 17) & 7;
    unsigned crn = (id >> 12) & 15, crm = (id >> 8) & 15, op2 = (id >> 5) & 7;
    struct Entry { unsigned o0, o1, crn, crm, o2; const char *name; };
    static const Entry kNames[] = {
        {3, 0, 1, 0, 0, "sctlr_el1"},
        {3, 0, 1, 0, 1, "actlr_el1"},
        {3, 0, 4, 2, 0, "spsel"},
        {3, 0, 4, 2, 1, "daif"},
        {3, 0, 4, 2, 2, "currentel"},
        {3, 0, 4, 6, 6, "daifset"},
        {3, 0, 4, 6, 7, "daifclr"},
        {3, 0, 12, 0, 0, "vbar_el1"},
        {3, 0, 13, 0, 4, "tpidr_el1"},
        {3, 0, 0, 0, 0, "midr_el1"},
        {3, 0, 0, 0, 5, "mpidr_el1"},
        {3, 3, 14, 0, 0, "cntfrq_el0"},
        {3, 3, 14, 0, 1, "cntpct_el0"},
        {3, 3, 14, 0, 2, "cntvct_el0"},
        {3, 3, 14, 2, 1, "cntp_ctl_el0"},
        {3, 3, 14, 2, 2, "cntp_cval_el0"},
        {3, 3, 14, 3, 1, "cntv_ctl_el0"},
        {3, 3, 14, 3, 2, "cntv_cval_el0"},
        {3, 3, 4, 2, 0, "nzcv"},
    };
    for (const Entry &e : kNames) {
        if (e.o0 == op0 && e.o1 == op1 && e.crn == crn && e.crm == crm && e.o2 == op2) {
            return e.name;
        }
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "s%u_%u_%u_%u_%u", op0, op1, crn, crm, op2);
    return buf;
}

void ARM64CPU::reset() {
    bool keepTrace = trace;
    *this = ARM64CPU{};
    trace = keepTrace;
}

StopReason ARM64CPU::run(MemoryBus &bus, uint64_t maxInstructions) {
    // Resumable stops: Svc clears and execution continues; terminal stops
    // (Halt/Error) return immediately forever.
    switch (stopReason) {
    case StopReason::Halt: return StopReason::Halt;
    case StopReason::Unimplemented:
    case StopReason::DataAbort:
    case StopReason::FetchAbort: return stopReason;
    case StopReason::Svc: stopReason = StopReason::Running; break;
    case StopReason::Running: break;
    }

    for (uint64_t i = 0; i < maxInstructions; ++i) {
        uint64_t pc = this->pc;
        uint64_t raw = 0, fault = 0;
        if (!bus.load(pc, 4, &raw, &fault)) {
            stopReason = StopReason::FetchAbort;
            faultAddr = fault;
            return stopReason;
        }
        uint32_t insn = uint32_t(raw);
        if (trace) {
            std::fprintf(stderr, "%016llx: %08x  x0=%016llx x1=%016llx\n",
                         (unsigned long long)pc, insn,
                         (unsigned long long)x[0], (unsigned long long)x[1]);
        }
        Decoded d = decode(insn);
        Step s = step(*this, bus, pc, d);
        ++insnCount;
        switch (s) {
        case Step::Continue: break;
        case Step::Halt:
            stopReason = StopReason::Halt;
            return stopReason;
        case Step::Svc:
            stopReason = StopReason::Svc;
            return stopReason;
        case Step::Unimplemented:
            stopReason = StopReason::Unimplemented;
            badInsn = insn;
            return stopReason;
        case Step::DataAbort:
            stopReason = StopReason::DataAbort;
            return stopReason;
        }
    }
    return StopReason::Running;
}

} // namespace lop
