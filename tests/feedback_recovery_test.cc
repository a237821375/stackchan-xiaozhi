#include "../main/boards/m5stack/stackchan-k151/feedback_recovery.h"
#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
using stackchan::FeedbackRecovery;
int main() {
    FeedbackRecovery r;
    assert(r.Update(true, 0));
    assert(!r.Update(false, 100));
    assert(!r.Update(true, 120));
    assert(!r.Update(true, 140));
    assert(r.PolicyTime(150) == 100);  // all motion timers freeze during the gap
    assert(!r.Update(false, 160));
    assert(!r.Update(true, 180));
    assert(!r.Update(true, 200));
    assert(r.Update(true, 220));
    assert(r.PolicyTime(220) == 100 && !r.timed_out());
    r.Confirm();
    assert(r.PolicyTime(300) == 180);
    r.Fail(300);  // write ACK failure must start another gap
    assert(!r.Update(true, 320));
    assert(!r.Update(true, 340));
    assert(r.Update(true, 360));
    // Failed writes after good reads must not renew the original deadline.
    r.Fail(360);
    assert(!r.Update(true, 380));
    assert(!r.Update(true, 400));
    assert(r.Update(true, 810));
    assert(r.timed_out());
    r.Confirm();

    // Actual policy + recovery gate: a missing frame while petting must not
    // latch a fault or consume the three-second release hold.
    stackchan::Policy p;
    FeedbackRecovery gate;
    auto step = [&](bool valid, int t) {
        if (!gate.Update(valid, t))
            return;
        p.Feedback({0, 10}, true, gate.PolicyTime(t));
        if (gate.timed_out())
            p.Fault();
        gate.Confirm();
    };
    step(true, 0);
    assert(p.Arm(0));
    p.Touch(true, 0);
    p.Touch(false, 100);
    step(false, 200);
    assert(!p.fault() && p.petting());
    step(true, 300);
    step(true, 320);
    step(true, 340);
    assert(!p.fault());
    step(true, 3200);
    p.Step(gate.PolicyTime(3200));
    assert(p.petting());
    step(true, 3240);
    p.Step(gate.PolicyTime(3240));
    assert(!p.petting());
    // A stop during a gap cancels pet/automatic behavior permanently.
    p.Resume(gate.PolicyTime(3240));
    p.Touch(true, gate.PolicyTime(3240));
    step(false, 3300);
    p.Stop(gate.PolicyTime(3300));
    step(true, 3320);
    step(true, 3340);
    step(true, 3360);
    assert(!p.automatic() && !p.petting());
    p.Fault(stackchan::HeadFault::Communication);
    p.Feedback({0, 10}, true, 9999);
    assert(p.fault_reason() == stackchan::HeadFault::Communication && p.fault());
    p.Fault(stackchan::HeadFault::ServoAlarm);
    p.Fault(stackchan::HeadFault::Communication);
    assert(p.fault_reason() == stackchan::HeadFault::ServoAlarm);
    std::cout << "feedback suspension, deadline, recovery and stop: PASS\n";
}
