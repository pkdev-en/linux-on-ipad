// Bare-metal self-test for the interpreter. Prints "hello", runs numbered
// checks, then prints PASS. A failure prints "FAIL<n>" where <n> is the check.
    .text
    .global _start
_start:
    mov     x0, #0x50000000          // top of the default 256 MiB of RAM
    mov     sp, x0
    mov     x9, #0x09000000          // PL011 base
    adr     x1, s_hello
    bl      puts

    // 1: loop, add, subs, b.ne, cmp
    mov     x20, #0x31
    mov     x3, #0
    mov     x4, #10
1:  add     x3, x3, x4
    subs    x4, x4, #1
    b.ne    1b
    cmp     x3, #55
    b.ne    fail

    // 2: logical immediate, UBFM (lsr/lsl), SBFM (sxtb)
    mov     x20, #0x32
    orr     x5, xzr, #0xff00ff00ff00ff00
    lsr     x6, x5, #8
    mov     x7, #0x00ff
    movk    x7, #0x00ff, lsl #16
    movk    x7, #0x00ff, lsl #32
    movk    x7, #0x00ff, lsl #48
    cmp     x6, x7
    b.ne    fail
    and     x8, x5, #0xffff
    mov     x10, #0xff00
    cmp     x8, x10
    b.ne    fail
    lsl     x11, x10, #4
    mov     x12, #0xff000
    cmp     x11, x12
    b.ne    fail
    mov     x1, #0x80
    sxtb    x2, w1
    mov     x3, #-128
    cmp     x2, x3
    b.ne    fail

    // 3: 32-bit arithmetic wraps, zero-extends and sets flags
    mov     x20, #0x33
    movz    w1, #0xffff
    movk    w1, #0xffff, lsl #16
    add     w2, w1, #1
    cbnz    x2, fail
    adds    w3, w1, #1
    b.ne    fail
    b.cc    fail
    mov     w1, #1
    subs    w2, w1, #2
    b.pl    fail

    // 4: stack, pre/post-index, byte loads, sign extension
    mov     x20, #0x34
    mov     x1, sp
    mov     x2, #0x1234
    str     x2, [sp, #-16]!
    ldr     x3, [sp]
    cmp     x2, x3
    b.ne    fail
    add     sp, sp, #16
    mov     x4, sp
    cmp     x1, x4
    b.ne    fail
    sub     sp, sp, #32
    mov     x5, #0x80
    strb    w5, [sp, #3]
    ldrsb   x6, [sp, #3]
    mov     x7, #-128
    cmp     x6, x7
    b.ne    fail
    ldrb    w6, [sp, #3]
    cmp     w6, #0x80
    b.ne    fail
    stur    x2, [sp, #8]
    ldur    x3, [sp, #8]
    cmp     x2, x3
    b.ne    fail
    add     sp, sp, #32

    // 5: mul, csel, cset
    mov     x20, #0x35
    mov     x1, #6
    mov     x2, #7
    mul     x3, x1, x2
    cmp     x3, #42
    b.ne    fail
    cmp     x1, x2                   // 6 - 7: N=1, so lt holds, ge does not
    csel    x4, x1, x2, lt
    cset    x5, ge                   // must be 0
    cset    x6, lt                   // must be 1
    csneg   x7, x1, x2, gt           // gt false -> -x2
    cmp     x4, #6
    b.ne    fail
    cbnz    x5, fail
    cmp     x6, #1
    b.ne    fail
    mov     x8, #-7
    cmp     x7, x8
    b.ne    fail

    // 6: bl/ret, tbz/tbnz
    mov     x20, #0x36
    mov     x0, #41
    bl      incr
    cmp     x0, #42
    b.ne    fail
    tbz     x0, #0, 2f
    b       fail
2:  tbnz    x0, #1, 3f
    b       fail
3:

    // 7: adrp/add, mrs
    mov     x20, #0x37
    adrp    x1, s_hello
    add     x1, x1, :lo12:s_hello
    adr     x2, s_hello
    cmp     x1, x2
    b.ne    fail
    mrs     x3, mpidr_el1
    mov     x4, #0x80000000
    cmp     x3, x4
    b.ne    fail
    mrs     x3, CurrentEL
    cmp     x3, #4
    b.ne    fail

    // 8: PSCI over HVC
    mov     x20, #0x38
    movz    x0, #0x8400, lsl #16     // PSCI_VERSION
    hvc     #0
    mov     x1, #0x20000             // any PSCI 0.2 .. 1.x version is fine
    cmp     x0, x1
    b.hs    fail                     // also rejects NOT_SUPPORTED (-1)
    cmp     x0, #2
    b.lo    fail

    adr     x1, s_pass
    bl      puts
    mov     x0, #0
    hlt     #0

fail:
    adr     x1, s_fail
    bl      puts
    mov     x0, x20
    bl      putc
    mov     x0, #10
    bl      putc
    mov     x0, #1
    hlt     #0

putc:
    strb    w0, [x9]
    ret

puts:
    stp     x29, x30, [sp, #-16]!
1:  ldrb    w0, [x1], #1
    cbz     w0, 2f
    bl      putc
    b       1b
2:  ldp     x29, x30, [sp], #16
    ret

incr:
    add     x0, x0, #1
    ret

    .balign 4
s_hello: .asciz "hello\n"
s_pass:  .asciz "PASS\n"
s_fail:  .asciz "FAIL"
