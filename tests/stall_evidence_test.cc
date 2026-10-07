#include "../main/boards/m5stack/stackchan-k151/stall_evidence.h"
#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/motion_policy.h"
int main() {
    using namespace stackchan;
    assert(ClassifyMotionStop(true, false) == HeadFault::NoProgress);
    assert(ClassifyMotionStop(false, true) == HeadFault::Stall);
    assert(ClassifyMotionStop(true, true) == HeadFault::Stall);
    assert(ClassifyMotionStop(false, false) == HeadFault::None);
    assert(std::string(FaultName(HeadFault::NoProgress)) == "motion_no_progress");
    assert(std::string(RemoteMotionBlockReason(false, true)) == "local_verification_required");
    assert(std::string(RemoteMotionBlockReason(true, false)) == "motion_not_armed");
    assert(std::string(RemoteMotionBlockReason(true, true)) == "none");
    FactoryStallEvidence low_load;
    for (int t = 50; t <= 2000; t += 50)
        assert(!low_load.Update(500, 550, 10, 0, t));
    FactoryStallEvidence rising_current;
    assert(!rising_current.Update(500, 550, 0, 0, 50));
    assert(!rising_current.Update(500, 550, 0, 80, 100));
    assert(rising_current.Update(500, 550, 0, 160, 150));
    FactoryStallEvidence rising_load;
    assert(!rising_load.Update(500, 550, 0, 0, 50));
    assert(!rising_load.Update(500, 550, 150, 0, 100));
    assert(!rising_load.Update(500, 550, 300, 0, 110));  // too soon: not another sample
    assert(rising_load.Update(500, 550, 300, 0, 150));
    FactoryStallEvidence progressing;
    for (int t = 50; t <= 500; t += 50)
        assert(!progressing.Update(500 + t / 25, 550, 700, 400, t));
    FactoryStallEvidence near;
    for (int t = 50; t <= 500; t += 50)
        assert(!near.Update(500, 507, 700, 400, t));
    FactoryStallEvidence changed;
    assert(!changed.Update(500, 550, 0, 0, 50));
    assert(!changed.Update(500, 550, 150, 0, 100));
    assert(!changed.Update(500, 450, 300, 0, 150));  // direction changed
    changed.Reset();
    assert(!changed.Update(500, 450, 650, 0, 200));
    std::cout << "factory stall evidence: low load, spikes, cadence, movement and reset: PASS\n";
}
