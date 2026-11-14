#ifndef LXEMU_H
#define LXEMU_H

/*
 * Public API of the core. This is the seam between the portable emulator and any
 * front end: the CLI (src/main.c) and the iOS app (the Swift files in ios/) both use only this.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LxVm LxVm; /* opaque */

typedef struct LxVmConfig {
    const char *kernel;      /* path to an arm64 Linux Image ...                       */
    const char *raw_program; /* ... or a bare-metal binary loaded at RAM base (tests)  */
    const char *initrd;      /* optional                                               */
    const char *cmdline;     /* optional; default "console=ttyAMA0 earlycon"           */
    const char *dump_dtb;    /* optional: write the generated device tree here         */
    uint64_t ram_mb;         /* default 256                                            */
    unsigned fb_width, fb_height; /* 0x0 = no display                                  */
    bool trace;
} LxVmConfig;

/* Returns NULL and fills err on failure. */
LxVm *lx_vm_create(const LxVmConfig *cfg, char *err, size_t errlen);
void lx_vm_destroy(LxVm *vm);

/* Runs at most max_insns instructions (0 = until it stops). Returns true while the
 * machine can still run, so a front end can call it in slices. */
bool lx_vm_run(LxVm *vm, uint64_t max_insns);

/* Prints why the machine stopped; returns a process exit status. */
int lx_vm_report(const LxVm *vm, FILE *out);

/* Guest framebuffer (x8r8g8b8), or NULL when there is no display. */
const uint32_t *lx_vm_framebuffer(const LxVm *vm, unsigned *width, unsigned *height, unsigned *stride_bytes);
int lx_vm_screenshot(const LxVm *vm, const char *ppm_path);

/* Input from the UI thread (evdev key codes; pointer in guest pixels). */
void lx_vm_key(LxVm *vm, uint16_t evdev_code, bool down);
void lx_vm_pointer(LxVm *vm, int32_t x, int32_t y, bool left_down);

#endif
