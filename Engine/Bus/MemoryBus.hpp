// Bus/MemoryBus.hpp -- routes CPU accesses between guest RAM and MMIO devices.
// Linux on iPad -- Apache-2.0.
//
// Board layout (matches QEMU "virt" and dts/lop-virt.dts):
//   0x0000_0000 .. 0x0800_0000   unmapped (flash later)
//   0x0800_0000 .. 0x0802_0000   GIC distributor/CPU interface (stub, reads 0)
//   0x0900_0000 .. 0x0900_1000   PL011 UART0
//   0x4000_0000 .. +ramSize      RAM (GuestMemory)
//
// This is the old flat Engine/Bus.h/cpp (RAM + dispatch) split in two:
// the RAM block lives in Memory/GuestMemory, the routing lives here.
#ifndef LOP_MEMORY_BUS_H
#define LOP_MEMORY_BUS_H

#include <cstdint>

#include "Memory/GuestMemory.hpp"
#include "Uart.h"

namespace lop {

class MemoryBus {
public:
    static constexpr uint64_t kGicBase = 0x08000000ull;
    static constexpr uint64_t kGicEnd = 0x08020000ull; // GICD + GICC pages
    static constexpr uint64_t kUartBase = 0x09000000ull;
    static constexpr uint64_t kUartEnd = kUartBase + 0x1000;

    static constexpr uint64_t kDefaultRamBase = 0x40000000ull;
    static constexpr uint64_t kDefaultRamSize = 512ull << 20;

    // Allocates guest RAM at [base, base+size). Returns false on bad sizes.
    bool init(uint64_t base = kDefaultRamBase, uint64_t size = kDefaultRamSize);

    GuestMemory &ram() { return ram_; }
    const GuestMemory &ram() const { return ram_; }
    Uart &uart() { return uart_; }

    // Raw pointer into guest RAM, see GuestMemory::ptr. Used by the
    // semihost layer in tools/lopvm.cpp.
    uint8_t *ramPtr(uint64_t addr, uint64_t len) { return ram_.ptr(addr, len); }

    // Loads used by the boot pipeline (copies of Image / DTB / initrd).
    bool copyIn(uint64_t addr, const void *data, uint64_t len,
                uint64_t *faultAddr = nullptr) {
        return ram_.copyIn(addr, data, len, faultAddr);
    }

    // Data accesses from the interpreter: MMIO devices first, RAM second.
    // `size` is 1, 2, 4 or 8. False = outside every region (data abort).
    bool load(uint64_t addr, unsigned size, uint64_t *out, uint64_t *faultAddr);
    bool store(uint64_t addr, unsigned size, uint64_t value,
               uint64_t *faultAddr);

private:
    // True when [addr, addr+len) fits inside [base, end).
    static bool inRange(uint64_t addr, uint64_t len, uint64_t base, uint64_t end) {
        return addr >= base && addr - base <= (end - base) - len;
    }

    GuestMemory ram_;
    Uart uart_;
};

} // namespace lop

#endif // LOP_MEMORY_BUS_H
