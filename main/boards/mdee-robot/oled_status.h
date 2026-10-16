#ifndef OLED_STATUS_H
#define OLED_STATUS_H

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <string>

// Tiny dedicated status screen (0.96" SSD1306 128x64): Wi-Fi, battery, mode, distances,
// current state and the last speech-to-text / reply text. No LVGL needed.
class OledStatus {
public:
    static OledStatus& GetInstance();
    bool Init(i2c_master_bus_handle_t bus);
    void SetState(const char* state);                       // "Listening...", "Speaking..."
    void SetChat(const char* role, const char* content);    // role: "user" | "assistant"

private:
    OledStatus() = default;
    static void Task(void* arg);
    void Render();
    void Clear();
    void Text(int col, int row, const std::string& s);      // 6x8 cells, 21 cols x 8 rows
    void Flush();

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint8_t fb_[128 * 64 / 8] = {0};
    std::mutex mutex_;
    std::string state_ = "Starting...";
    std::string chat_role_, chat_text_;
    bool ready_ = false;
};

#endif
