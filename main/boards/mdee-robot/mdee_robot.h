#ifndef MDEE_ROBOT_H
#define MDEE_ROBOT_H

// BRAIN-side proxy. The real motors/sensors/servos/lights live on the ESP32-S3 Super Mini
// ("body", see /mdee_body). This class has the same API as before but talks to it over UART.

#include <driver/i2c_master.h>
#include <atomic>
#include <mutex>
#include <string>

// NOTE: enum order must match mdee_body/main/mdee_robot.h
enum class MdeeDir { kForward, kBackward, kLeft, kRight, kStop };
enum class MdeeSpeed { kSlow, kMedium, kFast, kTurbo };
enum class MdeeMode { kManual, kWander, kFollow };
enum class MdeeLights { kOff, kOn, kChase, kBlink, kSignalLeft, kSignalRight, kBatteryBar };
enum class MdeeNeo { kOff, kSolid, kBreathe, kRainbow };
enum class MdeeEar { kAuto, kNeutral, kPerk, kDroop, kWiggle, kFlick };   // same order as body

class MdeeRobot {
public:
    static MdeeRobot& GetInstance();
    void Init(i2c_master_bus_handle_t unused = nullptr);   // starts the UART link to the body

    void Move(MdeeDir dir, MdeeSpeed speed, int duration_ms);
    void Drive(int left_pct, int right_pct, int hold_ms);
    void Stop();
    static int SpeedPct(MdeeSpeed s);
    static bool ParseSpeed(const std::string& name, MdeeSpeed& out);
    static bool ParseDir(const std::string& name, MdeeDir& out);

    void SetMode(MdeeMode mode);
    MdeeMode GetMode() const { return mode_; }
    static const char* ModeName(MdeeMode m);
    static bool ParseMode(const std::string& name, MdeeMode& out);

    void SetServo(int index, int angle);
    void Head(int pan, int tilt);
    void Fidget(bool enable);
    // Ear gestures (one servo swings both ears). duration_ms 0 = default / hold.
    void Ears(MdeeEar gesture, int duration_ms = 0);
    static bool ParseEar(const std::string& name, MdeeEar& out);

    int FrontCm() const { return front_cm_; }
    int RearCm() const { return rear_cm_; }
    float BatteryVolts() const { return battery_mv_ / 1000.0f; }
    int BatteryPercent() const { return battery_pct_; }
    bool LinkUp() const;

    void SetLights(MdeeLights pattern);
    void SetNeo(MdeeNeo effect, uint8_t r, uint8_t g, uint8_t b);

    std::string StatusJson();

private:
    MdeeRobot() = default;
    void Send(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    static void RxTask(void* arg);
    static void PingTask(void* arg);
    void ParseStatus(const char* line);

    std::mutex tx_mutex_;
    std::atomic<int> front_cm_{400}, rear_cm_{400}, battery_mv_{0}, battery_pct_{0};
    std::atomic<int> left_{0}, right_{0}, pan_{90}, tilt_{90}, aux_{90};
    std::atomic<bool> cutoff_{false};
    std::atomic<MdeeMode> mode_{MdeeMode::kManual};
    std::atomic<int64_t> last_status_ms_{0};
    bool inited_ = false;
};

#endif
