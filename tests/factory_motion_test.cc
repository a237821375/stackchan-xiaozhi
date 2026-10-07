#include <cassert>
#include <cmath>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/factory_axis.h"
#include "../main/boards/m5stack/stackchan-k151/factory_upstream/smooth_ui_toolkit/src/core/hal/hal.hpp"
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
#include "../main/boards/m5stack/stackchan-k151/motion_result.h"
#include "../main/boards/m5stack/stackchan-k151/stall_evidence.h"

int main() {
    uint32_t now = 0;
    smooth_ui_toolkit::ui_hal::on_get_tick([&] { return now; });
    {
        float pitch = 10;
        stackchan::FactoryAxis probe_axis(
            610, 5, 60, [&] { return pitch; },
            [&](int raw) {
                pitch = (raw - 610) / 3.2f;
                return true;
            });
        stackchan::PoseCommandResult result;
        probe_axis.Reset(pitch);
        result.Begin({0, pitch}, {0, 12.59f});
        probe_axis.Target(12.59f);
        for (int i = 0; i < 100; ++i) {
            const stackchan::Pose sampled{0, pitch};
            bool pending = probe_axis.animationPending();
            now += 20;
            probe_axis.update();
            pending = pending || probe_axis.animationPending();
            result.Update(sampled, true, pending, false);
        }
        assert(stackchan::AtTarget({0, pitch}, {0, 12.59f}));
        assert(result.finished() && result.target_reached());
    }
    {
        int written = -1;
        stackchan::FactoryAxis slow(
            461, -30, 30, [] { return 0.f; },
            [&](int raw) {
                written = raw;
                return true;
            });
        slow.Reset(0);
        assert(!slow.writtenGoal());
        slow.Target(5, 400);
        stackchan::FactoryStallEvidence actual_goal, premature_final_goal;
        now = 0;
        for (int i = 1; i <= 3; ++i) {
            now = i * 60;
            const int load = (i - 1) * 150;
            if (slow.writtenGoal())
                assert(!actual_goal.Update(461, *slow.writtenGoal(), load, 0, now));
            const bool premature = premature_final_goal.Update(461, 477, load, 0, now);
            if (i == 3)
                assert(premature);  // final policy target would falsely classify early acceleration
            slow.update();
            assert(slow.writtenGoal() && *slow.writtenGoal() == written);
        }
        slow.Reset(0);
        assert(!slow.writtenGoal());
        stackchan::FactoryAxis failed(461, -30, 30, [] { return 0.f; }, [](int) { return false; });
        failed.Reset(0);
        failed.Target(5);
        now += 20;
        failed.update();
        assert(failed.failed() && !failed.writtenGoal());
    }
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
    for (now = 20; now <= 360; now += 20)
        axis.update();
    assert(std::abs(actual) < 2.f);  // Faster profile must settle within 360 ms.
    for (; now <= 6000; now += 20)
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
        const uint32_t start = now;
        int excursions = 0;
        bool outside = false;
        for (int i = 0; i < 350; ++i) {
            now += 20;
            policy.Feedback(feedback(), true, now);
            auto target = policy.Step(now);
            assert(target);
            gesture_axis.Target(shake ? target->yaw : target->pitch);
            gesture_axis.update();
            const bool next_outside = angle - base > 4.f;
            if (next_outside && !outside)
                ++excursions;
            outside = next_outside;
            if (now - start == 3000) {
                assert(excursions >= 2);
                assert(std::abs(angle - base) < 1.f);
            }
        }
        std::cout << "gesture " << shake << " excursion=" << low - base << ".." << high - base
                  << std::endl;
        assert(high - base >= (shake ? 15.f : 10.f));
        if (shake)
            assert(base - low >= 15.f);
        assert(std::abs(angle - base) < 1.f);
    }
    {
        float pitch = 10, low = 100, high = 0;
        stackchan::Policy pet;
        pet.Feedback({0, pitch}, true, now);
        assert(pet.Arm(now));
        pet.Touch(true, now);
        stackchan::FactoryAxis pet_axis(
            610, 5, 60, [&] { return pitch; },
            [&](int raw) {
                pitch = (raw - 610) / 3.2f;
                return true;
            });
        pet_axis.Reset(pitch);
        for (int elapsed = 0; elapsed <= 11000; elapsed += 20) {
            now += 20;
            pet.Feedback({0, pitch}, true, now);
            if (elapsed == 6000)
                pet.Touch(false, now);
            auto target = pet.Step(now);
            assert(target);
            pet_axis.Target(target->pitch, pet.speed());
            pet_axis.update();
            if (elapsed >= 2000 && elapsed < 9000) {
                low = std::min(low, pitch);
                high = std::max(high, pitch);
            }
            if (elapsed == 8980)
                assert(pet.petting());
            if (elapsed == 9000)
                assert(!pet.petting());
        }
        assert(low < 26 && high > 30 && std::abs(pitch - 10) < 1);
        std::cout << "pet spring excursion=" << low << ".." << high << ", restored=" << pitch
                  << '\n';
    }
    // User repro: touch while a two-axis idle glance is still in flight.
    // Ensure ownership switches, the old yaw goal is cancelled, and release
    // restores the measured pose at touch rather than the old idle destination.
    for (int touch_delay : {20, 60, 120, 240}) {
        stackchan::Pose measured{0, 10}, commanded = measured, saved;
        stackchan::Policy policy;
        policy.Feedback(measured, true, now);
        assert(policy.Arm(now));
        policy.Idle(true, false, 0, now);
        stackchan::FactoryAxis yaw(
            461, -30, 30, [&] { return measured.yaw; },
            [&](int raw) {
                commanded.yaw = (raw - 461) / 3.2f;
                return true;
            });
        stackchan::FactoryAxis pitch(
            610, 5, 60, [&] { return measured.pitch; },
            [&](int raw) {
                commanded.pitch = (raw - 610) / 3.2f;
                return true;
            });
        yaw.Reset(measured.yaw);
        pitch.Reset(measured.pitch);
        now += 4000;
        const uint32_t start = now;
        for (int elapsed = 0; elapsed <= 5200; elapsed += 20) {
            now = start + elapsed;
            // Lagging physical feedback, so internal and actual angles differ.
            measured.yaw += (commanded.yaw - measured.yaw) * .4f;
            measured.pitch += (commanded.pitch - measured.pitch) * .4f;
            policy.Feedback(measured, true, now);
            policy.Idle(true, elapsed != 0 && (yaw.isMoving() || pitch.isMoving()), 0, now);
            if (elapsed == touch_delay) {
                assert(policy.idle_motion() && yaw.isMoving());
                saved = measured;
                policy.Touch(true, now);
                assert(policy.petting() && !policy.idle_motion());
            }
            if (elapsed == touch_delay + 1000)
                policy.Touch(false, now);
            const auto target = policy.Step(now);
            assert(target && !policy.fault());
            if (elapsed >= touch_delay)
                assert(std::abs(target->yaw - saved.yaw) < .01f);
            yaw.Target(target->yaw, policy.speed());
            pitch.Target(target->pitch, policy.speed());
            yaw.update();
            pitch.update();
        }
        assert(!policy.petting());
        assert(std::abs(measured.yaw - saved.yaw) < 1);
        assert(std::abs(measured.pitch - saved.pitch) < 1);
    }
    std::cout << "idle-to-touch handoff at four motion phases: PASS\n";
    {
        stackchan::Pose measured{0, 10};
        stackchan::Policy policy;
        policy.Feedback(measured, true, now);
        assert(policy.Arm(now));
        stackchan::FactoryAxis yaw(
            461, -30, 30, [&] { return measured.yaw; },
            [&](int raw) {
                measured.yaw = (raw - 461) / 3.2f;
                return true;
            });
        stackchan::FactoryAxis pitch(
            610, 5, 60, [&] { return measured.pitch; },
            [&](int raw) {
                measured.pitch = (raw - 610) / 3.2f;
                return true;
            });
        yaw.Reset(measured.yaw);
        pitch.Reset(measured.pitch);
        assert(policy.Dance(now, 19));
        const uint32_t start = now;
        float left = 0, right = 0, low = 60, high = 5;
        for (int elapsed = 0; elapsed <= 17000; elapsed += 20) {
            now = start + elapsed;
            policy.Feedback(measured, true, now);
            const auto target = policy.Step(now);
            assert(target && !policy.fault());
            yaw.Target(target->yaw, policy.speed());
            pitch.Target(target->pitch, policy.speed());
            const auto previous = measured;
            yaw.update();
            pitch.update();
            if (std::abs(measured.yaw - previous.yaw) >= 8 ||
                std::abs(measured.pitch - previous.pitch) >= 8)
                std::cerr << "dance step t=" << elapsed << " before=" << previous.yaw << ","
                          << previous.pitch << " after=" << measured.yaw << "," << measured.pitch
                          << " target=" << target->yaw << "," << target->pitch << std::endl;
            // Faster dance permits up to eight degrees per 20 ms command step.
            assert(std::abs(measured.yaw - previous.yaw) < 8);
            assert(std::abs(measured.pitch - previous.pitch) < 8);
            assert(stackchan::Policy::FeedbackSafe(measured));
            left = std::min(left, measured.yaw);
            right = std::max(right, measured.yaw);
            low = std::min(low, measured.pitch);
            high = std::max(high, measured.pitch);
        }
        assert(left < -20 && right > 20 && high - low > 30);
        assert(!policy.dancing() && std::abs(measured.yaw) < 1 &&
               std::abs(measured.pitch - 10) < 1);
        std::cout << "dance factory trajectory yaw=" << left << ".." << right << " pitch=" << low
                  << ".." << high << ": PASS\n";
    }
    std::cout << "factory spring trajectory: PASS\n";
}
