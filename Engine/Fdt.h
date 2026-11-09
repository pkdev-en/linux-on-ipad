// Fdt.h -- minimal flattened-device-tree reader / patcher.
// Linux on iPad -- Apache-2.0.
//
// The engine does not need a full DTB compiler: it receives a compiled blob
// (dts/lop-virt.dts -> dtc) and only has to (a) validate it and (b) patch
// 64-bit properties such as /chosen linux,initrd-start before copying it
// into guest RAM. Property lookup is by name across the whole tree, which
// is enough for the small lop-virt device tree.
#ifndef LOP_FDT_H
#define LOP_FDT_H

#include <cstdint>
#include <vector>

namespace lop {

bool fdtIsValid(const std::vector<uint8_t> &dtb);
bool fdtGetProp64(const std::vector<uint8_t> &dtb, const char *name, uint64_t &out);
bool fdtSetProp64(std::vector<uint8_t> &dtb, const char *name, uint64_t value);

} // namespace lop

#endif // LOP_FDT_H
