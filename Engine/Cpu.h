// Cpu.h -- compatibility shim. The CPU implementation moved to
// CPU/ARM64CPU.hpp; every existing #include "Cpu.h" keeps working
// unchanged.
// Linux on iPad -- Apache-2.0.
#ifndef LOP_CPU_H
#define LOP_CPU_H

#include "CPU/ARM64CPU.hpp"

namespace lop {
using Cpu = ARM64CPU; // pre-layering name, kept for the flat-layout sources
} // namespace lop

#endif // LOP_CPU_H
