#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace stackchan {
// K151 factory PY32IOExpander_Class (M5Stack, MIT): LED_CFG=0x24,
// LED RAM=0x30, little-endian RGB565, bit 6 refreshes the strip.
class DanceRgb {
public:
    template <typename Read, typename Write>
    bool Begin(Read read, Write write) {
        captured_ = false;
        // Factory RGB data pin 13: output, pull-up, push-pull. Preserve all
        // other GPIOs, including the servo power enable on pin 0.
        for (const auto& setting : std::array<std::array<uint8_t, 2>, 4>{
                 {{0x04, 0x20}, {0x0c, 0}, {0x0a, 0x20}, {0x14, 0}}}) {
            uint8_t value = 0;
            if (!read(setting[0], &value, 1))
                return false;
            value = (value & ~0x20) | setting[1];
            if (!write(setting[0], &value, 1))
                return false;
            uint8_t verified = 0;
            if (!read(setting[0], &verified, 1) || verified != value)
                return false;
        }
        captured_ = read(0x24, &config_, 1);
        return captured_;
    }
    template <typename Write>
    bool Frame(uint32_t tick, Write write) {
        if (!captured_)
            return false;
        constexpr uint16_t colors[] = {0x6000, 0x0300, 0x000c, 0x6300, 0x600c, 0x030c};
        std::array<uint8_t, 24> frame{};
        for (unsigned i = 0; i < 12; ++i) {
            const auto color = ((tick + i / 6) % 2 == 0) ? colors[(tick / 2 + i / 6) % 6] : 0;
            frame[i * 2] = color & 0xff;
            frame[i * 2 + 1] = color >> 8;
        }
        const uint8_t config = (config_ & 0x80) | 12 | 0x40;
        return write(0x30, frame.data(), frame.size()) && write(0x24, &config, 1);
    }
    template <typename Write>
    bool Off(Write write) {
        if (!captured_)
            return false;
        const std::array<uint8_t, 24> black{};
        // Refresh all twelve pixels even if the pre-dance count was zero.
        // A zero-length refresh leaves WS2812 pixels latched in their last color.
        const uint8_t config = (config_ & 0x80) | 12 | 0x40;
        if (!write(0x30, black.data(), black.size()) || !write(0x24, &config, 1))
            return false;
        captured_ = false;
        return true;
    }

private:
    bool captured_ = false;
    uint8_t config_ = 0;
};
}  // namespace stackchan
