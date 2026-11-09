// Linux/BootConfig.hpp -- prepares the ARM64 Linux boot parameters.
// Linux on iPad -- Apache-2.0.
//
// The arm64 Linux boot protocol (Documentation/arch/arm64/booting.rst):
//   x0 = device tree blob address, x1..x3 = 0, pc = kernel entry,
//   all other state (regs, flags, sysregs) reset.
// This struct owns the guest-RAM layout of the boot artefacts (DTB at
// ramBase+128 MiB, initrd at ramBase+160 MiB -- same as QEMU "virt"
// conventions used since the first boot), patches /chosen with the
// initrd range and applies the register state to the CPU.
#ifndef LOP_BOOT_CONFIG_H
#define LOP_BOOT_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "CPU/ARM64CPU.hpp"
#include "Memory/GuestMemory.hpp"

namespace lop {

struct BootConfig {
    // Guest-RAM offsets for the boot artefacts (added to ramBase).
    uint64_t dtbRamOffset = 128ull << 20;    // 0x48000000 for ramBase 0x40000000
    uint64_t initrdRamOffset = 160ull << 20; // 0x4A000000

    // Kernel command line. The kernel currently reads its cmdline from the
    // DTB /chosen/bootargs property (dts/lop-virt.dts); string-property
    // patching lands with the Fdt rewrite, until then this is informational.
    std::string cmdline = "earlycon=pl011,mmio32,0x09000000 console=ttyAMA0";

    // Copies the initrd into guest RAM at ramBase+initrdRamOffset and
    // returns its guest address in `addrOut`.
    bool placeInitrd(GuestMemory &ram, const std::vector<uint8_t> &initrd,
                     uint64_t ramBase, uint64_t &addrOut, std::string &err);

    // Copies the (already patched) DTB into guest RAM at
    // ramBase+dtbRamOffset and returns its guest address in `addrOut`.
    bool placeDtb(GuestMemory &ram, const std::vector<uint8_t> &dtb,
                  uint64_t ramBase, uint64_t &addrOut, std::string &err);

    // Patches /chosen linux,initrd-start / linux,initrd-end in `dtb`.
    static bool patchInitrd(std::vector<uint8_t> &dtb, uint64_t start,
                            uint64_t end, std::string &err);

    // Applies the boot register state: reset, x0 = dtbAddr, x1..x3 = 0,
    // pc = entry, sp = top of RAM minus 16 (scratch).
    void applyTo(ARM64CPU &cpu, uint64_t entry, uint64_t dtbAddr,
                 uint64_t ramBase, uint64_t ramSize);
};

} // namespace lop

#endif // LOP_BOOT_CONFIG_H
