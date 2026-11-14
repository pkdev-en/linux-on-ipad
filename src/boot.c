#include "boot.h"

#include "fdt.h"

#include <stdio.h>
#include <string.h>

#define MiB (1024ull * 1024ull)

static uint64_t le64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static uint32_t le32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

int lx_kernel_load(LxMachine *m, const uint8_t *img, size_t len, LxKernelInfo *ki,
                   char *err, size_t errlen)
{
    if (len >= 2 && img[0] == 0x1f && img[1] == 0x8b) {
        snprintf(err, errlen, "image is gzip-compressed; gunzip it first");
        return -1;
    }
    if (len < 64 || le32(img + 56) != 0x644d5241u) { /* "ARM\x64" */
        snprintf(err, errlen, "not an arm64 Linux Image (missing ARM\\x64 magic at offset 56)");
        return -1;
    }
    uint64_t text_offset = le64(img + 8);
    uint64_t image_size = le64(img + 16);
    uint64_t flags = le64(img + 24);

    static const unsigned shifts[4] = { 0, 12, 14, 16 };
    ki->page_shift = shifts[(flags >> 1) & 3];
    ki->big_endian = flags & 1;
    if (ki->big_endian) {
        snprintf(err, errlen, "big-endian kernels are not supported");
        return -1;
    }

    uint64_t size = image_size > len ? image_size : len; /* image_size covers BSS */
    uint64_t base = m->ram_base + text_offset;
    uint8_t *dst = lx_ram_ptr(m, base, size);
    if (!dst) {
        snprintf(err, errlen, "kernel (%llu bytes at +0x%llx) does not fit in RAM",
                 (unsigned long long)size, (unsigned long long)text_offset);
        return -1;
    }
    memcpy(dst, img, len);
    ki->load_pa = base;
    ki->end_pa = (base + size + 2 * MiB - 1) & ~(2 * MiB - 1);
    return 0;
}

static void cells2(uint32_t out[2], uint64_t v)
{
    out[0] = (uint32_t)(v >> 32);
    out[1] = (uint32_t)v;
}

uint8_t *lx_dtb_build(const LxMachine *m, const LxDtbCfg *cfg, size_t *len)
{
    enum { PH_GIC = 1, PH_CLK = 2 };
    const unsigned ncpus = cfg->ncpus ? cfg->ncpus : 1;
    char name[64];
    LxFdt f;
    lx_fdt_init(&f);

    lx_fdt_begin(&f, "");
    lx_fdt_prop_u32(&f, "#address-cells", 2);
    lx_fdt_prop_u32(&f, "#size-cells", 2);
    lx_fdt_prop_str(&f, "compatible", "linux,dummy-virt");
    lx_fdt_prop_str(&f, "model", "lxemu");
    lx_fdt_prop_u32(&f, "interrupt-parent", PH_GIC);

    lx_fdt_begin(&f, "chosen");
    lx_fdt_prop_str(&f, "bootargs", cfg->bootargs ? cfg->bootargs : "console=ttyAMA0 earlycon");
    snprintf(name, sizeof name, "/pl011@%llx", (unsigned long long)LX_UART_BASE);
    lx_fdt_prop_str(&f, "stdout-path", name);
    if (cfg->initrd_end > cfg->initrd_start) {
        lx_fdt_prop_u64(&f, "linux,initrd-start", cfg->initrd_start);
        lx_fdt_prop_u64(&f, "linux,initrd-end", cfg->initrd_end);
    }
    lx_fdt_end(&f);

    snprintf(name, sizeof name, "memory@%llx", (unsigned long long)m->ram_base);
    lx_fdt_begin(&f, name);
    lx_fdt_prop_str(&f, "device_type", "memory");
    uint32_t mem[4];
    cells2(mem, m->ram_base);
    cells2(mem + 2, m->ram_size);
    lx_fdt_prop_cells(&f, "reg", mem, 4);
    lx_fdt_end(&f);

    if (cfg->display && cfg->display->enabled) {
        const LxDisplay *d = cfg->display;
        uint32_t reg[4];
        cells2(reg, d->base);
        cells2(reg + 2, d->size);

        lx_fdt_begin(&f, "reserved-memory"); /* keep the kernel's allocator off the framebuffer */
        lx_fdt_prop_u32(&f, "#address-cells", 2);
        lx_fdt_prop_u32(&f, "#size-cells", 2);
        lx_fdt_prop_empty(&f, "ranges");
        snprintf(name, sizeof name, "framebuffer-mem@%llx", (unsigned long long)d->base);
        lx_fdt_begin(&f, name);
        lx_fdt_prop_cells(&f, "reg", reg, 4);
        lx_fdt_prop_empty(&f, "no-map");
        lx_fdt_end(&f);
        lx_fdt_end(&f);

        snprintf(name, sizeof name, "framebuffer@%llx", (unsigned long long)d->base);
        lx_fdt_begin(&f, name);
        lx_fdt_prop_str(&f, "compatible", "simple-framebuffer");
        cells2(reg + 2, (uint64_t)d->stride * d->height);
        lx_fdt_prop_cells(&f, "reg", reg, 4);
        lx_fdt_prop_u32(&f, "width", d->width);
        lx_fdt_prop_u32(&f, "height", d->height);
        lx_fdt_prop_u32(&f, "stride", d->stride);
        lx_fdt_prop_str(&f, "format", "x8r8g8b8");
        lx_fdt_end(&f);
    }

    lx_fdt_begin(&f, "cpus");
    lx_fdt_prop_u32(&f, "#address-cells", 1);
    lx_fdt_prop_u32(&f, "#size-cells", 0);
    for (unsigned i = 0; i < ncpus; i++) {
        snprintf(name, sizeof name, "cpu@%u", i);
        lx_fdt_begin(&f, name);
        lx_fdt_prop_str(&f, "device_type", "cpu");
        lx_fdt_prop_str(&f, "compatible", "arm,arm-v8");
        lx_fdt_prop_u32(&f, "reg", i);
        lx_fdt_prop_str(&f, "enable-method", "psci");
        lx_fdt_end(&f);
    }
    lx_fdt_end(&f);

    /* No EL2 is emulated, so PSCI calls arrive as HVC and are handled by the host. */
    lx_fdt_begin(&f, "psci");
    static const char *const psci_compat[] = { "arm,psci-0.2", "arm,psci" };
    lx_fdt_prop_strs(&f, "compatible", psci_compat, 2);
    lx_fdt_prop_str(&f, "method", "hvc");
    lx_fdt_end(&f);

    snprintf(name, sizeof name, "intc@%llx", (unsigned long long)LX_GIC_DIST_BASE);
    lx_fdt_begin(&f, name);
    lx_fdt_prop_str(&f, "compatible", "arm,cortex-a15-gic");
    lx_fdt_prop_u32(&f, "#interrupt-cells", 3);
    lx_fdt_prop_empty(&f, "interrupt-controller");
    uint32_t gic[8];
    cells2(gic, LX_GIC_DIST_BASE);
    cells2(gic + 2, LX_GIC_DIST_SIZE);
    cells2(gic + 4, LX_GIC_CPU_BASE);
    cells2(gic + 6, LX_GIC_CPU_SIZE);
    lx_fdt_prop_cells(&f, "reg", gic, 8);
    lx_fdt_prop_u32(&f, "phandle", PH_GIC);
    lx_fdt_end(&f);

    /* ARMv8 generic timer: secure phys, non-secure phys, virtual, hyp (PPIs 13,14,11,10). */
    lx_fdt_begin(&f, "timer");
    static const char *const timer_compat[] = { "arm,armv8-timer", "arm,armv7-timer" };
    lx_fdt_prop_strs(&f, "compatible", timer_compat, 2);
    uint32_t fl = (((1u << ncpus) - 1) << 8) | 4; /* CPU mask | level-high */
    uint32_t tint[12] = { 1, 13, fl, 1, 14, fl, 1, 11, fl, 1, 10, fl };
    lx_fdt_prop_cells(&f, "interrupts", tint, 12);
    lx_fdt_prop_empty(&f, "always-on");
    lx_fdt_end(&f);

    lx_fdt_begin(&f, "apb-pclk");
    lx_fdt_prop_str(&f, "compatible", "fixed-clock");
    lx_fdt_prop_u32(&f, "#clock-cells", 0);
    lx_fdt_prop_u32(&f, "clock-frequency", 24000000);
    lx_fdt_prop_str(&f, "clock-output-names", "clk24mhz");
    lx_fdt_prop_u32(&f, "phandle", PH_CLK);
    lx_fdt_end(&f);

    snprintf(name, sizeof name, "pl011@%llx", (unsigned long long)LX_UART_BASE);
    lx_fdt_begin(&f, name);
    static const char *const uart_compat[] = { "arm,pl011", "arm,primecell" };
    lx_fdt_prop_strs(&f, "compatible", uart_compat, 2);
    uint32_t ureg[4];
    cells2(ureg, LX_UART_BASE);
    cells2(ureg + 2, LX_UART_SIZE);
    lx_fdt_prop_cells(&f, "reg", ureg, 4);
    uint32_t uirq[3] = { 0, LX_UART_SPI, 4 }; /* SPI, number, level-high */
    lx_fdt_prop_cells(&f, "interrupts", uirq, 3);
    uint32_t uclk[2] = { PH_CLK, PH_CLK };
    lx_fdt_prop_cells(&f, "clocks", uclk, 2);
    static const char *const uclk_names[] = { "uartclk", "apb_pclk" };
    lx_fdt_prop_strs(&f, "clock-names", uclk_names, 2);
    lx_fdt_end(&f);

    lx_fdt_end(&f); /* root */
    return lx_fdt_finish(&f, len);
}
