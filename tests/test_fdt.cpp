// Unit test for Engine/Fdt.cpp. usage: test_fdt in.dtb out.dtb
#include "../Engine/Fdt.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> dtb((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    uint64_t v = 0;
    int fails = 0;
    auto check = [&](bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++fails; }
    };
    check(lop::fdtIsValid(dtb), "valid dtb");
    check(lop::fdtGetProp64(dtb, "linux,initrd-start", v) && v == 0x4a000000, "read initrd-start placeholder");
    check(lop::fdtSetProp64(dtb, "linux,initrd-start", 0x4a000000), "set initrd-start");
    check(lop::fdtSetProp64(dtb, "linux,initrd-end", 0x4a123456), "set initrd-end");
    check(lop::fdtGetProp64(dtb, "linux,initrd-end", v) && v == 0x4a123456, "read back initrd-end");
    check(!lop::fdtSetProp64(dtb, "no-such-prop", 1), "missing property is reported");
    std::vector<uint8_t> junk(64, 0);
    check(!lop::fdtIsValid(junk) && !lop::fdtSetProp64(junk, "x", 1), "junk rejected");
    std::ofstream o(argv[2], std::ios::binary);
    o.write((const char *)dtb.data(), (std::streamsize)dtb.size());
    std::printf(fails ? "fdt test FAILED\n" : "fdt test passed\n");
    return fails ? 1 : 0;
}
