// Linux/BootConfig.cpp -- boot-parameter preparation, see Linux/BootConfig.hpp.
// Linux on iPad -- Apache-2.0.
#include "Linux/BootConfig.hpp"

#include "Fdt.h"

namespace lop {

bool BootConfig::placeInitrd(GuestMemory &ram, const std::vector<uint8_t> &initrd,
                             uint64_t ramBase, uint64_t &addrOut, std::string &err) {
    uint64_t addr = ramBase + initrdRamOffset;
    if (!ram.copyIn(addr, initrd.data(), initrd.size())) {
        err = "initrd does not fit in guest RAM";
        return false;
    }
    addrOut = addr;
    return true;
}

bool BootConfig::placeDtb(GuestMemory &ram, const std::vector<uint8_t> &dtb,
                          uint64_t ramBase, uint64_t &addrOut, std::string &err) {
    uint64_t addr = ramBase + dtbRamOffset;
    if (!ram.copyIn(addr, dtb.data(), dtb.size())) {
        err = "DTB does not fit in guest RAM";
        return false;
    }
    addrOut = addr;
    return true;
}

bool BootConfig::patchInitrd(std::vector<uint8_t> &dtb, uint64_t start,
                             uint64_t end, std::string &err) {
    if (!fdtSetProp64(dtb, "linux,initrd-start", start) ||
        !fdtSetProp64(dtb, "linux,initrd-end", end)) {
        err = "DTB has no /chosen initrd properties to patch";
        return false;
    }
    return true;
}

void BootConfig::applyTo(ARM64CPU &cpu, uint64_t entry, uint64_t dtbAddr,
                         uint64_t ramBase, uint64_t ramSize) {
    cpu.reset();
    cpu.pc = entry;      // Image code0 branch skips the header
    cpu.x[0] = dtbAddr;  // boot protocol: DTB pointer
    cpu.x[1] = 0;        // x1..x3 reserved (must be zero)
    cpu.x[2] = 0;
    cpu.x[3] = 0;
    cpu.sp = ramBase + ramSize - 16; // scratch stack until the kernel sets its own
}

} // namespace lop
