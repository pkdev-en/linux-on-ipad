#ifndef LX_FDT_H
#define LX_FDT_H

#include <stddef.h>
#include <stdint.h>

/* Minimal flattened-device-tree writer (spec v17). Errors are sticky. */
typedef struct LxFdt {
    uint8_t *st;
    size_t st_len, st_cap; /* structure block */
    char *strs;
    size_t strs_len, strs_cap; /* strings block */
    int depth;
    int err;
} LxFdt;

void lx_fdt_init(LxFdt *f);
void lx_fdt_begin(LxFdt *f, const char *name); /* root node: name "" */
void lx_fdt_end(LxFdt *f);
void lx_fdt_prop(LxFdt *f, const char *name, const void *data, size_t len);
void lx_fdt_prop_empty(LxFdt *f, const char *name);
void lx_fdt_prop_u32(LxFdt *f, const char *name, uint32_t v);
void lx_fdt_prop_u64(LxFdt *f, const char *name, uint64_t v); /* two cells */
void lx_fdt_prop_cells(LxFdt *f, const char *name, const uint32_t *cells, size_t n);
void lx_fdt_prop_str(LxFdt *f, const char *name, const char *s);
void lx_fdt_prop_strs(LxFdt *f, const char *name, const char *const *list, size_t n);

/* Returns a malloc'd blob (caller frees) or NULL on error; releases the builder. */
uint8_t *lx_fdt_finish(LxFdt *f, size_t *len);

#endif
