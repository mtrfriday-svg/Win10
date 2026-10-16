#include "oled_status.h"
#include "config.h"
#include "mdee_robot.h"
#include "logic_battery.h"
#include "oled_font6x8.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <algorithm>
#include <esp_lcd_panel_vendor.h>
#include <cstring>
#include <cstdio>

#define TAG "OledStatus"

OledStatus& OledStatus::GetInstance() {
    static OledStatus inst;
    return inst;
}

bool OledStatus::Init(i2c_master_bus_handle_t bus) {
    if (i2c_master_probe(bus, OLED_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found at 0x%02X", OLED_I2C_ADDR);
        return false;
    }
    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = OLED_I2C_ADDR;
    io_config.scl_speed_hz = 400 * 1000;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 6;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_i2c(bus, &io_config, &io_) != ESP_OK) return false;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.bits_per_pixel = 1;
    esp_lcd_panel_ssd1306_config_t ssd = {};
    ssd.height = OLED_HEIGHT;
    panel_config.vendor_config = &ssd;
    if (esp_lcd_new_panel_ssd1306(io_, &panel_config, &panel_) != ESP_OK) return false;
    esp_lcd_panel_reset(panel_);
    if (esp_lcd_panel_init(panel_) != ESP_OK) return false;
    esp_lcd_panel_invert_color(panel_, false);
    esp_lcd_panel_disp_on_off(panel_, true);
    ready_ = true;
    xTaskCreate(&OledStatus::Task, "oled", 4096, this, 2, nullptr);
    return true;
}

void OledStatus::SetState(const char* state) {
    std::lock_guard<std::mutex> l(mutex_);
    state_ = state ? state : "";
}

void OledStatus::SetChat(const char* role, const char* content) {
    std::lock_guard<std::mutex> l(mutex_);
    chat_role_ = role ? role : "";
    chat_text_ = content ? content : "";
}

void OledStatus::Task(void* arg) {
    auto* self = static_cast<OledStatus*>(arg);
    while (true) {
        self->Render();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void OledStatus::Clear() { memset(fb_, 0, sizeof(fb_)); }

void OledStatus::Text(int col, int row, const std::string& s) {
    // page layout: byte = 8 vertical pixels of one column; row == page
    int x = col * 6;
    for (char ch : s) {
        if (x + 6 > OLED_WIDTH || row > 7) break;
        unsigned char c = (unsigned char)ch;
        if (c < 32 || c > 126) c = '?';                 // non-ASCII (e.g. Chinese) -> '?'
        memcpy(&fb_[row * OLED_WIDTH + x], kOledFont6x8[c - 32], 6);
        x += 6;
    }
}

void OledStatus::Flush() {
    esp_lcd_panel_draw_bitmap(panel_, 0, 0, OLED_WIDTH, OLED_HEIGHT, fb_);
}

void OledStatus::Render() {
    if (!ready_) return;
    auto& robot = MdeeRobot::GetInstance();
    char line[32];

    // Wi-Fi
    int rssi = 0;
    wifi_ap_record_t ap;
    bool wifi_ok = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    if (wifi_ok) rssi = ap.rssi;
    int bars = !wifi_ok ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;

    std::string state, role, text;
    {
        std::lock_guard<std::mutex> l(mutex_);
        state = state_; role = chat_role_; text = chat_text_;
    }

    Clear();
    auto& lb = LogicBattery::GetInstance();
    // row 0: wifi + mode
    if (!robot.LinkUp()) snprintf(line, sizeof(line), "WiFi %.*s%.*s NO BODY LINK", bars, "||||", 4 - bars, "....");
    else snprintf(line, sizeof(line), "WiFi %.*s%.*s %s", bars, "||||", 4 - bars, "....", MdeeRobot::ModeName(robot.GetMode()));
    Text(0, 0, line);

    // row 1: motor pack (M) and logic pack (L)
    char m[16], l[16];
    float v = robot.BatteryVolts();
    if (robot.LinkUp() && v > 4.0f) snprintf(m, sizeof(m), "M%4.1fV%3d%%", v, robot.BatteryPercent());
    else snprintf(m, sizeof(m), "M --.-V   ");
    if (lb.Connected()) snprintf(l, sizeof(l), "L%3.1fV%3d%%", lb.Volts(), lb.Percent());
    else snprintf(l, sizeof(l), "L --.-V   ");
    snprintf(line, sizeof(line), "%s %s", m, l);
    Text(0, 1, line);

    // row 2: distances, or a blinking battery alert
    int f = robot.FrontCm(), r = robot.RearCm();
    bool blink = (esp_timer_get_time() / 500000) & 1;
    if (lb.Critical()) Text(0, 2, "CHARGE NOW! STOPPED");
    else if (lb.Low()) { if (blink) Text(0, 2, "!! LOGIC BATT LOW !!"); }
    else {
        snprintf(line, sizeof(line), "Front %3s  Rear %3s",
                 f >= 400 ? "--" : std::to_string(f).c_str(), r >= 400 ? "--" : std::to_string(r).c_str());
        Text(0, 2, line);
    }
    // row 3: assistant state
    Text(0, 3, state.substr(0, 21));
    for (int x = 0; x < OLED_WIDTH; x++) fb_[3 * OLED_WIDTH + x] |= 0x80;   // separator under row 3

    // rows 4-7: last chat text, word-wrapped 21 chars/line
    if (text.empty()) {
        esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip;
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
            snprintf(line, sizeof(line), IPSTR, IP2STR(&ip.ip));
            Text(0, 5, "Phone control page:");
            Text(0, 6, std::string("http://") + line);
        }
    }
    std::string head = role == "user" ? "You: " : (role.empty() ? "" : "M'DEE: ");
    std::string full = head + text;
    int row = 4;
    size_t pos = 0;
    while (pos < full.size() && row <= 7) {
        size_t len = std::min<size_t>(21, full.size() - pos);
        if (pos + len < full.size()) {                    // try to break at a space
            size_t sp = full.rfind(' ', pos + len);
            if (sp != std::string::npos && sp > pos) len = sp - pos;
        }
        Text(0, row++, full.substr(pos, len));
        pos += len;
        while (pos < full.size() && full[pos] == ' ') pos++;
    }
    if (pos < full.size() && row > 7) Text(18, 7, "...");   // overflow marker
    Flush();
}
