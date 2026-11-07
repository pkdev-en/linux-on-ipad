// Built-in smoke test, embedded into Engine/SelfTest.cpp by tools/gen_selftest.sh.
// Prints to the PL011 UART and stops with HLT.
    .text
    .globl _start
_start:
    movz x20, #0x0900, lsl #16        // UART base 0x09000000
    adr  x0, msg1
    bl   puts

    mov  x0, #6
    mov  x1, #7
    mul  x0, x0, x1
    mov  x19, x0                      // remember 42
    adr  x0, msg2
    bl   puts
    mov  x0, x19
    bl   putdec
    bl   newline

    adr  x0, msg3
    bl   puts
    mov  x1, #0                       // fib(0)
    mov  x2, #1                       // fib(1)
    mov  x4, #20
1:  add  x3, x1, x2
    mov  x1, x2
    mov  x2, x3
    subs x4, x4, #1
    b.ne 1b
    mov  x0, x1                       // fib(20) = 6765
    bl   putdec
    bl   newline
    hlt  #0

puts:                                  // x0 = NUL-terminated string
1:  ldrb w1, [x0], #1
    cbz  w1, 2f
    strb w1, [x20]
    b    1b
2:  ret

newline:
    mov  w1, #10
    strb w1, [x20]
    ret

putdec:                                // x0 = unsigned number
    stp  x29, x30, [sp, #-48]!
    add  x9, sp, #47                   // end of digit buffer
    mov  x13, x9
    mov  x10, #10
1:  udiv x11, x0, x10
    msub x12, x11, x10, x0             // x12 = x0 % 10
    add  w12, w12, #'0'
    strb w12, [x9, #-1]!
    mov  x0, x11
    cbnz x0, 1b
2:  ldrb w1, [x9], #1
    strb w1, [x20]
    cmp  x9, x13
    b.lo 2b
    ldp  x29, x30, [sp], #48
    ret

msg1: .asciz "Hello from the ARM64 guest!\n"
msg2: .asciz "6*7="
msg3: .asciz "fib(20)="
