// LinuxOnPad — host test harness.
// Runs the engine natively (Linux/macOS/any host) to validate the interpreter
// before ever touching an iPad:
//   1. bare-metal payload test (no kernel needed)
//   2. real Linux boot: ./host_test --kernel Image --initramfs rootfs.cpio.gz
#include "../Engine/Engine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Bare-metal UART hello payload (hand-assembled A64, see payload_hello.S):
//   adr   x1, msg
//   movz  w2, #0x9000, lsl #16     ; UART base
// loop:
//   ldrb  w3, [x1], #1
//   cbz   w3, done
//   str   w3, [x2]
//   b     loop
// done:
//   wfi
//   b     .
// msg: "hello from LinuxOnPad bare-metal payload!\n\0"
static const uint32_t kPayload[] = {
    0x10000101, // adr  x1, 0x20 (msg)
    0x52A12002, // movz w2, #0x0900, lsl #16 (UART @0x09000000)
    0x38401423, // ldrb w3, [x1], #1 (post-index)
    0x34000063, // cbz  w3, +3 (done)
    0xB9000043, // str  w3, [x2]
    0x17FFFFFD, // b    -3 (loop)
    0xD503207F, // wfi
    0x14000000, // b    .
};

static std::vector<uint8_t> read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)n);
    if (n && fread(buf.data(), 1, (size_t)n, f) != (size_t)n) { fclose(f); exit(2); }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    const char *kernel_path = nullptr, *initrd_path = nullptr, *payload_path = nullptr;
    const char *bootargs = "console=ttyAMA0,115200n8 console=tty0 earlycon=pl011,mmio32,0x09000000 loglevel=7";
    uint64_t ram_mb = 256, max_sec = 60;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? argv[++i] : (char *)""; };
        if (a == "--kernel") kernel_path = next();
        else if (a == "--initramfs") initrd_path = next();
        else if (a == "--payload") payload_path = next();
        else if (a == "--bootargs") bootargs = next();
        else if (a == "--ram") ram_mb = strtoull(next(), nullptr, 0);
        else if (a == "--max-sec") max_sec = strtoull(next(), nullptr, 0);
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }

    lop_vm *vm = lop_vm_create(ram_mb * 1024 * 1024);
    if (!vm) { fprintf(stderr, "vm create failed\n"); return 2; }
    fprintf(stderr, "[host] %s\n", lop_engine_version());

    if (payload_path || (!kernel_path)) {
        // -------- bare-metal payload test --------
        std::vector<uint8_t> code;
        if (payload_path) code = read_file(payload_path);
        else {
            code.resize(sizeof(kPayload));
            memcpy(code.data(), kPayload, sizeof(kPayload));
            const char *msg = "hello from LinuxOnPad bare-metal payload!\n";
            code.insert(code.end(), msg, msg + strlen(msg) + 1);
        }
        if (code.size() % 4) code.resize((code.size() + 3) & ~size_t(3));
        const uint64_t load = 0x40010000ULL;
        if (lop_vm_write_mem(vm, load, code.data(), (uint64_t)code.size()) != 0) {
            fprintf(stderr, "[host] payload write failed\n"); return 2;
        }
        if (lop_vm_start_baremetal(vm, load) != 0) { fprintf(stderr, "[host] start failed\n"); return 2; }
        uint8_t buf[8192];
        auto t0 = std::chrono::steady_clock::now();
        std::string got;
        while (lop_vm_state(vm) == LOP_STATE_RUNNING) {
            lop_vm_run(vm, 100'000);
            uint64_t n = lop_vm_uart_read(vm, buf, sizeof buf);
            if (n) got.append((char *)buf, n);
            if (std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count() > 5) break;
            if (!got.empty() && got.back() == '\n') break;
        }
        printf("%s", got.c_str());
        int ok = got.find("hello from LinuxOnPad") != std::string::npos;
        fprintf(stderr, "[host] payload test: %s\n", ok ? "PASS" : "FAIL");
        lop_vm_destroy(vm);
        return ok ? 0 : 1;
    }

    // -------- real Linux boot --------
    std::vector<uint8_t> k = read_file(kernel_path);
    std::vector<uint8_t> r;
    if (initrd_path) r = read_file(initrd_path);
    if (lop_vm_load_kernel(vm, k.data(), (uint64_t)k.size()) != 0) {
        fprintf(stderr, "[host] kernel load failed\n"); return 2;
    }
    if (!r.empty())
        lop_vm_load_initramfs(vm, r.data(), (uint64_t)r.size());
    lop_vm_set_bootargs(vm, bootargs);
    if (lop_vm_boot(vm) != 0) { fprintf(stderr, "[host] boot failed\n"); return 2; }
    fprintf(stderr, "[host] %s\n", lop_vm_kernel_addr(vm));

    auto t0 = std::chrono::steady_clock::now();
    uint64_t uart_total = 0;
    uint8_t buf[8192];
    while (lop_vm_state(vm) == LOP_STATE_RUNNING) {
        lop_vm_run(vm, 4'000'000);
        uint64_t n = lop_vm_uart_read(vm, buf, sizeof buf);
        if (n) { fwrite(buf, 1, n, stdout); fflush(stdout); uart_total += n; }
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - t0).count();
        if (sec > (long long)max_sec) {
            fprintf(stderr, "\n[host] timeout after %llus (%llu bytes uart)\n",
                    (unsigned long long)max_sec, (unsigned long long)uart_total);
            return 3;
        }
    }
    uint64_t n;
    do { n = lop_vm_uart_read(vm, buf, sizeof buf); if (n) fwrite(buf, 1, n, stdout); } while (n);
    fflush(stdout);
    int st = lop_vm_state(vm);
    fprintf(stderr, "\n[host] state=%d (%s) uart=%llu bytes\n", st,
            st == LOP_STATE_EXITED ? "guest powered off" : "fault",
            (unsigned long long)uart_total);
    if (st == LOP_STATE_FAULT) fprintf(stderr, "[host] fault: %s\n", lop_vm_fault(vm));
    lop_vm_destroy(vm);
    return st == LOP_STATE_EXITED ? 0 : 1;
}
