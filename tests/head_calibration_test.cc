#include "../main/boards/m5stack/stackchan-k151/head_calibration.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace stackchan;
    HeadCalibration empty;
    assert(!empty.Valid() && !empty.Approved());
    auto first = ParseCalibrationCommand("head calibrate 461 610");
    assert(first && first->Valid() && !first->Approved());
    auto another = ParseCalibrationCommand("head calibrate 500 600");
    assert(another && another->Valid() && another->yaw_zero == 500);
    first->verified = 1;
    assert(first->Approved());
    auto replacement = ParseCalibrationCommand("head calibrate 462 610");
    assert(replacement && !replacement->Approved());
    for (auto command : {"head calibrate", "head calibrate 461", "head calibrate 461 610 extra",
                         "head calibrate 461.5 610", "head calibrate 0 610",
                         "head calibrate 928 610", "head calibrate 461 -1",
                         "head calibrate 461 832", "head calibrate 999999999999999999999999 610"})
        assert(!ParseCalibrationCommand(command));
    first->schema = 2;
    assert(!first->Valid() && !first->Approved());
    first->schema = 1;
    first->verified = 2;
    assert(!first->Valid() && !first->Approved());
    assert(ParseCalibrationCommand("head calibrate 96 0")->Valid());
    assert(ParseCalibrationCommand("head calibrate 927 831")->Valid());
    assert(EncoderPositionSafe(0) && EncoderPositionSafe(1023));
    assert(!EncoderPositionSafe(-1) && !EncoderPositionSafe(1024));
    assert(!EncoderPositionSafe(1030));  // Endpoint zeros must not mask invalid raw feedback.
    std::cout << "Per-device calibration: missing/corrupt config, bounds and approval reset PASS\n";
}
