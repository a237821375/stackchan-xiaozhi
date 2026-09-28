#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace stackchan {
// Factory hal_servo.cpp excludes target errors below eight encoder ticks.
inline constexpr float kSettleToleranceDegrees = 8.f / 3.2f;
inline bool RemoteMotionAllowed(bool approved, bool armed) { return approved && armed; }
class TorqueSafety {
public:
    void BeginWrite() { commanded_ = true; }
    bool ReleaseOnFault(bool fault) {
        if (fault && commanded_) {
            commanded_ = false;
            return true;
        }
        return false;
    }

private:
    bool commanded_ = false;
};
class AxisStall {
public:
    bool Update(float current, float target, int64_t now) {
        if (std::abs(target - current) < kSettleToleranceDegrees) {
            tracking_ = false;
            return false;
        }
        if (!tracking_ || std::abs(current - last_progress_) >= .3f) {
            tracking_ = true;
            last_progress_ = current;
            progress_at_ = now;
        }
        return now - progress_at_ > 1200;
    }
    void Reset() { tracking_ = false; }

private:
    bool tracking_ = false;
    float last_progress_ = 0;
    int64_t progress_at_ = 0;
};
struct Pose {
    float yaw = 0, pitch = 10;
};
class Policy {
public:
    static bool Safe(Pose p) {
        return std::isfinite(p.yaw) && std::isfinite(p.pitch) && p.yaw >= -30 && p.yaw <= 30 &&
               p.pitch >= 5 && p.pitch <= 60;
    }
    void Feedback(Pose p, bool valid, int64_t now) {
        valid_ = valid && Safe(p);
        if (valid_) {
            current_ = p;
            feedback_at_ = now;
        } else
            Fault();
    }
    bool Arm(int64_t now, bool automatic = true) {
        if (!Fresh(now))
            return false;
        armed_ = true;
        fault_ = false;
        base_ = target_ = current_;
        gesture_ = false;
        automatic_ = automatic;
        return true;
    }
    bool Move(Pose p, int64_t now) {
        if (!Ready(now) || !Safe(p))
            return false;
        base_ = target_ = p;
        gesture_ = auto_gesture_ = false;
        count_ = 2;
        return true;
    }
    bool Gesture(bool shake, int64_t now) {
        if (!Ready(now))
            return false;
        gesture_ = true;
        auto_gesture_ = false;
        shake_ = shake;
        gesture_at_ = now;
        count_ = 2;
        return true;
    }
    void Stop(int64_t now) {
        automatic_ = false;
        gesture_ = auto_gesture_ = false;
        if (Ready(now))
            base_ = target_ = current_;
    }
    bool Resume(int64_t now) {
        if (!Fresh(now))
            return false;
        if (!armed_ || fault_)
            return Arm(now);
        automatic_ = true;
        return true;
    }
    void Speaking(bool value, int64_t now) {
        if (value == speaking_)
            return;
        speaking_ = value;
        if (value) {
            next_micro_ = std::max(next_micro_, now + 1800);
        } else if (auto_gesture_) {
            gesture_ = auto_gesture_ = false;
            target_ = base_;
        }
    }
    void BeginTurn(int64_t now) {
        count_ = 0;
        next_micro_ = now + 1800;
    }
    void Emotion(int emotion) { emotion_ = emotion; }
    std::optional<Pose> Step(int64_t now) {
        if (!Ready(now))
            return std::nullopt;
        if (gesture_) {
            auto age = now - gesture_at_;
            // App speed 500: two explicit cycles, with a softer second cycle.
            const int64_t phase_ms = 450;
            const int phases = auto_gesture_ ? (shake_ ? 3 : 2) : (shake_ ? 5 : 4);
            if (age >= phase_ms * phases) {
                gesture_ = auto_gesture_ = false;
                target_ = base_;
            } else {
                target_ = base_;
                const int phase = age / phase_ms;
                float delta;
                if (auto_gesture_)
                    delta = phase == 0 ? 3.f : (phase == 1 && shake_ ? -3.f : 0.f);
                else if (shake_) {
                    constexpr float waypoints[] = {12.f, -12.f, 10.f, -10.f, 0.f};
                    delta = waypoints[phase];
                } else {
                    constexpr float waypoints[] = {8.f, 0.f, 6.f, 0.f};
                    delta = waypoints[phase] * (base_.pitch > 52.f ? -1.f : 1.f);
                }
                if (shake_)
                    target_.yaw = std::clamp(base_.yaw + delta, -30.f, 30.f);
                else
                    target_.pitch = std::clamp(
                        base_.pitch + (emotion_ == 2 && auto_gesture_ ? -delta : delta), 5.f, 60.f);
            }
        }
        if (!gesture_ && speaking_ && emotion_ != 3 && automatic_ && count_ < 2 &&
            now >= next_micro_ && std::abs(current_.yaw - base_.yaw) < 1.5f &&
            std::abs(current_.pitch - base_.pitch) < 1.5f) {
            gesture_ = auto_gesture_ = true;
            shake_ = emotion_ == 1;
            gesture_at_ = now;
            ++count_;
            next_micro_ = now + 4500;
            target_ = base_;
            if (shake_)
                target_.yaw = std::clamp(base_.yaw + 3.f, -30.f, 30.f);
            else
                target_.pitch = std::clamp(base_.pitch + (emotion_ == 2 ? -3.f : 3.f), 5.f, 60.f);
        }
        return target_;
    }
    void Fault() {
        fault_ = true;
        armed_ = false;
        gesture_ = auto_gesture_ = false;
        automatic_ = false;
    }
    bool fault() const { return fault_; }
    bool armed() const { return armed_; }
    bool automatic() const { return automatic_; }
    Pose base() const { return base_; }
    Pose target() const { return target_; }
    Pose current() const { return current_; }

private:
    bool Fresh(int64_t now) const {
        return valid_ && now >= feedback_at_ && now - feedback_at_ <= 500;
    }
    bool Ready(int64_t now) {
        if (!Fresh(now)) {
            Fault();
            return false;
        }
        return armed_ && !fault_;
    }
    Pose current_, base_, target_;
    bool valid_ = false, armed_ = false, fault_ = false, automatic_ = false, speaking_ = false,
         gesture_ = false, shake_ = false, auto_gesture_ = false;
    int emotion_ = 0;
    unsigned count_ = 0;
    int64_t feedback_at_ = 0, gesture_at_ = 0, next_micro_ = 0;
};
}  // namespace stackchan
