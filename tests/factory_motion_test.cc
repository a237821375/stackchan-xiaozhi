#include <cassert>
#include <cmath>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/factory_axis.h"
#include "../main/boards/m5stack/stackchan-k151/factory_upstream/smooth_ui_toolkit/src/core/hal/hal.hpp"
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"

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
    // Exercise the policy together with the actual factory spring, not just targets.
    for (bool shake : {false, true}) {
        float angle = shake ? 0.f : 20.f;
        const float base = angle;
        float low = angle, high = angle;
        stackchan::Policy policy;
        auto feedback = [&] {
            return shake ? stackchan::Pose{angle, 20} : stackchan::Pose{0, angle};
        };
        policy.Feedback(feedback(), true, now);
        assert(policy.Arm(now));
        stackchan::FactoryAxis gesture_axis(
            shake ? 461 : 610, shake ? -30 : 5, shake ? 30 : 60, [&] { return angle; },
            [&](int raw) {
                angle = (raw - (shake ? 461 : 610)) / 3.2f;
                low = std::min(low, angle);
                high = std::max(high, angle);
                return true;
            });
        gesture_axis.Reset(angle);
        assert(policy.Gesture(shake, now));
        for (int i = 0; i < 350; ++i) {
            now += 20;
            policy.Feedback(feedback(), true, now);
            auto target = policy.Step(now);
            assert(target);
            gesture_axis.Target(shake ? target->yaw : target->pitch);
            gesture_axis.update();
        }
        std::cout << "gesture " << shake << " excursion=" << low - base << ".." << high - base
                  << std::endl;
        assert(high - base >= 4.f);
        if (shake)
            assert(base - low >= 4.f);
        assert(std::abs(angle - base) < 1.f);
    }
    std::cout << "factory spring trajectory: PASS\n";
}
