#ifndef LX_DISPLAY_H
#define LX_DISPLAY_H

#include "machine.h"

/*
 * Linear framebuffer carved out of the top of guest RAM and described to Linux as a
 * "simple-framebuffer" (simplefb / simpledrm). The guest draws straight into it and the
 * host presenter (Metal on iOS) reads the same memory: no device model and no copies.
 * Pixel format is x8r8g8b8: each pixel is a little-endian u32 0x00RRGGBB.
 */
typedef struct LxDisplay {
    bool enabled;
    uint64_t base, size; /* guest physical; size rounded up to 1 MiB */
    unsigned width, height, stride; /* stride in bytes */
} LxDisplay;

/* w == h == 0 disables the display. Returns -1 if the size is invalid or too big for RAM. */
int lx_display_init(LxDisplay *d, const LxMachine *m, unsigned w, unsigned h);
const uint32_t *lx_display_pixels(const LxDisplay *d, const LxMachine *m); /* NULL if disabled */
int lx_display_write_ppm(const LxDisplay *d, const LxMachine *m, const char *path);

#endif
