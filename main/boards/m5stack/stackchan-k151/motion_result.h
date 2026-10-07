#pragma once
#include <cmath>
#include "motion_policy.h"

namespace stackchan {
// One encoder tick, including the factory integer angle-to-raw conversion.
// Diagnostic encoder alignment only: not a mechanical accuracy guarantee,
// action-completion requirement, stall criterion or safety threshold.
inline constexpr float kArrivalToleranceDegrees = 1.f / 3.2f + 0.0001f;
enum class PositionOutcome { Unavailable, Disabled, Moving, Stopped };
inline bool AtTarget(Pose actual, Pose target) {
    return std::isfinite(actual.yaw) && std::isfinite(actual.pitch) && std::isfinite(target.yaw) &&
           std::isfinite(target.pitch) &&
           std::abs(actual.yaw - target.yaw) <= kArrivalToleranceDegrees &&
           std::abs(actual.pitch - target.pitch) <= kArrivalToleranceDegrees;
}
inline PositionOutcome PositionState(bool valid, bool armed, bool animation_active,
                                     bool servo_moving, Pose, Pose) {
    if (!valid)
        return PositionOutcome::Unavailable;
    if (!armed)
        return PositionOutcome::Disabled;
    if (animation_active || servo_moving)
        return PositionOutcome::Moving;
    return PositionOutcome::Stopped;
}
inline const char* PositionName(PositionOutcome state) {
    switch (state) {
        case PositionOutcome::Unavailable:
            return "feedback_unavailable";
        case PositionOutcome::Disabled:
            return "disabled";
        case PositionOutcome::Moving:
            return "moving";
        case PositionOutcome::Stopped:
            return "stopped";
        default:
            return "feedback_unavailable";
    }
}
// Records the latest explicit pose command, not a whole dance or automatic behavior.
class PoseCommandResult {
public:
    void Begin(Pose origin, Pose target) {
        origin_ = origin;
        target_ = target;
        has_command_ = true;
        finished_ = moved_ = reached_ = false;
    }
    void Clear() { has_command_ = finished_ = moved_ = reached_ = false; }
    void Update(Pose actual, bool valid, bool animation_active, bool servo_moving) {
        if (!has_command_ || finished_ || !valid || !std::isfinite(actual.yaw) ||
            !std::isfinite(actual.pitch))
            return;
        moved_ = moved_ || std::abs(actual.yaw - origin_.yaw) >= 1.f / 3.2f ||
                 std::abs(actual.pitch - origin_.pitch) >= 1.f / 3.2f;
        if (!animation_active && !servo_moving) {
            finished_ = true;
            reached_ = AtTarget(actual, target_);
        }
    }
    bool has_command() const { return has_command_; }
    bool finished() const { return finished_; }
    bool motion_observed() const { return moved_; }
    bool target_reached() const { return reached_; }

private:
    Pose origin_, target_;
    bool has_command_ = false, finished_ = false, moved_ = false, reached_ = false;
};
}  // namespace stackchan
