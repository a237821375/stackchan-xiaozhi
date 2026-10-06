#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
using namespace stackchan;
int main() {
    Policy fast;
    fast.Feedback({0, 10}, true, 0);
    assert(fast.Arm(0) && fast.Dance(0, 17));
    assert(fast.speed() == 650);
    assert(fast.Step(0)->yaw >= 20);
    fast.Feedback(fast.target(), true, 450);
    assert(fast.Step(450)->yaw <= -20);
    Policy p;
    p.Feedback({8, 20}, true, 0);
    assert(!p.Dance(0, 17));
    assert(p.Arm(0));
    assert(p.Dance(0, 17));
    float low_yaw = 30, high_yaw = -30, low_pitch = 60, high_pitch = 5;
    for (int t = 0; t < 15000; t += 20) {
        p.Feedback(p.target(), true, t);
        p.Idle(true, false, 123, t);
        auto target = p.Step(t);
        assert(target && Policy::Safe(*target) && p.dancing());
        low_yaw = std::min(low_yaw, target->yaw);
        high_yaw = std::max(high_yaw, target->yaw);
        low_pitch = std::min(low_pitch, target->pitch);
        high_pitch = std::max(high_pitch, target->pitch);
    }
    assert(low_yaw <= -20 && high_yaw >= 20 && high_pitch - low_pitch >= 30);
    p.Feedback(p.target(), true, 15000);
    auto target = p.Step(15000);
    assert(!p.dancing() && target->yaw == 8 && target->pitch == 20);
    assert(p.Dance(15000, 4));
    assert(p.Move({0, 10}, 15000));
    assert(!p.dancing() && p.Step(15000)->pitch == 10);
    assert(p.Dance(15000, 5));
    p.Touch(true, 15000);
    assert(!p.dancing() && p.petting());
    p.Touch(false, 15000);
    p.Feedback({0, 10}, true, 18000);
    p.Step(18000);
    assert(p.Dance(18000, 6));
    p.EndDance(18000);
    assert(!p.dancing());
    assert(p.Dance(18000, 7));
    p.Fault(HeadFault::Communication);
    assert(!p.dancing() && !p.Step(18000));
    std::cout << "dance range, timeout, command/touch/fault cancellation: PASS\n";
}
