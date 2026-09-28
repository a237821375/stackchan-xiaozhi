#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/face_state.h"
#include "../main/boards/m5stack/stackchan-k151/servo_power.h"
#include "../main/boards/m5stack/stackchan-k151/servo_protocol.h"
int main() {
    using namespace stackchan;
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
    auto req = Packet(1, 2, {56, 2});
    assert((req == Bytes{255, 255, 1, 4, 2, 56, 2, 190}));
    Bytes data;
    assert(Reply({255, 255, 1, 4, 0, 1, 205, 44}, 1, 2, data));
    assert(Word(data.data()) == 461);
    assert(!Reply({255, 255, 1, 4, 0, 1, 205, 45}, 1, 2, data));
    assert(!Reply({255, 255, 2, 4, 0, 1, 205, 43}, 1, 2, data));
    assert(!Reply({255, 255, 1, 4, 0, 1}, 1, 2, data));
    assert(!Reply({255, 255, 1, 4, 1, 1, 205, 43}, 1, 2, data));
    assert(FaceAsset("neutral", false) == "neutral");
    assert(FaceAsset("neutral", true) == "neutral_talk");
    assert(FaceAsset("sleepy", true) == "sleepy");
    assert(FaceAsset("happy", false) == "happy");
    assert(FaceAsset("evil_input", true) == "neutral_talk");
    std::cout << "servo protocol and face selection: PASS\n";
}
