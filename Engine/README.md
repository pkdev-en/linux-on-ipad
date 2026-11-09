# Engine

The C++ core: an AArch64 interpreter plus the virtual board it runs on.
Compiled into the iOS app by XcodeGen (`project.yml` picks up every file
under `Engine/` recursively) and on the host by the root `Makefile`
(`find Engine -name '*.cpp'`).

## Layout

```
Engine/
  CPU/      ARM64CPU        -- architectural state + fetch-decode-execute loop
  Memory/   GuestMemory     -- the guest RAM block (bounds-checked)
  Bus/      MemoryBus       -- routes CPU accesses: RAM / PL011 / GIC stub
  Linux/    KernelLoader    -- arm64 Image header parse + load into RAM
            BootConfig      -- DTB/initrd placement + boot register state
  Machine   -- the board wiring: CPU + Bus + boot pipeline + run()
  ...       -- decoder/interpreter, devices, FDT, selftest, app ABI
```

`Bus.h`, `Bus.cpp`, `Cpu.h`, `Cpu.cpp` at the top level are compatibility
shims (`using Bus = MemoryBus; using Cpu = ARM64CPU;`) so pre-layering
includes keep compiling. Delete them once nothing includes them anymore.

## Files

| File | Purpose |
| --- | --- |
| `CPU/ARM64CPU.hpp/.cpp` | architectural state (x0–x30, SP, PC, NZCV, DAIF, sysregs) + the fetch-decode-execute loop |
| `Memory/GuestMemory.hpp/.cpp` | guest physical RAM: bounds-checked load/store, blob copies, .bss zeroing |
| `Bus/MemoryBus.hpp/.cpp` | guest physical map: RAM @ 0x40000000, PL011 @ 0x09000000, GIC stub |
| `Linux/KernelLoader.hpp/.cpp` | arm64 Image header parsing, placement (text_offset/flags), .bss clearing |
| `Linux/BootConfig.hpp/.cpp` | initrd/DTB placement, /chosen patching, boot register state (x0 = DTB) |
| `Decode.h/.cpp` | AArch64 instruction decoder (bit patterns verified against keystone) |
| `Interp.h/.cpp` | instruction executor (semantics cross-checked vs QEMU + unicorn oracle) |
| `Machine.h/.cpp` | the board: run loop, `loadFlat`, Linux boot protocol (`bootLinux`) |
| `Fdt.h/.cpp` | flattened device tree validation + `/chosen` initrd patching |
| `Uart.h/.cpp` | PL011 UART model — guest console output, host keyboard input later |
| `SelfTest.h/.cpp` | embedded bare-metal smoke test (generated from `tests/selftest.S`) |
| `EngineApi.h/.cpp` | small C ABI for SwiftUI (`lop_selftest`) |
| `EngineInfo.h/.cpp` | original scaffold entry point (kept) |

## Implementation order (matches the original plan)

1. Guest memory map and safe memory access — `Memory/GuestMemory`
2. ARM64 instruction decoder — `Decode`
3. Interpreter for a large instruction subset — `Interp`
4. Virtual devices (UART first) — `Uart`, GIC stub in `Bus/MemoryBus`
5. Linux boot protocol (Image + DTB + initrd) — `Linux/KernelLoader`,
   `Linux/BootConfig`, `Machine::bootLinux`, `Fdt`
6. MMU + interrupts (M3), then virtio (M4), framebuffer (M5), JIT (M6)

Verification: `make selftest` (embedded payload), `make semtest` (24k+
instruction semantics vs a Unicorn/QEMU oracle), `make test` (qemu-user
differential, boot protocol, privileged sysregs).
