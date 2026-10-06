#include "../main/boards/m5stack/stackchan-k151/servo_diagnostics.h"
#include <cassert>
#include <iostream>
using namespace stackchan;
int main() {
    ServoDiagnostics stats;
    stats.Transaction(1, true, true, 0, 0);
    stats.Transaction(1, true, false, 1, 0);
    stats.Transaction(1, true, false, 2, 0);
    stats.Transaction(1, true, false, 3, 0);
    stats.Transaction(1, true, false, 4, 0);
    const auto& yaw = stats.axis[0];
    assert(yaw.reads == 5 && yaw.read_failures == 4);
    assert(yaw.missing == 1 && yaw.checksum == 1 && yaw.wrong_id == 1 && yaw.wrong_length == 1);
    assert(yaw.writes == 0 && yaw.alarms == 0);
    stats.Transaction(2, false, true, 0, 0);
    stats.Transaction(2, false, false, 0, 4);
    stats.Transaction(2, true, false, 2, 4);   // Stale status after CRC failure is not an alarm.
    stats.Transaction(2, false, false, 0, 0);  // Unexpected return count.
    assert(stats.axis[1].writes == 3 && stats.axis[1].write_failures == 2);
    assert(stats.axis[1].reads == 1 && stats.axis[1].read_failures == 1);
    assert(stats.axis[1].alarms == 1 && stats.axis[1].checksum == 1 && stats.axis[1].other == 1);
    stats.Transaction(0, true, false, 1, 0);
    stats.Transaction(255, true, false, 1, 0);
    assert(yaw.reads == 5 && stats.axis[1].reads == 1);  // Reject invalid IDs safely.
    stats.Data(21);
    stats.Data(6);
    stats.Error(RxError::Frame);
    stats.Error(RxError::Frame);
    stats.Error(RxError::Parity);
    stats.Error(RxError::FifoOverflow);
    stats.Error(RxError::BufferFull);
    stats.Error(RxError::Break);
    assert(stats.data_events == 2 && stats.data_bytes == 27);
    assert(stats.frame_errors == 2 && stats.parity_errors == 1 && stats.fifo_overflows == 1 &&
           stats.buffer_full == 1 && stats.breaks == 1);
    std::cout << "Per-servo outcomes, stale alarms, UART errors and receive bytes: PASS\n";
}
