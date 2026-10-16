#ifndef LOGIC_BATTERY_H
#define LOGIC_BATTERY_H

#include <atomic>

// Logic battery monitor (2 x Li-ion in parallel, measured at the battery terminal through a
// 100k/100k divider on GPIO1). Needed because the 5V boost converter hides the real battery state.
class LogicBattery {
public:
    static LogicBattery& GetInstance();
    void Init();

    float Volts() const { return volts_; }
    int Percent() const { return percent_; }
    bool Connected() const { return connected_; }   // divider wired and reading a sane Li-ion voltage
    bool Low() const { return low_; }                // <= LOGIC_BAT_LOW_PCT for 5 s
    bool Critical() const { return critical_; }      // < LOGIC_BAT_CRITICAL_V for 5 s -> robot stops

private:
    LogicBattery() = default;
    static void Task(void* arg);

    std::atomic<float> volts_{0.0f};
    std::atomic<int> percent_{0};
    std::atomic<bool> connected_{false}, low_{false}, critical_{false};
    bool inited_ = false;
};

#endif
