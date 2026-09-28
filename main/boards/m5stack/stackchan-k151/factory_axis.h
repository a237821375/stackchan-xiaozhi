#pragma once
#include <algorithm>
#include <functional>
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
    }
    void Target(float degrees) {
        const int target =
            std::clamp(static_cast<int>(degrees * 10), _angle_limit.x, _angle_limit.y);
        if (target == target_)
            return;
        target_ = target;
        moveWithSpeed(target_, 650);  // user-selected faster profile; App default is 500
    }
    int getCurrentAngle() override { return static_cast<int>(feedback_() * 10); }
    bool failed() const { return failed_; }

protected:
    void set_angle_impl(int angle) override {
        if (failed_)
            return;
        angle = std::clamp(angle, _angle_limit.x, _angle_limit.y);
        // Exact factory 0.1-degree -> SCSCL encoder conversion.
        const int raw = zero_ + angle * 16 / 5 / 10;
        failed_ = !write_(raw);
    }

private:
    int zero_, target_ = 0;
    bool failed_ = false;
    std::function<float()> feedback_;
    std::function<bool(int)> write_;
};
}  // namespace stackchan
