#include <cassert>
#include <cmath>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
using namespace stackchan;
bool near(float a, float b) { return std::abs(a - b) < .01f; }
int main() {
    TorqueSafety safety;
    assert(!safety.ReleaseOnFault(true));  // read-only startup
    safety.BeginWrite();
    assert(safety.ReleaseOnFault(true));   // first partial enable fails
    assert(!safety.ReleaseOnFault(true));  // do not repeatedly write while faulted
    safety.BeginWrite();                   // resume, yaw enables, pitch fails in the same iteration
    assert(safety.ReleaseOnFault(true));   // must release both again
    Policy probe;
    probe.Feedback({0, 10}, true, 0);
    assert(probe.Arm(0, false));
    probe.Speaking(true, 0);
    probe.Feedback({0, 10}, true, 2000);
    assert(near(probe.Step(2000)->pitch, 10));
    assert(!probe.automatic());
    assert(!RemoteMotionAllowed(false, true));
    assert(!RemoteMotionAllowed(true, false));
    assert(RemoteMotionAllowed(true, true));
    // Real hardware settles four encoder ticks (1.25 degrees) from center.
    // The factory driver ignores stall detection inside eight encoder ticks.
    AxisStall settled;
    for (int t = 0; t <= 3000; t += 100)
        assert(!settled.Update(1.25f, 0, t));
    AxisStall yaw_stall, pitch_stall;
    for (int t = 0; t <= 1300; t += 50) {
        bool stopped = yaw_stall.Update(0, 10, t);
        bool moving_axis_stopped = pitch_stall.Update(10 + t / 200.f, 30, t);
        assert(!moving_axis_stopped);
        if (t == 1300)
            assert(stopped);
    }
    assert(!yaw_stall.Update(10, 10, 1400));
    Policy p;
    assert(!p.Move({0, 20}, 0));
    p.Feedback({0, 10}, true, 0);
    assert(!p.Move({0, 20}, 0));
    assert(p.Arm(0));
    assert(!p.Move({31, 20}, 0));
    assert(!p.Move({0, 4}, 0));
    assert(!p.Move({0, NAN}, 0));
    assert(p.Move({10, 20}, 0));
    auto goal = p.Step(100);
    assert(goal && near(goal->yaw, 10) && near(goal->pitch, 20));
    p.Feedback({10, 20}, true, 100);
    assert(p.Gesture(false, 200));
    goal = p.Step(200);
    assert(goal && goal->pitch > 20 && goal->pitch <= 28);
    assert(p.Move({-10, 25}, 300));
    goal = p.Step(1500);
    assert(!goal && p.fault());
    Policy q;
    q.Feedback({0, 10}, true, 0);
    assert(q.Arm(0));
    assert(q.Move({0, 20}, 0));
    q.Feedback({0, 20}, true, 1);
    q.BeginTurn(10);
    q.Speaking(true, 10);
    q.Feedback({0, 20}, true, 2010);
    goal = q.Step(2010);
    assert(goal && goal->pitch > 20);
    q.Speaking(false, 2020);
    goal = q.Step(2020);
    assert(goal && near(goal->pitch, 20));
    q.Stop(2020);
    assert(!q.automatic());
    q.Speaking(true, 2030);
    q.Feedback({0, 20}, true, 4030);
    goal = q.Step(4030);
    assert(goal && near(goal->pitch, 20));
    assert(q.Resume(4030));
    q.Feedback({0, 20}, true, 9000);
    q.Step(9000);
    q.Feedback({0, 20}, false, 9010);
    assert(!q.Step(9010));
    assert(!q.Move({0, 20}, 9010));
    Policy edge;
    edge.Feedback({29, 59}, true, 0);
    assert(edge.Arm(0));
    assert(edge.Gesture(false, 0));
    for (int t = 0; t <= 5000; t += 20) {
        edge.Feedback({29, 59}, true, t);
        if (auto out = edge.Step(t))
            assert(out->pitch <= 60 && out->pitch >= 5 && out->yaw <= 30);
    }
    assert(near(edge.base().pitch, 59) && near(edge.target().pitch, 59));
    // An explicit gesture must survive speaking ending; a later Move cancels it.
    Policy e;
    e.Feedback({0, 20}, true, 0);
    assert(e.Arm(0));
    e.Speaking(true, 0);
    assert(e.Gesture(true, 0));
    e.Speaking(false, 1);
    assert(e.Step(100)->yaw > 0);
    assert(e.Move({8, 22}, 100));
    assert(near(e.Step(200)->yaw, 8));
    std::cout << "motion policy: PASS\n";
}
