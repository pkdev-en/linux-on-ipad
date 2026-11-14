# lxemu — an arm64 Linux machine for iPad (work in progress)

A from-scratch emulator that will boot stock **Debian** and **Arch Linux ARM** on
iOS/iPadOS. Not a fork of anything; it only borrows *ideas* (see "License").

## Status: foundation done, **cannot boot Linux yet**

| Piece | State |
|---|---|
| Machine, bus, RAM, PL011 UART (transmit only) | done, tested |
| QEMU-`virt`-compatible memory map | done |
| Device-tree writer + DTB for the machine (incl. framebuffer) | done, checked with `dtc` |
| arm64 `Image` loader + Linux boot protocol (x0 = DTB, initrd) | done, tested with a synthetic Image |
| AArch64 reference interpreter (integer subset, EL1, MMU off) | done, output identical to QEMU on the test program |
| Display: framebuffer in guest RAM, exposed as `simple-framebuffer` | done, tested end to end (guest pixels → PPM) |
| Input queue (lock-free, evdev-shaped events) | done, tested incl. two-thread stress |
| Public C API (`src/lxemu.h`) used by CLI and iOS front end | done, tested with sliced runs |
| iOS front end: Metal framebuffer view + VM wrapper (`ios/`) | written, **never compiled** — first compile is the macOS CI job |
| GitHub Actions CI (`.github/workflows/ci.yml`) | written, **never run** (YAML parses; the Linux job's commands were run locally) |
| MMU, exceptions, full ISA, FP/ASIMD, GIC, timer, virtio | **not started** — see milestones |

Nothing has run on an iPad and no real Linux kernel has been executed yet.

## Layout

    src/lxemu.h      public API: the only thing a front end sees
    src/vm.c         assembles machine + CPU + display + input; load/boot logic
    src/machine.*    RAM, physical bus, PL011
    src/cpu.*        reference interpreter (later: JIT, with this as the oracle)
    src/boot.*       Image loader, device-tree builder
    src/fdt.*        flattened-device-tree writer
    src/display.*    framebuffer carve-out + PPM screenshot
    src/input.*      key/pointer event queue
    src/main.c       CLI (tests and desktop use)
    ios/             Swift: FramebufferView (Metal), VirtualMachine, bridging header
    scripts/         build-ios.sh
    tests/           assembly programs, unit tests, QEMU differential check

## Build and test

    make                    # ./lxemu, plain C11
    make lib                # build/liblxemu.a
    make check              # needs binutils-aarch64-linux-gnu, device-tree-compiler, python3
    sh tests/qemu_diff.sh   # optional: compare against qemu-system-aarch64
    sh scripts/build-ios.sh # macOS + Xcode: iOS static lib + Swift type-check

    ./lxemu --kernel Image --initrd initrd.img --append "console=ttyAMA0 earlycon" --ram 512 --fb 1280x800

On a real kernel it will currently stop with an exact message such as
`unimplemented instruction 0x... at pc=...` — that message is the work queue.

## CI (GitHub Actions)

* **linux:** `make check`, the QEMU comparison, then everything again under ASan + UBSan.
* **macos:** `scripts/build-ios.sh` (iOS arm64 static library + Swift type-check against
  the iOS SDK), a native build, and the tests via Homebrew's `aarch64-elf-binutils`
  (that last step is `continue-on-error` because it has not been run yet). Uploads
  `liblxemu.a` as an artifact.
* Not here yet: an Xcode project / `.ipa` (needs an app target — see milestone 6).

## Design decisions

* **C11 core, host-independent.** Built and tested on a desktop; the iOS app links it.
* **Reference interpreter first.** Correctness before speed. The JIT (arm64→arm64, most
  instructions copied unchanged, memory accesses through a software TLB) comes later and is
  tested block by block against this interpreter. QEMU is used only on a development
  machine as an independent check; it is not part of the product.
* **QEMU `virt` memory map.** Debian and Arch kernels already have drivers for every device
  on it; no kernel patches.
* **Direct kernel boot, no UEFI/ACPI.** Image + DTB (+ initrd) in RAM, MMU off, `x0` = DTB.
* **Display without a device model.** A framebuffer region at the top of RAM is described
  to Linux as `simple-framebuffer` (needs simplefb/simpledrm in the guest kernel — check the
  distro config); the Metal view reads that memory directly. A virtio-gpu device is only
  needed later for resizing.
* **PSCI over HVC** handled by the host (no EL2 is emulated).

## The decision that shapes the MMU: 4 KiB pages

Stock Debian and Arch Linux ARM arm64 kernels use **4 KiB** pages; the iPad's CPU uses
16 KiB. The software MMU/TLB must therefore support the 4 KiB granule and never assume
guest pages equal host pages. (A custom 16 KiB-page kernel would sidestep this but is a
later optimisation.) The loader prints the page size a kernel declares.

## Guest preparation (NOT yet verified — nothing boots yet)

* **Debian:** `debootstrap --arch=arm64 stable rootfs` (needs `qemu-user-static` for the
  second stage on an x86 machine) into an ext4 image; kernel from `linux-image-arm64`
  (the loader rejects gzip: decompress first).
* **Arch Linux ARM:** the generic `ArchLinuxARM-aarch64-latest.tar.gz` rootfs and the
  `linux-aarch64` kernel `Image`.
* Until virtio-blk exists, boot a small **initramfs** (`--initrd`) first.

## Milestones

1. **Foundation** — done (this repo).
2. **MMU + exception model:** stage-1 translation (4/16/64 KiB granules, 48-bit VA),
   software TLB, permission/access-flag faults; VBAR vectors, ESR/FAR/ELR/SPSR, SVC, ERET,
   SP_EL0/SP_EL1, IRQ/FIQ masking.
3. **ISA completion:** extended-register and 2-source ops, ADC/SBC, CCMP, REV/CLZ/RBIT,
   LDR literal and register-offset, exclusives (+ hide LSE via ID registers), TLBI/DC/IC,
   MSR-immediate, ID registers, **FP/ASIMD** (glibc uses it in userspace).
4. **GICv2 + generic timer + PL011 RX/IRQ** → boot an initramfs to a shell.
5. **virtio-blk / net / rng / input** → Debian and Arch rootfs, networking, keyboard/mouse.
6. **iOS app + JIT:** app target / `.ipa` build in CI, JIT region (same debugger/StikJIT
   route as other iOS emulators), then the copy-through translator.

## Known gaps (deliberate, so they are not forgotten)

* MMU-off alignment rules: real CPUs fault on unaligned access to Device memory (all memory
  while the MMU is off); the interpreter allows it.
* Unknown system registers are a hard stop, not RAZ/WI, so bugs surface early.
* One CPU only (no secondary-core PSCI CPU_ON yet).
* The input queue exists but nothing consumes it until virtio-input.

## License

Not chosen yet — pick one before publishing. Henge and QEMU are GPL-2.0-or-later: reading
them for behaviour is fine, but copying code would make this project a derivative work.
Everything here was written independently.
