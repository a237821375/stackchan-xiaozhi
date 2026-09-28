#pragma once
// Keep protocol eligibility separate from actual PCM activity, including tail audio.
class SpeakingGate {
public:
    void State(bool speech, bool playback_idle) {
        speech_state_ = speech;
        if (speech)
            eligible_ = true;
        else if (playback_idle)
            eligible_ = false;
        if (playback_idle)
            active_ = false;
    }
    bool Output(bool playback_idle) {
        if (!playback_idle && eligible_)
            active_ = true;
        return active_;
    }
    bool Drain(bool playback_idle) {
        if (playback_idle) {
            active_ = false;
            if (!speech_state_)
                eligible_ = false;
        }
        return active_;
    }
    bool active() const { return active_; }

private:
    bool speech_state_ = false, eligible_ = false, active_ = false;
};
