#pragma once
#include <driver/i2c_master.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include "motion_policy.h"

class StackchanHead {
public:
    void Start(i2c_master_bus_handle_t bus);
    void SetSpeaking(bool value) { speaking_.store(value); }
    void BeginSpeech() { turn_.fetch_add(1); }
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
    std::atomic<unsigned> turn_{0};
    std::atomic<int> emotion_{0};
    std::atomic<bool> diagnostics_{false};
    void Run();
    void Console();
    void RegisterTools();
    std::string StatusJson();
    std::string Submit(Action action, stackchan::Pose pose = {}, bool local = false);
};
