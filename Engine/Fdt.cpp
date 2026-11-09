// Fdt.cpp -- flattened device tree blob reader / patcher, see Fdt.h.
//
// DTB format (devicetree-specification v0.3+):
//   header (all big-endian u32): magic, totalsize, off_dt_struct, off_dt_strings,
//     off_mem_rsvmap, version, last_comp_version, boot_cpuid_phys,
//     size_dt_strings, size_dt_struct
//   memory reservation block, then the struct block with tokens:
//     1 begin node (name, NUL, pad), 2 end node, 3 property (len, nameoff, data),
//     4 nop, 9 end
//   then the strings block (NUL-terminated property names).
#include "Fdt.h"

#include <cstring>

namespace lop {

namespace {

uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
[[maybe_unused]] uint64_t be64(const uint8_t *p) {
    return (uint64_t(be32(p)) << 32) | be32(p + 4);
}
void putBe64(uint8_t *p, uint64_t v) {
    p[0] = uint8_t(v >> 56); p[1] = uint8_t(v >> 48);
    p[2] = uint8_t(v >> 40); p[3] = uint8_t(v >> 32);
    p[4] = uint8_t(v >> 24); p[5] = uint8_t(v >> 16);
    p[6] = uint8_t(v >> 8);  p[7] = uint8_t(v);
}

struct Header {
    uint32_t magic, totalSize, offStruct, offStrings;
};

bool parseHeader(const std::vector<uint8_t> &dtb, Header &h) {
    if (dtb.size() < 40) return false;
    h.magic = be32(dtb.data());
    h.totalSize = be32(dtb.data() + 4);
    h.offStruct = be32(dtb.data() + 8);
    h.offStrings = be32(dtb.data() + 12);
    if (h.magic != 0xD00DFEEDu) return false;
    if (h.totalSize != dtb.size()) return false; // blob must be exactly the DTB
    if (h.offStruct >= dtb.size() || h.offStrings >= dtb.size()) return false;
    return true;
}

// Walks the struct block looking for a property whose name matches and whose
// payload is exactly 8 bytes (a /bits/ 64 property). Returns the byte offset
// of the payload, or SIZE_MAX when absent.
size_t findProp64(const std::vector<uint8_t> &dtb, const Header &h, const char *name) {
    size_t off = h.offStruct;
    for (;;) {
        if (off + 4 > dtb.size()) return SIZE_MAX;
        uint32_t token = be32(dtb.data() + off);
        off += 4;
        switch (token) {
        case 1: { // BEGIN_NODE: name (NUL, padded to 4)
            while (off < dtb.size() && dtb[off] != 0) ++off;
            if (off >= dtb.size()) return SIZE_MAX;
            ++off;
            off = (off + 3) & ~size_t(3);
            break;
        }
        case 2: // END_NODE
        case 4: // NOP
            break;
        case 3: { // PROP
            if (off + 8 > dtb.size()) return SIZE_MAX;
            uint32_t len = be32(dtb.data() + off);
            uint32_t nameOff = be32(dtb.data() + off + 4);
            off += 8;
            size_t sPos = h.offStrings + nameOff;
            if (sPos >= dtb.size()) return SIZE_MAX;
            const char *pname = reinterpret_cast<const char *>(dtb.data() + sPos);
            if (off + len > dtb.size()) return SIZE_MAX;
            if (len == 8 && std::strcmp(pname, name) == 0) return off;
            off = (off + len + 3) & ~size_t(3);
            break;
        }
        case 9: // END
            return SIZE_MAX;
        default:
            return SIZE_MAX;
        }
    }
}

} // namespace

bool fdtIsValid(const std::vector<uint8_t> &dtb) {
    Header h;
    if (!parseHeader(dtb, h)) return false;
    // structure must end with the END token
    size_t off = h.offStruct;
    for (;;) {
        if (off + 4 > dtb.size()) return false;
        uint32_t token = be32(dtb.data() + off);
        off += 4;
        switch (token) {
        case 1:
            while (off < dtb.size() && dtb[off] != 0) ++off;
            if (off >= dtb.size()) return false;
            ++off;
            off = (off + 3) & ~size_t(3);
            break;
        case 2:
        case 4:
            break;
        case 3: {
            if (off + 8 > dtb.size()) return false;
            uint32_t len = be32(dtb.data() + off);
            uint32_t nameOff = be32(dtb.data() + off + 4);
            off += 8;
            if (h.offStrings + nameOff >= dtb.size()) return false;
            if (off + len > dtb.size()) return false;
            off = (off + len + 3) & ~size_t(3);
            break;
        }
        case 9:
            return true;
        default:
            return false;
        }
    }
}

bool fdtGetProp64(const std::vector<uint8_t> &dtb, const char *name, uint64_t &out) {
    Header h;
    if (!parseHeader(dtb, h)) return false;
    size_t off = findProp64(dtb, h, name);
    if (off == SIZE_MAX) return false;
    const uint8_t *p = dtb.data() + off;
    out = (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) |
          (uint64_t(p[3]) << 32) | (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) |
          (uint64_t(p[6]) << 8) | p[7];
    return true;
}

bool fdtSetProp64(std::vector<uint8_t> &dtb, const char *name, uint64_t value) {
    Header h;
    if (!parseHeader(dtb, h)) return false;
    size_t off = findProp64(dtb, h, name);
    if (off == SIZE_MAX) return false;
    putBe64(dtb.data() + off, value);
    return true;
}

} // namespace lop
