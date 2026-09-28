#include "../main/display/speaking_gate.h"
#include <cassert>
#include <iostream>
int main() {
    SpeakingGate gate;
    gate.State(true, true);
    assert(!gate.active());  // TTS metadata before audio
    assert(gate.Output(false));
    assert(!gate.Drain(true));   // late stop metadata cannot keep mouth moving
    assert(gate.Output(false));  // next audio packet resumes talking
    gate.State(false, false);
    assert(gate.active());  // queued audio still draining
    assert(gate.Output(false));
    assert(!gate.Drain(true));
    assert(!gate.Output(false));  // a UI beep is not speech
    gate.State(true, true);
    assert(gate.Output(false));  // notification speech
    assert(!gate.Drain(true));
    std::cout << "speaking gate: PASS\n";
}
