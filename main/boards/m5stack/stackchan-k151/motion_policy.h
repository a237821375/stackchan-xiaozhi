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
enum class HeadFault {
    None,
    Communication,
    UnsafeFeedback,
    ServoAlarm,
    Stall,
    NoProgress,
    Overload
};
inline const char* FaultName(HeadFault reason) {
    switch (reason) {
        case HeadFault::Communication:
            return "communication_timeout";
        case HeadFault::UnsafeFeedback:
            return "unsafe_feedback_or_configuration";
        case HeadFault::ServoAlarm:
            return "servo_alarm";
        case HeadFault::Stall:
            return "mechanical_stall";
        case HeadFault::NoProgress:
            return "motion_no_progress";
        case HeadFault::Overload:
            return "overload";
        default:
            return "none";
    }
}
inline HeadFault ClassifyMotionStop(bool no_progress, bool stall_evidence) {
    return stall_evidence ? HeadFault::Stall
                          : (no_progress ? HeadFault::NoProgress : HeadFault::None);
}
inline const char* RemoteMotionBlockReason(bool approved, bool armed) {
    return !approved ? "local_verification_required" : (!armed ? "motion_not_armed" : "none");
}
class Policy {
public:
    static bool Safe(Pose p) {
        return std::isfinite(p.yaw) && std::isfinite(p.pitch) && p.yaw >= -30 && p.yaw <= 30 &&
               p.pitch >= 5 && p.pitch <= 60;
    }
    static Pose Clamp(Pose p) {
        return {std::clamp(p.yaw, -30.f, 30.f), std::clamp(p.pitch, 5.f, 60.f)};
    }
    static bool FeedbackSafe(Pose p) {
        // Actual feedback may settle slightly beyond a commanded endpoint.
        // This tolerance never expands the range of emitted targets.
        return std::isfinite(p.yaw) && std::isfinite(p.pitch) &&
               p.yaw >= -30 - kSettleToleranceDegrees && p.yaw <= 30 + kSettleToleranceDegrees &&
               p.pitch >= 5 - kSettleToleranceDegrees && p.pitch <= 60 + kSettleToleranceDegrees;
    }
    void Feedback(Pose p, bool valid, int64_t now) {
        valid_ = valid && FeedbackSafe(p);
        if (valid_) {
            current_ = p;
            feedback_at_ = now;
        } else
            Fault(HeadFault::UnsafeFeedback);
    }
    bool Arm(int64_t now, bool automatic = true) {
        if (!Fresh(now))
            return false;
        armed_ = true;
        fault_ = false;
        fault_reason_ = HeadFault::None;
        base_ = target_ = Clamp(current_);
        gesture_ = false;
        automatic_ = automatic;
        CancelBackground(now);
        return true;
    }
    bool Move(Pose p, int64_t now) {
        if (!Ready(now) || !Safe(p))
            return false;
        CancelBackground(now);
        base_ = target_ = p;
        gesture_ = auto_gesture_ = false;
        count_ = 2;
        return true;
    }
    bool Gesture(bool shake, int64_t now) {
        if (!Ready(now))
            return false;
        CancelBackground(now);
        gesture_ = true;
        auto_gesture_ = false;
        shake_ = shake;
        gesture_at_ = now;
        count_ = 2;
        return true;
    }
    bool Dance(int64_t now, uint32_t seed) {
        if (!Ready(now) || touched_)
            return false;
        const Pose restore = dancing_ ? dance_restore_ : Clamp(current_);
        CancelBackground(now);
        gesture_ = auto_gesture_ = false;
        dance_restore_ = restore;
        dance_seed_ = seed;
        dance_started_ = now;
        dancing_ = true;
        count_ = 2;
        return true;
    }
    void EndDance(int64_t now) {
        if (!dancing_)
            return;
        dancing_ = false;
        base_ = target_ = dance_restore_;
        next_idle_ = now + 8000;
    }
    bool dancing() const { return dancing_; }
    void Stop(int64_t now) {
        CancelBackground(now);
        automatic_ = false;
        gesture_ = auto_gesture_ = false;
        if (Ready(now))
            base_ = target_ = Clamp(current_);
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
    void Idle(bool idle, bool motor_busy, uint32_t random, int64_t now) {
        if (idle != idle_) {
            if (!idle && idle_owned_ && !petting_) {
                base_ = target_ = Clamp(current_);  // freeze glance within command limits
                idle_owned_ = false;
            }
            next_idle_ = now + 4000;
            idle_ = idle;
        }
        if (!idle || !automatic_ || petting_ || gesture_ || dancing_ || motor_busy ||
            now < next_idle_ || !Ready(now))
            return;
        // Same four choices as the factory IdleMotionModifier, within this unit's limits.
        float yaw = float((random >> 8) % 41) - 20.f;
        float pitch = 5.f + float((random >> 16) % 26);
        const unsigned action = random % 100;
        if (action >= 50 && action < 80) {
            yaw = std::clamp(current_.yaw + float((random >> 8) % 21) - 10.f, -25.f, 25.f);
            pitch = std::clamp(current_.pitch + float((random >> 16) % 13) - 6.f, 5.f, 35.f);
        } else if (action >= 90)
            yaw = 0;
        base_ = target_ = {yaw, pitch};
        pet_returning_ = false;
        idle_owned_ = true;
        next_idle_ = now + 4000 + (random % 4001);
    }
    void Touch(bool touched, int64_t now) {
        if (touched == touched_)
            return;
        touched_ = touched;
        if (touched) {
            const Pose restore = dancing_ ? dance_restore_ : Clamp(current_);
            EndDance(now);
            if (!automatic_ || !Ready(now))
                return;
            if (petting_) {
                pet_restore_at_ = 0;
                return;
            }
            pet_restore_ = restore;
            petting_ = true;
            pet_returning_ = false;
            pet_started_ = now;
            pet_restore_at_ = 0;
            idle_owned_ = false;
            gesture_ = auto_gesture_ = false;
            target_ = Clamp({current_.yaw, current_.pitch + 18.f});
            pet_center_ = target_;
        } else if (petting_)
            pet_restore_at_ = now + 3000;
    }
    bool petting() const { return petting_; }
    bool idle_motion() const { return idle_owned_; }
    int speed() const { return petting_ || pet_returning_ ? 350 : (idle_owned_ ? 400 : 650); }
    std::optional<Pose> Step(int64_t now) {
        if (!Ready(now))
            return std::nullopt;
        if (dancing_) {
            const auto age = now - dance_started_;
            if (age >= 15000) {
                EndDance(now);
                return target_;
            }
            const uint32_t phase = static_cast<uint32_t>(age / 450);
            uint32_t value = dance_seed_ + phase * 0x9e3779b9u;
            value ^= value >> 16;
            value *= 0x85ebca6bu;
            value ^= value >> 13;
            const float yaw = 20.f + value % 11;
            const float pitch =
                (phase / 2) % 2 ? 40.f + (value >> 8) % 21 : 5.f + (value >> 8) % 16;
            target_ = {phase % 2 ? -yaw : yaw, pitch};
            return target_;
        }
        if (pet_returning_ && std::abs(current_.yaw - target_.yaw) < kSettleToleranceDegrees &&
            std::abs(current_.pitch - target_.pitch) < kSettleToleranceDegrees)
            pet_returning_ = false;
        if (petting_) {
            if (!touched_ && pet_restore_at_ && now >= pet_restore_at_) {
                petting_ = false;
                pet_returning_ = true;
                base_ = target_ = pet_restore_;
                next_idle_ = now + 4000;
            } else {
                // Give the first lift time to settle, then use complete spring
                // waypoints rather than retargeting from feedback every frame.
                target_ = pet_center_;
                const auto age = now - pet_started_;
                if (age >= 1200) {
                    const float delta = ((age - 1200) / 700) % 2 == 0 ? -3.f : 3.f;
                    target_.pitch = std::clamp(pet_center_.pitch + delta, 5.f, 60.f);
                }
                return target_;
            }
        }
        if (gesture_) {
            auto age = now - gesture_at_;
            // Two explicit cycles, with a softer second cycle; micro-motion stays small.
            const int64_t phase_ms = auto_gesture_ ? 450 : 400;
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
                    constexpr float waypoints[] = {18.f, -18.f, 15.f, -15.f, 0.f};
                    delta = waypoints[phase];
                } else {
                    constexpr float waypoints[] = {12.f, 0.f, 10.f, 0.f};
                    delta = waypoints[phase] * (base_.pitch > 48.f ? -1.f : 1.f);
                }
                if (shake_)
                    target_.yaw = std::clamp(base_.yaw + delta, -30.f, 30.f);
                else
                    target_.pitch = std::clamp(
                        base_.pitch + (emotion_ == 2 && auto_gesture_ ? -delta : delta), 5.f, 60.f);
            }
        }
        if (!gesture_ && !pet_returning_ && speaking_ && emotion_ != 3 && automatic_ &&
            count_ < 2 && now >= next_micro_ && std::abs(current_.yaw - base_.yaw) < 1.5f &&
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
    void Fault(HeadFault reason = HeadFault::UnsafeFeedback) {
        if (!fault_ ||
            (fault_reason_ == HeadFault::Communication && reason != HeadFault::Communication))
            fault_reason_ = reason;
        petting_ = pet_returning_ = idle_owned_ = dancing_ = false;
        fault_ = true;
        armed_ = false;
        gesture_ = auto_gesture_ = false;
        automatic_ = false;
    }
    bool fault() const { return fault_; }
    HeadFault fault_reason() const { return fault_reason_; }
    bool armed() const { return armed_; }
    bool automatic() const { return automatic_; }
    Pose base() const { return base_; }
    Pose target() const { return target_; }
    Pose current() const { return current_; }

private:
    void CancelBackground(int64_t now) {
        EndDance(now);
        petting_ = pet_returning_ = idle_owned_ = false;
        next_idle_ = now + 8000;
    }
    bool Fresh(int64_t now) const {
        return valid_ && now >= feedback_at_ && now - feedback_at_ <= 500;
    }
    bool Ready(int64_t now) {
        if (!Fresh(now)) {
            Fault(HeadFault::Communication);
            return false;
        }
        return armed_ && !fault_;
    }
    Pose current_, base_, target_;
    HeadFault fault_reason_ = HeadFault::None;
    Pose pet_restore_, pet_center_;
    Pose dance_restore_;
    bool dancing_ = false;
    uint32_t dance_seed_ = 0;
    int64_t dance_started_ = 0;
    bool pet_returning_ = false;
    int64_t pet_started_ = 0;
    bool petting_ = false, touched_ = false, idle_ = false, idle_owned_ = false;
    int64_t pet_restore_at_ = 0, next_idle_ = 0;
    bool valid_ = false, armed_ = false, fault_ = false, automatic_ = false, speaking_ = false,
         gesture_ = false, shake_ = false, auto_gesture_ = false;
    int emotion_ = 0;
    unsigned count_ = 0;
    int64_t feedback_at_ = 0, gesture_at_ = 0, next_micro_ = 0;
};
}  // namespace stackchan
