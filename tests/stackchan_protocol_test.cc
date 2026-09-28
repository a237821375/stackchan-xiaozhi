#include <cassert>
#include <iostream>
#include "../main/boards/m5stack/stackchan-k151/face_state.h"
#include "../main/boards/m5stack/stackchan-k151/servo_protocol.h"
int main() {
    using namespace stackchan;
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
