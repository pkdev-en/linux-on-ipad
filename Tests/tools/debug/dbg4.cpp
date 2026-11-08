// Debug 4: exclusive access sequence ldxr/add/stlxr/cbnz
#include "../download/linuxonpad-m1m2/Engine/Engine.h"
#include "../download/linuxonpad-m1m2/Engine/Machine.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace lop;

int main() {
    Machine m;
    m.ram_size = 64 * 1024 * 1024;
    m.ram = (uint8_t *)calloc(1, m.ram_size);
    m.fb = (uint8_t *)calloc(1, (size_t)m.fb_width * m.fb_height * 4);
    devices_register(m);

    // code at ram_base: ldxr w2,[x1]; add w2,w2,w0; stlxr w3,w2,[x1]; cbnz w3,-3; ret
    std::vector<uint32_t> code = {
        0x885F7C22, // ldxr  w2, [x1]
        0x0B000042, // add   w2, w2, w0
        0x8803FC22, // stlxr w3, w2, [x1]
        0x35FFFFA3, // cbnz  w3, -3
        0xD65F03C0, // ret
    };
    memcpy(m.ram, code.data(), code.size() * 4);
    m.cpu.reset();
    m.cpu.pc = m.ram_base;
    m.cpu.x[0] = 5;
    m.cpu.x[1] = m.ram_base + 0x1000;
    *(uint32_t *)(m.ram + 0x1000) = 100;
    m.state = 1;

    for (int i = 0; i < 12 && m.state == 1; i++) {
        uint64_t pc = m.cpu.pc;
        interp_run(m, 1);
        printf("step %2d: PC=%llx -> %llx  w0=%u w2=%u w3=%u mem=%u state=%d excl=%d\n",
               i, (unsigned long long)pc, (unsigned long long)m.cpu.pc,
               (unsigned)m.cpu.x[0], (unsigned)m.cpu.x[2], (unsigned)m.cpu.x[3],
               *(unsigned *)(m.ram + 0x1000), m.state, (int)m.excl_valid);
        if (m.state == 3) { printf("FAULT: %s\n", m.fault.c_str()); break; }
    }
    return 0;
}
