// Machine.cpp -- the board wiring, see Machine.h.
// The CPU state lives in CPU/ARM64CPU, the routing in Bus/MemoryBus, the
// RAM in Memory/GuestMemory and the Linux boot pipeline in
// Linux/KernelLoader + Linux/BootConfig. Machine glues them together.
// Linux on iPad -- Apache-2.0.
#include "Machine.h"

#include <cstdio>
#include <cstring>

#include "Decode.h"
#include "Fdt.h"
#include "Interp.h"
#include "Linux/BootConfig.hpp"
#include "Linux/KernelLoader.hpp"

namespace lop {

Machine::Machine(const MachineConfig &cfg) : cfg_(cfg) {
    bus_.init(cfg_.ramBase, cfg_.ramSize);
}

bool Machine::loadFlat(const void *data, size_t size, uint64_t loadAddr, std::string &err) {
    if (!size) { err = "empty image"; return false; }
    if (!bus_.copyIn(loadAddr, data, size)) {
        err = "load address 0x" + std::to_string(loadAddr) + " outside guest RAM";
        return false;
    }
    cpu_.reset();
    cpu_.pc = loadAddr;
    cpu_.sp = cfg_.ramBase + cfg_.ramSize - 16; // scratch stack for bare-metal payloads
    return true;
}

bool Machine::bootLinux(const std::vector<uint8_t> &kernel, const std::vector<uint8_t> &dtb,
                        const std::vector<uint8_t> *initrd, std::string &err) {
    // 1. Parse and load the kernel Image (honours text_offset / flags,
    //    zeroes .bss). Entry is the placement: code0 skips the header.
    KernelInfo info;
    if (!KernelLoader::parse(kernel, info, err)) return false;
    KernelLoad loaded;
    if (!KernelLoader::load(bus_.ram(), kernel, cfg_.ramBase, info, loaded, err))
        return false;

    // 2. Place the initrd and patch its range into /chosen, then place
    //    the patched DTB (initrd @ +160 MiB, DTB @ +128 MiB).
    BootConfig boot;
    std::vector<uint8_t> patched = dtb;
    if (initrd && !initrd->empty()) {
        uint64_t initrdAddr = 0;
        if (!boot.placeInitrd(bus_.ram(), *initrd, cfg_.ramBase, initrdAddr, err))
            return false;
        if (!BootConfig::patchInitrd(patched, initrdAddr, initrdAddr + initrd->size(), err))
            return false;
    }
    uint64_t dtbAddr = 0;
    if (!boot.placeDtb(bus_.ram(), patched, cfg_.ramBase, dtbAddr, err)) return false;

    // 3. Apply the boot register state (x0 = DTB, x1..x3 = 0, pc = entry).
    boot.applyTo(cpu_, loaded.entry, dtbAddr, cfg_.ramBase, cfg_.ramSize);
    return true;
}

Stop Machine::run(uint64_t maxInstructions) {
    switch (cpu_.run(bus_, maxInstructions)) {
    case StopReason::Halt: return Stop::Halt;
    case StopReason::Svc: return Stop::Svc;
    case StopReason::Running: return Stop::None;
    default: return Stop::Error; // Unimplemented / DataAbort / FetchAbort
    }
}

} // namespace lop
