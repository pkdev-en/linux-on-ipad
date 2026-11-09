// semtest_main.cpp -- differential semantics test for the interpreter.
// Expected values were recorded from Unicorn (QEMU's engine) by
// tools/gen_oracle.py; this harness replays each case on the LinuxOnPad
// engine instruction-by-instruction and compares x0/x30/sp/NZCV and the
// first 32 bytes of the scratch buffer after every instruction.
// Build:  make semtest     (see Makefile)
#include "../Engine/Decode.h"
#include "../Engine/Interp.h"
#include "../Engine/Machine.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "semtest_cases.cpp"

using namespace lop;

int main() {
    MachineConfig cfg;
    cfg.ramSize = 64ull << 10; // matches the oracle layout (buf at +0x1000)

    int fails = 0, checks = 0;
    for (int ci = 0; ci < lopsem::kNumCases; ++ci) {
        const lopsem::Case &c = lopsem::kCases[ci];
        Machine m(cfg);
        std::string err;
        if (!m.loadFlat(c.code, c.codeLen, cfg.ramBase, err)) {
            std::printf("FAIL (load) %s: %s\n", c.name, err.c_str());
            ++fails;
            continue;
        }
        uint64_t bufAddr = cfg.ramBase + lopsem::kBufOffset;
        // unicorn boots with Z set (NZCV = 0x40000000); match it
        m.cpu().setNzcvNibble(0x4);
        for (int i = 0; i < c.preludeInsns; ++i) m.run(1);

        for (int k = 0; k < c.nCheckpoints; ++k) {
            const lopsem::Checkpoint &cp = c.cps[k];
            // the oracle stops stepping when pc reaches the trailing hlt
            if (m.cpu().pc == cfg.ramBase + c.codeLen - 4) {
                ++fails;
                if (fails <= 25)
                    std::printf("FAIL case %d [%s]: engine halted early at step %d\n",
                                ci, c.name, k);
                break;
            }
            Stop s = m.run(1);
            Cpu &cpu = m.cpu();
            ++checks;
            uint32_t nzcv = cpu.nzcvNibble();
            uint8_t *buf = m.bus().ramPtr(bufAddr, 32);
            if (s != Stop::None || cpu.x[0] != cp.x0 || cpu.x[30] != cp.x30 ||
                cpu.sp != cp.sp || nzcv != cp.nzcv ||
                std::memcmp(buf, cp.buf, 32) != 0) {
                ++fails;
                if (fails <= 25) {
                    uint64_t pcBefore = cpu.pc;
                    (void)pcBefore;
                    std::printf("FAIL case %d [%s]\n  step %d/%d pc=0x%llx\n",
                                ci, c.name, k + 1, c.nCheckpoints,
                                (unsigned long long)cpu.pc);
                    std::printf("  engine x0=%016llx x30=%016llx sp=%016llx nzcv=%x\n",
                                (unsigned long long)cpu.x[0],
                                (unsigned long long)cpu.x[30],
                                (unsigned long long)cpu.sp, nzcv);
                    std::printf("  oracle x0=%016llx x30=%016llx sp=%016llx nzcv=%x\n",
                                (unsigned long long)cp.x0, (unsigned long long)cp.x30,
                                (unsigned long long)cp.sp, cp.nzcv);
                    if (s != Stop::None) {
                        std::printf("  stop: %s\n", cpu.describeStop().c_str());
                        uint64_t pcNow = cpu.pc;
                        uint64_t raw = 0;
                        m.bus().load(pcNow, 4, &raw, nullptr);
                        Decoded dd = decode(uint32_t(raw));
                        std::printf("  insn %08x -> %s\n", uint32_t(raw), opName(dd.op));
                    }
                }
                break;
            }
        }
    }
    std::printf("semtest: %d cases, %d instruction checks, %d failures\n",
                lopsem::kNumCases, checks, fails);
    return fails ? 1 : 0;
}
