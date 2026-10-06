#pragma once
#include <cstdint>
namespace stackchan {
class FeedbackRecovery {
public:
    bool Update(bool valid, int64_t now) {
        if (!valid)
            Fail(now);
        if (gap_at_ >= 0 && now - gap_at_ >= 500)
            timed_out_ = true;
        if (!valid)
            return false;
        if (pause_at_ >= 0) {
            if (++good_ < 3)
                return false;
            paused_total_ += now - pause_at_;
            pause_at_ = -1;
        }
        return true;
    }
    void Fail(int64_t now) {
        if (gap_at_ < 0)
            gap_at_ = now;
        if (pause_at_ < 0)
            pause_at_ = now;
        good_ = 0;
        if (now - gap_at_ >= 500)
            timed_out_ = true;
    }
    // Only after a healthy complete control cycle (including any writes).
    void Confirm() {
        if (paused())
            return;
        gap_at_ = -1;
        good_ = 0;
        timed_out_ = false;
    }
    int64_t PolicyTime(int64_t now) const {
        return (pause_at_ >= 0 ? pause_at_ : now) - paused_total_;
    }
    bool timed_out() const { return timed_out_; }
    bool paused() const { return pause_at_ >= 0; }

private:
    int64_t gap_at_ = -1, pause_at_ = -1, paused_total_ = 0;
    unsigned good_ = 0;
    bool timed_out_ = false;
};
}  // namespace stackchan
