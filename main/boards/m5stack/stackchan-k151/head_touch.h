#pragma once
#include <cstdint>
namespace stackchan {
// Original StackChan Si12T.cpp setup: address 0x68, low sensitivity level 3.
// This adapter fails on any transaction/readback error instead of using old data.
template <typename Write, typename Read>
bool ConfigureHeadTouch(Write write, Read read) {
    for (uint8_t r = 0x0a; r <= 0x0f; ++r)
        if (!write(r, 0))
            return false;
    if (!write(9, 0x0f) || !write(9, 7) || !write(8, 0x22))
        return false;
    for (uint8_t r = 2; r <= 6; ++r)
        if (!write(r, 0x33))
            return false;
    for (uint8_t r = 2; r <= 0x0f; ++r) {
        if (r == 7)
            continue;
        uint8_t actual = 0, expected = r <= 6 ? 0x33 : (r == 8 ? 0x22 : (r == 9 ? 7 : 0));
        if (!read(r, actual) || actual != expected)
            return false;
    }
    return true;
}
class TouchFilter {
public:
    bool Update(uint8_t raw, bool valid, int64_t now) {
        if (!valid) {
            if (now - last_valid_ >= 300) {
                touched_ = candidate_ = false;
                samples_ = 0;
            }
            return touched_;
        }
        last_valid_ = now;
        bool contact = (raw & 0x3f) != 0;
        if (contact != candidate_) {
            candidate_ = contact;
            samples_ = 0;
        }
        if (++samples_ >= 2) {
            touched_ = candidate_;
            samples_ = 2;
        }
        return touched_;
    }

private:
    bool touched_ = false, candidate_ = false;
    int samples_ = 0;
    int64_t last_valid_ = 0;
};
}  // namespace stackchan
