#include <cassert>
#include <cmath>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/factory_axis.h"
#include "../main/boards/m5stack/stackchan-k151/factory_upstream/smooth_ui_toolkit/src/core/hal/hal.hpp"

int main() {
    uint32_t now = 0;
    smooth_ui_toolkit::ui_hal::on_get_tick([&] { return now; });
    float actual = 27.5f;
    int writes = 0, last = -1;
    stackchan::FactoryAxis axis(
        461, -30, 30, [&] { return actual; },
        [&](int raw) {
            ++writes;
            last = raw;
            // Simulate encoder quantization and a one-tick deadband.
            if (std::abs(raw - (461 + actual * 3.2f)) > 1)
                actual = (raw - 461) / 3.2f;
            return true;
        });
    axis.Reset(actual);
    assert(writes == 0);  // adopting feedback must not command the device
    axis.Target(0);
    for (now = 20; now <= 6000; now += 20)
        axis.update();
    assert(writes > 20 && last == 461 && std::abs(actual) < 1);
    axis.Target(30);
    for (int i = 0; i < 10; ++i) {
        now += 20;
        axis.update();
    }
    float stopped = actual;
    axis.Reset(stopped);
    for (int i = 0; i < 200; ++i) {
        now += 20;
        axis.update();
    }
    assert(std::abs(actual - stopped) < 1);
    assert(!axis.failed());
    std::cout << "factory spring trajectory: PASS\n";
}
