#include "machine.h"

#include <stdlib.h>
#include <string.h>

/* ---- PL011 UART (transmit only for now) ---------------------------------- */

static uint64_t pl011_read(void *opaque, uint64_t off, unsigned size)
{
    LxPl011 *u = opaque;
    (void)size;
    switch (off) {
    case 0x000: return 0;                       /* DR: no input yet            */
    case 0x018: return (1u << 7) | (1u << 4);   /* FR: TX FIFO empty, RX empty */
    case 0x024: return u->ibrd;
    case 0x028: return u->fbrd;
    case 0x02c: return u->lcr_h;
    case 0x030: return u->cr;
    case 0x038: return u->imsc;
    case 0x03c: return 0;                       /* RIS */
    case 0x040: return 0;                       /* MIS */
    /* PrimeCell ID registers: the amba bus in Linux probes these. */
    case 0xfe0: return 0x11;
    case 0xfe4: return 0x10;
    case 0xfe8: return 0x14;
    case 0xfec: return 0x00;
    case 0xff0: return 0x0d;
    case 0xff4: return 0xf0;
    case 0xff8: return 0x05;
    case 0xffc: return 0xb1;
    default:    return 0;
    }
}

static void pl011_write(void *opaque, uint64_t off, uint64_t val, unsigned size)
{
    LxPl011 *u = opaque;
    (void)size;
    switch (off) {
    case 0x000:
        if (u->out) {
            fputc((int)(val & 0xff), u->out);
            fflush(u->out);
        }
        break;
    case 0x024: u->ibrd = (uint32_t)val; break;
    case 0x028: u->fbrd = (uint32_t)val; break;
    case 0x02c: u->lcr_h = (uint32_t)val; break;
    case 0x030: u->cr = (uint32_t)val; break;
    case 0x038: u->imsc = (uint32_t)val; break;
    default: break; /* ICR, DMACR, IFLS, ... accepted and ignored */
    }
}

/* ---- machine -------------------------------------------------------------- */

int lx_machine_init(LxMachine *m, uint64_t ram_bytes)
{
    memset(m, 0, sizeof *m);
    m->ram = calloc(1, ram_bytes);
    if (!m->ram)
        return -1;
    m->ram_base = LX_RAM_BASE;
    m->ram_size = ram_bytes;
    m->uart.out = stdout;
    m->uart.cr = 0x300;

    LxMmio uart = { LX_UART_BASE, LX_UART_SIZE, &m->uart, pl011_read, pl011_write };
    lx_machine_add_mmio(m, &uart);
    return 0;
}

void lx_machine_free(LxMachine *m)
{
    free(m->ram);
    m->ram = NULL;
}

bool lx_machine_add_mmio(LxMachine *m, const LxMmio *dev)
{
    if (m->nmmio >= LX_MAX_MMIO)
        return false;
    m->mmio[m->nmmio++] = *dev;
    return true;
}

uint8_t *lx_ram_ptr(const LxMachine *m, uint64_t pa, uint64_t len)
{
    if (pa < m->ram_base || len > m->ram_size || pa - m->ram_base > m->ram_size - len)
        return NULL;
    return m->ram + (pa - m->ram_base);
}

bool lx_bus_read(LxMachine *m, uint64_t pa, unsigned size, uint64_t *val)
{
    uint8_t *p = lx_ram_ptr(m, pa, size);
    if (p) {
        uint64_t v = 0;
        memcpy(&v, p, size);
        *val = v;
        return true;
    }
    for (int i = 0; i < m->nmmio; i++) {
        const LxMmio *d = &m->mmio[i];
        if (pa >= d->base && pa - d->base < d->size) {
            *val = d->read(d->opaque, pa - d->base, size);
            return true;
        }
    }
    return false;
}

bool lx_bus_write(LxMachine *m, uint64_t pa, unsigned size, uint64_t val)
{
    uint8_t *p = lx_ram_ptr(m, pa, size);
    if (p) {
        memcpy(p, &val, size);
        return true;
    }
    for (int i = 0; i < m->nmmio; i++) {
        const LxMmio *d = &m->mmio[i];
        if (pa >= d->base && pa - d->base < d->size) {
            d->write(d->opaque, pa - d->base, val, size);
            return true;
        }
    }
    return false;
}
