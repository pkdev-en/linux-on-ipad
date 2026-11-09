// CPU/ARM64CPU.hpp -- AArch64 architectural CPU state + basic execution.
// Linux on iPad -- Apache-2.0.
//
// State first (x0..x30, SP, PC, NZCV, DAIF, sysregs, stop bookkeeping),
// then the basic execution engine: run() is the fetch-decode-execute loop
// that drives the verified decoder (Decode.h) and interpreter (Interp.h)
// through the MemoryBus. This is the old flat Engine/Cpu.h plus the run
// loop that used to live in Machine.cpp, so a CPU can execute against any
// bus (the board is no longer baked into the loop).
#ifndef LOP_ARM64_CPU_H
#define LOP_ARM64_CPU_H

#include <cstdint>
#include <map>
#include <string>

namespace lop {

class MemoryBus;

// System-register identifier, packed as (op0,op1,CRn,CRm,op2).
inline uint64_t sysregId(unsigned op0, unsigned op1, unsigned crn, unsigned crm,
                         unsigned op2) {
    return ((uint64_t)(op0 & 3) << 20) | ((uint64_t)(op1 & 7) << 17) |
           ((uint64_t)(crn & 15) << 12) | ((uint64_t)(crm & 15) << 8) |
           ((uint64_t)(op2 & 7) << 5);
}

// Why the interpreter stopped.
enum class StopReason {
    Running,       // still executing
    Halt,          // HLT executed
    Svc,           // SVC #imm executed (pc already advanced past it)
    Unimplemented, // instruction the engine does not know yet
    DataAbort,     // load/store outside guest RAM / supported MMIO
    FetchAbort,    // instruction fetch outside guest RAM
};

struct ARM64CPU {
    uint64_t x[31] = {0}; // x0..x30 (31 is SP/ZR depending on context, kept in sp)
    uint64_t sp = 0;
    uint64_t pc = 0;

    // NZCV condition flags.
    bool n = false, z = false, c = false, v = false;

    // DAIF interrupt mask (M3: real interrupt model plugs in here).
    uint32_t daif = 0;

    uint64_t insnCount = 0;

    // Stop bookkeeping.
    StopReason stopReason = StopReason::Running;
    uint32_t stopImm = 0;   // HLT imm16 / SVC imm16
    uint64_t faultAddr = 0; // DataAbort / FetchAbort address
    uint64_t badInsn = 0;   // Unimplemented encoding

    // -v style per-instruction trace (stderr) requested by tools/lopvm.cpp.
    bool trace = false;

    // Values written through MSR / read back through MRS for registers we
    // model as plain storage. NZCV, CurrentEL, MIDR_EL1, CNTFRQ_EL0 and
    // DAIF are handled specially and do not live here.
    std::map<uint64_t, uint64_t> sysregs_;

    const std::map<uint64_t, uint64_t> &sysregs() const { return sysregs_; }

    uint64_t readSysreg(uint64_t id) const {
        auto it = sysregs_.find(id);
        return it == sysregs_.end() ? 0 : it->second;
    }
    void writeSysreg(uint64_t id, uint64_t value) { sysregs_[id] = value; }

    uint32_t nzcvNibble() const {
        return (uint32_t(n ? 1 : 0) << 3) | (uint32_t(z ? 1 : 0) << 2) |
               (uint32_t(c ? 1 : 0) << 1) | uint32_t(v ? 1 : 0);
    }
    void setNzcvNibble(uint32_t nib) {
        n = nib & 8; z = nib & 4; c = nib & 2; v = nib & 1;
    }

    // Human-readable reason we stopped (used by tools/lopvm.cpp dumps).
    std::string describeStop() const;

    static std::string sysregName(uint64_t id);

    // --- basic execution ---------------------------------------------------

    // Resets all architectural state. The trace flag survives (tools set it
    // once up front and expect it to live across reboots).
    void reset();

    // Fetch-decode-execute loop: runs up to maxInstructions instructions
    // through `bus` and returns why it stopped. Svc stops are resumable --
    // service the call, then call run() again; Halt and the error stops are
    // terminal (every later run() returns the same reason).
    StopReason run(MemoryBus &bus, uint64_t maxInstructions);
};

} // namespace lop

#endif // LOP_ARM64_CPU_H
