# Tests — host-side validation harness

The engine is written to be testable **on the host** (macOS/Linux) without an
iPad or Xcode. Everything here compiles with plain g++/clang++.

## 1. Bare-metal payload test (M1)

```sh
Tests/build_host.sh
# runs: build/host_test   →  "hello from LinuxOnPad bare-metal payload!"  → PASS
```

The payload is embedded in `host_test.cpp` (hand-assembled A64) and also
provided as `payload_hello.S` if you have an aarch64 toolchain.

## 2. Real Linux boot (M2)

1. Get an arm64 kernel **Image** (uncompressed, magic `ARM\x64` at 0x38):
   - Alpine netboot: <https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/netboot/>
     `vmlinuz-lts` is an EFI zboot wrapper — extract:
     ```sh
     python3 - <<'EOF'
     import gzip, struct
     d = open('vmlinuz-lts','rb').read()
     po, ps = struct.unpack_from('<II', d, 8)
     open('Image','wb').write(gzip.decompress(d[po:po+ps]))
     EOF
     ```
2. Build a tiny busybox initramfs:
   ```sh
   # busybox-static arm64 .deb from Debian, extract ./usr/bin/busybox, then:
   python3 Tests/tools/mkinitramfs.py usr/bin/busybox initramfs-busybox.cpio.gz
   ```
3. Boot:
   ```sh
   ./build/host_test --kernel Image --initramfs initramfs-busybox.cpio.gz \
       --bootargs "console=ttyAMA0,115200n8 earlycon=pl011,mmio32,0x09000000" \
       --ram 256 --max-sec 300
   ```

### Debug tooling (in ../scripts during development — port as needed)

The engine exposes debug hooks used by the harnesses:

- `Machine.pc_trace` — ring of the last 32 PCs, included in fault reports
- `Machine.break_pc` / `break_hit` — engine breakpoint (yield slice on match)
- `Machine.watch_lo/hi` — log guest stores into a physical range
- fault string includes `sctlr.M`, `tcr`, `ttbr0/1` at fault time

Typical loop: run → fault report gives PC+INSN → disassemble with capstone →
implement/fix → re-run.
