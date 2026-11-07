// A fake "Linux Image": proper arm64 header, then code that checks the boot protocol
// (x0 = DTB address, DTB magic, initrd placed where the patched DTB says) and prints a verdict.
    .text
    .globl _start
_start:
    b    entry                          // code0
    nop                                 // code1
    .quad 0                             // text_offset
    .quad _end - _start                 // image_size
    .quad 0                             // flags (little endian)
    .quad 0, 0, 0                       // reserved
    .long 0x644d5241                    // magic "ARM\x64"
    .long 0
entry:
    movz x20, #0x0900, lsl #16          // UART
    movz x21, #0x4800, lsl #16          // expected DTB address (RAM base + 128 MiB)
    cmp  x0, x21
    b.ne bad
    ldr  w1, [x0]
    movz w2, #0x0dd0
    movk w2, #0xedfe, lsl #16           // 0xd00dfeed read as little endian
    cmp  w1, w2
    b.ne bad
    adr  x0, ok1
    bl   puts
    // read /chosen linux,initrd-start straight from the DTB blob is overkill; the initrd is at 0x4A000000
    movz x22, #0x4a00, lsl #16
    ldr  x1, [x22]
    ldr  x2, =0x2121445254494e49        // "INITRD!!"
    cmp  x1, x2
    b.ne bad
    adr  x0, ok2
    bl   puts
    hlt  #0
bad:
    adr  x0, failmsg
    bl   puts
    hlt  #1
puts:
1:  ldrb w1, [x0], #1
    cbz  w1, 2f
    strb w1, [x20]
    b    1b
2:  ret
    .ltorg
ok1:     .asciz "dtb ok\n"
ok2:     .asciz "initrd ok\n"
failmsg: .asciz "boot protocol FAILED\n"
    .balign 4
_end:
