// LinuxOnPad — M1 bare-metal test payload.
// Prints a hello string to the PL011 UART at 0x09000000, then parks.
// Assemble with: aarch64-linux-gnu-as -o payload.o payload_hello.S
//                aarch64-linux-gnu-objcopy -O binary payload.o payload_hello.bin
// Then run:      ./build/host_test --payload payload_hello.bin

    .arm64
    .text
    .globl _start
_start:
    adr     x1, msg                 // x1 -> message
    movz    w2, #0x0900, lsl #16    // w2 = 0x09000000 (UART base)
loop:
    ldrb    w3, [x1], #1            // load byte, post-increment
    cbz     w3, done                // NUL terminator -> done
    str     w3, [x2]                // PL011 DR: transmit byte
    b       loop
done:
    wfi                             // idle (engine yields the slice)
    b       .

    .balign 4
msg:
    .asciz "hello from LinuxOnPad bare-metal payload!\n"
