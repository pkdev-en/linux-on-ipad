#include "fdt.h"

#include <stdlib.h>
#include <string.h>

enum { FDT_BEGIN_NODE = 1, FDT_END_NODE = 2, FDT_PROP = 3, FDT_END = 9 };

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int grow(void **buf, size_t *cap, size_t need)
{
    if (need <= *cap)
        return 0;
    size_t nc = *cap ? *cap : 256;
    while (nc < need)
        nc *= 2;
    void *nb = realloc(*buf, nc);
    if (!nb)
        return -1;
    *buf = nb;
    *cap = nc;
    return 0;
}

static void st_put(LxFdt *f, const void *d, size_t n)
{
    if (f->err || !n)
        return;
    if (grow((void **)&f->st, &f->st_cap, f->st_len + n)) {
        f->err = 1;
        return;
    }
    memcpy(f->st + f->st_len, d, n);
    f->st_len += n;
}

static void st_u32(LxFdt *f, uint32_t v)
{
    uint8_t b[4];
    be32(b, v);
    st_put(f, b, 4);
}

static void st_pad(LxFdt *f)
{
    static const uint8_t zero[4];
    size_t r = f->st_len & 3;
    if (r)
        st_put(f, zero, 4 - r);
}

static uint32_t str_off(LxFdt *f, const char *name)
{
    size_t i = 0;
    while (i < f->strs_len) {
        if (strcmp(f->strs + i, name) == 0)
            return (uint32_t)i;
        i += strlen(f->strs + i) + 1;
    }
    size_t n = strlen(name) + 1;
    if (f->err || grow((void **)&f->strs, &f->strs_cap, f->strs_len + n)) {
        f->err = 1;
        return 0;
    }
    memcpy(f->strs + f->strs_len, name, n);
    uint32_t off = (uint32_t)f->strs_len;
    f->strs_len += n;
    return off;
}

void lx_fdt_init(LxFdt *f)
{
    memset(f, 0, sizeof *f);
}

void lx_fdt_begin(LxFdt *f, const char *name)
{
    st_u32(f, FDT_BEGIN_NODE);
    st_put(f, name, strlen(name) + 1);
    st_pad(f);
    f->depth++;
}

void lx_fdt_end(LxFdt *f)
{
    if (f->depth <= 0) {
        f->err = 1;
        return;
    }
    st_u32(f, FDT_END_NODE);
    f->depth--;
}

void lx_fdt_prop(LxFdt *f, const char *name, const void *data, size_t len)
{
    uint32_t so = str_off(f, name);
    st_u32(f, FDT_PROP);
    st_u32(f, (uint32_t)len);
    st_u32(f, so);
    st_put(f, data, len);
    st_pad(f);
}

void lx_fdt_prop_empty(LxFdt *f, const char *name)
{
    lx_fdt_prop(f, name, NULL, 0);
}

void lx_fdt_prop_u32(LxFdt *f, const char *name, uint32_t v)
{
    lx_fdt_prop_cells(f, name, &v, 1);
}

void lx_fdt_prop_u64(LxFdt *f, const char *name, uint64_t v)
{
    uint32_t c[2] = { (uint32_t)(v >> 32), (uint32_t)v };
    lx_fdt_prop_cells(f, name, c, 2);
}

void lx_fdt_prop_cells(LxFdt *f, const char *name, const uint32_t *cells, size_t n)
{
    uint8_t *b = malloc(n * 4 + 1);
    if (!b) {
        f->err = 1;
        return;
    }
    for (size_t i = 0; i < n; i++)
        be32(b + i * 4, cells[i]);
    lx_fdt_prop(f, name, b, n * 4);
    free(b);
}

void lx_fdt_prop_str(LxFdt *f, const char *name, const char *s)
{
    lx_fdt_prop(f, name, s, strlen(s) + 1);
}

void lx_fdt_prop_strs(LxFdt *f, const char *name, const char *const *list, size_t n)
{
    size_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += strlen(list[i]) + 1;
    char *b = malloc(total + 1);
    if (!b) {
        f->err = 1;
        return;
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        size_t l = strlen(list[i]) + 1;
        memcpy(b + o, list[i], l);
        o += l;
    }
    lx_fdt_prop(f, name, b, total);
    free(b);
}

uint8_t *lx_fdt_finish(LxFdt *f, size_t *len)
{
    st_u32(f, FDT_END);
    uint8_t *blob = NULL;
    if (!f->err && f->depth == 0) {
        const size_t off_rsv = 40, off_st = off_rsv + 16;
        const size_t off_str = off_st + f->st_len;
        const size_t total = off_str + f->strs_len;
        blob = calloc(1, total);
        if (blob) {
            be32(blob + 0, 0xd00dfeedu);
            be32(blob + 4, (uint32_t)total);
            be32(blob + 8, (uint32_t)off_st);
            be32(blob + 12, (uint32_t)off_str);
            be32(blob + 16, (uint32_t)off_rsv);
            be32(blob + 20, 17); /* version             */
            be32(blob + 24, 16); /* last_comp_version   */
            be32(blob + 28, 0);  /* boot_cpuid_phys     */
            be32(blob + 32, (uint32_t)f->strs_len);
            be32(blob + 36, (uint32_t)f->st_len);
            memcpy(blob + off_st, f->st, f->st_len);
            if (f->strs_len)
                memcpy(blob + off_str, f->strs, f->strs_len);
            *len = total;
        }
    }
    free(f->st);
    free(f->strs);
    memset(f, 0, sizeof *f);
    return blob;
}
