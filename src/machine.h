#ifndef LX_MACHINE_H
#define LX_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Physical memory map. This is the layout of QEMU's "virt" board: stock arm64
 * kernels (Debian, Arch Linux ARM) already know how to drive every device on
 * it, so no kernel patches are needed.
 */
#define LX_GIC_DIST_BASE 0x08000000ull /* GICv2 distributor   (not implemented yet) */
#define LX_GIC_DIST_SIZE 0x00010000ull
#define LX_GIC_CPU_BASE  0x08010000ull /* GICv2 CPU interface (not implemented yet) */
#define LX_GIC_CPU_SIZE  0x00010000ull
#define LX_UART_BASE     0x09000000ull /* PL011                                     */
#define LX_UART_SIZE     0x00001000ull
#define LX_VIRTIO_BASE   0x0a000000ull /* virtio-mmio slots   (not implemented yet) */
#define LX_VIRTIO_STRIDE 0x00000200ull
#define LX_VIRTIO_COUNT  32
#define LX_RAM_BASE      0x40000000ull

#define LX_UART_SPI 1 /* GIC SPI number wired to the PL011 */

typedef struct LxMmio {
    uint64_t base, size;
    void *opaque;
    uint64_t (*read)(void *opaque, uint64_t off, unsigned size);
    void (*write)(void *opaque, uint64_t off, uint64_t val, unsigned size);
} LxMmio;

typedef struct LxPl011 {
    FILE *out; /* transmitted bytes go here (stdout by default) */
    uint32_t ibrd, fbrd, lcr_h, cr, imsc;
} LxPl011;

#define LX_MAX_MMIO 16

typedef struct LxMachine {
    uint8_t *ram;
    uint64_t ram_base, ram_size;
    LxMmio mmio[LX_MAX_MMIO];
    int nmmio;
    LxPl011 uart;
} LxMachine;

/* Guest memory is little-endian; the host must be too (true for every Apple target). */
int lx_machine_init(LxMachine *m, uint64_t ram_bytes);
void lx_machine_free(LxMachine *m);
bool lx_machine_add_mmio(LxMachine *m, const LxMmio *dev);

/* Returns a host pointer if [pa, pa+len) lies fully inside RAM, else NULL. */
uint8_t *lx_ram_ptr(const LxMachine *m, uint64_t pa, uint64_t len);

/* Physical bus access, size in {1,2,4,8}. Return false on a bus error. */
bool lx_bus_read(LxMachine *m, uint64_t pa, unsigned size, uint64_t *val);
bool lx_bus_write(LxMachine *m, uint64_t pa, unsigned size, uint64_t val);

#endif
