#include "../main/audio/duplex_audio_stats.h"
#include <cassert>
#include <cstdint>
#include <vector>
int main() {
    DuplexAudioStats stats;
    const int16_t input[] = {300, 0, 400, 100};
    assert(stats.Add(input, 4));
    assert(stats.frames() == 2);
    assert(stats.mic_rms() == 353 && stats.ref_rms() == 70);
    assert(stats.mic_peak() == 400 && stats.ref_peak() == 100);
    assert(!stats.Add(input, 3));
    assert(stats.frames() == 2);
    stats.Reset();
    const int16_t limit[] = {-32768, 32767};
    assert(stats.Add(limit, 2));
    assert(stats.mic_rms() == 32768 && stats.ref_rms() == 32767);
    assert(stats.mic_peak() == 32768 && stats.ref_peak() == 32767);
    assert(stats.mic_full_scale() == 1 && stats.ref_full_scale() == 1);
    stats.Reset();
    assert(stats.mic_full_scale() == 0 && stats.ref_full_scale() == 0);
    // A delayed reference with DC offset must correlate across Add boundaries.
    std::vector<int16_t> reference(2400), interleaved(4800);
    uint32_t random = 42;
    for (size_t i = 0; i < reference.size(); ++i) {
        random = random * 1664525U + 1013904223U;
        reference[i] = static_cast<int16_t>((random >> 16) % 10001) - 5000;
        interleaved[2 * i] = i >= 24 ? reference[i - 24] + 100 : 0;
        interleaved[2 * i + 1] = reference[i];
    }
    for (size_t i = 0; i < interleaved.size(); i += 48)
        assert(stats.Add(interleaved.data() + i, 48, true));
    assert(stats.correlation_lag_samples() == 24);
    assert(stats.correlation_permille() >= 999);
    stats.Reset();
    const int16_t silence[] = {0, 0, 0, 0};
    assert(stats.Add(silence, 4));
    assert(stats.correlation_permille() == 0);
    stats.Reset();
    assert(stats.frames() == 0 && stats.mic_rms() == 0 && stats.ref_rms() == 0);
}
