// Linux/KernelLoader.cpp -- arm64 Image parsing and loading, see Linux/KernelLoader.hpp.
// Linux on iPad -- Apache-2.0.
#include "Linux/KernelLoader.hpp"

#include <cstring>

namespace lop {

bool KernelLoader::parse(const std::vector<uint8_t> &image, KernelInfo &info,
                         std::string &err) {
    if (image.size() < kMinImageSize) {
        err = "kernel too small to be an arm64 Image";
        return false;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, image.data() + kMagicOffset, 4);
    if (magic != kMagic) {
        err = "bad Image magic (expected ARM64)";
        return false;
    }
    std::memcpy(&info.textOffset, image.data() + 0x08, 8);
    std::memcpy(&info.imageSize, image.data() + 0x10, 8);
    std::memcpy(&info.flags, image.data() + 0x18, 8);

    if (info.bigEndian()) {
        err = "big-endian arm64 Images are not supported";
        return false;
    }
    // Sanity guards against a corrupt header (real values: text_offset is
    // 0 or a few hundred KiB, image_size tens to a couple hundred MiB).
    if (info.textOffset > (32ull << 20) || info.imageSize > (8ull << 30)) {
        err = "implausible Image header (text_offset / image_size)";
        return false;
    }
    return true;
}

bool KernelLoader::load(GuestMemory &ram, const std::vector<uint8_t> &image,
                        uint64_t ramBase, const KernelInfo &info,
                        KernelLoad &out, std::string &err) {
    // Where the Image lands (see header comment for the rule).
    uint64_t placement = info.anywherePlaced() ? ramBase : ramBase + info.textOffset;

    if (!ram.copyIn(placement, image.data(), image.size())) {
        err = "kernel does not fit in guest RAM";
        return false;
    }

    // image_size is the full footprint including .bss; the file may be
    // shorter and the rest must be zero.
    uint64_t footprint = info.imageSize ? info.imageSize : image.size();
    uint64_t bss = footprint > image.size() ? footprint - image.size() : 0;
    if (bss && !ram.zero(placement + image.size(), bss)) {
        err = "kernel .bss does not fit in guest RAM";
        return false;
    }

    out.placement = placement;
    out.entry = placement; // code0 branch skips the 64-byte header
    out.bssBytes = bss;
    return true;
}

} // namespace lop
