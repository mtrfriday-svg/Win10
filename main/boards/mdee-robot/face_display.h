#ifndef FACE_DISPLAY_H
#define FACE_DISPLAY_H

#include "display/lcd_display.h"

// Procedural animated face for the ST7789: two eyes (blink, look around, eyelid tilt),
// a smile/frown arc and a mouth that moves while the robot is speaking.
// Driven by the normal XiaoZhi SetEmotion()/SetStatus() calls, so the AI controls the face.
class FaceDisplay : public SpiLcdDisplay {
public:
    FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetTheme(Theme* theme) override { current_theme_ = theme; }
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetHideSubtitle(bool hide) override {}

private:
    struct Look {
        uint32_t color;
        int cover;       // eyelid cover in px (0 = wide open)
        int tilt;        // eyelid tilt in 0.1 deg (+ = angry, - = sad)
        int smile;       // >0 smile half-width (deg), <0 frown half-width, 0 flat
        int open;        // static mouth opening (px), 0 = closed
        int look_x, look_y;  // pupil offset bias
        bool wink;
    };
    static Look LookFor(const std::string& emotion);
    static void TimerCb(lv_timer_t* t);
    void Tick();
    void Apply();

    static constexpr int kEyeW = 70, kEyeH = 90;
    lv_obj_t* eye_[2] = {nullptr, nullptr};
    lv_obj_t* pupil_[2] = {nullptr, nullptr};
    lv_obj_t* lid_[2] = {nullptr, nullptr};
    lv_obj_t* smile_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* logo_bg_ = nullptr;      // boot logo overlay (deleted after the animation)
    lv_obj_t* logo_gif_ = nullptr;
    int64_t logo_end_ms_ = 0;

    Look look_;
    std::string emotion_ = "neutral";
    bool speaking_ = false;
    bool listening_ = false;
    int blink_phase_ = 0;          // 0 = open, >0 closing/opening frames
    int64_t next_blink_ms_ = 0;
    int64_t next_glance_ms_ = 0;
    int gaze_x_ = 0, gaze_y_ = 0, gaze_tx_ = 0, gaze_ty_ = 0;
    int talk_open_ = 0;
};

#endif
