#include "display.h"

#include <stdio.h>
#include <string.h>

#define MiB (1024ull * 1024ull)

int lx_display_init(LxDisplay *d, const LxMachine *m, unsigned w, unsigned h)
{
    memset(d, 0, sizeof *d);
    if (!w && !h)
        return 0;
    if (!w || !h || w > 8192 || h > 8192)
        return -1;
    d->width = w;
    d->height = h;
    d->stride = w * 4;
    uint64_t bytes = (uint64_t)d->stride * h;
    d->size = (bytes + MiB - 1) & ~(MiB - 1);
    if (d->size * 4 > m->ram_size) /* never let the display take more than a quarter of RAM */
        return -1;
    d->base = m->ram_base + m->ram_size - d->size;
    d->enabled = true;
    return 0;
}

const uint32_t *lx_display_pixels(const LxDisplay *d, const LxMachine *m)
{
    if (!d->enabled)
        return NULL;
    return (const uint32_t *)lx_ram_ptr(m, d->base, (uint64_t)d->stride * d->height);
}

int lx_display_write_ppm(const LxDisplay *d, const LxMachine *m, const char *path)
{
    const uint32_t *px = lx_display_pixels(d, m);
    if (!px)
        return -1;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%u %u\n255\n", d->width, d->height);
    for (unsigned y = 0; y < d->height; y++) {
        for (unsigned x = 0; x < d->width; x++) {
            uint32_t p = px[(size_t)y * (d->stride / 4) + x];
            unsigned char rgb[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
            fwrite(rgb, 1, 3, f);
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}
