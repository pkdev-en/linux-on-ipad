/* Drives the public API the way the iOS app does: create, run in small slices, inspect. */
#include "../src/lxemu.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 64;
    LxVmConfig cfg = { .raw_program = argv[1], .fb_width = 64, .fb_height = 32 };
    char err[200];
    LxVm *vm = lx_vm_create(&cfg, err, sizeof err);
    if (!vm) {
        fprintf(stderr, "create failed: %s\n", err);
        return 1;
    }
    int slices = 0;
    while (lx_vm_run(vm, 100))
        slices++;
    fprintf(stderr, "slices=%d\n", slices);
    if (slices < 2) {
        fputs("FAIL: expected the run to take several slices\n", stderr);
        return 1;
    }
    unsigned w, h, stride;
    if (!lx_vm_framebuffer(vm, &w, &h, &stride) || w != 64 || h != 32 || stride != 256) {
        fputs("FAIL: framebuffer geometry\n", stderr);
        return 1;
    }
    lx_vm_key(vm, 30, true);          /* must not crash with nothing consuming it */
    lx_vm_pointer(vm, 10, 10, false);
    int status = lx_vm_report(vm, stderr);
    lx_vm_destroy(vm);
    return status;
}
