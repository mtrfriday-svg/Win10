#include "mdee_robot.h"
#include "config.h"
#include "logic_battery.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#define TAG "MdeeLink"

static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }
static inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

MdeeRobot& MdeeRobot::GetInstance() {
    static MdeeRobot instance;
    return instance;
}

void MdeeRobot::Init(i2c_master_bus_handle_t) {
    if (inited_) return;
    inited_ = true;
    uart_config_t cfg = {};
    cfg.baud_rate = BODY_LINK_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(BODY_LINK_UART, 1024, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(BODY_LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BODY_LINK_UART, BODY_LINK_TX_PIN, BODY_LINK_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(&MdeeRobot::RxTask, "body_rx", 4096, this, 5, nullptr);
    xTaskCreate(&MdeeRobot::PingTask, "body_ping", 2048, this, 3, nullptr);
    ESP_LOGI(TAG, "Body link on UART%d TX=%d RX=%d", (int)BODY_LINK_UART, (int)BODY_LINK_TX_PIN, (int)BODY_LINK_RX_PIN);
}

void MdeeRobot::Send(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    buf[n++] = '\n';
    std::lock_guard<std::mutex> l(tx_mutex_);
    uart_write_bytes(BODY_LINK_UART, buf, n);
}

void MdeeRobot::PingTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    while (true) {
        self->Send("PING");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void MdeeRobot::RxTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    char buf[128];
    size_t len = 0;
    uint8_t ch;
    while (true) {
        if (uart_read_bytes(BODY_LINK_UART, &ch, 1, pdMS_TO_TICKS(200)) != 1) continue;
        if (ch == '\n' || ch == '\r') {
            if (len > 0) { buf[len] = 0; self->ParseStatus(buf); len = 0; }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = (char)ch;
        } else {
            len = 0;
        }
    }
}

void MdeeRobot::ParseStatus(const char* line) {
    int f, r, mv, pct, cut, l, rt, m, pan, tilt, aux;
    if (sscanf(line, "T %d %d %d %d %d %d %d %d %d %d %d", &f, &r, &mv, &pct, &cut, &l, &rt, &m, &pan, &tilt, &aux) == 11) {
        front_cm_ = f; rear_cm_ = r; battery_mv_ = mv; battery_pct_ = pct; cutoff_ = cut != 0;
        left_ = l; right_ = rt; pan_ = pan; tilt_ = tilt; aux_ = aux;
        if (m >= 0 && m <= 2) mode_ = (MdeeMode)m;
        last_status_ms_ = NowMs();
    }
}

bool MdeeRobot::LinkUp() const {
    int64_t t = last_status_ms_;
    return t != 0 && NowMs() - t < 1500;
}

// ---------------------------------------------------------------- same API as before
int MdeeRobot::SpeedPct(MdeeSpeed s) {
    switch (s) {
        case MdeeSpeed::kSlow: return SPEED_SLOW_PCT;
        case MdeeSpeed::kMedium: return SPEED_MEDIUM_PCT;
        case MdeeSpeed::kFast: return SPEED_FAST_PCT;
        case MdeeSpeed::kTurbo: return SPEED_TURBO_PCT;
    }
    return SPEED_MEDIUM_PCT;
}

bool MdeeRobot::ParseSpeed(const std::string& n, MdeeSpeed& out) {
    if (n == "slow") out = MdeeSpeed::kSlow;
    else if (n == "medium" || n == "normal") out = MdeeSpeed::kMedium;
    else if (n == "fast") out = MdeeSpeed::kFast;
    else if (n == "turbo" || n == "high" || n == "high_speed") out = MdeeSpeed::kTurbo;
    else return false;
    return true;
}

bool MdeeRobot::ParseDir(const std::string& n, MdeeDir& out) {
    if (n == "forward") out = MdeeDir::kForward;
    else if (n == "backward" || n == "back" || n == "reverse") out = MdeeDir::kBackward;
    else if (n == "left") out = MdeeDir::kLeft;
    else if (n == "right") out = MdeeDir::kRight;
    else if (n == "stop") out = MdeeDir::kStop;
    else return false;
    return true;
}

const char* MdeeRobot::ModeName(MdeeMode m) {
    switch (m) {
        case MdeeMode::kManual: return "manual";
        case MdeeMode::kWander: return "wander";
        case MdeeMode::kFollow: return "follow";
    }
    return "manual";
}

bool MdeeRobot::ParseMode(const std::string& n, MdeeMode& out) {
    if (n == "manual" || n == "idle") out = MdeeMode::kManual;
    else if (n == "wander" || n == "explore") out = MdeeMode::kWander;
    else if (n == "follow") out = MdeeMode::kFollow;
    else return false;
    return true;
}

void MdeeRobot::Move(MdeeDir dir, MdeeSpeed speed, int duration_ms) {
    int s = SpeedPct(speed);
    switch (dir) {
        case MdeeDir::kForward:  Drive(s, s, duration_ms); break;
        case MdeeDir::kBackward: Drive(-s, -s, duration_ms); break;
        case MdeeDir::kLeft:     Drive(-s, s, duration_ms); break;
        case MdeeDir::kRight:    Drive(s, -s, duration_ms); break;
        case MdeeDir::kStop:     Stop(); break;
    }
}

void MdeeRobot::Drive(int left_pct, int right_pct, int hold_ms) {
    mode_ = MdeeMode::kManual;
    if (LogicBattery::GetInstance().Critical()) { Send("S"); return; }   // brain battery nearly dead
    Send("D %d %d %d", Clamp(left_pct, -100, 100), Clamp(right_pct, -100, 100), hold_ms);
}

void MdeeRobot::Stop() {
    mode_ = MdeeMode::kManual;
    Send("S");
}

void MdeeRobot::SetMode(MdeeMode mode) {
    if (mode != MdeeMode::kManual && LogicBattery::GetInstance().Critical()) mode = MdeeMode::kManual;
    mode_ = mode;
    Send("MODE %s", ModeName(mode));
}

void MdeeRobot::SetServo(int index, int angle) { Send("SV %d %d", index, Clamp(angle, 0, 180)); }
void MdeeRobot::Head(int pan, int tilt) { SetServo(0, pan); SetServo(1, tilt); }
void MdeeRobot::Fidget(bool enable) { Send("FID %d", enable ? 1 : 0); }
void MdeeRobot::Ears(MdeeEar g, int duration_ms) { Send("EAR %d %d", (int)g, duration_ms); }

bool MdeeRobot::ParseEar(const std::string& n, MdeeEar& out) {
    if (n == "auto" || n == "idle") out = MdeeEar::kAuto;
    else if (n == "neutral" || n == "relaxed") out = MdeeEar::kNeutral;
    else if (n == "perk" || n == "up" || n == "alert" || n == "forward") out = MdeeEar::kPerk;
    else if (n == "droop" || n == "down" || n == "back" || n == "sad") out = MdeeEar::kDroop;
    else if (n == "wiggle" || n == "wag" || n == "happy") out = MdeeEar::kWiggle;
    else if (n == "flick" || n == "twitch") out = MdeeEar::kFlick;
    else return false;
    return true;
}

void MdeeRobot::SetLights(MdeeLights p) { Send("LT %d", (int)p); }
void MdeeRobot::SetNeo(MdeeNeo e, uint8_t r, uint8_t g, uint8_t b) { Send("NEO %d %d %d %d", (int)e, r, g, b); }

std::string MdeeRobot::StatusJson() {
    char buf[420];
    snprintf(buf, sizeof(buf),
             "{\"logic_v\":%.2f,\"logic_pct\":%d,\"logic_low\":%s,\"link\":%s,\"mode\":\"%s\",\"front_cm\":%d,\"rear_cm\":%d,\"battery_v\":%.2f,"
             "\"battery_pct\":%d,\"battery_cutoff\":%s,\"left\":%d,\"right\":%d,"
             "\"pan\":%d,\"tilt\":%d,\"aux\":%d}",
             LogicBattery::GetInstance().Volts(), LogicBattery::GetInstance().Percent(),
             LogicBattery::GetInstance().Low() ? "true" : "false",
             LinkUp() ? "true" : "false", ModeName(mode_), (int)front_cm_, (int)rear_cm_,
             BatteryVolts(), (int)battery_pct_, cutoff_ ? "true" : "false", (int)left_, (int)right_,
             (int)pan_, (int)tilt_, (int)aux_);
    return buf;
}
