#include "stackchan_head.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "mcp_server.h"
#include "servo_protocol.h"
#include "settings.h"

namespace {
using namespace stackchan;
constexpr auto kUart = UART_NUM_1;
constexpr char kTag[] = "StackchanHead";
// This unit's original factory NVS: servo/zero_pos_1=461, zero_pos_2=610.
constexpr int kYawZero = 461, kPitchZero = 610;
int64_t Now() { return esp_timer_get_time() / 1000; }
int Raw(float angle, int zero) { return zero + static_cast<int>(std::lround(angle * 3.2f)); }
float Degrees(int raw, int zero) { return (raw - zero) / 3.2f; }
bool Exchange(uint8_t id, uint8_t instruction, const Bytes& parameters, size_t size, Bytes& data) {
    auto request = Packet(id, instruction, parameters);
    uart_flush_input(kUart);
    if (uart_write_bytes(kUart, request.data(), request.size()) != static_cast<int>(request.size()))
        return false;
    Bytes reply(size + 6);
    size_t received = 0;
    auto deadline = Now() + 25;
    while (received < reply.size() && Now() < deadline) {
        int n = uart_read_bytes(kUart, reply.data() + received, reply.size() - received,
                                pdMS_TO_TICKS(10));
        if (n > 0)
            received += n;
    }
    reply.resize(received);
    return Reply(reply, id, size, data);
}
bool Read(uint8_t id, uint8_t reg, uint8_t size, Bytes& data) {
    return Exchange(id, 2, {reg, size}, size, data);
}
bool Write(uint8_t id, const Bytes& data) {
    Bytes ack;
    return Exchange(id, 3, data, 0, ack);
}
bool Position(uint8_t id, int raw) {
    Bytes p{42};
    AddWord(p, raw);
    AddWord(p, 50);
    AddWord(p, 0);
    return Write(id, p);
}
struct Feedback {
    int raw = -1, load = 0, current = 0;
};
bool FeedbackOf(uint8_t id, Feedback& f) {
    Bytes p;
    if (!Read(id, 56, 15, p))
        return false;
    f.raw = Word(p.data());
    f.load = Word(p.data() + 4) & 0x3ff;
    f.current = Word(p.data() + 13) & 0x7fff;
    return f.raw >= 0 && f.raw <= 1000;
}
float Approach(float from, float to) { return from + std::clamp(to - from, -.4f, .4f); }
}  // namespace

void StackchanHead::Start() {
    uint8_t mac[6]{};
    constexpr uint8_t calibrated_unit[6] = {0x80, 0x45, 0x6b, 0x4d, 0x3a, 0x94};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK || memcmp(mac, calibrated_unit, 6) != 0) {
        ESP_LOGE(kTag, "Calibration belongs to another unit; head disabled");
        return;
    }
    uart_config_t cfg{};
    cfg.baud_rate = 1000000;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    // Official K151 SCSerial::begin(UART1, 1Mbps, TX=6, RX=7).
    if (uart_driver_install(kUart, 512, 512, 0, nullptr, 0) != ESP_OK ||
        uart_param_config(kUart, &cfg) != ESP_OK ||
        uart_set_pin(kUart, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(kTag, "UART unavailable; head motion disabled");
        return;
    }
    RegisterTools();
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->Run(); }, "head", 6144, this, 3,
                    nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Head task unavailable; motion stays disabled");
        return;
    }
    if (xTaskCreate([](void* p) { static_cast<StackchanHead*>(p)->Console(); }, "head_console",
                    4096, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Local head console unavailable");
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
    char b[512];
    snprintf(b, sizeof(b),
             "{\"feedback_valid\":%s,\"armed\":%s,\"approved\":%s,\"fault\":%s,\"automatic\":%s,"
             "\"state\":\"%s\",\"yaw\":%.2f,\"pitch\":%.2f,\"target_yaw\":%.2f,\"target_pitch\":%."
             "2f,\"raw_yaw\":%d,\"raw_pitch\":%d}",
             status_.valid ? "true" : "false", status_.armed ? "true" : "false",
             status_.approved ? "true" : "false", status_.fault ? "true" : "false",
             status_.automatic ? "true" : "false",
             status_.fault
                 ? "fault"
                 : (!status_.armed ? "disabled" : (status_.moving ? "moving" : "completed")),
             status_.pose.yaw, status_.pose.pitch, status_.target.yaw, status_.target.pitch,
             status_.raw_yaw, status_.raw_pitch);
    return b;
}
std::string StackchanHead::Submit(Action action, Pose pose, bool local) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!local && action != Action::Stop && action != Action::Resume &&
        !RemoteMotionAllowed(status_.approved, status_.armed))
        return "rejected: local head movement verification required";
    if (action != Action::Stop && !status_.valid)
        return "rejected: head feedback unavailable or outside safe range";
    if (action == Action::Move && !Policy::Safe(pose))
        return "rejected: yaw must be -30..30, pitch 5..60 degrees";
    if (action == Action::Adjust &&
        !Policy::Safe({status_.base.yaw + pose.yaw, status_.base.pitch + pose.pitch}))
        return "rejected: adjusted target outside safe range";
    if (action == Action::Resume && !status_.approved)
        return "rejected: local motion verification required";
    if (action != Action::Arm && action != Action::Approve && action != Action::Stop &&
        action != Action::Resume && !status_.armed)
        return "rejected: local head calibration check has not been armed";
    command_ = Command{action, pose};
    return "accepted: queued; get_head_position reports actual position and completion";
}
void StackchanHead::Run() {
    Policy policy;
    bool approved = false;
    {
        Settings settings("head_ctl", false);
        approved = settings.GetInt("verified", 0) == 1;
    }
    // Position mode must already be configured. Never write angle-limit EEPROM.
    bool mode_ok = true;
    for (uint8_t id : {1, 2}) {
        Bytes p;
        if (!Read(id, 9, 4, p)) {
            mode_ok = false;
            continue;
        }
        const int low = Word(p.data()), high = Word(p.data() + 2);
        ESP_LOGI(kTag, "Servo %u stored range: %d..%d", id, low, high);
        const int required_low = id == 1 ? Raw(-30, kYawZero) : Raw(5, kPitchZero);
        const int required_high = id == 1 ? Raw(30, kYawZero) : Raw(60, kPitchZero);
        mode_ok = mode_ok && high > low && low <= required_low && high >= required_high;
    }
    bool first = true, torque = false;
    AxisStall yaw_stall, pitch_stall;
    unsigned current_turn = 0;
    TorqueSafety torque_safety;
    int64_t last_log = 0;
    while (true) {
        Feedback yaw, pitch;
        const bool yaw_ok = FeedbackOf(1, yaw);
        const bool pitch_ok = FeedbackOf(2, pitch);
        bool read_ok = yaw_ok && pitch_ok;
        const auto now = Now();
        Pose current{Degrees(yaw.raw, kYawZero), Degrees(pitch.raw, kPitchZero)};
        bool valid = read_ok && mode_ok && Policy::Safe(current);
        policy.Feedback(current, valid, now);
        if (first && valid && approved)
            policy.Arm(now);
        first = false;
        const unsigned next_turn = turn_.load();
        if (next_turn != current_turn) {
            policy.BeginTurn(now);
            current_turn = next_turn;
        }
        std::optional<Command> cmd;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cmd = command_;
            command_.reset();
        }
        if (cmd) {
            switch (cmd->action) {
                case Action::Arm:
                    policy.Arm(now, false);
                    break;
                case Action::Approve:
                    if (valid && policy.armed()) {
                        Settings settings("head_ctl", true);
                        settings.SetInt("verified", 1);
                        approved = true;
                        policy.Resume(now);
                    }
                    break;
                case Action::Move:
                    policy.Move(cmd->pose, now);
                    break;
                case Action::Adjust: {
                    auto b = policy.base();
                    policy.Move({b.yaw + cmd->pose.yaw, b.pitch + cmd->pose.pitch}, now);
                    break;
                }
                case Action::Nod:
                    policy.Gesture(false, now);
                    break;
                case Action::Shake:
                    policy.Gesture(true, now);
                    break;
                case Action::Stop:
                    policy.Stop(now);
                    if (valid && torque) {
                        torque_safety.BeginWrite();
                        if (!(Position(1, yaw.raw) && Position(2, pitch.raw)))
                            policy.Fault();
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
        bool moving = target && (std::abs(target->yaw - current.yaw) > 1 ||
                                 std::abs(target->pitch - current.pitch) > 1);
        if (moving && valid) {
            const bool yaw_stuck = yaw_stall.Update(current.yaw, target->yaw, now);
            const bool pitch_stuck = pitch_stall.Update(current.pitch, target->pitch, now);
            if (yaw_stuck || pitch_stuck || yaw.load >= 650 || pitch.load >= 650 ||
                yaw.current >= 350 || pitch.current >= 350) {
                policy.Fault();
                target.reset();
                ESP_LOGE(kTag, "Head stalled/overloaded; targets stopped");
            }
        } else {
            yaw_stall.Reset();
            pitch_stall.Reset();
        }
        if (target && valid) {
            torque_safety.BeginWrite();
            bool ok = true;
            if (!torque) {
                // Set the present position BEFORE enabling torque (avoid an old goal).
                ok = Position(1, yaw.raw) && Position(2, pitch.raw) && Write(1, {40, 1}) &&
                     Write(2, {40, 1});
                torque = ok;
            }
            if (ok && moving) {
                Pose next{Approach(current.yaw, target->yaw),
                          Approach(current.pitch, target->pitch)};
                ok = Position(1, Raw(next.yaw, kYawZero)) &&
                     Position(2, Raw(next.pitch, kPitchZero));
            }
            if (!ok) {
                policy.Fault();
                ESP_LOGE(kTag, "Servo command failed; motion disabled");
            }
        }
        if (torque_safety.ReleaseOnFault(policy.fault())) {
            // Release both independently, including a partially successful enable.
            Write(1, {40, 0});
            Write(2, {40, 0});
            torque = false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = {
                current,        policy.base(),  policy.target(),    yaw.raw,  pitch.raw, valid,
                policy.armed(), policy.fault(), policy.automatic(), approved, moving};
        }
        if (now - last_log > 3000) {
            ESP_LOGI(kTag, "%s", StatusJson().c_str());
            last_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
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
            if (strcmp(line, "head status") == 0)
                result = StatusJson();
            else if (strcmp(line, "head arm") == 0)
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
    m.AddTool("self.robot.get_head_position",
              "Read actual head feedback, target and completion. Robot's own left is negative yaw. "
              "Pitch is factory calibrated; safe forward preset is yaw=0,pitch=10. Never claim "
              "motion succeeded if feedback is invalid.",
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
