#pragma once
#include <cmath>
#include <cstdint>

namespace stackchan {
// Evidence rules from factory hal_servo.cpp. This is separate from the
// conservative no-progress timeout and the immediate overload protection.
class FactoryStallEvidence {
public:
    bool Update(int position, int target, int load, int current, int64_t now) {
        if (now - last_sample_at_ < 50)
            return false;
        last_sample_at_ = now;
        const int delta = target - position;
        if (std::abs(delta) < 8) {
            Reset();
            return false;
        }
        const int direction = delta > 0 ? 1 : -1;
        if (valid_ && direction == last_direction_) {
            const int progress = std::abs(position - last_position_);
            const bool spike = current >= 350 || current - last_current_ >= 80 || load >= 650 ||
                               load - last_load_ >= 150;
            if (progress <= 1 && spike)
                ++confirmations_;
            else if (progress > 1)
                confirmations_ = 0;
        } else {
            confirmations_ = 0;
        }
        last_position_ = position;
        last_load_ = load;
        last_current_ = current;
        last_direction_ = direction;
        valid_ = true;
        return confirmations_ >= 2;
    }
    void Reset() {
        valid_ = false;
        last_direction_ = 0;
        confirmations_ = 0;
    }

private:
    int64_t last_sample_at_ = 0;
    int last_position_ = 0, last_load_ = 0, last_current_ = 0, last_direction_ = 0;
    unsigned confirmations_ = 0;
    bool valid_ = false;
};
}  // namespace stackchan
