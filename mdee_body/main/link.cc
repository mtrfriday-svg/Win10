#include "link.h"
#include "body_config.h"
#include "mdee_robot.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define TAG "Link"

static std::atomic<int64_t> g_last_rx_ms{0};
static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }

static void Handle(char* line) {
    auto& r = MdeeRobot::GetInstance();
    char cmd[12] = {0};
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    char word[16] = {0};
    int n = sscanf(line, "%11s", cmd);
    if (n != 1) return;
    g_last_rx_ms = NowMs();

    if (!strcmp(cmd, "D")) {                     // D left right hold_ms
        if (sscanf(line, "D %d %d %d", &a, &b, &c) == 3) r.Drive(a, b, c);
    } else if (!strcmp(cmd, "S")) {
        r.Stop();
    } else if (!strcmp(cmd, "MODE")) {           // MODE manual|wander|follow
        MdeeMode m;
        if (sscanf(line, "MODE %15s", word) == 1 && MdeeRobot::ParseMode(word, m)) r.SetMode(m);
    } else if (!strcmp(cmd, "SV")) {             // SV idx angle
        if (sscanf(line, "SV %d %d", &a, &b) == 2) r.SetServo(a, b);
    } else if (!strcmp(cmd, "EAR")) {            // EAR gesture [ms]
        int n2 = sscanf(line, "EAR %d %d", &a, &b);
        if (n2 >= 1 && a >= 0 && a <= 5) r.Ears((MdeeEar)a, n2 == 2 ? b : 0);
    } else if (!strcmp(cmd, "FID")) {
        if (sscanf(line, "FID %d", &a) == 1) r.Fidget(a != 0);
    } else if (!strcmp(cmd, "LT")) {             // LT pattern_index
        if (sscanf(line, "LT %d", &a) == 1 && a >= 0 && a <= 6) r.SetLights((MdeeLights)a);
    } else if (!strcmp(cmd, "NEO")) {            // NEO effect r g b
        if (sscanf(line, "NEO %d %d %d %d", &a, &b, &c, &d) == 4 && a >= 0 && a <= 3)
            r.SetNeo((MdeeNeo)a, b & 255, c & 255, d & 255);
    }                                            // "PING" only refreshes g_last_rx_ms
    (void)e;
}

static void RxTask(void*) {
    char buf[96];
    size_t len = 0;
    uint8_t ch;
    while (true) {
        int got = uart_read_bytes(LINK_UART_NUM, &ch, 1, pdMS_TO_TICKS(100));
        if (got == 1) {
            if (ch == '\n' || ch == '\r') {
                if (len > 0) { buf[len] = 0; Handle(buf); len = 0; }
            } else if (len < sizeof(buf) - 1) {
                buf[len++] = (char)ch;
            } else {
                len = 0;                          // overlong garbage line
            }
        }
        // failsafe: brain silent -> no autonomous driving and no runaway
        auto& r = MdeeRobot::GetInstance();
        int64_t last = g_last_rx_ms;
        if (last != 0 && NowMs() - last > LINK_TIMEOUT_MS && r.GetMode() != MdeeMode::kManual) {
            ESP_LOGW(TAG, "Brain silent, stopping autonomy");
            r.Stop();
        }
    }
}

static void TxTask(void*) {
    while (true) {
        std::string s = MdeeRobot::GetInstance().StatusLine();
        uart_write_bytes(LINK_UART_NUM, s.c_str(), s.size());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void LinkStart() {
    uart_config_t cfg = {};
    cfg.baud_rate = LINK_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART_NUM, 1024, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART_NUM, LINK_TX_PIN, LINK_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(RxTask, "link_rx", 4096, nullptr, 7, nullptr);
    xTaskCreate(TxTask, "link_tx", 3072, nullptr, 3, nullptr);
}
