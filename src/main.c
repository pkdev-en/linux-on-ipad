#include "lxemu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    fputs("usage: lxemu --kernel Image [--initrd FILE] [--append ARGS] [--ram MiB] [--fb WxH]\n"
          "       lxemu --raw prog.bin   (bare-metal test, loaded at RAM base)\n"
          "options: --dump-dtb FILE  --screenshot FILE.ppm  --max-insns N  --trace\n",
          stderr);
}

int main(int argc, char **argv)
{
    LxVmConfig cfg = { 0 };
    const char *shot = NULL;
    uint64_t max_insns = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has = i + 1 < argc;
        if (!strcmp(a, "--kernel") && has) cfg.kernel = argv[++i];
        else if (!strcmp(a, "--raw") && has) cfg.raw_program = argv[++i];
        else if (!strcmp(a, "--initrd") && has) cfg.initrd = argv[++i];
        else if (!strcmp(a, "--append") && has) cfg.cmdline = argv[++i];
        else if (!strcmp(a, "--dump-dtb") && has) cfg.dump_dtb = argv[++i];
        else if (!strcmp(a, "--screenshot") && has) shot = argv[++i];
        else if (!strcmp(a, "--ram") && has) cfg.ram_mb = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(a, "--max-insns") && has) max_insns = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(a, "--fb") && has) {
            if (sscanf(argv[++i], "%ux%u", &cfg.fb_width, &cfg.fb_height) != 2) {
                usage();
                return 64;
            }
        } else if (!strcmp(a, "--trace")) cfg.trace = true;
        else {
            usage();
            return 64;
        }
    }
    if (!cfg.kernel == !cfg.raw_program) {
        usage();
        return 64;
    }

    char err[200];
    LxVm *vm = lx_vm_create(&cfg, err, sizeof err);
    if (!vm) {
        fprintf(stderr, "lxemu: %s\n", err);
        return 1;
    }
    lx_vm_run(vm, max_insns);
    int status = lx_vm_report(vm, stderr);
    if (shot && lx_vm_screenshot(vm, shot)) {
        fprintf(stderr, "lxemu: cannot write screenshot (is --fb set?)\n");
        status = status ? status : 1;
    }
    lx_vm_destroy(vm);
    return status;
}
