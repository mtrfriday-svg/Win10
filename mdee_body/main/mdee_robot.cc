#include "mdee_robot.h"
#include "body_config.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define TAG "MdeeRobot"

static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }
static inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// LEDC allocation: timer0 = motors (20 kHz), timer1 = servos (50 Hz)
#define MOTOR_TIMER   LEDC_TIMER_0
#define SERVO_TIMER   LEDC_TIMER_1
#define MOTOR_CH_L    LEDC_CHANNEL_0
#define MOTOR_CH_R    LEDC_CHANNEL_1
#define SERVO_CH_BASE 2
#define MOTOR_DUTY_BITS 10
#define SERVO_DUTY_BITS 14
#define MOTOR_MIN_START_PCT 22   // TT motors stall below this; scaled so 1% -> ~22%
#define RAMP_STEP_PCT 8          // per 20 ms tick -> gentle acceleration

MdeeRobot& MdeeRobot::GetInstance() {
    static MdeeRobot instance;
    return instance;
}

// ---------------------------------------------------------------- init
void MdeeRobot::Init() {
    if (inited_) return;
    inited_ = true;
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = (i2c_port_t)0;
    bc.sda_io_num = I2C_SDA_PIN;
    bc.scl_io_num = I2C_SCL_PIN;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bc, &i2c_bus_) != ESP_OK) i2c_bus_ = nullptr;
    InitMotors();
    InitServos();
    InitUltrasonic();
    InitLights(i2c_bus_);
    InitBattery();
    xTaskCreate(&MdeeRobot::ControlTask, "mdee_ctl", 4096, this, 6, nullptr);
    xTaskCreate(&MdeeRobot::SensorTask, "mdee_sns", 3072, this, 4, nullptr);
    ESP_LOGI(TAG, "Robot ready");
}

void MdeeRobot::InitMotors() {
    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << MOTOR_AIN1) | (1ULL << MOTOR_AIN2) | (1ULL << MOTOR_BIN1) | (1ULL << MOTOR_BIN2);
    gpio_config(&io);
    gpio_config_t sb = {};
    sb.mode = GPIO_MODE_OUTPUT;
    sb.pin_bit_mask = 1ULL << MOTOR_STBY;
    gpio_config(&sb);
    gpio_set_level(MOTOR_STBY, 0);                       // 10k pulldown keeps it low at boot too

    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = (ledc_timer_bit_t)MOTOR_DUTY_BITS;
    t.timer_num = MOTOR_TIMER;
    t.freq_hz = 20000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {};
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.timer_sel = MOTOR_TIMER;
    c.duty = 0;
    c.hpoint = 0;
    c.channel = MOTOR_CH_L;
    c.gpio_num = MOTOR_PWMA;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    c.channel = MOTOR_CH_R;
    c.gpio_num = MOTOR_PWMB;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    SetMotorRaw(0, 0);
    gpio_set_level(MOTOR_STBY, 1);                       // driver enabled
}

void MdeeRobot::InitServos() {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = (ledc_timer_bit_t)SERVO_DUTY_BITS;
    t.timer_num = SERVO_TIMER;
    t.freq_hz = 50;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    const gpio_num_t pins[3] = {SERVO_PAN_PIN, SERVO_TILT_PIN, SERVO_AUX_PIN};
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t c = {};
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.timer_sel = SERVO_TIMER;
        c.channel = (ledc_channel_t)(SERVO_CH_BASE + i);
        c.gpio_num = pins[i];
        c.duty = 0;
        c.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&c));
        ServoWrite(i, 90);
    }
}

void MdeeRobot::InitUltrasonic() {
    gpio_config_t out = {};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = 1ULL << US_TRIG_PIN;
    gpio_config(&out);
    gpio_set_level(US_TRIG_PIN, 0);

    gpio_config_t in = {};
    in.mode = GPIO_MODE_INPUT;
    in.pull_down_en = GPIO_PULLDOWN_ENABLE;
    in.pin_bit_mask = (1ULL << US_ECHO_FRONT_PIN) | (1ULL << US_ECHO_REAR_PIN);
    gpio_config(&in);
}

void MdeeRobot::InitLights(i2c_master_bus_handle_t bus) {
    // 2x WS2812 ("NeoPixel")
    led_strip_config_t sc = {};
    sc.strip_gpio_num = NEOPIXEL_PIN;
    sc.max_leds = NEOPIXEL_COUNT;
    sc.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    sc.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rc = {};
    rc.resolution_hz = 10 * 1000 * 1000;
    if (led_strip_new_rmt_device(&sc, &rc, &neo_strip_) == ESP_OK) {
        led_strip_clear(neo_strip_);
    } else {
        ESP_LOGW(TAG, "NeoPixel init failed");
        neo_strip_ = nullptr;
    }

    // 8 standard LEDs on a PCF8574 (active low: 3V3 -> resistor -> LED -> pin)
    if (bus != nullptr && i2c_master_probe(bus, PCF8574_ADDR, 100) == ESP_OK) {
        i2c_device_config_t dev = {};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address = PCF8574_ADDR;
        dev.scl_speed_hz = 100000;
        if (i2c_master_bus_add_device(bus, &dev, &pcf_) != ESP_OK) pcf_ = nullptr;
    } else {
        ESP_LOGW(TAG, "PCF8574 not found at 0x%02X - LED bar disabled", PCF8574_ADDR);
    }
    WritePcf(0);
}

void MdeeRobot::InitBattery() {
    adc_oneshot_unit_init_cfg_t uc = {};
    uc.unit_id = BATTERY_ADC_UNIT;
    if (adc_oneshot_new_unit(&uc, &adc_) != ESP_OK) { adc_ = nullptr; return; }
    adc_oneshot_chan_cfg_t cc = {};
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc_, BATTERY_ADC_CHANNEL, &cc);

    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = BATTERY_ADC_UNIT;
    cal.chan = BATTERY_ADC_CHANNEL;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cal, &adc_cali_) != ESP_OK) adc_cali_ = nullptr;
}

// ---------------------------------------------------------------- low-level
void MdeeRobot::SetMotorRaw(int left_pct, int right_pct) {
    auto apply = [](int pct, bool invert, gpio_num_t in1, gpio_num_t in2, ledc_channel_t ch) {
        if (invert) pct = -pct;
        int mag = std::abs(pct);
        if (mag == 0) {
            gpio_set_level(in1, 0);
            gpio_set_level(in2, 0);   // coast
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, 0);
        } else {
            gpio_set_level(in1, pct > 0 ? 1 : 0);
            gpio_set_level(in2, pct > 0 ? 0 : 1);
            int eff = MOTOR_MIN_START_PCT + (mag * (100 - MOTOR_MIN_START_PCT)) / 100;
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, (eff * ((1 << MOTOR_DUTY_BITS) - 1)) / 100);
        }
        ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
    };
    apply(left_pct, MOTOR_LEFT_INVERT, MOTOR_AIN1, MOTOR_AIN2, MOTOR_CH_L);
    apply(right_pct, MOTOR_RIGHT_INVERT, MOTOR_BIN1, MOTOR_BIN2, MOTOR_CH_R);
}

void MdeeRobot::ServoWrite(int index, int angle) {
    angle = Clamp(angle, 0, 180);
    int pulse_us = 500 + (angle * 1900) / 180;               // SG90: ~0.5-2.4 ms
    uint32_t duty = ((uint32_t)pulse_us * (1 << SERVO_DUTY_BITS)) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(SERVO_CH_BASE + index), duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(SERVO_CH_BASE + index));
}

int MdeeRobot::MeasureCm(gpio_num_t echo) {
    gpio_set_level(US_TRIG_PIN, 0);
    esp_rom_delay_us(3);
    gpio_set_level(US_TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(US_TRIG_PIN, 0);

    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(echo) == 0) {
        if (esp_timer_get_time() - t0 > 6000) return 400;
    }
    int64_t rise = esp_timer_get_time();
    while (gpio_get_level(echo) == 1) {
        if (esp_timer_get_time() - rise > 15000) return 400;   // ~2.5 m
    }
    int cm = (int)((esp_timer_get_time() - rise) / 58);
    return cm < 2 ? 400 : cm;                                   // <2 cm = garbage
}

void MdeeRobot::WritePcf(uint8_t leds_on_mask) {
    if (pcf_ == nullptr) return;
    uint8_t v = ~leds_on_mask;
    if (v == pcf_last_) return;
    if (i2c_master_transmit(pcf_, &v, 1, 20) == ESP_OK) pcf_last_ = v;
}

// ---------------------------------------------------------------- public API
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
        case MdeeDir::kLeft:     Drive(-s, s, duration_ms); break;    // pivot on the spot
        case MdeeDir::kRight:    Drive(s, -s, duration_ms); break;
        case MdeeDir::kStop:     Stop(); break;
    }
}

void MdeeRobot::Drive(int left_pct, int right_pct, int hold_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = MdeeMode::kManual;                     // user input always overrides autonomy
    cmd_left_ = Clamp(left_pct, -100, 100);
    cmd_right_ = Clamp(right_pct, -100, 100);
    // hold_ms == 0 means "until Stop()" but we still cap it at 60 s for safety.
    drive_deadline_ms_ = NowMs() + (hold_ms > 0 ? Clamp(hold_ms, 50, 60000) : 60000);
}

void MdeeRobot::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = MdeeMode::kManual;
    cmd_left_ = cmd_right_ = 0;
    drive_deadline_ms_ = 0;
}

void MdeeRobot::SetMode(MdeeMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
    cmd_left_ = cmd_right_ = 0;
    drive_deadline_ms_ = 0;
    wander_state_ = W::kCruise;
    avoid_count_ = 0;
}

void MdeeRobot::SetServo(int index, int angle) {
    if (index < 0 || index > 2) return;
    if (index == 2) {                                   // ears: fixed angle
        ear_hold_angle_ = Clamp(angle, 0, 180);
        ear_mode_ = MdeeEar::kHold;
        ear_until_ms_ = 0;
        return;
    }
    servo_target_[index] = Clamp(angle, 0, 180);
}

void MdeeRobot::Ears(MdeeEar g, int duration_ms) {
    int64_t now = NowMs();
    ear_mode_ = g;
    ear_start_ms_ = now;
    if (g == MdeeEar::kWiggle) ear_until_ms_ = now + (duration_ms > 0 ? Clamp(duration_ms, 200, 20000) : 1500);
    else if (g == MdeeEar::kFlick) ear_until_ms_ = now + 450;
    else ear_until_ms_ = duration_ms > 0 ? now + Clamp(duration_ms, 200, 60000) : 0;
}

int MdeeRobot::EarTarget(int64_t now) {
    const int N = EAR_NEUTRAL, P = EAR_PERK, D = EAR_DROOP;
    if (ear_until_ms_ != 0 && now > ear_until_ms_) { ear_mode_ = MdeeEar::kAuto; ear_until_ms_ = 0; }
    int t = N;
    switch (ear_mode_) {
        case MdeeEar::kNeutral: t = N; break;
        case MdeeEar::kPerk: t = P; break;
        case MdeeEar::kDroop: t = D; break;
        case MdeeEar::kWiggle: t = N + (int)(EAR_WIGGLE_DEG * sinf(now / 70.0f)); break;
        case MdeeEar::kFlick: {
            int phase = (int)((now - ear_start_ms_) / 150);          // back, forward, rest
            t = phase == 0 ? N + (D - N) * 6 / 10 : (phase == 1 ? N + (P - N) * 6 / 10 : N);
            break;
        }
        case MdeeEar::kHold: t = ear_hold_angle_; break;
        case MdeeEar::kAuto:
        default:
            // idle "pet" life: a small ear flick every 5-12 s; droop when the battery is nearly empty
            t = (battery_cutoff_ || (battery_v_ > 4.0f && BatteryPercent() < 10)) ? D : N;
            if (now > next_ear_twitch_ms_) {
                next_ear_twitch_ms_ = now + 5000 + (esp_random() % 7000);
                ear_mode_ = MdeeEar::kFlick;
                ear_start_ms_ = now;
                ear_until_ms_ = now + 450;
            }
            break;
    }
    if (EAR_INVERT) t = 180 - t;
    return Clamp(t, 0, 180);
}

void MdeeRobot::Head(int pan, int tilt) {
    SetServo(0, pan);
    SetServo(1, tilt);
}

int MdeeRobot::BatteryPercent() const {
    float v = battery_v_;
    int p = (int)((v - BATTERY_EMPTY_V) * 100.0f / (BATTERY_FULL_V - BATTERY_EMPTY_V));
    return Clamp(p, 0, 100);
}

void MdeeRobot::SetLights(MdeeLights p) { lights_ = p; }

void MdeeRobot::SetNeo(MdeeNeo effect, uint8_t r, uint8_t g, uint8_t b) {
    neo_ = effect;
    neo_r_ = r; neo_g_ = g; neo_b_ = b;
}

std::string MdeeRobot::StatusJson() {
    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"mode\":\"%s\",\"front_cm\":%d,\"rear_cm\":%d,\"battery_v\":%.2f,"
             "\"battery_pct\":%d,\"battery_cutoff\":%s,\"left\":%d,\"right\":%d,"
             "\"pan\":%d,\"tilt\":%d,\"aux\":%d}",
             ModeName(mode_), (int)front_cm_, (int)rear_cm_, (float)battery_v_,
             BatteryPercent(), battery_cutoff_ ? "true" : "false", out_left_, out_right_,
             servo_target_[0], servo_target_[1], servo_target_[2]);
    return buf;
}

// ---------------------------------------------------------------- tasks
void MdeeRobot::SensorTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    int fbuf[3] = {400, 400, 400}, rbuf[3] = {400, 400, 400};
    int idx = 0, low_count = 0, loops = 0;
    while (true) {
        fbuf[idx] = self->MeasureCm(US_ECHO_FRONT_PIN);
        vTaskDelay(pdMS_TO_TICKS(35));
        rbuf[idx] = self->MeasureCm(US_ECHO_REAR_PIN);
        idx = (idx + 1) % 3;
        auto median = [](int* b) {
            int a[3] = {b[0], b[1], b[2]};
            std::sort(a, a + 3);
            return a[1];
        };
        self->front_cm_ = median(fbuf);
        self->rear_cm_ = median(rbuf);
        vTaskDelay(pdMS_TO_TICKS(35));

        if (self->adc_ != nullptr && ++loops % 7 == 0) {            // ~every 0.5 s
            long sum = 0; int n = 0;
            for (int i = 0; i < 16; i++) {
                int raw = 0;
                if (adc_oneshot_read(self->adc_, BATTERY_ADC_CHANNEL, &raw) == ESP_OK) { sum += raw; n++; }
            }
            if (n > 0) {
                int mv = 0;
                int raw = (int)(sum / n);
                if (self->adc_cali_ && adc_cali_raw_to_voltage(self->adc_cali_, raw, &mv) == ESP_OK) {}
                else mv = raw * 3100 / 4095;
                float v = (mv / 1000.0f) * (BATTERY_R_UPPER_OHM + BATTERY_R_LOWER_OHM) / BATTERY_R_LOWER_OHM * BATTERY_CAL_FACTOR;
                float old = self->battery_v_;
                self->battery_v_ = (old > 4.0f && v > 4.0f) ? (old * 0.8f + v * 0.2f) : v;
                // below 4 V = no battery connected (running from USB): never cut off
                if (v > 4.0f && v < BATTERY_EMPTY_V - 0.3f) low_count++; else low_count = 0;
                self->battery_cutoff_ = low_count >= 10;                // ~5 s below threshold
            }
        }
    }
}

void MdeeRobot::ControlTask(void* arg) {
    auto* self = static_cast<MdeeRobot*>(arg);
    while (true) {
        self->ControlStep();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void MdeeRobot::ControlStep() {
    int64_t now = NowMs();
    int tl, tr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_ == MdeeMode::kWander) WanderStep(now);
        else if (mode_ == MdeeMode::kFollow) FollowStep();
        else if (drive_deadline_ms_ != 0 && now > drive_deadline_ms_) {   // deadman
            cmd_left_ = cmd_right_ = 0;
            drive_deadline_ms_ = 0;
        }
        tl = cmd_left_;
        tr = cmd_right_;
    }

    // ---- safety layer ----
    int front = front_cm_, rear = rear_cm_;
    bool pivot = (tl * tr) < 0;
    if (!pivot) {
        if (tl + tr > 0) {                                  // moving forward
            if (front < OBSTACLE_STOP_CM) { tl = tr = 0; }
            else if (front < OBSTACLE_SLOW_CM) {
                int pct = 40 + 60 * (front - OBSTACLE_STOP_CM) / (OBSTACLE_SLOW_CM - OBSTACLE_STOP_CM);
                tl = tl * pct / 100; tr = tr * pct / 100;
            }
        } else if (tl + tr < 0) {                           // reversing: rear sensor
            if (rear < OBSTACLE_STOP_CM) { tl = tr = 0; }
            else if (rear < OBSTACLE_SLOW_CM) {
                int pct = 40 + 60 * (rear - OBSTACLE_STOP_CM) / (OBSTACLE_SLOW_CM - OBSTACLE_STOP_CM);
                tl = tl * pct / 100; tr = tr * pct / 100;
            }
        }
    }
    if (battery_cutoff_) tl = tr = 0;

    // ---- ramp (instant when stopping) ----
    auto ramp = [](int cur, int target) {
        if (target == 0) return 0;
        if (target > cur) return std::min(cur + RAMP_STEP_PCT, target);
        return std::max(cur - RAMP_STEP_PCT, target);
    };
    int nl = ramp(out_left_, tl), nr = ramp(out_right_, tr);
    if (nl != out_left_ || nr != out_right_) {
        out_left_ = nl; out_right_ = nr;
        SetMotorRaw(out_left_, out_right_);
    }

    // ---- servo slew (3 deg per tick) + idle fidget ----
    bool idle = (out_left_ == 0 && out_right_ == 0);
    if (fidget_ && idle && mode_ == MdeeMode::kManual && now > next_fidget_ms_) {
        next_fidget_ms_ = now + 7000 + (esp_random() % 9000);
        if (servo_target_[0] == servo_cur_[0] && servo_target_[1] == servo_cur_[1]) {
            servo_target_[0] = 90 + (int)(esp_random() % 61) - 30;
            servo_target_[1] = 90 + (int)(esp_random() % 21) - 10;
        }
    }
    servo_target_[2] = EarTarget(now);
    for (int i = 0; i < 3; i++) {
        if (servo_cur_[i] != servo_target_[i]) {
            int d = servo_target_[i] - servo_cur_[i];
            int step = (i == 2) ? 9 : 3;                    // ears are quick, neck is smooth
            servo_cur_[i] += Clamp(d, -step, step);
            ServoWrite(i, servo_cur_[i]);
        }
    }

    LightsStep(now);
}

void MdeeRobot::WanderStep(int64_t now) {
    const int cruise = SpeedPct(MdeeSpeed::kMedium);
    int front = front_cm_, rear = rear_cm_;
    switch (wander_state_) {
        case W::kCruise:
            cmd_left_ = cmd_right_ = cruise;
            if (front < OBSTACLE_STOP_CM + 6) {
                if (now - avoid_window_start_ms_ > 6000) { avoid_window_start_ms_ = now; avoid_count_ = 0; }
                avoid_count_++;
                wander_turn_dir_ = (esp_random() & 1) ? 1 : -1;
                if (rear > OBSTACLE_STOP_CM + 10) {
                    wander_state_ = W::kBackup;
                    wander_until_ms_ = now + (avoid_count_ >= 3 ? 900 : 450);   // stuck? back up more
                } else {
                    wander_state_ = W::kTurn;
                    wander_until_ms_ = now + 500 + (esp_random() % 500);
                }
            }
            break;
        case W::kBackup:
            cmd_left_ = cmd_right_ = -SpeedPct(MdeeSpeed::kSlow);
            if (now > wander_until_ms_ || rear < OBSTACLE_STOP_CM + 4) {
                wander_state_ = W::kTurn;
                wander_until_ms_ = now + 450 + (esp_random() % 650) + (avoid_count_ >= 3 ? 500 : 0);
            }
            break;
        case W::kTurn: {
            int s = SpeedPct(MdeeSpeed::kMedium);
            cmd_left_ = -s * wander_turn_dir_;
            cmd_right_ = s * wander_turn_dir_;
            if (now > wander_until_ms_) wander_state_ = W::kCruise;
            break;
        }
    }
}

void MdeeRobot::FollowStep() {
    // Keeps ~30 cm from whatever is in front (hand, person, ball). Ultrasonic is a cone, not a
    // camera, so it follows "the nearest thing ahead", not a specific person.
    int d = front_cm_;
    if (d >= 90) { cmd_left_ = cmd_right_ = 0; }
    else if (d > 40) { int s = d > 65 ? SPEED_MEDIUM_PCT : SPEED_SLOW_PCT; cmd_left_ = cmd_right_ = s; }
    else if (d < 22 && rear_cm_ > OBSTACLE_STOP_CM + 8) { cmd_left_ = cmd_right_ = -SPEED_SLOW_PCT; }
    else { cmd_left_ = cmd_right_ = 0; }
}

void MdeeRobot::LightsStep(int64_t now) {
    // ---- 8 LED bar: bits 0-3 = left side, bits 4-7 = right side ----
    uint8_t mask = 0;
    switch (lights_) {
        case MdeeLights::kOff: mask = 0; break;
        case MdeeLights::kOn: mask = 0xFF; break;
        case MdeeLights::kChase: {
            int p = (now / 90) % 14;
            int i = p < 8 ? p : 14 - p;
            mask = 1 << i;
            break;
        }
        case MdeeLights::kBlink: mask = ((now / 400) & 1) ? 0xFF : 0; break;
        case MdeeLights::kSignalLeft: mask = ((now / 350) & 1) ? 0x0F : 0; break;
        case MdeeLights::kSignalRight: mask = ((now / 350) & 1) ? 0xF0 : 0; break;
        case MdeeLights::kBatteryBar: {
            int n = (BatteryPercent() * 8 + 99) / 100;
            mask = n >= 8 ? 0xFF : (uint8_t)((1 << n) - 1);
            break;
        }
    }
    WritePcf(mask);

    // ---- NeoPixels ----
    static int64_t last_neo = 0;
    if (neo_strip_ != nullptr && now - last_neo >= 40) {
        last_neo = now;
        switch (neo_) {
            case MdeeNeo::kOff: led_strip_clear(neo_strip_); break;
            case MdeeNeo::kSolid:
                for (int i = 0; i < NEOPIXEL_COUNT; i++) led_strip_set_pixel(neo_strip_, i, neo_r_, neo_g_, neo_b_);
                led_strip_refresh(neo_strip_);
                break;
            case MdeeNeo::kBreathe: {
                float k = 0.15f + 0.85f * (0.5f + 0.5f * sinf(now / 450.0f));
                for (int i = 0; i < NEOPIXEL_COUNT; i++)
                    led_strip_set_pixel(neo_strip_, i, neo_r_ * k, neo_g_ * k, neo_b_ * k);
                led_strip_refresh(neo_strip_);
                break;
            }
            case MdeeNeo::kRainbow:
                for (int i = 0; i < NEOPIXEL_COUNT; i++)
                    led_strip_set_pixel_hsv(neo_strip_, i, (uint16_t)((now / 8 + i * 60) % 360), 255, 60);
                led_strip_refresh(neo_strip_);
                break;
        }
    }
}

std::string MdeeRobot::StatusLine() {
    char buf[128];
    snprintf(buf, sizeof(buf), "T %d %d %d %d %d %d %d %d %d %d %d\n", (int)front_cm_, (int)rear_cm_,
             (int)(battery_v_ * 1000.0f), BatteryPercent(), battery_cutoff_ ? 1 : 0, out_left_,
             out_right_, (int)mode_, servo_target_[0], servo_target_[1], servo_target_[2]);
    return buf;
}
