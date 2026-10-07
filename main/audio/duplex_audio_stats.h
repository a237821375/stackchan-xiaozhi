#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Statistics only; input is the interleaved microphone/reference stream.
// Owned by the input task. No recording; an optional bounded reference history
// is used only for correlation calculations. Input is never altered.
class DuplexAudioStats {
public:
    bool Add(const int16_t* data, size_t samples, bool correlate = false) {
        if (data == nullptr || samples % 2 != 0)
            return false;
        for (size_t i = 0; i < samples; i += 2) {
            const int64_t mic = data[i], ref = data[i + 1];
            if (correlate) {
                ref_history_[history_pos_] = static_cast<int16_t>(ref);
                // Sample correlations at 1/8 rate to bound input-task overhead.
                if ((correlation_frames_++ % 8) == 0)
                    for (size_t j = 0; j < lags_.size(); ++j) {
                        if (history_frames_ < lags_[j])
                            continue;
                        const int64_t delayed =
                            ref_history_[(history_pos_ + ref_history_.size() - lags_[j]) %
                                         ref_history_.size()];
                        auto& c = correlation_[j];
                        ++c.count;
                        c.x += mic;
                        c.y += delayed;
                        c.xx += mic * mic;
                        c.yy += delayed * delayed;
                        c.xy += mic * delayed;
                    }
                history_pos_ = (history_pos_ + 1) % ref_history_.size();
                history_frames_ = std::min(history_frames_ + 1, ref_history_.size());
            }
            mic_full_scale_ += mic == -32768 || mic == 32767;
            ref_full_scale_ += ref == -32768 || ref == 32767;
            mic_energy_ += mic * mic;
            ref_energy_ += ref * ref;
            mic_peak_ = std::max(mic_peak_, static_cast<unsigned>(mic < 0 ? -mic : mic));
            ref_peak_ = std::max(ref_peak_, static_cast<unsigned>(ref < 0 ? -ref : ref));
        }
        frames_ += samples / 2;
        return true;
    }
    size_t mic_full_scale() const { return mic_full_scale_; }
    size_t ref_full_scale() const { return ref_full_scale_; }
    unsigned correlation_lag_samples() const { return lags_[BestCorrelation()]; }
    int correlation_permille() const {
        return static_cast<int>(std::round(Coefficient(correlation_[BestCorrelation()]) * 1000));
    }
    void Reset() {
        correlation_ = {};
        ref_history_ = {};
        history_pos_ = history_frames_ = correlation_frames_ = 0;
        mic_full_scale_ = ref_full_scale_ = 0;
        frames_ = mic_energy_ = ref_energy_ = 0;
        mic_peak_ = ref_peak_ = 0;
    }
    size_t frames() const { return frames_; }
    unsigned mic_rms() const {
        return frames_ ? std::sqrt(static_cast<double>(mic_energy_) / frames_) : 0;
    }
    unsigned ref_rms() const {
        return frames_ ? std::sqrt(static_cast<double>(ref_energy_) / frames_) : 0;
    }
    unsigned mic_peak() const { return mic_peak_; }
    unsigned ref_peak() const { return ref_peak_; }

private:
    // Coarse candidate delays for 24 kHz raw capture: 0, 1, 2, 5, 10 ms.
    // This diagnoses correlation, not a calibrated AEC delay or acceptance score.
    static constexpr std::array<unsigned, 5> lags_ = {0, 24, 48, 120, 240};
    struct Correlation {
        size_t count = 0;
        int64_t x = 0, y = 0, xx = 0, yy = 0, xy = 0;
    };
    static double Coefficient(const Correlation& c) {
        if (c.count < 2)
            return 0;
        const double n = c.count;
        const double xx = n * c.xx - static_cast<double>(c.x) * c.x;
        const double yy = n * c.yy - static_cast<double>(c.y) * c.y;
        if (xx <= 0 || yy <= 0)
            return 0;
        const double xy = n * c.xy - static_cast<double>(c.x) * c.y;
        return std::clamp(xy / std::sqrt(xx * yy), -1.0, 1.0);
    }
    size_t BestCorrelation() const {
        size_t best = 0;
        for (size_t j = 1; j < lags_.size(); ++j)
            if (std::abs(Coefficient(correlation_[j])) > std::abs(Coefficient(correlation_[best])))
                best = j;
        return best;
    }
    std::array<Correlation, 5> correlation_{};
    std::array<int16_t, 241> ref_history_{};
    size_t history_pos_ = 0, history_frames_ = 0, correlation_frames_ = 0;
    size_t mic_full_scale_ = 0, ref_full_scale_ = 0;
    size_t frames_ = 0;
    uint64_t mic_energy_ = 0, ref_energy_ = 0;
    unsigned mic_peak_ = 0, ref_peak_ = 0;
};
