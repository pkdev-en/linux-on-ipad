// Machine.h -- the virtual board: CPU + bus + UART + boot pipeline.
// Linux on iPad -- Apache-2.0.
#ifndef LOP_MACHINE_H
#define LOP_MACHINE_H

#include <cstdint>
#include <string>
#include <vector>

#include "Bus.h"
#include "Cpu.h"

namespace lop {

enum class Stop {
    None, // instruction budget exhausted, CPU still running
    Halt, // HLT executed
    Svc,  // SVC #imm executed -- host decides what it means
    Error,// unimplemented instruction / data or fetch abort
};

struct MachineConfig {
    uint64_t ramBase = 0x40000000ull; // QEMU "virt" style RAM base
    uint64_t ramSize = 512ull << 20;  // 512 MiB
};

class Machine {
public:
    explicit Machine(const MachineConfig &cfg);

    Cpu &cpu() { return cpu_; }
    Bus &bus() { return bus_; }
    Uart &uart() { return bus_.uart(); }

    // Copies a flat binary into guest RAM and points the CPU at it
    // (bare-metal payloads, selftest).
    bool loadFlat(const void *data, size_t size, uint64_t loadAddr, std::string &err);

    // Boots the ARM64 Linux boot protocol: Image at ramBase, DTB at
    // ramBase+128MiB (patched with initrd ranges), initrd at ramBase+160MiB,
    // x0 = DTB address, pc = ramBase (the Image header branch skips the
    // 64-byte header). This is what tools/lopvm.cpp --kernel drives.
    bool bootLinux(const std::vector<uint8_t> &kernel, const std::vector<uint8_t> &dtb,
                   const std::vector<uint8_t> *initrd, std::string &err);

    // Runs up to maxInstructions, returns why it stopped.
    Stop run(uint64_t maxInstructions);

private:
    MachineConfig cfg_;
    Bus bus_;
    Cpu cpu_;
};

} // namespace lop

#endif // LOP_MACHINE_H
