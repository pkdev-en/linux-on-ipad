#include "../Engine/EngineApi.h"

#include <cstdio>

int main() {
    char out[512];
    int rc = lop_selftest(out, sizeof out);
    std::printf("%s", out);
    std::printf(rc == 0 ? "SELFTEST PASS\n" : "SELFTEST FAIL\n");
    return rc == 0 ? 0 : 1;
}
