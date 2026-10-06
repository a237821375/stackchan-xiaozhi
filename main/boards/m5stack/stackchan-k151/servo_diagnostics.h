#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace stackchan {
enum class RxError { Frame, Parity, FifoOverflow, BufferFull, Break };
struct AxisBusStats {
    uint32_t reads = 0, read_failures = 0, writes = 0, write_failures = 0;
    uint32_t missing = 0, checksum = 0, wrong_id = 0, wrong_length = 0, alarms = 0, other = 0;
};
struct ServoDiagnostics {
    std::array<AxisBusStats, 2> axis{};
    uint32_t data_events = 0, data_bytes = 0, frame_errors = 0, parity_errors = 0;
    uint32_t fifo_overflows = 0, buffer_full = 0, breaks = 0;
    // One entry per actual attempt, including retries. All access is by the UART owner.
    void Transaction(uint8_t id, bool read, bool ok, int transport, int status) {
        if (id < 1 || id > 2)
            return;
        auto& a = axis[id - 1];
        if (read) {
            ++a.reads;
            if (!ok)
                ++a.read_failures;
        } else {
            ++a.writes;
            if (!ok)
                ++a.write_failures;
        }
        if (ok)
            return;
        switch (transport) {
            case 1:
                ++a.missing;
                break;
            case 2:
                ++a.checksum;
                break;
            case 3:
                ++a.wrong_id;
                break;
            case 4:
                ++a.wrong_length;
                break;
            default:
                if (transport == 0 && status != 0)
                    ++a.alarms;
                else
                    ++a.other;
        }
    }
    void Data(size_t bytes) {
        ++data_events;
        data_bytes += bytes;
    }
    void Error(RxError error) {
        switch (error) {
            case RxError::Frame:
                ++frame_errors;
                break;
            case RxError::Parity:
                ++parity_errors;
                break;
            case RxError::FifoOverflow:
                ++fifo_overflows;
                break;
            case RxError::BufferFull:
                ++buffer_full;
                break;
            case RxError::Break:
                ++breaks;
                break;
        }
    }
};
}  // namespace stackchan
