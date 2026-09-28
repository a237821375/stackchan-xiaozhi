#include "../main/boards/m5stack/stackchan-k151/purr_loop.h"
#include <cassert>
#include <iostream>

int main() {
    // Constant input makes gain, fade and silence measurable independently of the recording.
    const int16_t source[] = {10000, 10000, 10000, 10000};
    stackchan::PurrLoop loop(source, 4, 1000);
    int16_t out[20]{};
    assert(!loop.Render(out, 20, false, true));
    assert(loop.Render(out, 20, true, true));
    assert(out[0] > 0 && out[0] < out[19]);
    for (int i = 0; i < 20; ++i)
        loop.Render(out, 20, true, true);
    assert(out[19] == 2500);  // -12 dB relative to original PCM, not global volume.
    assert(!loop.Render(out, 20, true, false));  // listening/speaking interrupts immediately
    assert(loop.Render(out, 20, true, true));
    assert(out[0] < 2500);  // resume fades in, no hard edge
    for (int i = 0; i < 20; ++i)
        loop.Render(out, 20, true, true);
    assert(loop.Render(out, 20, false, true));
    assert(out[0] > out[19]);
    for (int i = 0; i < 30; ++i)
        loop.Render(out, 20, false, true);
    assert(!loop.Render(out, 20, false, true));
    stackchan::PurrLoop empty(nullptr, 0, 1000);
    assert(!empty.Render(out, 20, true, true));
    std::cout << "Purr fade, loop, interruption and release passed\n";
}
