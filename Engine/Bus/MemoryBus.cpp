// Bus/MemoryBus.cpp -- MMIO dispatch, see Bus/MemoryBus.hpp.
// Linux on iPad -- Apache-2.0.
#include "Bus/MemoryBus.hpp"

namespace lop {

bool MemoryBus::init(uint64_t base, uint64_t size) {
    return ram_.init(base, size);
}

bool MemoryBus::load(uint64_t addr, unsigned size, uint64_t *out,
                     uint64_t *faultAddr) {
    // MMIO devices first.
    if (inRange(addr, size, kUartBase, kUartEnd)) {
        *out = uart_.read(addr - kUartBase, size);
        return true;
    }
    if (inRange(addr, size, kGicBase, kGicEnd)) {
        *out = 0; // GIC stub until the interrupt model lands (M3)
        return true;
    }
    // Then RAM.
    return ram_.load(addr, size, out, faultAddr);
}

bool MemoryBus::store(uint64_t addr, unsigned size, uint64_t value,
                      uint64_t *faultAddr) {
    if (inRange(addr, size, kUartBase, kUartEnd)) {
        uart_.write(addr - kUartBase, uint32_t(value), size);
        return true;
    }
    if (inRange(addr, size, kGicBase, kGicEnd)) {
        return true; // GIC stub: writes ignored
    }
    return ram_.store(addr, size, value, faultAddr);
}

} // namespace lop
