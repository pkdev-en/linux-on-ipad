// Linux/KernelLoader.hpp -- reads an arm64 Linux Image and loads it into guest RAM.
// Linux on iPad -- Apache-2.0.
//
// arm64 Image header (Documentation/arch/arm64/booting.rst):
//
//   u32 code0/code1   0x00  branch past the 64-byte header, then nops
//   u64 text_offset   0x08  load offset from a 2 MiB-aligned RAM base
//   u64 image_size    0x10  full memory footprint incl. .bss (0 = file size)
//   u64 flags         0x18  bit3 = may be placed anywhere (2 MiB-aligned),
//                           bit2 = big-endian (we only take little-endian)
//   u32 magic         0x38  "ARM\x64" = 0x644D5241
//
// Placement rule used here (QEMU "virt" convention):
//   - flags bit3 set  -> load at the RAM base itself (2 MiB-aligned);
//   - otherwise       -> load at ramBase + text_offset (mandatory).
// The entry point is the placement itself: code0 is a branch that skips
// the header, so execution starts at offset 0 of the loaded image.
#ifndef LOP_KERNEL_LOADER_H
#define LOP_KERNEL_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

#include "Memory/GuestMemory.hpp"

namespace lop {

// Parsed header fields.
struct KernelInfo {
    uint64_t textOffset = 0;
    uint64_t imageSize = 0;
    uint64_t flags = 0;

    bool anywherePlaced() const { return flags & (1ull << 3); }
    bool bigEndian() const { return flags & (1ull << 2); }
};

// Where the image landed.
struct KernelLoad {
    uint64_t placement = 0; // guest address the Image was copied to
    uint64_t entry = 0;     // first instruction (== placement, see header)
    uint64_t bssBytes = 0;  // bytes zeroed past the file image (.bss)
};

class KernelLoader {
public:
    static constexpr uint64_t kMagicOffset = 0x38;
    static constexpr uint32_t kMagic = 0x644D5241u; // "ARM\x64"
    static constexpr uint64_t kMinImageSize = 0x40; // header itself
    static constexpr uint64_t k2MiB = 2ull << 20;

    // Validates the arm64 Image header and fills `info`.
    static bool parse(const std::vector<uint8_t> &image, KernelInfo &info,
                      std::string &err);

    // Copies the Image into guest RAM (honouring text_offset / flags) and
    // zeroes the .bss tail up to image_size.
    static bool load(GuestMemory &ram, const std::vector<uint8_t> &image,
                     uint64_t ramBase, const KernelInfo &info, KernelLoad &out,
                     std::string &err);
};

} // namespace lop

#endif // LOP_KERNEL_LOADER_H
