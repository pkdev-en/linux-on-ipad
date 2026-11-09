// Memory/GuestMemory.hpp -- the guest's physical RAM.
// Linux on iPad -- Apache-2.0.
//
// Owns one contiguous little-endian RAM block starting at `base` (QEMU
// "virt" style: 0x40000000). Everything is bounds-checked: an access
// outside [base, base+size) fails and reports the faulting address. The
// MemoryBus calls into this after MMIO misses; the CPU turns failures
// into data/fetch aborts.
#ifndef LOP_GUEST_MEMORY_H
#define LOP_GUEST_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lop {

class GuestMemory {
public:
    // Allocates the RAM array at [base, base+size).
    // Returns false for unreasonable sizes (< 64 KiB or > 64 GiB).
    bool init(uint64_t base, uint64_t size);

    uint64_t base() const { return base_; }
    uint64_t size() const { return size_; }
    uint64_t end() const { return base_ + size_; }

    // True when [addr, addr+len) lies fully inside RAM.
    bool contains(uint64_t addr, uint64_t len) const;

    // Raw host pointer for [addr, addr+len), or nullptr when the range is
    // not fully inside RAM (zero-length ranges included). Used by the
    // semihost layer in tools/lopvm.cpp.
    uint8_t *ptr(uint64_t addr, uint64_t len);
    const uint8_t *ptr(uint64_t addr, uint64_t len) const;

    // RAM load/store from the interpreter path. `size` is 1, 2, 4 or 8;
    // the guest is little-endian and so are our hosts, so plain memcpy.
    bool load(uint64_t addr, unsigned size, uint64_t *out,
              uint64_t *faultAddr = nullptr) const;
    bool store(uint64_t addr, unsigned size, uint64_t value,
               uint64_t *faultAddr = nullptr);

    // Copy helpers for the boot pipeline (kernel Image / DTB / initrd).
    bool copyIn(uint64_t addr, const void *data, uint64_t len,
                uint64_t *faultAddr = nullptr);
    bool copyOut(uint64_t addr, void *data, uint64_t len,
                 uint64_t *faultAddr = nullptr) const;

    // Zeroes [addr, addr+len): clears kernel .bss and scratch areas.
    bool zero(uint64_t addr, uint64_t len, uint64_t *faultAddr = nullptr);

private:
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    std::vector<uint8_t> ram_;
};

} // namespace lop

#endif // LOP_GUEST_MEMORY_H
