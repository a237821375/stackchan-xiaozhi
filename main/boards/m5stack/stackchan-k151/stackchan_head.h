#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include "motion_policy.h"

class StackchanHead {
public:
    void Start();
    void SetSpeaking(bool value) { speaking_.store(value); }
    void SetEmotion(const std::string& emotion);

private:
    enum class Action { Move, Adjust, Nod, Shake, Stop, Resume, Arm, Approve };
    struct Command {
        Action action;
        stackchan::Pose pose;
    };
    struct Status {
        stackchan::Pose pose, base, target;
        int raw_yaw = -1, raw_pitch = -1;
        bool valid = false, armed = false, fault = false, automatic = false, approved = false,
             moving = false;
    };
    std::mutex mutex_;
    Status status_;
    std::optional<Command> command_;
    std::atomic<bool> speaking_{false};
    std::atomic<int> emotion_{0};
    void Run();
    void Console();
    void RegisterTools();
    std::string StatusJson();
    std::string Submit(Action action, stackchan::Pose pose = {});
};
