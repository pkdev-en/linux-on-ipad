// Bus.h -- compatibility shim. The bus implementation moved to
// Bus/MemoryBus.hpp (RAM block in Memory/GuestMemory); every existing
// #include "Bus.h" keeps working unchanged.
// Linux on iPad -- Apache-2.0.
#ifndef LOP_BUS_H
#define LOP_BUS_H

#include "Bus/MemoryBus.hpp"

namespace lop {
using Bus = MemoryBus; // pre-layering name, kept for the flat-layout sources
} // namespace lop

#endif // LOP_BUS_H
