#include "logic_battery.h"
#include "config.h"
#include "application.h"
#include "assets/lang_config.h"
#include "mdee_robot.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "LogicBattery"

LogicBattery& LogicBattery::GetInstance() {
    static LogicBattery inst;
    return inst;
}

void LogicBattery::Init() {
    if (inited_) return;
    inited_ = true;
    xTaskCreate(&LogicBattery::Task, "logic_bat", 4096, this, 2, nullptr);
}

void LogicBattery::Task(void* arg) {
    auto* self = static_cast<LogicBattery*>(arg);

    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t uc = {};
    uc.unit_id = LOGIC_BAT_ADC_UNIT;
    if (adc_oneshot_new_unit(&uc, &adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed");
        vTaskDelete(nullptr);
        return;
    }
    adc_oneshot_chan_cfg_t cc = {};
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc, LOGIC_BAT_ADC_CHANNEL, &cc);

    adc_cali_handle_t cali = nullptr;
    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = LOGIC_BAT_ADC_UNIT;
    cal.chan = LOGIC_BAT_ADC_CHANNEL;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cal, &cali) != ESP_OK) cali = nullptr;

    float filtered = 0.0f;
    int low_count = 0, crit_count = 0;
    bool warned = false;
    while (true) {
        long sum = 0;
        int n = 0;
        for (int i = 0; i < 16; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc, LOGIC_BAT_ADC_CHANNEL, &raw) == ESP_OK) { sum += raw; n++; }
        }
        if (n > 0) {
            int raw = (int)(sum / n), mv = 0;
            if (!(cali && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK)) mv = raw * 3100 / 4095;
            float v = (mv / 1000.0f) * (LOGIC_BAT_R_UPPER_OHM + LOGIC_BAT_R_LOWER_OHM) / LOGIC_BAT_R_LOWER_OHM;
            bool sane = v > 2.5f && v < 4.6f;            // a Li-ion cell; otherwise divider not wired
            filtered = (filtered > 2.5f && sane) ? filtered * 0.85f + v * 0.15f : v;
            self->connected_ = sane;
            self->volts_ = filtered;
            int pct = (int)((filtered - LOGIC_BAT_EMPTY_V) * 100.0f / (LOGIC_BAT_FULL_V - LOGIC_BAT_EMPTY_V));
            pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
            self->percent_ = pct;

            if (sane) {
                low_count = (pct <= LOGIC_BAT_LOW_PCT) ? low_count + 1 : 0;
                crit_count = (filtered < LOGIC_BAT_CRITICAL_V) ? crit_count + 1 : 0;
            } else {
                low_count = crit_count = 0;
            }
            bool low = low_count >= 10;                   // 10 x 0.5 s
            bool crit = crit_count >= 10;
            if (low && !warned) {                         // announce once
                warned = true;
                auto& app = Application::GetInstance();
                app.Schedule([&app]() { app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY); });
            }
            if (!low && pct > LOGIC_BAT_LOW_PCT + 8) warned = false;   // re-arm after recharge
            if (crit && !self->critical_) {
                ESP_LOGW(TAG, "Logic battery critical (%.2f V): stopping robot", filtered);
                MdeeRobot::GetInstance().Stop();
                MdeeRobot::GetInstance().Ears(MdeeEar::kDroop, 0);
            }
            self->low_ = low;
            self->critical_ = crit;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
