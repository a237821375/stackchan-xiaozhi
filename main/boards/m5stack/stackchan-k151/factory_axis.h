#pragma once
#include <algorithm>
#include <functional>
#include <optional>
#include "factory_servo.h"

namespace stackchan {
// Thin hardware boundary around the original StackChan Servo animation class.
class FactoryAxis : public motion::Servo {
public:
    FactoryAxis(int zero, int low, int high, std::function<float()> feedback,
                std::function<bool(int)> write)
        : zero_(zero), feedback_(std::move(feedback)), write_(std::move(write)) {
        set_angle_limit({low * 10, high * 10});
        setAutoTorqueReleaseEnabled(false);  // explicit stop holds the selected pose
    }
    void Reset(float degrees) {
        target_ = static_cast<int>(degrees * 10);
        stop_motion_at_angle(target_);
        failed_ = false;
        written_goal_.reset();
    }
    void Target(float degrees, int speed = 650) {
        const int target =
            std::clamp(static_cast<int>(degrees * 10), _angle_limit.x, _angle_limit.y);
        if (target == target_)
            return;
        target_ = target;
        moveWithSpeed(target_, std::clamp(speed, 100, 650));
    }
    int getCurrentAngle() override { return static_cast<int>(feedback_() * 10); }
    bool failed() const { return failed_; }
    std::optional<int> writtenGoal() const { return written_goal_; }
    // The final snap is a write even after the spring reports done.
    bool animationPending() { return !_angle_anim.done() || _snap_to_target_on_rest; }

protected:
    void set_angle_impl(int angle) override {
        if (failed_)
            return;
        angle = std::clamp(angle, _angle_limit.x, _angle_limit.y);
        // Exact factory 0.1-degree -> SCSCL encoder conversion.
        const int raw = zero_ + angle * 16 / 5 / 10;
        failed_ = !write_(raw);
        if (!failed_)
            written_goal_ = raw;
    }

private:
    int zero_, target_ = 0;
    bool failed_ = false;
    std::optional<int> written_goal_;
    std::function<float()> feedback_;
    std::function<bool(int)> write_;
};
}  // namespace stackchan
