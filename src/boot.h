#ifndef LX_BOOT_H
#define LX_BOOT_H

#include "display.h"
#include "machine.h"

/* Result of loading an arm64 Linux "Image" (Documentation/arch/arm64/booting.rst). */
typedef struct LxKernelInfo {
    uint64_t load_pa;    /* where the image was copied; also the entry point */
    uint64_t end_pa;     /* end of image incl. BSS, rounded up to 2 MiB      */
    unsigned page_shift; /* 12/14/16 as the kernel declares; 0 = unspecified */
    bool big_endian;
} LxKernelInfo;

/* Copies the image into RAM. Returns 0 on success, -1 with a message in err. */
int lx_kernel_load(LxMachine *m, const uint8_t *img, size_t len, LxKernelInfo *ki,
                   char *err, size_t errlen);

typedef struct LxDtbCfg {
    const char *bootargs; /* NULL = default */
    uint64_t initrd_start, initrd_end; /* 0/0 = no initrd */
    unsigned ncpus;
    const LxDisplay *display; /* NULL or disabled = no framebuffer */
} LxDtbCfg;

/* Builds the device tree for the machine. Caller frees the blob. */
uint8_t *lx_dtb_build(const LxMachine *m, const LxDtbCfg *cfg, size_t *len);

#endif
