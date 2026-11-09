// Interp.h -- instruction interpreter (execute one decoded instruction).
// Linux on iPad -- Apache-2.0.
#ifndef LOP_INTERP_H
#define LOP_INTERP_H

#include "Bus.h"
#include "Cpu.h"
#include "Decode.h"

namespace lop {

enum class Step {
    Continue,      // pc already advanced by step()
    Halt,          // HLT executed
    Svc,           // SVC executed (host implements the call)
    Unimplemented, // d.op was Unimplemented (cpu.badInsn set)
    DataAbort,     // load/store faulted (cpu.faultAddr set)
};

// Executes the decoded instruction. pc is the address of the instruction.
// The callee fully manages cpu.pc: on Continue/Halt/Svc it points past the
// instruction, on Unimplemented/DataAbort it stays on the instruction.
Step step(Cpu &cpu, Bus &bus, uint64_t pc, const Decoded &d);

} // namespace lop

#endif // LOP_INTERP_H
