#include <cassert>
#include <cstring>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/face_state.h"
#include "../main/boards/m5stack/stackchan-k151/factory_upstream/ftservo/SCS.h"
#include "../main/boards/m5stack/stackchan-k151/servo_power.h"
#include "../main/boards/m5stack/stackchan-k151/servo_protocol.h"
#include "../main/boards/m5stack/stackchan-k151/servo_tx_frame.h"
class TestBus : public SCS {
public:
    TestBus() : SCS(1) {}
    stackchan::Bytes tx, rx;
    size_t pos = 0;
    void Response(stackchan::Bytes bytes) {
        rx = std::move(bytes);
        pos = 0;
        tx.clear();
    }

protected:
    int writeSCS(unsigned char* p, int n) override {
        tx.insert(tx.end(), p, p + n);
        return n;
    }
    int writeSCS(unsigned char b) override {
        tx.push_back(b);
        return 1;
    }
    int readSCS(unsigned char* p, int n) override {
        int count = std::min(size_t(n), rx.size() - pos);
        if (p)
            std::memcpy(p, rx.data() + pos, count);
        pos += count;
        return count;
    }
    int readSCS(unsigned char* p, int n, unsigned long) override { return readSCS(p, n); }
    void rFlushSCS() override {}
    void wFlushSCS() override {}
};
int main() {
    using namespace stackchan;
    ServoTxFrame frame;
    const uint8_t header[]{255, 255, 1, 4, 2, 56}, payload[]{15}, checksum[]{178};
    int sends = 0;
    assert(frame.Append(header, 6) == 6 && frame.Append(payload, 1) == 1 &&
           frame.Append(checksum, 1) == 1);
    assert(sends == 0);
    assert(frame.Flush([&](const uint8_t* p, size_t n) {
        ++sends;
        assert(n == 8 && p[0] == 255 && p[7] == 178);
        return int(n);
    }));
    assert(sends == 1);
    uint8_t excess[65]{};
    assert(frame.Append(excess, 65) == 0);
    assert(!frame.Flush([&](const uint8_t*, size_t) {
        ++sends;
        return 0;
    }));
    assert(sends == 1);
    assert(ServoReportedAlarm(0, 1));
    assert(!ServoReportedAlarm(1, 1));  // stale alarm after a new missing reply is not valid
    bool recovered = false;
    int calls = 0, recoveries = 0;
    assert(WithBusRecovery(
        [&] {
            ++calls;
            return recovered;
        },
        [] { return false; },
        [&] {
            recovered = true;
            ++recoveries;
        }));
    assert(calls == 2 && recoveries == 1);
    calls = recoveries = 0;
    assert(!WithBusRecovery(
        [&] {
            ++calls;
            return false;
        },
        [] { return false; }, [&] { ++recoveries; }));
    assert(calls == 2 && recoveries == 1);
    calls = recoveries = 0;
    assert(!WithBusRecovery(
        [&] {
            ++calls;
            return false;
        },
        [] { return true; }, [&] { ++recoveries; }));
    assert(calls == 1 && recoveries == 0);
    int attempts = 0;
    assert(ReadFeedbackWithRetry([&] { return ++attempts == 2; }, [] { return false; }));
    assert(attempts == 2);
    attempts = 0;
    assert(!ReadFeedbackWithRetry(
        [&] {
            ++attempts;
            return false;
        },
        [] { return false; }));
    assert(attempts == 2);  // bounded, no stale-feedback fallback
    attempts = 0;
    assert(!ReadFeedbackWithRetry(
        [&] {
            ++attempts;
            return false;
        },
        [] { return true; }));
    assert(attempts == 1);  // servo alarm must not be masked by a retry
    uint8_t regs[32]{};
    for (auto& r : regs)
        r = 0xaa;
    regs[2] = 1;
    int writes = 0;
    auto read = [&](uint8_t r, uint8_t& v) {
        v = regs[r];
        return true;
    };
    auto write = [&](uint8_t r, uint8_t v) {
        ++writes;
        regs[r] = v;
        return true;
    };
    assert(EnableServoPower(read, write));
    assert(writes == 4 && regs[3] == 0xab && regs[5] == 0xab);
    assert(regs[9] == 0xab && regs[11] == 0xaa && regs[13] == 0xaa);
    writes = 0;
    regs[2] = 0xff;
    assert(!EnableServoPower(read, write) && writes == 0);
    auto failed_read = [](uint8_t, uint8_t&) { return false; };
    assert(!EnableServoPower(failed_read, write) && writes == 0);
    TestBus bus;
    uint8_t data[2]{};
    bus.Response({255, 255, 1, 4, 0, 1, 205, 44});
    assert(bus.Read(1, 56, data, 2) == 2 && bus.getState() == 0);
    assert((bus.tx == Bytes{255, 255, 1, 4, 2, 56, 2, 190}));
    assert(Word(data) == 461);
    bus.Response({255, 255, 1, 4, 0, 1, 205, 45});
    assert(bus.Read(1, 56, data, 2) == 0 && bus.getLastError() != 0);
    bus.Response({255, 255, 2, 4, 0, 1, 205, 43});
    assert(bus.Read(1, 56, data, 2) == 0);
    bus.Response({255, 255, 1, 4, 0, 1});
    assert(bus.Read(1, 56, data, 2) == 0);
    bus.Response({255, 255, 1, 4, 1, 1, 205, 43});
    assert(bus.Read(1, 56, data, 2) == 2 &&
           bus.getState() == 1);  // hardware boundary rejects alarm
    assert(FaceAsset("neutral", false) == "neutral");
    assert(FaceAsset("neutral", true) == "neutral_talk");
    assert(FaceAsset("sleepy", true) == "sleepy");
    assert(FaceAsset("happy", false) == "happy");
    assert(FaceAsset("evil_input", true) == "neutral_talk");
    std::cout << "servo protocol and face selection: PASS\n";
}
