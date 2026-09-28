#pragma once
#include <cstdint>
namespace stackchan {
// Official K151 PY32IOExpander: VM_EN is pin 0 (I2C address 0x6f).
// Preserve unrelated pins; never turn a failed read into a register value.
template <typename Read, typename Write>
bool EnableServoPower(Read read, Write write) {
    uint8_t version = 0;
    if (!read(0x02, version) || version == 0 || version == 255)
        return false;
    auto bit = [&](uint8_t reg, bool value) {
        uint8_t old = 0;
        if (!read(reg, old))
            return false;
        const uint8_t desired = value ? (old | 1) : (old & 0xfe);
        if (!write(reg, desired))
            return false;
        uint8_t actual = 0;
        return read(reg, actual) && actual == desired;
    };
    return bit(0x03, true) && bit(0x0b, false) && bit(0x09, true) && bit(0x05, true);
}
}  // namespace stackchan
