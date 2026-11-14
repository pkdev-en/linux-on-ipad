#ifndef LX_CPU_H
#define LX_CPU_H

#include "machine.h"

typedef enum {
    LX_RUN = 0,
    LX_HALT,     /* HLT #0 (test exit) or WFI with nothing to wake us */
    LX_POWEROFF, /* PSCI SYSTEM_OFF   */
    LX_RESET,    /* PSCI SYSTEM_RESET */
    LX_FAULT     /* unimplemented feature or guest bug; see cpu->why */
} LxStop;

/*
 * Reference interpreter for AArch64 (EL1 only, MMU off). It exists first so
 * that every device, the boot path and later the JIT have something
 * trustworthy to be compared against.
 */
typedef struct LxCpu {
    LxMachine *m;
    uint64_t x[31], sp, pc;
    uint32_t nzcv; /* N=31 Z=30 C=29 V=28 */
    uint64_t sctlr, cpacr, vbar, tpidr0, tpidr1, daif;
    uint64_t icount;
    bool trace;
    LxStop stop;
    char why[200];
} LxCpu;

void lx_cpu_reset(LxCpu *c, LxMachine *m);
void lx_cpu_run(LxCpu *c, uint64_t max_insns); /* budget for this call; 0 = until it stops */

#endif
