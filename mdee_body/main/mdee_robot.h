#ifndef MDEE_ROBOT_H
#define MDEE_ROBOT_H

#include <driver/i2c_master.h>
#include <led_strip.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <mutex>
#include <string>

enum class MdeeDir { kForward, kBackward, kLeft, kRight, kStop };
enum class MdeeSpeed { kSlow, kMedium, kFast, kTurbo };
enum class MdeeMode { kManual, kWander, kFollow };
enum class MdeeLights { kOff, kOn, kChase, kBlink, kSignalLeft, kSignalRight, kBatteryBar };
enum class MdeeNeo { kOff, kSolid, kBreathe, kRainbow };
// Ear behaviours (servo index 2). kHold = fixed angle from SetServo(2, angle).
enum class MdeeEar { kAuto, kNeutral, kPerk, kDroop, kWiggle, kFlick, kHold };

// Everything physical on the robot: motors, servos, ultrasonics, lights, battery, autonomy.
class MdeeRobot {
public:
    static MdeeRobot& GetInstance();

    // Creates its own I2C bus (PCF8574 LED bar). Safe to call once.
    void Init();
    // One status line for the brain: "T front rear mv pct cutoff left right mode pan tilt aux"
    std::string StatusLine();

    // ---- Driving ----
    // duration_ms == 0 -> keep going until Stop()/deadman. Blocked by obstacles/low battery.
    void Move(MdeeDir dir, MdeeSpeed speed, int duration_ms);
    // Raw differential drive for joystick: -100..100 each side. hold_ms refreshes the deadman.
    void Drive(int left_pct, int right_pct, int hold_ms);
    void Stop();
    static int SpeedPct(MdeeSpeed s);
    static bool ParseSpeed(const std::string& name, MdeeSpeed& out);
    static bool ParseDir(const std::string& name, MdeeDir& out);

    // ---- Autonomy ----
    void SetMode(MdeeMode mode);
    MdeeMode GetMode() const { return mode_; }
    static const char* ModeName(MdeeMode m);
    static bool ParseMode(const std::string& name, MdeeMode& out);

    // ---- Servos (0 = pan, 1 = tilt, 2 = aux), angle 0..180 ----
    void SetServo(int index, int angle);
    void Head(int pan, int tilt);
    void Fidget(bool enable) { fidget_ = enable; }
    // Ear gestures. duration_ms 0 = stay until changed (wiggle/flick default to a short burst).
    void Ears(MdeeEar gesture, int duration_ms);

    // ---- Sensors ----
    int FrontCm() const { return front_cm_; }   // 400 = nothing seen
    int RearCm() const { return rear_cm_; }
    float BatteryVolts() const { return battery_v_; }
    int BatteryPercent() const;

    // ---- Lights ----
    void SetLights(MdeeLights pattern);
    void SetNeo(MdeeNeo effect, uint8_t r, uint8_t g, uint8_t b);

    std::string StatusJson();

private:
    MdeeRobot() = default;

    void InitMotors();
    void InitServos();
    void InitUltrasonic();
    void InitLights(i2c_master_bus_handle_t bus);
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    void InitBattery();

    void SetMotorRaw(int left_pct, int right_pct);
    void ServoWrite(int index, int angle);
    int MeasureCm(gpio_num_t echo);

    static void ControlTask(void* arg);   // 20 ms: deadman, safety, autonomy, lights
    static void SensorTask(void* arg);    // ultrasonic + battery
    void ControlStep();
    void WanderStep(int64_t now_ms);
    void FollowStep();
    void LightsStep(int64_t now_ms);
    void WritePcf(uint8_t leds_on_mask);

    std::mutex mutex_;
    std::atomic<int> front_cm_{400};
    std::atomic<int> rear_cm_{400};
    std::atomic<float> battery_v_{12.0f};
    std::atomic<bool> battery_cutoff_{false};

    int cmd_left_ = 0, cmd_right_ = 0;     // requested (percent)
    int out_left_ = 0, out_right_ = 0;     // applied after safety/ramp
    int64_t drive_deadline_ms_ = 0;
    MdeeMode mode_ = MdeeMode::kManual;

    // wander state machine
    enum class W { kCruise, kBackup, kTurn } wander_state_ = W::kCruise;
    int64_t wander_until_ms_ = 0;
    int wander_turn_dir_ = 1;
    int avoid_count_ = 0;
    int64_t avoid_window_start_ms_ = 0;

    std::atomic<bool> fidget_{true};
    int64_t next_fidget_ms_ = 0;
    int EarTarget(int64_t now_ms);
    MdeeEar ear_mode_ = MdeeEar::kAuto;
    int64_t ear_until_ms_ = 0, ear_start_ms_ = 0, next_ear_twitch_ms_ = 0;
    int ear_hold_angle_ = 90;
    int servo_target_[3] = {90, 90, 90};
    int servo_cur_[3] = {90, 90, 90};

    MdeeLights lights_ = MdeeLights::kOff;
    MdeeNeo neo_ = MdeeNeo::kSolid;
    uint8_t neo_r_ = 0, neo_g_ = 40, neo_b_ = 0;
    led_strip_handle_t neo_strip_ = nullptr;
    i2c_master_dev_handle_t pcf_ = nullptr;
    uint8_t pcf_last_ = 0xFF;

    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t adc_cali_ = nullptr;
    bool inited_ = false;
};

#endif
