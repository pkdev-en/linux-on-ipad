// Debug 5: run until PC hits target, then single-step printing pc/x30/sp.
#include "../download/linuxonpad-m1m2/Engine/Engine.h"
#include "../download/linuxonpad-m1m2/Engine/Machine.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace lop;

int main(int argc, char **argv) {
    uint64_t target = argc > 1 ? strtoull(argv[1], nullptr, 0) : 0xffff800080e5a0c0ULL;
    Machine m;
    m.ram_size = 256ULL * 1024 * 1024;
    m.ram = (uint8_t *)calloc(1, m.ram_size);
    m.fb = (uint8_t *)calloc(1, (size_t)m.fb_width * m.fb_height * 4);
    devices_register(m);

    FILE *f = fopen("/tmp/linuxboot/Image", "rb");
    fseek(f, 0, SEEK_END); long kn = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> k(kn); fread(k.data(), 1, kn, f); fclose(f);
    uint64_t text_offset = 0, image_size = 0;
    memcpy(&text_offset, k.data() + 8, 8);
    memcpy(&image_size, k.data() + 16, 8);
    if (!text_offset) text_offset = 0x80000;
    m.kernel_load = m.ram_base + text_offset;
    m.kernel_mem_size = image_size ? image_size : kn;
    memcpy(m.ram + (m.kernel_load - m.ram_base), k.data(), kn);

    f = fopen("/tmp/linuxboot/initramfs-busybox.cpio.gz", "rb");
    fseek(f, 0, SEEK_END); long rn = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> r(rn); fread(r.data(), 1, rn, f); fclose(f);
    uint64_t cur = (m.kernel_load + m.kernel_mem_size + 0xFFF) & ~0xFFF;
    m.dtb_addr = cur; cur += 0x40000;
    cur = (cur + 0xFFF) & ~0xFFF;
    m.initrd_addr = cur; m.initrd_size = rn;
    memcpy(m.ram + (m.initrd_addr - m.ram_base), r.data(), rn);
    m.bootargs = "console=ttyAMA0,115200n8 earlycon=pl011,mmio32,0x09000000 loglevel=7";
    dtb_build(m);
    m.cpu.reset();
    m.cpu.pc = m.kernel_load;
    m.cpu.x[0] = m.dtb_addr;
    m.state = 1;

    // run until target PC (engine breakpoint)
    m.break_pc = target;
    uint64_t steps = 0;
    while (m.state == 1) {
        interp_run(m, 10'000'000);
        steps += 10'000'000;
        if (m.break_hit) break;
        if (m.state != 1) { printf("state=%d fault=%s\n", m.state, m.fault.c_str()); return 1; }
    }
    m.break_pc = 0; m.break_hit = false;
    printf("hit target after %lluM steps, pc=0x%llx\n",
           (unsigned long long)(steps / 1000000), (unsigned long long)m.cpu.pc);

    for (int i = 0; i < 80; i++) {
        uint64_t pc = m.cpu.pc;
        std::string e;
        uint32_t w = mmu_fetch32(m, pc, &e);
        interp_run(m, 1);
        uint64_t sp = m.cpu.sp();
        auto rd = [&](uint64_t va) -> uint64_t {
            std::string e2; uint64_t pa; int lvl;
            // identity: use mmu_read
            return mmu_read(m, va, 8, AccType::DataR, &e2);
        };
        printf("s%02d pc=%llx insn=%08x -> %llx  x30=%llx sp=%llx [sp]=%llx [sp+8]=%llx\n",
               i, (unsigned long long)pc, w, (unsigned long long)m.cpu.pc,
               (unsigned long long)m.cpu.x[30], (unsigned long long)sp,
               (unsigned long long)rd(sp), (unsigned long long)rd(sp + 8));
        if (m.state != 1) { printf("state=%d fault=%s\n", m.state, m.fault.c_str()); break; }
    }
    return 0;
}
