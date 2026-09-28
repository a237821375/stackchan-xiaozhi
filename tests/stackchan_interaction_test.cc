#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/head_touch.h"
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
using namespace stackchan;
int main() {
    uint8_t regs[32]{};
    assert(ConfigureHeadTouch(
        [&](uint8_t r, uint8_t v) {
            regs[r] = v;
            return true;
        },
        [&](uint8_t r, uint8_t& v) {
            v = regs[r];
            return true;
        }));
    assert(regs[8] == 0x22 && regs[9] == 7 && regs[2] == 0x33 && regs[6] == 0x33);
    assert(!ConfigureHeadTouch([](uint8_t, uint8_t) { return false; },
                               [](uint8_t, uint8_t&) { return true; }));
    TouchFilter filter;
    assert(!filter.Update(1, true, 0));
    assert(filter.Update(1, true, 50));
    assert(filter.Update(0, false, 100));
    assert(!filter.Update(0, false, 400));    // stale data must not hold a pet forever
    assert(!filter.Update(0xc0, true, 500));  // only three low two-bit channels
    Policy p;
    p.Feedback({0, 10}, true, 0);
    assert(p.Arm(0));
    p.Idle(true, false, 123, 0);
    p.Feedback({0, 10}, true, 5000);
    p.Idle(true, false, 123, 5000);
    auto target = p.Step(5000);
    assert(target && (target->yaw != 0 || target->pitch != 10));
    assert(Policy::Safe(*target));
    p.Feedback({5, 15}, true, 5020);
    p.Idle(false, true, 0, 5020);
    target = p.Step(5020);
    assert(target && target->yaw == 5 && target->pitch == 15);
    p.Touch(true, 5020);
    assert(p.petting());
    target = p.Step(5020);
    assert(target->pitch == 33);
    p.Feedback(*target, true, 5100);
    p.Touch(false, 5100);
    p.Feedback({5, 33}, true, 8090);
    p.Step(8090);
    assert(p.petting());
    p.Feedback({5, 33}, true, 8100);
    target = p.Step(8100);
    assert(!p.petting() && target->pitch == 15);
    p.Touch(true, 8100);
    assert(p.petting());
    assert(p.Move({-10, 20}, 8100));
    assert(!p.petting());
    p.Touch(true, 8100);
    assert(!p.petting());  // held touch cannot steal a new command
    p.Touch(false, 8200);
    p.Feedback({-10, 20}, true, 12000);
    target = p.Step(12000);
    assert(target->yaw == -10 && target->pitch == 20);  // no stale pet restore
    p.Stop(12000);
    p.Touch(true, 12000);
    assert(!p.petting());
    p.Feedback({-10, 20}, true, 20000);
    p.Idle(true, false, 456, 20000);
    target = p.Step(20000);
    assert(target->yaw == -10 && target->pitch == 20);
    assert(p.Resume(20000));
    p.Touch(false, 20000);
    p.Touch(true, 20000);
    assert(p.petting());
    p.Feedback({0, 10}, false, 20001);
    assert(!p.petting() && !p.Step(20001));
    Policy strokes;
    strokes.Feedback({0, 10}, true, 0);
    assert(strokes.Arm(0));
    strokes.Touch(true, 0);
    strokes.Feedback({0, 28}, true, 100);
    strokes.Touch(false, 100);
    strokes.Touch(true, 200);
    assert(strokes.Step(200)->pitch == 28);  // repeated strokes must not ratchet upward
    std::cout << "idle and pet interactions: PASS\n";
}
