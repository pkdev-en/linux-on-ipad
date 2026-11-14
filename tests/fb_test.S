// Draws into the framebuffer that lxemu carves out of the top of RAM
// (--fb 64x32 on 256 MiB: 0x4ff00000). First row red, last pixel blue.
    .text
    .global _start
_start:
    mov     x0, #0x4ff00000
    mov     w1, #0xff0000            // 0x00RRGGBB: red
    mov     x2, #64
1:  str     w1, [x0], #4
    subs    x2, x2, #1
    b.ne    1b
    mov     x3, #0x4ff00000
    mov     x4, #0x1ffc              // (31*64 + 63) * 4: last pixel
    add     x3, x3, x4
    mov     w1, #0xff                // blue
    str     w1, [x3]
    mov     x0, #0
    hlt     #0
