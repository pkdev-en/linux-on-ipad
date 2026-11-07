// Privileged-register checks that qemu-user cannot run (it executes at EL0).
// Run with: lopvm --flat sysreg.bin --semihost   -> exit code 0 = pass, N = test N failed.
    .text
    .globl _start
_start:
    mov  x20, #1
    mrs  x0, currentel
    cmp  x0, #4                         // EL1
    b.ne fail

    mov  x20, #2
    mov  x1, #0x1234
    msr  tpidr_el1, x1
    mrs  x0, tpidr_el1
    cmp  x0, x1
    b.ne fail

    mov  x20, #3
    msr  daifset, #3
    mrs  x0, daif
    cmp  x0, #0xC0
    b.ne fail

    mov  x20, #4
    msr  daifset, #15
    msr  daifclr, #5
    mrs  x0, daif
    mov  x1, #0x280
    cmp  x0, x1
    b.ne fail

    mov  x20, #5
    mrs  x0, midr_el1
    movz x1, #0xd083
    movk x1, #0x410f, lsl #16
    cmp  x0, x1
    b.ne fail

    mov  x20, #6
    adrp x0, _start
    movz x1, #0x4000, lsl #16
    cmp  x0, x1
    b.ne fail

    mov  x20, #7
    msr  sctlr_el1, xzr                 // writing 0 must be accepted
    mrs  x0, sctlr_el1
    cbnz x0, fail

    mov  x0, #0
    mov  x8, #93
    svc  #0
fail:
    mov  x0, x20
    mov  x8, #93
    svc  #0
