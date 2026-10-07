#include "stackchan_head.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "application.h"
#include "buffered_servo_bus.h"
#include "dance_rgb.h"
#include "factory_axis.h"
#include "factory_upstream/ftservo/SCSCL.h"
#include "factory_upstream/smooth_ui_toolkit/src/core/hal/hal.hpp"
#include "feedback_recovery.h"
#include "head_touch.h"
#include "mcp_server.h"
#include "servo_power.h"
#include "servo_protocol.h"

namespace {
using namespace stackchan;
constexpr auto kUart = UART_NUM_1;
constexpr char kTag[] = "StackchanHead";
HeadCalibration LoadCalibration() {
    HeadCalibration value;
    nvs_handle_t handle = 0;
    if (nvs_open("head_cal", NVS_READONLY, &handle) != ESP_OK)
        return value;
    size_t size = sizeof(value);
    const auto result = nvs_get_blob(handle, "config", &value, &size);
    nvs_close(handle);
    return result == ESP_OK && size == sizeof(value) && value.Valid() ? value : HeadCalibration{};
}
bool SaveCalibration(const HeadCalibration& value) {
    if (!value.Valid())
        return false;
    nvs_handle_t handle = 0;
    if (nvs_open("head_cal", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto result = nvs_set_blob(handle, "config", &value, sizeof(value));
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}
int64_t Now() { return esp_timer_get_time() / 1000; }
int Raw(float angle, int zero) { return zero + static_cast<int>(std::lround(angle * 3.2f)); }
float Degrees(int raw, int zero) { return (raw - zero) / 3.2f; }
BufferedServoBus factory_bus;
bool bus_alarm = false;  // Sole UART worker owns this per-cycle latch.
bool BusOk(int result, int expected, uint8_t id, bool read = false) {
    bus_alarm |= ServoReportedAlarm(factory_bus.getLastError(), factory_bus.getState());
    const bool ok =
        result == expected && factory_bus.getLastError() == 0 && factory_bus.getState() == 0;
    factory_bus.RecordOutcome(id, read, ok);
    static unsigned logged = 0;
    if (!ok && logged++ < 8) {
        ESP_LOGW(kTag, "Factory bus id=%u result=%d expected=%d transport_error=%u servo_status=%u",
                 id, result, expected, factory_bus.getLastError(), factory_bus.getState());
        factory_bus.DumpFailure();
    }
    return ok;
}
bool Read(uint8_t id, uint8_t reg, uint8_t size, Bytes& data) {
    data.resize(size);
    return BusOk(factory_bus.Read(id, reg, data.data(), size), size, id, true);
}
void RecoverBus() {
    // Wait beyond the factory 10 ms receive timeout so a delayed response cannot
    // be mistaken for the next transaction. This worker is the sole bus owner.
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_flush_input(kUart);
}
bool Write(uint8_t id, const Bytes& data) {
    // Runtime torque only. EEPROM/mode/calibration methods are not exposed.
    if (data.size() != 2 || data[0] != 40)
        return false;
    return WithBusRecovery(
        [&] { return BusOk(factory_bus.EnableTorque(id, data[1]), 1, id); },
        [] { return ServoReportedAlarm(factory_bus.getLastError(), factory_bus.getState()); },
        RecoverBus);
}
bool Position(uint8_t id, int raw) {
    if (!EncoderPositionSafe(raw))
        return false;
    return WithBusRecovery(
        [&] { return BusOk(factory_bus.WritePos(id, raw, 20, 0), 1, id); },
        [] { return ServoReportedAlarm(factory_bus.getLastError(), factory_bus.getState()); },
        RecoverBus);
}
using Feedback = ServoFeedback;
bool FeedbackOf(uint8_t id, Feedback& f) {
    Bytes p;
    if (!WithBusRecovery(
            [&] { return Read(id, 56, 15, p); },
            [] { return ServoReportedAlarm(factory_bus.getLastError(), factory_bus.getState()); },
            RecoverBus))
        return false;
    // Valid packets with unsafe positions are safety faults, not dropped frames.
    return DecodeServoFeedback(p, f);
}
}  // namespace

void StackchanHead::Start(i2c_master_bus_handle_t bus, std::function<void(bool)> pet_display) {
    i2c_bus_ = bus;
    pet_display_ = std::move(pet_display);
    calibration_ = LoadCalibration();
    RegisterTools();
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->Console(); }, "head_console",
                    4096, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Local calibration console unavailable; head disabled");
        return;
    }
    if (!calibration_.Valid()) {
        ESP_LOGW(kTag,
                 "No per-device calibration. Head disabled; use local head calibrate and restart");
        return;
    }
    i2c_device_config_t power_cfg{};
    power_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    power_cfg.device_address = 0x6f;
    power_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t power = nullptr;
    if (i2c_master_bus_add_device(bus, &power_cfg, &power) != ESP_OK)
        return;
    auto read_power = [power](uint8_t reg, uint8_t& value) {
        return i2c_master_transmit_receive(power, &reg, 1, &value, 1, 100) == ESP_OK;
    };
    auto write_power = [power](uint8_t reg, uint8_t value) {
        const uint8_t bytes[]{reg, value};
        return i2c_master_transmit(power, bytes, sizeof(bytes), 100) == ESP_OK;
    };
    bool powered = false;
    for (int attempt = 0; attempt < 6 && !powered; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(200));
        powered = EnableServoPower(read_power, write_power);
    }
    i2c_master_bus_rm_device(power);
    if (!powered) {
        ESP_LOGE(kTag, "PY32 VM_EN unavailable; head disabled");
        return;
    }
    ESP_LOGI(kTag, "PY32 VM_EN enabled and read-back verified");
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->RunDanceRgb(); }, "dance_rgb",
                    3072, this, 1, nullptr) != pdPASS)
        ESP_LOGE(kTag, "Dance RGB task unavailable");
    vTaskDelay(pdMS_TO_TICKS(200));
    // Use factory UART parameters and packet driver, with an RX diagnostic event queue.
    if (!factory_bus.begin(kUart, 1000000, 6, 7)) {
        ESP_LOGE(kTag, "Factory UART unavailable; head disabled");
        return;
    }
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->PollHeadTouch(); }, "head_touch",
                    4096, this, 2, nullptr) != pdPASS)
        ESP_LOGE(kTag, "Head touch task unavailable");
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->Run(); }, "head", 6144, this, 3,
                    nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Head task unavailable; motion stays disabled");
        return;
    }
}
void StackchanHead::RunDanceRgb() {
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x6f;
    cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(i2c_bus_, &cfg, &device) != ESP_OK) {
        ESP_LOGE(kTag, "Dance RGB device unavailable");
        vTaskDelete(nullptr);
        return;
    }
    auto read = [device](uint8_t reg, uint8_t* data, size_t size) {
        return i2c_master_transmit_receive(device, &reg, 1, data, size, 20) == ESP_OK;
    };
    auto write = [device](uint8_t reg, const uint8_t* data, size_t size) {
        uint8_t bytes[25];
        if (size > 24)
            return false;
        bytes[0] = reg;
        std::copy_n(data, size, bytes + 1);
        return i2c_master_transmit(device, bytes, size + 1, 20) == ESP_OK;
    };
    DanceRgb lights;
    bool captured = false;
    bool initialized = false;
    uint32_t tick = 0;
    while (true) {
        if (!initialized) {
            if (!captured)
                captured = lights.Begin(read, write);
            if (captured && lights.Off(write)) {
                initialized = true;
                captured = false;
                rgb_ready_.store(true);
                ESP_LOGI(kTag, "RGB initialized: all twelve lights off");
            }
        } else if (dancing_.load() && !rgb_fault_.load()) {
            if (!captured) {
                captured = lights.Begin(read, write);
                tick = 0;
            }
            if (!captured || !lights.Frame(tick++, write)) {
                rgb_ready_.store(false);
                rgb_fault_.store(true);
                ESP_LOGW(kTag, "RGB communication failed; cancelling dance");
            }
        } else if (captured) {
            // Retry blackout on later ticks if the I2C bus is temporarily unavailable.
            if (lights.Off(write)) {
                captured = false;
                ESP_LOGI(kTag, "Dance ended: all twelve RGB lights off");
            }
        } else {
            uint8_t config = 0;
            const bool ready = read(0x24, &config, 1);
            rgb_ready_.store(ready);
            if (ready)
                rgb_fault_.store(false);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
void StackchanHead::PollHeadTouch() {
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x68;
    cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(i2c_bus_, &cfg, &dev) != ESP_OK) {
        vTaskDelete(nullptr);
        return;
    }
    auto read = [dev](uint8_t reg, uint8_t& value) {
        return i2c_master_transmit_receive(dev, &reg, 1, &value, 1, 20) == ESP_OK;
    };
    auto write = [dev](uint8_t reg, uint8_t value) {
        const uint8_t bytes[]{reg, value};
        return i2c_master_transmit(dev, bytes, 2, 20) == ESP_OK;
    };
    bool configured = false;
    for (int attempt = 0; attempt < 3 && !configured; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(200));
        configured = ConfigureHeadTouch(write, read);
    }
    if (!configured) {
        ESP_LOGE(kTag, "Si12T head touch setup failed");
        i2c_master_bus_rm_device(dev);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag, "Si12T head touch ready: 0x68, factory sensitivity 3");
    TouchFilter filter;
    unsigned failures = 0;
    while (true) {
        uint8_t raw = 0;
        bool valid = read(0x10, raw);
        bool contact = filter.Update(raw, valid, Now());
        bool old = head_touched_.exchange(contact);
        if (contact != old)
            ESP_LOGI(kTag, "Head touch %s raw=0x%02x", contact ? "pressed" : "released", raw);
        if (!valid && failures++ < 3)
            ESP_LOGW(kTag, "Head touch read failed");
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void StackchanHead::SetEmotion(const std::string& s) {
    emotion_.store(
        s == "sleepy"
            ? 3
            : (s == "sad" || s == "crying" ? 2 : (s == "thinking" || s == "confused" ? 1 : 0)));
}
std::string StackchanHead::StatusJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    char b[1024];
    const auto position_state =
        PositionState(status_.valid, status_.armed, status_.animation_active, status_.servo_moving,
                      status_.pose, status_.target);
    snprintf(
        b, sizeof(b),
        "{\"calibrated\":%s,\"calibration_restart_required\":%s,\"feedback_valid\":%s,\"armed\":%s,"
        "\"approved\":%s,\"fault\":%s,\"automatic\":%s,"
        "\"state\":\"%s\",\"yaw\":%.2f,\"pitch\":%.2f,\"target_yaw\":%.2f,\"target_pitch\":%."
        "2f,\"raw_yaw\":%d,\"raw_pitch\":%d,\"fault_reason\":\"%s\","
        "\"dancing\":%s,\"rgb_ready\":%s,\"servo_moving\":%s,"
        "\"pose_command_tracking\":%s,\"pose_command_finished\":%s,"
        "\"pose_command_motion_observed\":%s,\"pose_command_target_reached\":%s}",
        calibration_.Valid() ? "true" : "false",
        calibration_restart_required_.load() ? "true" : "false", status_.valid ? "true" : "false",
        status_.armed ? "true" : "false", status_.approved ? "true" : "false",
        status_.fault ? "true" : "false", status_.automatic ? "true" : "false",
        status_.fault
            ? "fault"
            : (status_.recovering ? "recovering_communication" : PositionName(position_state)),
        status_.pose.yaw, status_.pose.pitch, status_.target.yaw, status_.target.pitch,
        status_.raw_yaw, status_.raw_pitch, FaultName(status_.fault_reason),
        dancing_.load() ? "true" : "false", rgb_ready_.load() ? "true" : "false",
        status_.servo_moving ? "true" : "false",
        status_.pose_command.has_command() ? "true" : "false",
        status_.pose_command.finished() ? "true" : "false",
        status_.pose_command.motion_observed() ? "true" : "false",
        status_.pose_command.target_reached() ? "true" : "false");
    return b;
}
std::string StackchanHead::Submit(Action action, Pose pose, bool local) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool stopping = action == Action::Stop || action == Action::EndDance;
    if (!stopping && (!calibration_.Valid() || calibration_restart_required_.load()))
        return "rejected: local per-device calibration and restart required";
    if (action == Action::Approve &&
        (status_.moving || status_.animation_active || status_.servo_moving || !status_.armed))
        return "rejected: finish local probes and wait until head is stationary before approval";
    if (!stopping && status_.recovering) {
        if (status_.fault)
            return std::string("rejected: head motion is locked after a fault; fault_reason=") +
                   FaultName(status_.fault_reason) +
                   ". Waiting alone will not unlock motion. Servo feedback is still unstable; "
                   "check base power and connection. Do not describe this as a mechanical jam, "
                   "promise automatic recovery, or repeatedly call resume. After valid feedback "
                   "and checking for obstruction, use head_action resume once and verify status.";
        return "temporarily unavailable: recovering servo communication (not a latched fault). "
               "Read get_head_position before retrying; do not repeatedly call resume or describe "
               "this as a mechanical jam.";
    }
    if (!stopping && action != Action::Resume && action != Action::Arm && status_.fault)
        return std::string("rejected: head stopped; fault_reason=") +
               FaultName(status_.fault_reason) +
               ". Motion remains locked; waiting alone will not restart it. Communication timeout "
               "is not evidence of a mechanical jam, and overload alone is not proof of a jam. "
               "Check base power, connection and obstruction, then call head_action resume once "
               "when feedback is valid and verify get_head_position.";
    if (!local && !stopping && action != Action::Resume &&
        !RemoteMotionAllowed(status_.approved, status_.armed))
        return !status_.approved ? "rejected: local head movement verification required"
                                 : "rejected: motion stopped after a fault; check head position "
                                   "and obstruction, then use resume";
    if (!stopping && !status_.valid)
        return "rejected: head feedback unavailable or outside safe range";
    if (action == Action::Move && !Policy::Safe(pose))
        return "rejected: yaw must be -30..30, pitch 5..60 degrees";
    if (action == Action::Adjust &&
        !Policy::Safe({status_.base.yaw + pose.yaw, status_.base.pitch + pose.pitch}))
        return "rejected: adjusted target outside safe range";
    if (action == Action::Resume && !status_.approved)
        return "rejected: local motion verification required";
    if (action == Action::Dance && (!rgb_ready_.load() || rgb_fault_.load()))
        return "rejected: RGB communication unavailable; dance not started";
    if (action == Action::Dance && head_touched_.load())
        return "rejected: head is being touched; release it before dancing";
    if (action != Action::Arm && action != Action::Approve && !stopping &&
        action != Action::Resume && !status_.armed)
        return "rejected: local head calibration check has not been armed";
    ESP_LOGI(kTag, "Command source=%s action=%d yaw=%.2f pitch=%.2f", local ? "USB" : "AI",
             int(action), pose.yaw, pose.pitch);
    command_ = Command{action, pose};
    return "accepted: queued; get_head_position reports actual position and completion";
}
void StackchanHead::Run() {
    Policy policy;
    bool approved = calibration_.Approved();
    // Position mode must already be configured. Never write angle-limit EEPROM.
    bool mode_ok = true;
    for (uint8_t id : {1, 2}) {
        Bytes p;
        bool got_range = false;
        for (int attempt = 0; attempt < 6 && !got_range; ++attempt) {
            got_range = Read(id, 9, 4, p);
            if (!got_range)
                vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (!got_range) {
            mode_ok = false;
            continue;
        }
        const int low = Word(p.data()), high = Word(p.data() + 2);
        ESP_LOGI(kTag, "Servo %u stored range: %d..%d", id, low, high);
        const int required_low =
            id == 1 ? Raw(-30, calibration_.yaw_zero) : Raw(5, calibration_.pitch_zero);
        const int required_high =
            id == 1 ? Raw(30, calibration_.yaw_zero) : Raw(60, calibration_.pitch_zero);
        mode_ok = mode_ok && high > low && low <= required_low && high >= required_high;
        if (Read(id, 26, 2, p))
            ESP_LOGI(kTag, "Servo %u deadband CW=%u CCW=%u ticks", id, p[0], p[1]);
    }
    bool first = true, torque = false;
    AxisStall yaw_stall, pitch_stall;
    PoseCommandResult pose_command;
    smooth_ui_toolkit::ui_hal::on_get_tick([] { return static_cast<uint32_t>(Now()); });
    Pose current;
    FactoryAxis yaw_axis(
        calibration_.yaw_zero, -30, 30, [&] { return current.yaw; },
        [](int raw) { return Position(1, raw); });
    FactoryAxis pitch_axis(
        calibration_.pitch_zero, 5, 60, [&] { return current.pitch; },
        [](int raw) { return Position(2, raw); });
    unsigned current_turn = 0;
    TorqueSafety torque_safety;
    int64_t last_log = 0, last_diagnostic_log = 0;
    bool shown_pet = false;
    FeedbackRecovery recovery;
    bool stop_during_gap = false, inhibit_torque = false;
    TickType_t frame_tick = xTaskGetTickCount();
    while (true) {
        bus_alarm = false;
        if (calibration_restart_required_.load()) {
            policy.Fault(HeadFault::UnsafeFeedback);
            inhibit_torque = true;
            approved = false;
        }
        const bool diagnostic_requested = diagnostics_.exchange(false);
        if (diagnostic_requested) {
            for (uint8_t id : {1, 2}) {
                Bytes registers;
                if (Read(id, 40, 8, registers))
                    ESP_LOGI(kTag, "Servo %u torque=%u stored_goal=%d time=%d speed=%d", id,
                             registers[0], Word(registers.data() + 2), Word(registers.data() + 4),
                             Word(registers.data() + 6));
            }
        }
        Feedback yaw, pitch;
        const bool yaw_ok = FeedbackOf(1, yaw);
        const bool pitch_ok = FeedbackOf(2, pitch);
        if (diagnostic_requested) {
            // Use this cycle's existing feedback: no extra UART transactions and
            // no unit assumptions about current/load/voltage registers.
            auto report = [](unsigned id, bool valid, const Feedback& f) {
                if (!valid) {
                    ESP_LOGW(kTag, "Servo %u feedback unavailable", id);
                    return;
                }
                ESP_LOGI(kTag,
                         "Servo %u feedback raw=%d speed_word=%d load_raw=%d current_raw=%d "
                         "voltage_raw=%d temperature_raw=%d moving=%d",
                         id, f.raw, f.speed_word, f.load, f.current, f.voltage_raw,
                         f.temperature_raw, f.moving);
            };
            report(1, yaw_ok, yaw);
            report(2, pitch_ok, pitch);
        }
        const bool read_ok = yaw_ok && pitch_ok;
        const auto wall_now = Now();
        const Pose sample{Degrees(yaw.raw, calibration_.yaw_zero),
                          Degrees(pitch.raw, calibration_.pitch_zero)};
        // Check each valid axis independently, even when its peer has lost communication.
        const bool unsafe =
            !mode_ok ||
            (yaw_ok &&
             (!EncoderPositionSafe(yaw.raw) || !Policy::FeedbackSafe({sample.yaw, 10}))) ||
            (pitch_ok &&
             (!EncoderPositionSafe(pitch.raw) || !Policy::FeedbackSafe({0, sample.pitch})));
        const bool overload = (yaw_ok && (yaw.load >= 650 || yaw.current >= 350)) ||
                              (pitch_ok && (pitch.load >= 650 || pitch.current >= 350));
        if (overload && !policy.fault()) {
            ESP_LOGE(kTag,
                     "Overload sample: yaw load=%d current=%d raw=%d; pitch load=%d current=%d "
                     "raw=%d; dance=%d",
                     yaw.load, yaw.current, yaw.raw, pitch.load, pitch.current, pitch.raw,
                     policy.dancing());
        }
        if (bus_alarm || unsafe || overload) {
            policy.Fault(bus_alarm ? HeadFault::ServoAlarm
                                   : (unsafe ? HeadFault::UnsafeFeedback : HeadFault::Overload));
            first = false;
        }
        const bool was_paused = recovery.paused();
        const bool ready = recovery.Update(read_ok, wall_now);
        if (!was_paused && recovery.paused())
            ESP_LOGW(kTag, "Communication gap: motion suspended; waiting for 3 valid frames");
        if (recovery.timed_out()) {
            if (!policy.fault())
                ESP_LOGE(kTag, "Communication unavailable for 500 ms; motion disabled");
            policy.Fault(HeadFault::Communication);
            first = false;
        }
        // Never convert missing feedback (-1) into a physical angle or spring origin.
        const bool valid = read_ok && !unsafe && !bus_alarm && !overload;
        if (valid)
            current = sample;
        const auto now = recovery.PolicyTime(wall_now);
        if (recovery.paused() || rgb_fault_.load())
            policy.EndDance(now);
        bool moving = false;
        bool animation_active = false;
        std::optional<Command> cmd;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cmd = command_;
            command_.reset();
        }
        if ((recovery.timed_out() || calibration_restart_required_.load()) && cmd &&
            cmd->action != Action::Stop && cmd->action != Action::EndDance) {
            ESP_LOGW(kTag,
                     "Queued command cancelled: communication timeout or calibration restart");
            cmd.reset();
        }
        if (!ready || !valid) {
            if (cmd && (cmd->action == Action::Stop || cmd->action == Action::EndDance)) {
                policy.Stop(now);
                stop_during_gap = inhibit_torque = true;
            } else if (cmd) {
                ESP_LOGW(kTag,
                         "Queued command cancelled: communication recovery or unsafe feedback");
            }
        } else {
            bool writes_ok = true;
            policy.Feedback(current, true, now);
            if (was_paused) {
                yaw_axis.Reset(current.yaw);
                pitch_axis.Reset(current.pitch);
                yaw_stall.Reset();
                pitch_stall.Reset();
                ESP_LOGI(kTag, "Communication stable: 3 valid frames; springs adopted actual pose");
            }
            if (stop_during_gap) {
                policy.Stop(now);  // Hold the newly measured pose, never the old interrupted goal.
                stop_during_gap = false;
            }
            if (first && approved && !policy.fault())
                policy.Arm(now);
            first = false;
            policy.Idle(Application::GetInstance().GetDeviceState() == kDeviceStateIdle,
                        yaw_axis.isMoving() || pitch_axis.isMoving(), esp_random(), now);
            policy.Touch(head_touched_.load(), now);
            const unsigned next_turn = turn_.load();
            if (next_turn != current_turn) {
                policy.BeginTurn(now);
                current_turn = next_turn;
            }
            if (cmd) {
                pose_command.Clear();
                yaw_axis.Reset(current.yaw);
                pitch_axis.Reset(current.pitch);
                if (cmd->action != Action::Stop && cmd->action != Action::EndDance)
                    inhibit_torque = false;
                switch (cmd->action) {
                    case Action::Arm:
                        policy.Arm(now, false);
                        break;
                    case Action::Approve: {
                        // Serialize the persistent approval with local config/revoke writes.
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (policy.armed() && !calibration_restart_required_.load()) {
                            auto saved = calibration_;
                            saved.verified = 1;
                            if (SaveCalibration(saved)) {
                                approved = true;
                                policy.Resume(now);
                            } else {
                                policy.Fault(HeadFault::UnsafeFeedback);
                                ESP_LOGE(kTag, "Calibration approval not saved; motion disabled");
                            }
                        }
                        break;
                    }
                    case Action::Move:
                        if (policy.Move(cmd->pose, now))
                            pose_command.Begin(current, cmd->pose);
                        break;
                    case Action::Adjust: {
                        auto base = policy.base();
                        const Pose goal{base.yaw + cmd->pose.yaw, base.pitch + cmd->pose.pitch};
                        if (policy.Move(goal, now))
                            pose_command.Begin(current, goal);
                        break;
                    }
                    case Action::Nod:
                        policy.Gesture(false, now);
                        break;
                    case Action::Shake:
                        policy.Gesture(true, now);
                        break;
                    case Action::Dance:
                        if (rgb_ready_.load() && !rgb_fault_.load())
                            policy.Dance(now, esp_random());
                        break;
                    case Action::EndDance:
                        policy.EndDance(now);
                        break;
                    case Action::Stop:
                        policy.Stop(now);
                        if (was_paused) {
                            stop_during_gap = inhibit_torque = true;
                        } else if (torque) {
                            torque_safety.BeginWrite();
                            writes_ok = Position(1, yaw.raw) && Position(2, pitch.raw);
                        }
                        break;
                    case Action::Resume:
                        if (approved)
                            policy.Resume(now);
                        break;
                }
            }
            policy.Emotion(emotion_.load());
            policy.Speaking(speaking_.load(), now);
            auto target = policy.Step(now);
            if (inhibit_torque || !writes_ok)
                target.reset();
            moving =
                target && (std::abs(target->yaw - current.yaw) >= kSettleToleranceDegrees ||
                           std::abs(target->pitch - current.pitch) >= kSettleToleranceDegrees ||
                           yaw_axis.isMoving() || pitch_axis.isMoving());
            if (moving) {
                const bool yaw_stuck = yaw_stall.Update(current.yaw, target->yaw, now);
                const bool pitch_stuck = pitch_stall.Update(current.pitch, target->pitch, now);
                if (yaw_stuck || pitch_stuck) {
                    policy.Fault(HeadFault::Stall);
                    target.reset();
                    ESP_LOGE(kTag, "Head stopped: mechanical stall=%d/%d", yaw_stuck, pitch_stuck);
                }
            } else {
                yaw_stall.Reset();
                pitch_stall.Reset();
            }
            if (target) {
                torque_safety.BeginWrite();
                bool ok = true;
                if (!torque) {
                    // Preload actual position before (possibly partially) enabling torque.
                    ok = Position(1, yaw.raw) && Position(2, pitch.raw) && Write(1, {40, 1}) &&
                         Write(2, {40, 1});
                    torque = ok;
                    yaw_axis.Reset(current.yaw);
                    pitch_axis.Reset(current.pitch);
                }
                if (ok) {
                    yaw_axis.Target(target->yaw, policy.speed());
                    pitch_axis.Target(target->pitch, policy.speed());
                    // Feedback was sampled before these writes. Preserve pending state so
                    // completion waits for a fresh sample after the final factory snap.
                    animation_active = yaw_axis.animationPending() || pitch_axis.animationPending();
                    yaw_axis.update();
                    if (!yaw_axis.failed())
                        pitch_axis.update();
                    ok = !yaw_axis.failed() && !pitch_axis.failed();
                }
                writes_ok = ok;
            }
            if (!writes_ok) {
                if (cmd && cmd->action == Action::Stop)
                    stop_during_gap = inhibit_torque = true;
                if (bus_alarm)
                    policy.Fault(HeadFault::ServoAlarm);
                else {
                    recovery.Fail(Now());
                    if (recovery.timed_out())
                        policy.Fault(HeadFault::Communication);
                    ESP_LOGW(kTag, "Command acknowledgement lost; motion suspended");
                }
            }
            if (!recovery.paused())
                recovery.Confirm();
        }
        if (torque_safety.ReleaseOnFault(policy.fault() || stop_during_gap)) {
            // Release both independently, including a partially successful enable.
            Write(1, {40, 0});
            Write(2, {40, 0});
            torque = false;
            if (bus_alarm)
                policy.Fault(HeadFault::ServoAlarm);
        }
        if (policy.fault() || recovery.paused() || inhibit_torque)
            moving = false;
        animation_active =
            animation_active || yaw_axis.animationPending() || pitch_axis.animationPending();
        const bool servo_moving = (yaw_ok && yaw.moving) || (pitch_ok && pitch.moving);
        pose_command.Update(current, valid && ready && !policy.fault() && !inhibit_torque,
                            animation_active, servo_moving);
        dancing_.store(policy.dancing() && !policy.fault() && !recovery.paused());
        if (policy.petting() != shown_pet) {
            shown_pet = policy.petting();
            ESP_LOGI(kTag, "Pet interaction %s", shown_pet ? "active" : "ended");
            Application::GetInstance().Schedule([this, active = shown_pet] {
                if (pet_display_)
                    pet_display_(active);
            });
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = {current,
                       policy.base(),
                       policy.target(),
                       yaw.raw,
                       pitch.raw,
                       valid && ready && !recovery.paused(),
                       policy.armed(),
                       policy.fault(),
                       policy.automatic(),
                       approved,
                       moving,
                       recovery.paused(),
                       policy.fault_reason(),
                       animation_active,
                       servo_moving,
                       pose_command};
        }
        if (wall_now - last_log > (moving ? 200 : 3000)) {
            ESP_LOGI(kTag, "%s", StatusJson().c_str());
            last_log = wall_now;
        }
        if (wall_now - last_diagnostic_log >= 5000) {
            factory_bus.PrintDiagnostics(wall_now);
            last_diagnostic_log = wall_now;
        }
        // Include UART work in the frame, without catch-up bursts after timeouts.
        if (xTaskGetTickCount() - frame_tick >= pdMS_TO_TICKS(40))
            frame_tick = xTaskGetTickCount();
        vTaskDelayUntil(&frame_tick, pdMS_TO_TICKS(20));
    }
}
std::string StackchanHead::ConfigureCalibration(const char* line) {
    const auto value = ParseCalibrationCommand(line);
    if (!value)
        return "rejected: head calibrate <yaw_zero> <pitch_zero>; use this unit's measured factory "
               "zeros";
    std::lock_guard<std::mutex> lock(mutex_);
    if (calibration_restart_required_.load())
        return "rejected: restart to load the saved calibration before another configuration "
               "change";
    if (status_.armed || status_.approved || calibration_.Approved())
        return "rejected: use head revoke and restart before changing an enabled calibration";
    if (!SaveCalibration(*value))
        return "rejected: calibration could not be saved";
    calibration_restart_required_.store(true);
    return "saved: approval cleared; restart, inspect read-only feedback, then locally probe both "
           "axes";
}
std::string StackchanHead::RevokeCalibration() {
    if (!calibration_.Valid())
        return "rejected: no valid calibration to revoke";
    std::lock_guard<std::mutex> lock(mutex_);
    if (calibration_restart_required_.load())
        return "rejected: restart to load the saved calibration before revoking approval";
    auto value = calibration_;
    value.verified = 0;
    if (!SaveCalibration(value))
        return "rejected: calibration approval could not be revoked";
    calibration_restart_required_.store(true);
    return "saved: approval revoked; motion will stop; restart before calibration changes";
}
void StackchanHead::Console() {
    // Local USB console only. No calibration/arming tool is exposed to the AI.
    char line[64];
    size_t used = 0;
    while (true) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == '\n' || c == '\r') {
            line[used] = 0;
            used = 0;
            std::string result;
            if (std::strncmp(line, "head calibrate", 14) == 0)
                result = ConfigureCalibration(line);
            else if (strcmp(line, "head revoke") == 0)
                result = RevokeCalibration();
            else if (strcmp(line, "head calibration") == 0)
                result = "calibration yaw_zero=" + std::to_string(calibration_.yaw_zero) +
                         " pitch_zero=" + std::to_string(calibration_.pitch_zero) + " " +
                         StatusJson();
            else if (strcmp(line, "head status") == 0)
                result = StatusJson();
            else if (strcmp(line, "head diag") == 0) {
                diagnostics_.store(true);
                result = "read-only diagnostics queued";
            } else if (strcmp(line, "head arm") == 0)
                result = Submit(Action::Arm, {}, true);
            else if (strcmp(line, "head approve") == 0)
                result = Submit(Action::Approve, {}, true);
            else if (strcmp(line, "head stop") == 0)
                result = Submit(Action::Stop, {}, true);
            else if (strcmp(line, "head probe yaw") == 0)
                result = Submit(Action::Adjust, {2, 0}, true);
            else if (strcmp(line, "head probe pitch") == 0)
                result = Submit(Action::Adjust, {0, 2}, true);
            else if (strcmp(line, "head center") == 0)
                result = Submit(Action::Move, {0, 10}, true);
            else if (strcmp(line, "head nod") == 0)
                result = Submit(Action::Nod, {}, true);
            else if (strcmp(line, "head shake") == 0)
                result = Submit(Action::Shake, {}, true);
            else if (strcmp(line, "head dance") == 0)
                result = Submit(Action::Dance, {}, true);
            else if (strcmp(line, "head dance stop") == 0)
                result = Submit(Action::EndDance, {}, true);
            else if (strcmp(line, "head left") == 0)
                result = Submit(Action::Move, {-10, 10}, true);
            else if (strcmp(line, "head right") == 0)
                result = Submit(Action::Move, {10, 10}, true);
            if (!result.empty())
                ESP_LOGI(kTag, "console: %s", result.c_str());
        } else if (used + 1 < sizeof(line))
            line[used++] = static_cast<char>(c);
        else
            used = 0;
    }
}
void StackchanHead::RegisterTools() {
    auto& m = McpServer::GetInstance();
    m.AddTool("self.robot.dance",
              "跳舞/扭一扭: action=start starts a 15-second random head dance with flashing RGB "
              "lights. action=stop stops dancing, restores the pre-dance pose and turns off all "
              "RGB lights. "
              "Touching the head, a head command, or a fault cancels dancing. Call this tool "
              "when the user asks to dance; do not substitute a single nod or shake. Accepted "
              "means queued; check get_head_position.dancing and fault_reason for actual status.",
              PropertyList({Property("action", kPropertyTypeString)}),
              [this](const PropertyList& p) -> ReturnValue {
                  const auto s = p["action"].value<std::string>();
                  if (s == "start")
                      return Submit(Action::Dance);
                  if (s == "stop")
                      return Submit(Action::EndDance);
                  return std::string("rejected: action must be start or stop");
              });
    m.AddTool(
        "self.robot.get_head_position",
        "Read actual head feedback, target and completion. Robot's own left is negative yaw. "
        "Pitch is factory calibrated; safe forward preset is yaw=0,pitch=10. Never claim "
        "motion succeeded if feedback is invalid. state=target_not_reached means animation and "
        "servo stopped without measured arrival; this alone is not a jam and must not trigger "
        "repeated retries. For the latest explicit pose command, pose_command_finished only "
        "means it stopped; check pose_command_motion_observed and pose_command_target_reached "
        "separately. These fields do not certify an entire dance or gesture. "
        "Read fault_reason: communication_timeout or "
        "recovering_communication is a bus issue, not proof of a jam. Only mechanical_stall "
        "means measured failure to move. overload means the load/current threshold was exceeded; "
        "it is not proof of a mechanical jam. If fault=true, motion is locked: waiting will not "
        "restart it. Once power/connection and obstruction are checked and feedback_valid=true, "
        "use head_action resume once and verify status. Never repeatedly resume with invalid "
        "feedback or promise it will recover merely by waiting.",
        {}, [this](const PropertyList&) -> ReturnValue { return StatusJson(); });
    m.AddTool("self.robot.set_head_pose",
              "Control head when user asks 回正/抬头/低头/左看/右看. pose: center, up, down, left, "
              "right. Hold position afterwards. Left/right are robot's own view. Motion is small "
              "and slow.",
              PropertyList({Property("pose", kPropertyTypeString)}),
              [this](const PropertyList& p) -> ReturnValue {
                  auto s = p["pose"].value<std::string>();
                  if (s == "center")
                      return Submit(Action::Move, {0, 10});
                  if (s == "up")
                      return Submit(Action::Move, {0, 20});
                  if (s == "down")
                      return Submit(Action::Move, {0, 5});
                  if (s == "left")
                      return Submit(Action::Move, {-10, 10});
                  if (s == "right")
                      return Submit(Action::Move, {10, 10});
                  return std::string("rejected: unknown pose");
              });
    m.AddTool(
        "self.robot.adjust_head",
        "Small relative adjustment from the user-selected pose. For 再抬一点 use pitch_delta=2. "
        "Positive pitch looks up. Robot's own left is negative yaw. Limits enforced by device.",
        PropertyList({Property("yaw_delta", kPropertyTypeInteger, 0, -10, 10),
                      Property("pitch_delta", kPropertyTypeInteger, 0, -10, 10)}),
        [this](const PropertyList& p) -> ReturnValue {
            return Submit(Action::Adjust, {float(p["yaw_delta"].value<int>()),
                                           float(p["pitch_delta"].value<int>())});
        });
    m.AddTool("self.robot.head_action",
              "action: nod=点头, shake=摇头, stop=别动/停止 (hold and pause automatic gestures), "
              "resume=恢复动作. nod/shake return to the user-selected pose.",
              PropertyList({Property("action", kPropertyTypeString)}),
              [this](const PropertyList& p) -> ReturnValue {
                  auto s = p["action"].value<std::string>();
                  if (s == "nod")
                      return Submit(Action::Nod);
                  if (s == "shake")
                      return Submit(Action::Shake);
                  if (s == "stop")
                      return Submit(Action::Stop);
                  if (s == "resume")
                      return Submit(Action::Resume);
                  return std::string("rejected: unknown action");
              });
}
