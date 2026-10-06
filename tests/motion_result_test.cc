#include "../main/boards/m5stack/stackchan-k151/motion_result.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace stackchan;
    // Actual failed probe: animation ended, but the encoder never moved.
    PoseCommandResult probe;
    probe.Begin({0.62f, 10.3125f}, {2.f, 12.f});
    probe.Update({0.62f, 10.3125f}, true, false, false);
    assert(probe.finished());
    assert(!probe.motion_observed());
    assert(!probe.target_reached());
    assert(PositionState(true, true, false, false, {0.62f, 10.3125f}, {2.f, 12.f}) ==
           PositionOutcome::TargetNotReached);

    // The hardware moving flag must prevent an early completion even at the target.
    PoseCommandResult moving;
    moving.Begin({0, 10}, {0, 12});
    moving.Update({0, 12}, true, false, true);
    assert(!moving.finished());
    assert(moving.motion_observed());
    assert(PositionState(true, true, false, true, {0, 12}, {0, 12}) == PositionOutcome::Moving);
    moving.Update({0, 12}, true, false, false);
    assert(moving.finished() && moving.target_reached());

    // Arrival is encoder-precise, independent of the larger stall exclusion threshold.
    assert(PositionState(true, true, false, false, {-0.3125f, 10.3125f}, {0, 10}) ==
           PositionOutcome::Completed);
    assert(PositionState(true, true, false, false, {-0.9375f, 10.3125f}, {0, 10}) ==
           PositionOutcome::TargetNotReached);
    assert(PositionState(false, true, false, false, {0, 10}, {0, 10}) ==
           PositionOutcome::Unavailable);
    assert(PositionState(true, false, false, false, {0, 10}, {0, 10}) == PositionOutcome::Disabled);

    // Missing feedback and an unfinished spring cannot certify arrival.
    PoseCommandResult invalid;
    invalid.Begin({0, 10}, {0, 12});
    invalid.Update({0, 12}, false, false, false);
    assert(!invalid.finished() && !invalid.motion_observed());
    invalid.Update({0, 12}, true, true, false);
    assert(!invalid.finished());
    invalid.Update({0, 12}, true, false, false);
    assert(invalid.finished() && invalid.target_reached());
    invalid.Clear();
    assert(!invalid.has_command() && !invalid.finished());
    std::cout << "encoder arrival, hardware motion and honest probe outcome: PASS\n";
}
