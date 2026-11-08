// Debug 2: isolate the LDRB path.
#include "../download/linuxonpad-m1m2/Engine/Engine.h"
#include "../download/linuxonpad-m1m2/Engine/Machine.h"

#include <cstdio>
#include <cstring>

using namespace lop;

int main() {
    Machine m;
    m.ram_size = 64 * 1024 * 1024;
    m.ram = (uint8_t *)calloc(1, m.ram_size);
    m.fb = (uint8_t *)calloc(1, (size_t)m.fb_width * m.fb_height * 4);
    devices_register(m);

    const uint64_t msg_addr = 0x40010020;
    m.ram[msg_addr - m.ram_base] = 'h';

    printf("bus_read direct: %02x\n", (unsigned)bus_read(m, msg_addr, 1));
    std::string e;
    uint64_t v = mmu_read(m, msg_addr, 1, AccType::DataR, &e);
    printf("mmu_read: %02llx err='%s'\n", (unsigned long long)v, e.c_str());

    // Now via interpreter: ldrb w3, [x1], #1 with x1 = msg_addr
    m.ram[0x30] = 0x23; m.ram[0x31] = 0x14; m.ram[0x32] = 0x40; m.ram[0x33] = 0x38; // 0x38401423 @ PA 0x40000030
    m.cpu.reset();
    m.cpu.pc = 0x40000030;    // identity (MMU off)
    m.cpu.x[1] = msg_addr;
    m.state = 1;
    m.cpu.pstate.I = 1; // keep IRQs masked
    interp_run(m, 1);
    printf("after LDRB: x3=%llx x1=%llx state=%d fault=%s\n",
           (unsigned long long)m.cpu.x[3], (unsigned long long)m.cpu.x[1], m.state, m.fault.c_str());
    return 0;
}
