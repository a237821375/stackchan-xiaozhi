#pragma once
#include "display/lcd_display.h"
#include "face_state.h"
#include "stackchan_head.h"

// The application already schedules emotion/state changes on its main loop.
class StackchanDisplay : public SpiLcdDisplay {
public:
    StackchanDisplay(StackchanHead& head, esp_lcd_panel_io_handle_t io,
                     esp_lcd_panel_handle_t panel, int w, int h, int x, int y, bool mx, bool my,
                     bool swap)
        : SpiLcdDisplay(io, panel, w, h, x, y, mx, my, swap), head_(head) {}
    void SetEmotion(const char* emotion) override {
        emotion_ = stackchan::NormalizeEmotion(emotion ? emotion : "neutral");
        head_.SetEmotion(emotion_);
        Render();
    }
    void SetSpeaking(bool speaking) override {
        if (speaking_ == speaking)
            return;
        speaking_ = speaking;
        head_.SetSpeaking(speaking);
        Render();
    }

private:
    void Render() {
        if (!IsSetupUICalled())
            return;
        const auto name = stackchan::FaceAsset(emotion_, speaking_);
        LcdDisplay::SetEmotion(name.c_str());
    }
    StackchanHead& head_;
    std::string emotion_ = "neutral";
    bool speaking_ = false;
};
