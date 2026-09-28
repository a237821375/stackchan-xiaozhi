#include <cassert>
#include <cmath>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
using namespace stackchan;
bool near(float a, float b) { return std::abs(a - b) < .01f; }
int main() {
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
    assert(goal && goal->pitch > 20 && goal->pitch <= 24);
    assert(p.Move({-10, 25}, 300));
    goal = p.Step(1500);
    assert(!goal && p.fault());
    Policy q;
    q.Feedback({0, 10}, true, 0);
    assert(q.Arm(0));
    assert(q.Move({0, 20}, 0));
    q.Feedback({0, 20}, true, 1);
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
    for (int t = 0; t <= 2000; t += 20) {
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
