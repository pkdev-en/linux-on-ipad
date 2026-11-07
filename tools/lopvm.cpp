// Host command-line frontend for the engine (build with `make`, run build/lopvm).
// Lives outside Engine/ on purpose: Engine/ is compiled into the iOS app, which has its own main().
#include "../Engine/Machine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace lop;

static bool readFile(const char *path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  lopvm --flat FILE [--load-addr A] [--ram MB] [--semihost] [--max N] [--trace]\n"
        "  lopvm --kernel Image --dtb FILE.dtb [--initrd FILE] [--ram MB] [--max N] [--trace]\n"
        "\n"
        "  --semihost   handle SVC #0 like Linux: x8=64 write(fd,buf,len), x8=93 exit(code)\n"
        "               (used by the differential tests against qemu-user)\n"
        "  --max N      stop after N instructions (default 4000000000)\n");
}

static void dumpState(Machine &m) {
    Cpu &c = m.cpu();
    std::fprintf(stderr, "\n--- CPU state ---\n%s\n", c.describeStop().c_str());
    std::fprintf(stderr, "instructions executed: %llu\n", (unsigned long long)c.insnCount);
    for (int r = 0; r < 31; ++r)
        std::fprintf(stderr, "x%-2d=%016llx%s", r, (unsigned long long)c.x[r], (r % 4 == 3) ? "\n" : "  ");
    std::fprintf(stderr, "sp =%016llx  pc =%016llx  NZCV=%d%d%d%d\n", (unsigned long long)c.sp,
                 (unsigned long long)c.pc, c.n, c.z, c.c, c.v);
    if (!c.sysregs().empty()) {
        std::fprintf(stderr, "system registers touched:\n");
        for (auto &kv : c.sysregs())
            std::fprintf(stderr, "  %-18s = 0x%llx\n", Cpu::sysregName(kv.first).c_str(),
                         (unsigned long long)kv.second);
    }
}

int main(int argc, char **argv) {
    const char *flat = nullptr, *kernel = nullptr, *dtb = nullptr, *initrd = nullptr;
    uint64_t loadAddr = 0, maxInsns = 4000000000ULL, ramMb = 512;
    bool haveLoadAddr = false, semihost = false, trace = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--flat") flat = next();
        else if (a == "--kernel") kernel = next();
        else if (a == "--dtb") dtb = next();
        else if (a == "--initrd") initrd = next();
        else if (a == "--load-addr") { loadAddr = std::strtoull(next(), nullptr, 0); haveLoadAddr = true; }
        else if (a == "--ram") ramMb = std::strtoull(next(), nullptr, 0);
        else if (a == "--max") maxInsns = std::strtoull(next(), nullptr, 0);
        else if (a == "--semihost") semihost = true;
        else if (a == "--trace") trace = true;
        else { usage(); return 2; }
    }
    if (!flat && !kernel) { usage(); return 2; }

    MachineConfig cfg;
    cfg.ramSize = ramMb << 20;
    Machine m(cfg);
    m.cpu().trace = trace;
    m.uart().setTxSink([](uint8_t b) { std::fputc(b, stdout); std::fflush(stdout); });

    std::string err;
    if (flat) {
        std::vector<uint8_t> bin;
        if (!readFile(flat, bin)) { std::fprintf(stderr, "cannot read %s\n", flat); return 1; }
        if (!haveLoadAddr) loadAddr = cfg.ramBase;
        if (!m.loadFlat(bin.data(), bin.size(), loadAddr, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    } else {
        std::vector<uint8_t> k, d, r;
        if (!dtb) { std::fprintf(stderr, "--kernel needs --dtb\n"); return 2; }
        if (!readFile(kernel, k)) { std::fprintf(stderr, "cannot read %s\n", kernel); return 1; }
        if (!readFile(dtb, d)) { std::fprintf(stderr, "cannot read %s\n", dtb); return 1; }
        if (initrd && !readFile(initrd, r)) { std::fprintf(stderr, "cannot read %s\n", initrd); return 1; }
        if (!m.bootLinux(k, d, initrd ? &r : nullptr, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    }

    uint64_t remaining = maxInsns;
    for (;;) {
        uint64_t before = m.cpu().insnCount;
        Stop s = m.run(remaining);
        remaining -= (m.cpu().insnCount - before);
        if (s == Stop::None) {
            std::fprintf(stderr, "\ninstruction limit reached\n");
            dumpState(m);
            return 3;
        }
        if (s == Stop::Halt) {
            std::fflush(stdout);
            return 0;
        }
        if (s == Stop::Svc && semihost && m.cpu().stopImm == 0) {
            Cpu &c = m.cpu();
            if (c.x[8] == 64) { // write(fd, buf, len)
                uint8_t *p = m.bus().ramPtr(c.x[1], c.x[2]);
                if (!p) { c.x[0] = (uint64_t)-14; continue; } // -EFAULT
                FILE *f = (c.x[0] == 2) ? stderr : stdout;
                std::fwrite(p, 1, c.x[2], f);
                std::fflush(f);
                c.x[0] = c.x[2];
                continue;
            }
            if (c.x[8] == 93) { // exit(code)
                std::fflush(stdout);
                return (int)(c.x[0] & 0xFF);
            }
            std::fprintf(stderr, "unsupported semihost syscall %llu\n", (unsigned long long)c.x[8]);
        }
        dumpState(m);
        return 1;
    }
}
