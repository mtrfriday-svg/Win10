#include "face_display.h"
#include "mdee_robot.h"
#include "oled_status.h"
#include "assets/lang_config.h"
#include "lvgl_image.h"
#include "boot_logo.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cstring>

#define TAG "FaceDisplay"

static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }

FaceDisplay::FaceDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                         int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                         bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    look_ = LookFor("neutral");
}

FaceDisplay::Look FaceDisplay::LookFor(const std::string& e) {
    //            color      cover tilt smile open lx ly wink
    if (e == "happy" || e == "relaxed" || e == "confident" || e == "funny")
        return {0x00E0FF, 0, 0, 60, 0, 0, 0, false};
    if (e == "laughing")  return {0x00E0FF, 26, 0, 62, 20, 0, 0, false};
    if (e == "loving" || e == "kissy" || e == "delicious")
        return {0xFF4FA0, 0, 0, 58, 0, 0, 0, false};
    if (e == "embarrassed") return {0xFF8FB0, 8, 0, 30, 0, 0, 6, false};
    if (e == "winking" || e == "silly") return {0x00E0FF, 0, 0, 60, 0, 0, 0, true};
    if (e == "cool")      return {0x38FF9A, 30, 0, 32, 0, 0, 0, false};
    if (e == "sad")       return {0x5A90FF, 14, -180, -40, 0, 0, 8, false};
    if (e == "crying")    return {0x40A0FF, 18, -200, -45, 10, 0, 10, false};
    if (e == "angry")     return {0xFF3030, 24, 220, -22, 0, 0, 0, false};
    if (e == "surprised" || e == "shocked") return {0xFFFFFF, 0, 0, 0, 30, 0, -6, false};
    if (e == "thinking")  return {0xB070FF, 8, 0, 0, 0, 14, -14, false};
    if (e == "confused")  return {0xFFC040, 6, 120, -20, 0, 10, -6, false};
    if (e == "sleepy")    return {0x4070A0, 58, 0, 14, 0, 0, 8, false};
    return {0x00E0FF, 0, 0, 36, 0, 0, 0, false};   // neutral / unknown
}

void FaceDisplay::SetupUI() {
    if (setup_ui_called_) return;
    Display::SetupUI();
    DisplayLockGuard lock(this);

    const int W = LV_HOR_RES, H = LV_VER_RES;
    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    const int gap = 36;
    const int eye_y = H * 28 / 100;
    for (int i = 0; i < 2; i++) {
        int x = W / 2 + (i == 0 ? -(kEyeW + gap / 2) : gap / 2);
        eye_[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(eye_[i]);
        lv_obj_set_size(eye_[i], kEyeW, kEyeH);
        lv_obj_set_pos(eye_[i], x, eye_y);
        lv_obj_set_style_radius(eye_[i], 32, 0);
        lv_obj_set_style_bg_opa(eye_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(eye_[i], lv_color_hex(look_.color), 0);
        lv_obj_set_style_clip_corner(eye_[i], true, 0);
        lv_obj_remove_flag(eye_[i], LV_OBJ_FLAG_SCROLLABLE);

        pupil_[i] = lv_obj_create(eye_[i]);
        lv_obj_remove_style_all(pupil_[i]);
        lv_obj_set_size(pupil_[i], 26, 26);
        lv_obj_set_style_radius(pupil_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pupil_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pupil_[i], lv_color_white(), 0);
        lv_obj_set_pos(pupil_[i], (kEyeW - 26) / 2, (kEyeH - 26) / 2);

        lid_[i] = lv_obj_create(eye_[i]);
        lv_obj_remove_style_all(lid_[i]);
        lv_obj_set_size(lid_[i], kEyeW + 60, kEyeH);
        lv_obj_set_pos(lid_[i], -30, -kEyeH);
        lv_obj_set_style_bg_opa(lid_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(lid_[i], lv_color_black(), 0);
    }

    const int mouth_cy = H * 74 / 100;
    smile_ = lv_arc_create(screen);
    lv_obj_remove_style(smile_, nullptr, LV_PART_KNOB);
    lv_obj_remove_style(smile_, nullptr, LV_PART_INDICATOR);
    lv_obj_remove_flag(smile_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(smile_, 130, 130);
    lv_obj_set_pos(smile_, W / 2 - 65, mouth_cy - 95);
    lv_arc_set_rotation(smile_, 0);
    lv_obj_set_style_arc_width(smile_, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(smile_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(smile_, lv_color_hex(0xFF5C8A), LV_PART_MAIN);

    mouth_ = lv_obj_create(screen);
    lv_obj_remove_style_all(mouth_);
    lv_obj_set_style_radius(mouth_, 20, 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(0xFF5C8A), 0);
    lv_obj_set_size(mouth_, 56, 6);
    lv_obj_set_pos(mouth_, W / 2 - 28, mouth_cy - 3);
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);

    // ---- boot logo overlay (black screen + GIF), removed by Tick() after BOOT_LOGO_MS ----
    static lv_image_dsc_t logo_dsc;
    logo_dsc = {};
    logo_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    logo_dsc.header.cf = LV_COLOR_FORMAT_RAW;
    logo_dsc.header.w = BOOT_LOGO_WIDTH;
    logo_dsc.header.h = BOOT_LOGO_HEIGHT;
    logo_dsc.data_size = sizeof(kBootLogoGif);
    logo_dsc.data = kBootLogoGif;
    logo_bg_ = lv_obj_create(screen);
    lv_obj_remove_style_all(logo_bg_);
    lv_obj_set_size(logo_bg_, W, H);
    lv_obj_set_pos(logo_bg_, 0, 0);
    lv_obj_set_style_bg_color(logo_bg_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(logo_bg_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(logo_bg_, LV_OBJ_FLAG_SCROLLABLE);
    logo_gif_ = lv_gif_create(logo_bg_);
    lv_gif_set_src(logo_gif_, &logo_dsc);
    lv_obj_align(logo_gif_, LV_ALIGN_CENTER, 0, 0);
    logo_end_ms_ = NowMs() + BOOT_LOGO_MS;

    next_blink_ms_ = NowMs() + 2500 + BOOT_LOGO_MS;
    next_glance_ms_ = NowMs() + 1800 + BOOT_LOGO_MS;
    lv_timer_create(&FaceDisplay::TimerCb, 33, this);
    Apply();
}

void FaceDisplay::TimerCb(lv_timer_t* t) { static_cast<FaceDisplay*>(lv_timer_get_user_data(t))->Tick(); }

void FaceDisplay::Tick() {
    int64_t now = NowMs();
    if (logo_bg_ != nullptr && now > logo_end_ms_) {      // boot animation finished -> show the face
        lv_obj_delete(logo_bg_);
        logo_bg_ = nullptr;
        logo_gif_ = nullptr;
    }

    // blink: 0 -> closing (1..4) -> opening (5..8) -> 0
    if (blink_phase_ == 0 && now > next_blink_ms_ && look_.cover < 40) blink_phase_ = 1;
    int blink_cover = 0;
    if (blink_phase_ > 0) {
        int p = blink_phase_ <= 4 ? blink_phase_ : 9 - blink_phase_;      // 1,2,3,4,4,3,2,1
        if (blink_phase_ == 5) p = 4;
        blink_cover = kEyeH * p / 4;
        if (++blink_phase_ > 9) { blink_phase_ = 0; next_blink_ms_ = now + 2200 + (esp_random() % 4200); }
    }

    // glance around
    if (now > next_glance_ms_) {
        next_glance_ms_ = now + 1200 + (esp_random() % 2800);
        gaze_tx_ = look_.look_x + (int)(esp_random() % 21) - 10;
        gaze_ty_ = look_.look_y + (int)(esp_random() % 13) - 6;
    }
    gaze_x_ += (gaze_tx_ - gaze_x_) / 4;
    gaze_y_ += (gaze_ty_ - gaze_y_) / 4;

    for (int i = 0; i < 2; i++) {
        lv_obj_set_pos(pupil_[i], (kEyeW - 26) / 2 + gaze_x_, (kEyeH - 26) / 2 + gaze_y_);
        int cover = look_.cover;
        if (look_.wink && i == 1) cover = kEyeH;
        if (blink_cover > cover) cover = blink_cover;
        // lid sits above the eye and slides down by `cover`
        lv_obj_set_pos(lid_[i], -30, -kEyeH + cover + (look_.tilt != 0 ? 8 : 0));
        int rot = (i == 0) ? look_.tilt : -look_.tilt;
        lv_obj_set_style_transform_pivot_x(lid_[i], (kEyeW + 60) / 2, 0);
        lv_obj_set_style_transform_pivot_y(lid_[i], kEyeH, 0);
        lv_obj_set_style_transform_rotation(lid_[i], rot, 0);
    }

    // mouth: talking overrides static look
    if (speaking_) {
        int target = 6 + (int)(esp_random() % 30);
        talk_open_ += (target - talk_open_) / 2;
        lv_obj_remove_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(smile_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_, 52 + talk_open_ / 3, talk_open_);
        lv_obj_set_pos(mouth_, LV_HOR_RES / 2 - (52 + talk_open_ / 3) / 2, LV_VER_RES * 74 / 100 - talk_open_ / 2);
    }
}

void FaceDisplay::Apply() {
    // static look for the current emotion (called under display lock)
    for (int i = 0; i < 2; i++) lv_obj_set_style_bg_color(eye_[i], lv_color_hex(look_.color), 0);
    gaze_tx_ = look_.look_x;
    gaze_ty_ = look_.look_y;

    const int H = LV_VER_RES, W = LV_HOR_RES;
    if (speaking_) return;                                // Tick() animates the mouth
    if (look_.open > 0) {
        lv_obj_remove_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_, look_.open + 26, look_.open);
        lv_obj_set_style_radius(mouth_, look_.open / 2 + 4, 0);
        lv_obj_set_pos(mouth_, W / 2 - (look_.open + 26) / 2, H * 74 / 100 - look_.open / 2);
        lv_obj_add_flag(smile_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(smile_, LV_OBJ_FLAG_HIDDEN);
    int hw = look_.smile == 0 ? 14 : (look_.smile > 0 ? look_.smile : -look_.smile);
    int centre = look_.smile >= 0 ? 90 : 270;             // 90 = bottom of circle (smile), 270 = top (frown)
    lv_arc_set_bg_angles(smile_, centre - hw, centre + hw);
    int mouth_cy = H * 74 / 100;
    // frown arc sits lower so it reads as a frown, smile arc higher
    lv_obj_set_pos(smile_, W / 2 - 65, look_.smile >= 0 ? mouth_cy - 95 : mouth_cy - 10);
}

void FaceDisplay::SetEmotion(const char* emotion) {
    if (!setup_ui_called_ || emotion == nullptr) return;
    DisplayLockGuard lock(this);
    emotion_ = emotion;
    look_ = LookFor(emotion_);
    Apply();
    // ears follow the mood like a pet's
    auto& robot = MdeeRobot::GetInstance();
    const std::string& e = emotion_;
    if (e == "happy" || e == "laughing" || e == "loving" || e == "funny" || e == "silly" ||
        e == "winking" || e == "delicious" || e == "kissy")  robot.Ears(MdeeEar::kWiggle, 1800);
    else if (e == "sad" || e == "crying" || e == "sleepy" || e == "angry" || e == "embarrassed")
        robot.Ears(MdeeEar::kDroop, 4000);
    else if (e == "surprised" || e == "shocked" || e == "thinking" || e == "confused")
        robot.Ears(MdeeEar::kPerk, 2500);
    else if (!listening_) robot.Ears(MdeeEar::kAuto);       // keep ears perked while listening
}

void FaceDisplay::SetStatus(const char* status) {
    if (status == nullptr) return;
    OledStatus::GetInstance().SetState(status);
    auto& robot = MdeeRobot::GetInstance();
    bool speaking = strcmp(status, Lang::Strings::SPEAKING) == 0;
    listening_ = strcmp(status, Lang::Strings::LISTENING) == 0;
    if (speaking) { robot.SetNeo(MdeeNeo::kBreathe, 0, 200, 60); robot.Ears(MdeeEar::kAuto); }
    else if (strcmp(status, Lang::Strings::LISTENING) == 0) { robot.SetNeo(MdeeNeo::kSolid, 0, 60, 255); robot.Ears(MdeeEar::kPerk); }
    else if (strcmp(status, Lang::Strings::CONNECTING) == 0) robot.SetNeo(MdeeNeo::kBreathe, 150, 60, 255);
    else if (strcmp(status, Lang::Strings::STANDBY) == 0) { robot.SetNeo(MdeeNeo::kBreathe, 0, 120, 160); robot.Ears(MdeeEar::kAuto); }
    if (!setup_ui_called_) return;
    DisplayLockGuard lock(this);
    bool was = speaking_;
    speaking_ = speaking;
    if (was && !speaking_) Apply();                      // restore static mouth when TTS ends
}

void FaceDisplay::SetChatMessage(const char* role, const char* content) {
    OledStatus::GetInstance().SetChat(role, content);     // speech-to-text + replies go to the OLED
}

void FaceDisplay::ClearChatMessages() { OledStatus::GetInstance().SetChat("", ""); }

void FaceDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (notification) OledStatus::GetInstance().SetState(notification);
}

void FaceDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void FaceDisplay::UpdateStatusBar(bool update_all) {}   // no status bar on the face; OLED has it

void FaceDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {}   // not used on this robot
