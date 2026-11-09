// Memory/GuestMemory.cpp -- guest RAM backing store, see Memory/GuestMemory.hpp.
// Linux on iPad -- Apache-2.0.
#include "Memory/GuestMemory.hpp"

#include <cstring>

namespace lop {

bool GuestMemory::init(uint64_t base, uint64_t size) {
    if (size < (1u << 16) || size > (64ull << 30)) return false;
    base_ = base;
    size_ = size;
    ram_.assign(size_t(size), 0);
    return true;
}

bool GuestMemory::contains(uint64_t addr, uint64_t len) const {
    return len != 0 && addr >= base_ && addr - base_ <= size_ &&
           len <= size_ - (addr - base_);
}

uint8_t *GuestMemory::ptr(uint64_t addr, uint64_t len) {
    if (!contains(addr, len)) return nullptr;
    return ram_.data() + size_t(addr - base_);
}

const uint8_t *GuestMemory::ptr(uint64_t addr, uint64_t len) const {
    if (!contains(addr, len)) return nullptr;
    return ram_.data() + size_t(addr - base_);
}

bool GuestMemory::load(uint64_t addr, unsigned size, uint64_t *out,
                       uint64_t *faultAddr) const {
    const uint8_t *p = ptr(addr, size);
    if (!p) {
        if (faultAddr) *faultAddr = addr;
        return false;
    }
    uint64_t v = 0;
    std::memcpy(&v, p, size); // guest is little-endian; so are our hosts
    *out = v;
    return true;
}

bool GuestMemory::store(uint64_t addr, unsigned size, uint64_t value,
                        uint64_t *faultAddr) {
    uint8_t *p = ptr(addr, size);
    if (!p) {
        if (faultAddr) *faultAddr = addr;
        return false;
    }
    std::memcpy(p, &value, size);
    return true;
}

bool GuestMemory::copyIn(uint64_t addr, const void *data, uint64_t len,
                         uint64_t *faultAddr) {
    uint8_t *p = ptr(addr, len);
    if (!p) {
        if (faultAddr) *faultAddr = addr;
        return false;
    }
    std::memcpy(p, data, size_t(len));
    return true;
}

bool GuestMemory::copyOut(uint64_t addr, void *data, uint64_t len,
                          uint64_t *faultAddr) const {
    const uint8_t *p = ptr(addr, len);
    if (!p) {
        if (faultAddr) *faultAddr = addr;
        return false;
    }
    std::memcpy(data, p, size_t(len));
    return true;
}

bool GuestMemory::zero(uint64_t addr, uint64_t len, uint64_t *faultAddr) {
    uint8_t *p = ptr(addr, len);
    if (!p) {
        if (faultAddr) *faultAddr = addr;
        return false;
    }
    std::memset(p, 0, size_t(len));
    return true;
}

} // namespace lop
