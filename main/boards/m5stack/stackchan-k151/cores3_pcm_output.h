#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

template <class Writer>
int WriteCoreS3Pcm32(const int16_t* data, int samples, Writer writer) {
    if (data == nullptr || samples <= 0)
        return 0;
    // One DMA frame, bounded stack storage; no allocation on the audio path.
    std::array<int32_t, 240> frame;
    int written = 0;
    while (written < samples) {
        const int count = std::min(samples - written, static_cast<int>(frame.size()));
        for (int i = 0; i < count; ++i) {
            // Multiplication keeps signed negative samples defined in C++.
            frame[i] = static_cast<int32_t>(data[written + i]) * 65536;
        }
        if (!writer(frame.data(), count * sizeof(int32_t)))
            break;
        written += count;
    }
    return written;
}
