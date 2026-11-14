#include "lxemu.h"

#include "boot.h"
#include "cpu.h"
#include "display.h"
#include "input.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MiB (1024ull * 1024ull)

struct LxVm {
    LxMachine m;
    LxCpu cpu;
    LxDisplay disp;
    LxInput input;
    bool raw, machine_ok;
    char info[160];
};

static uint8_t *read_file(const char *path, size_t *len, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (!b || n < 0 || fread(b, 1, (size_t)n, f) != (size_t)n) {
        snprintf(err, errlen, "cannot read %s", path);
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

LxVm *lx_vm_create(const LxVmConfig *cfg, char *err, size_t errlen)
{
    LxVm *vm = calloc(1, sizeof *vm);
    uint8_t *file = NULL, *dtb = NULL;
    size_t n = 0, dtb_len = 0;
    uint64_t dtb_pa = 0, limit;
    LxDtbCfg dc;

    if (!vm) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    if (!cfg->kernel == !cfg->raw_program) {
        snprintf(err, errlen, "exactly one of kernel / raw_program is required");
        goto fail;
    }
    uint64_t ram_mb = cfg->ram_mb ? cfg->ram_mb : 256;
    if (ram_mb < 16 || lx_machine_init(&vm->m, ram_mb * MiB)) {
        snprintf(err, errlen, "cannot allocate %" PRIu64 " MiB of guest RAM", ram_mb);
        goto fail;
    }
    vm->machine_ok = true;
    lx_cpu_reset(&vm->cpu, &vm->m);
    vm->cpu.trace = cfg->trace;
    lx_input_init(&vm->input);
    if (lx_display_init(&vm->disp, &vm->m, cfg->fb_width, cfg->fb_height)) {
        snprintf(err, errlen, "invalid or too large framebuffer %ux%u", cfg->fb_width, cfg->fb_height);
        goto fail;
    }
    limit = vm->disp.enabled ? vm->disp.base : vm->m.ram_base + vm->m.ram_size;
    dc = (LxDtbCfg){ .bootargs = cfg->cmdline, .ncpus = 1, .display = &vm->disp };

    if (cfg->raw_program) {
        vm->raw = true;
        file = read_file(cfg->raw_program, &n, err, errlen);
        uint8_t *dst = file ? lx_ram_ptr(&vm->m, vm->m.ram_base, n) : NULL;
        if (!file)
            goto fail;
        if (!dst) {
            snprintf(err, errlen, "raw program does not fit in RAM");
            goto fail;
        }
        memcpy(dst, file, n);
        vm->cpu.pc = vm->m.ram_base;
        vm->cpu.sp = limit;
    } else {
        LxKernelInfo ki;
        file = read_file(cfg->kernel, &n, err, errlen);
        if (!file || lx_kernel_load(&vm->m, file, n, &ki, err, errlen))
            goto fail;
        snprintf(vm->info, sizeof vm->info, "kernel at 0x%" PRIx64 "..0x%" PRIx64 ", %s pages", ki.load_pa,
                 ki.end_pa,
                 ki.page_shift == 12   ? "4 KiB"
                 : ki.page_shift == 14 ? "16 KiB"
                 : ki.page_shift == 16 ? "64 KiB"
                                       : "unspecified-size");
        free(file);
        file = NULL;
        dtb_pa = ki.end_pa;
        if (cfg->initrd) {
            uint64_t rpa = ki.end_pa + 2 * MiB;
            file = read_file(cfg->initrd, &n, err, errlen);
            uint8_t *dst = file && rpa + n <= limit ? lx_ram_ptr(&vm->m, rpa, n) : NULL;
            if (!file)
                goto fail;
            if (!dst) {
                snprintf(err, errlen, "initrd does not fit in RAM");
                goto fail;
            }
            memcpy(dst, file, n);
            dc.initrd_start = rpa;
            dc.initrd_end = rpa + n;
        }
        vm->cpu.pc = ki.load_pa;
    }

    dtb = lx_dtb_build(&vm->m, &dc, &dtb_len);
    if (!dtb) {
        snprintf(err, errlen, "device tree build failed");
        goto fail;
    }
    if (cfg->dump_dtb) {
        FILE *f = fopen(cfg->dump_dtb, "wb");
        bool ok = f && fwrite(dtb, 1, dtb_len, f) == dtb_len;
        if (f)
            fclose(f);
        if (!ok) {
            snprintf(err, errlen, "cannot write %s", cfg->dump_dtb);
            goto fail;
        }
    }
    if (cfg->kernel) {
        uint8_t *dst = lx_ram_ptr(&vm->m, dtb_pa, dtb_len);
        if (!dst) {
            snprintf(err, errlen, "no room for the device tree");
            goto fail;
        }
        memcpy(dst, dtb, dtb_len);
        vm->cpu.x[0] = dtb_pa; /* arm64 boot protocol: x0 = DTB, x1..x3 = 0 */
    }
    free(dtb);
    free(file);
    return vm;

fail:
    free(dtb);
    free(file);
    lx_vm_destroy(vm);
    return NULL;
}

void lx_vm_destroy(LxVm *vm)
{
    if (!vm)
        return;
    if (vm->machine_ok)
        lx_machine_free(&vm->m);
    free(vm);
}

bool lx_vm_run(LxVm *vm, uint64_t max_insns)
{
    lx_cpu_run(&vm->cpu, max_insns);
    return vm->cpu.stop == LX_RUN;
}

int lx_vm_report(const LxVm *vm, FILE *out)
{
    const LxCpu *c = &vm->cpu;
    int status = 0;
    if (vm->info[0])
        fprintf(out, "lxemu: %s\n", vm->info);
    switch (c->stop) {
    case LX_RUN:
        fprintf(out, "lxemu: instruction limit reached after %" PRIu64 " insns, pc=0x%" PRIx64 "\n", c->icount,
                c->pc);
        status = 3;
        break;
    case LX_HALT:
        fprintf(out, "lxemu: halted (%s) pc=0x%" PRIx64 " x0=0x%" PRIx64 " after %" PRIu64 " insns\n", c->why,
                c->pc, c->x[0], c->icount);
        status = vm->raw ? (int)(c->x[0] & 0xff) : 0;
        break;
    case LX_POWEROFF:
    case LX_RESET:
        fprintf(out, "lxemu: guest requested %s\n", c->stop == LX_POWEROFF ? "power off" : "reset");
        break;
    case LX_FAULT:
        fprintf(out, "lxemu: STOPPED: %s (after %" PRIu64 " insns)\n", c->why, c->icount);
        status = 2;
        break;
    }
    return status;
}

const uint32_t *lx_vm_framebuffer(const LxVm *vm, unsigned *width, unsigned *height, unsigned *stride_bytes)
{
    const uint32_t *px = lx_display_pixels(&vm->disp, &vm->m);
    if (px) {
        *width = vm->disp.width;
        *height = vm->disp.height;
        *stride_bytes = vm->disp.stride;
    }
    return px;
}

int lx_vm_screenshot(const LxVm *vm, const char *ppm_path)
{
    return lx_display_write_ppm(&vm->disp, &vm->m, ppm_path);
}

void lx_vm_key(LxVm *vm, uint16_t evdev_code, bool down)
{
    lx_input_key(&vm->input, evdev_code, down);
}

void lx_vm_pointer(LxVm *vm, int32_t x, int32_t y, bool left_down)
{
    lx_input_pointer(&vm->input, x, y, left_down);
}
