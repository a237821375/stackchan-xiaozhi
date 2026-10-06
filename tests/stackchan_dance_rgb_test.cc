#include <array>
#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/dance_rgb.h"
using namespace stackchan;
int main() {
    std::array<uint8_t, 256> ram{};
    ram[0x24] = 0;
    ram[0x04] = 0x81;
    ram[0x0a] = 0x02;
    ram[0x0c] = 0x24;
    ram[0x14] = 0x28;
    for (int i = 0; i < 24; ++i)
        ram[0x30 + i] = i + 1;
    std::array<uint8_t, 24> visible{};
    auto read = [&](uint8_t reg, uint8_t* data, size_t len) {
        std::copy_n(ram.data() + reg, len, data);
        return true;
    };
    auto write = [&](uint8_t reg, const uint8_t* data, size_t len) {
        std::copy_n(data, len, ram.data() + reg);
        if (reg == 0x24 && (data[0] & 64) && (ram[0x04] & 0x20) && (ram[0x0a] & 0x20) &&
            !(ram[0x0c] & 0x20) && !(ram[0x14] & 0x20))
            std::copy_n(ram.data() + 0x30, std::min<size_t>(data[0] & 63, 12) * 2, visible.data());
        return true;
    };
    DanceRgb lights;
    assert(lights.Begin(read, write));
    assert(lights.Frame(0, write));
    assert(ram[0x24] == (12 | 64));
    assert(visible[1] != 0);  // A refreshed frame must reach the RGB data pin.
    assert(ram[0x04] == 0xa1 && ram[0x0a] == 0x22 && ram[0x0c] == 0x04 &&
           ram[0x14] == 0x08);  // Preserve other pins.
    const auto first = ram;
    assert(lights.Frame(2, write));
    assert(ram != first);
    assert(lights.Off(write));
    for (int i = 0; i < 24; ++i)
        assert(visible[i] == 0);
    assert((ram[0x24] & 63) == 12);
    DanceRgb bad;
    assert(!bad.Begin([](uint8_t, uint8_t*, size_t) { return false; }, write));
    const auto before_failed_init = ram;
    assert(!bad.Begin(read, [](uint8_t, const uint8_t*, size_t) { return false; }));
    assert(ram == before_failed_init);
    assert(!bad.Begin(
        [](uint8_t, uint8_t* data, size_t) {
            *data = 0;
            return true;
        },
        write));
    assert(!bad.Frame(0, write));
    assert(!bad.Off(write));
    assert(lights.Begin(read, write) && lights.Frame(0, write));
    bool fail_refresh = true;
    auto flaky_write = [&](uint8_t reg, const uint8_t* data, size_t len) {
        if (reg == 0x24 && fail_refresh) {
            fail_refresh = false;
            return false;
        }
        return write(reg, data, len);
    };
    assert(!lights.Off(flaky_write));
    assert(lights.Off(flaky_write));
    for (auto pixel : visible)
        assert(pixel == 0);
    std::cout << "RGB frames, blackout with zero initial count, failed reads and retry: PASS\n";
}
