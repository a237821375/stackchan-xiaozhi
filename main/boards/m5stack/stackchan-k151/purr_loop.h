#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace stackchan {
class PurrLoop {
public:
    PurrLoop(const int16_t* samples, size_t size, int rate)
        : samples_(samples), size_(size), fade_samples_(std::max(1, rate / 3)) {}
    // Only the audio output task owns playback state; inputs are snapshots.
    bool Render(int16_t* out, size_t count, bool petting, bool allowed) {
        if (!allowed || !samples_ || size_ == 0) {
            level_ = 0;
            return false;
        }
        if (!petting && level_ == 0)
            return false;
        for (size_t i = 0; i < count; ++i) {
            level_ = petting ? std::min(fade_samples_, level_ + 1) : std::max(0, level_ - 1);
            out[i] = static_cast<int16_t>(static_cast<int64_t>(samples_[position_]) * level_ /
                                          (4LL * fade_samples_));
            position_ = (position_ + 1) % size_;
        }
        return true;
    }

private:
    const int16_t* samples_;
    size_t size_;
    int fade_samples_;
    int level_ = 0;
    size_t position_ = 0;
};
}  // namespace stackchan
