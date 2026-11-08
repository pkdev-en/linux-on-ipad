// Debug 3: run the kernel, print PC histogram sample to find spin loops.
#include "../download/linuxonpad-m1m2/Engine/Engine.h"
#include "../download/linuxonpad-m1m2/Engine/Machine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>
#include <vector>

using namespace lop;

int main(int argc, char **argv) {
    const char *kernel = argc > 1 ? argv[1] : "/tmp/linuxboot/Image";
    const char *initrd = argc > 2 ? argv[2] : "/tmp/linuxboot/initramfs-busybox.cpio.gz";

    Machine m;
    m.ram_size = 256ULL * 1024 * 1024;
    m.ram = (uint8_t *)calloc(1, m.ram_size);
    m.fb = (uint8_t *)calloc(1, (size_t)m.fb_width * m.fb_height * 4);
    devices_register(m);

    // load kernel
    FILE *f = fopen(kernel, "rb");
    if (!f) { perror("kernel"); return 2; }
    fseek(f, 0, SEEK_END); long kn = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> k(kn);
    fread(k.data(), 1, kn, f); fclose(f);
    uint64_t text_offset = 0, image_size = 0;
    memcpy(&text_offset, k.data() + 8, 8);
    memcpy(&image_size, k.data() + 16, 8);
    if (!text_offset) text_offset = 0x80000;
    m.kernel_load = m.ram_base + text_offset;
    m.kernel_mem_size = image_size ? image_size : kn;
    memcpy(m.ram + (m.kernel_load - m.ram_base), k.data(), kn);

    // initramfs
    f = fopen(initrd, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long rn = ftell(f); fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> r(rn); fread(r.data(), 1, rn, f); fclose(f);
        uint64_t cur = (m.kernel_load + m.kernel_mem_size + 0xFFFFF) & ~0xFFFFFULL;
        m.dtb_addr = cur; cur += 0x40000;
        cur = (cur + 0xFFF) & ~0xFFF;
        m.initrd_addr = cur; m.initrd_size = rn;
        memcpy(m.ram + (m.initrd_addr - m.ram_base), r.data(), rn);
        printf("kernel@%llx dtb@%llx initrd@%llx\n",
               (unsigned long long)m.kernel_load, (unsigned long long)m.dtb_addr,
               (unsigned long long)m.initrd_addr);
    }
    m.bootargs = "console=ttyAMA0,115200n8 earlycon=pl011,mmio32,0x09000000 loglevel=7 nokaslr";
    if (!dtb_build(m)) { printf("dtb failed\n"); return 2; }

    m.cpu.reset();
    m.cpu.pc = m.kernel_load;
    m.cpu.x[0] = m.dtb_addr;
    m.state = 1;
    m.watch_lo = 0x41ead000ULL; m.watch_hi = 0x41eaf000ULL;
    m.watch_arm_break = true;

    auto t0 = std::chrono::steady_clock::now();
    uint64_t total = 0;
    std::map<uint64_t, uint64_t> hot; // PC >> 4 -> count (16-byte buckets)
    unsigned last_exc_seen = 0;
    bool dumped = false;
    while (m.state == 1) {
        interp_run(m, 10'000'000);
        total += 10'000'000;
        if (!dumped && m.last_exc_count >= 1) {
            dumped = true;
            printf("=== flow after first exception (single steps) ===\n");
            for (int i = 0; i < 40; i++) {
                uint64_t pc_before = m.cpu.pc;
                interp_run(m, 1);
                uint64_t pc = m.cpu.pc;
                std::string e;
                uint32_t w = mmu_fetch32(m, pc_before, &e);
                printf("  step%02d pc=0x%llx insn=0x%08x -> 0x%llx x0=%llx x1=%llx x3=%u\n",
                       i, (unsigned long long)pc_before, w, (unsigned long long)pc,
                       (unsigned long long)m.cpu.x[0], (unsigned long long)m.cpu.x[1],
                       (unsigned)m.cpu.x[3]);
                if (m.state != 1) break;
            }
        }
        if (m.last_exc_count != last_exc_seen && m.last_exc_count < 50) {
            // disassemble around elr FROM GUEST RAM (post-alternatives)
            {
                uint64_t va0 = m.last_exc_pc & ~7ULL;
                printf("   RAM code around elr:\n");
                for (int k = -8; k <= 4; k++) {
                    uint64_t va = va0 + k * 4;
                    std::string e2;
                    uint32_t w = mmu_fetch32(m, va, &e2);
                    printf("     %c 0x%llx: 0x%08x%s\n", va == va0 ? '>' : ' ',
                           (unsigned long long)va, w, e2.c_str());
                }
            }
            // sanity: linear map translation checks (PAGE_OFFSET + phys)
            {
                uint64_t tests[] = {0xffff800081eae1a8ULL, 0xffff800081e00000ULL, 0xffff800081eae000ULL, 0xffff8000800bdee0ULL};
                const char *names[] = {"bm_pte+1a8", "kimg 2MB blk", "bm_pte+0", "code"};
                for (int t = 0; t < 3; t++) {
                    uint64_t pa; std::string e;
                    bool ok = mmu_translate(m, tests[t], pa, &e);
                    printf("   t %s va=%llx -> %s pa=%llx\n", names[t],
                           (unsigned long long)tests[t], ok ? "OK " : "ERR",
                           (unsigned long long)pa);
                }
            }
            // manual walk of TTBR1 for the faulting VA
            uint64_t va = m.last_exc_far, ttbr = m.cpu.ttbr1_el1 & ~0xFFFULL;
            printf("   walk TTBR1=%llx for va=%llx:\n", (unsigned long long)m.cpu.ttbr1_el1,
                   (unsigned long long)va);
            uint64_t tp = ttbr;
            int shift = 39, level = 0;
            while (level <= 3) {
                uint64_t idx = (va >> shift) & 0x1FF;
                if (!m.pa_is_ram(tp + idx * 8)) { printf("      L%d table PA %llx not in RAM\n", level, (unsigned long long)(tp + idx*8)); break; }
                uint64_t desc; memcpy(&desc, m.ram + (tp + idx * 8 - m.ram_base), 8);
                printf("      L%d idx=%llu @pa=%llx desc=%llx (%s)\n", level,
                       (unsigned long long)idx, (unsigned long long)(tp + idx * 8),
                       (unsigned long long)desc,
                       (desc & 3) == 1 ? "BLOCK" : (desc & 3) == 3 ? (level == 3 ? "PAGE" : "TABLE") : "INVALID");
                if ((desc & 3) != 3 || level == 3) break;
                tp = desc & 0x0000FFFFFFFFF000ULL;
                shift -= 9; level++;
            }
            // dump L3 table entries 0..63 of the final table
            uint64_t l3tab = tp;
            int nonzero = 0;
            printf("      watch on that table: count=%llu last pc=%llx pa=%llx\n",
                   (unsigned long long)m.watch_count, (unsigned long long)m.watch_last_pc,
                   (unsigned long long)m.watch_last_pa);
            printf("      L3 table @%llx entries: ", (unsigned long long)l3tab);
            for (int e = 0; e < 64; e++) {
                uint64_t d; memcpy(&d, m.ram + (l3tab + e * 8 - m.ram_base), 8);
                if (d & 3) { nonzero++; if (nonzero <= 6) printf("[%d]=%llx ", e, (unsigned long long)d); }
            }
            printf("... (%d non-zero of first 64)\n", nonzero);
            printf("exc#%u: elr=0x%llx esr=0x%llx -> vec=0x%llx far=0x%llx sctlr.M=%d\n",
                   m.last_exc_count, (unsigned long long)m.last_exc_pc,
                   (unsigned long long)m.last_exc_esr, (unsigned long long)m.last_exc_target,
                   (unsigned long long)m.last_exc_far, (int)(m.cpu.sctlr_el1 & 1));
            last_exc_seen = m.last_exc_count;
        }
        if (m.cpu.pstate.EL == 1) hot[(m.cpu.pc >> 4) << 4] += 10'000'000;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - t0).count();
        if (sec >= 20) {
            printf("t=%llus total=%lluM state=%d el=%d pc=0x%llx sctlr.M=%d uart=%zu\n",
                   (unsigned long long)sec, (unsigned long long)(total / 1000000),
                   m.state, (int)m.cpu.pstate.EL, (unsigned long long)m.cpu.pc,
                   (int)(m.cpu.sctlr_el1 & 1), m.uart_tx.size());
            // top hot spots (real)
            std::vector<std::pair<uint64_t, uint64_t>> v(hot.begin(), hot.end());
            std::sort(v.begin(), v.end(), [](auto &a, auto &b){ return a.second > b.second; });
            for (size_t i = 0; i < v.size() && i < 8; i++)
                printf("   hot 0x%012llx: %lluM\n", (unsigned long long)v[i].first,
                       (unsigned long long)(v[i].second / 1000000));
            printf("   watch: count=%llu last: pa=%llx val=%llx pc=%llx\n",
                   (unsigned long long)m.watch_count, (unsigned long long)m.watch_last_pa,
                   (unsigned long long)m.watch_last_val, (unsigned long long)m.watch_last_pc);
            printf("   oddLR: pc=0x%llx insn=0x%08x val=0x%llx\n",
                   (unsigned long long)m.odd_lr_pc, m.odd_lr_insn, (unsigned long long)m.odd_lr_val);
            printf("   exclfail: pc=0x%llx addr=0x%llx want=%llu have=%llu valid=%d\n",
                   (unsigned long long)m.excl_fail_pc, (unsigned long long)m.excl_fail_addr,
                   (unsigned long long)m.excl_want, (unsigned long long)m.excl_have,
                   m.excl_fail_valid);
            printf("   exceptions=%u last: elr=0x%llx esr=0x%llx vec=0x%llx far=0x%llx vbar=0x%llx\n",
                   m.last_exc_count, (unsigned long long)m.last_exc_pc,
                   (unsigned long long)m.last_exc_esr, (unsigned long long)m.last_exc_target,
                   (unsigned long long)m.last_exc_far, (unsigned long long)m.cpu.vbar_el1);
            hot.clear();
            t0 = std::chrono::steady_clock::now();
        }
        if (m.state == 3) { printf("FAULT: %s\n", m.fault.c_str()); break; }
        if (sec > 90) break;
    }
    return 0;
}
