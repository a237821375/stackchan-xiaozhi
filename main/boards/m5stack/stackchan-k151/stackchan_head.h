#pragma once
#include <driver/i2c_master.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include "head_calibration.h"
#include "motion_policy.h"
#include "motion_result.h"

class StackchanHead {
public:
    void Start(i2c_master_bus_handle_t bus, std::function<void(bool)> pet_display);
    void SetSpeaking(bool value) { speaking_.store(value); }
    void BeginSpeech() { turn_.fetch_add(1); }
    void SetEmotion(const std::string& emotion);

private:
    enum class Action { Move, Adjust, Nod, Shake, Stop, Resume, Arm, Approve, Dance, EndDance };
    struct Command {
        Action action;
        stackchan::Pose pose;
    };
    struct Status {
        stackchan::Pose pose, base, target;
        int raw_yaw = -1, raw_pitch = -1;
        bool valid = false, armed = false, fault = false, automatic = false, approved = false,
             moving = false;
        bool recovering = false;
        stackchan::HeadFault fault_reason = stackchan::HeadFault::None;
        bool animation_active = false, servo_moving = false;
        stackchan::PoseCommandResult pose_command;
    };
    std::mutex mutex_;
    Status status_;
    stackchan::HeadCalibration calibration_;
    std::atomic<bool> calibration_restart_required_{false};
    std::optional<Command> command_;
    std::atomic<bool> speaking_{false};
    std::atomic<unsigned> turn_{0};
    std::atomic<int> emotion_{0};
    std::atomic<bool> diagnostics_{false};
    std::atomic<bool> head_touched_{false};
    std::atomic<bool> dancing_{false}, rgb_ready_{false}, rgb_fault_{false};
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    std::function<void(bool)> pet_display_;
    void PollHeadTouch();
    void RunDanceRgb();
    void Run();
    void Console();
    void RegisterTools();
    std::string ConfigureCalibration(const char* line);
    std::string RevokeCalibration();
    std::string StatusJson();
    std::string Submit(Action action, stackchan::Pose pose = {}, bool local = false);
};
