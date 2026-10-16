#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/led.h"

#include "audio_url_player.h"
#include "favorites.h"
#include "face_display.h"
#include "logic_battery.h"
#include "mdee_robot.h"
#include "oled_status.h"
#include "web_control.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MdeeBoard"

class MdeeBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    FaceDisplay* display_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = (i2c_port_t)0;
        cfg.sda_io_num = I2C_SDA_PIN;
        cfg.scl_io_num = I2C_SCL_PIN;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus_));
    }

    void InitializeFace() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = DISPLAY_MOSI_PIN;
        bus.miso_io_num = GPIO_NUM_NC;
        bus.sclk_io_num = DISPLAY_CLK_PIN;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = DISPLAY_CS_PIN;
        io_cfg.dc_gpio_num = DISPLAY_DC_PIN;
        io_cfg.spi_mode = DISPLAY_SPI_MODE;
        io_cfg.pclk_hz = 40 * 1000 * 1000;
        io_cfg.trans_queue_depth = 10;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_cfg, &io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;
        panel_cfg.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new FaceDisplay(io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                                   DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            if (AudioUrlPlayer::GetInstance().IsPlaying()) {        // click = stop the music
                AudioUrlPlayer::GetInstance().Stop();
                return;
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        // double-click = emergency stop + back to manual
        boot_button_.OnDoubleClick([]() { MdeeRobot::GetInstance().Stop(); });
    }

    // ------------------------------------------------------------------ AI tools (MCP)
    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();
        auto& robot = MdeeRobot::GetInstance();

        mcp.AddTool("self.robot.move",
            "Drive the robot body. Use for commands like 'go forward', 'back up', 'turn left', "
            "'spin right', 'come here slowly'. Left/right pivot on the spot. The robot "
            "automatically stops before obstacles.\n"
            "Args:\n"
            "  `direction`: forward | backward | left | right | stop\n"
            "  `speed`: slow | medium | fast | turbo (turbo = high speed). Default medium.\n"
            "  `duration_ms`: how long to move, 100-10000. Default 1500. A quarter turn is about "
            "500-700 ms at medium speed.",
            PropertyList({Property("direction", kPropertyTypeString),
                          Property("speed", kPropertyTypeString, std::string("medium")),
                          Property("duration_ms", kPropertyTypeInteger, 1500, 100, 10000)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeDir dir; MdeeSpeed sp = MdeeSpeed::kMedium;
                if (!MdeeRobot::ParseDir(p["direction"].value<std::string>(), dir)) return false;
                MdeeRobot::ParseSpeed(p["speed"].value<std::string>(), sp);
                robot.Move(dir, sp, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.robot.stop",
            "Stop moving immediately and switch to manual mode. Use when the user says stop, halt, "
            "freeze, or stay.",
            PropertyList(), [&robot](const PropertyList&) -> ReturnValue { robot.Stop(); return true; });

        mcp.AddTool("self.robot.set_mode",
            "Set autonomy mode.\n"
            "  manual: only moves when told.\n"
            "  wander: roams around the room on its own and avoids obstacles (explore, patrol, play).\n"
            "  follow: keeps about 30 cm behind whatever is in front of it (hand, person).",
            PropertyList({Property("mode", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeMode m;
                if (!MdeeRobot::ParseMode(p["mode"].value<std::string>(), m)) return false;
                robot.SetMode(m);
                return true;
            });

        mcp.AddTool("self.robot.head",
            "Move the neck. `pan` 0-180 turns the head left/right (90 = straight ahead), `tilt` "
            "0-180 nods it up/down (90 = level).",
            PropertyList({Property("pan", kPropertyTypeInteger, 90, 0, 180),
                          Property("tilt", kPropertyTypeInteger, 90, 0, 180)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                robot.Head(p["pan"].value<int>(), p["tilt"].value<int>());
                return true;
            });

        mcp.AddTool("self.robot.ears",
            "Move the robot's ears like a pet. gesture: perk (ears up, alert/curious), droop (ears "
            "back, sad/sleepy), wiggle (happy, excited), flick (quick twitch), neutral (relaxed), "
            "auto (natural idle twitches). `duration_ms` optional, 200-20000.",
            PropertyList({Property("gesture", kPropertyTypeString),
                          Property("duration_ms", kPropertyTypeInteger, 0, 0, 20000)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                MdeeEar g;
                if (!MdeeRobot::ParseEar(p["gesture"].value<std::string>(), g)) return false;
                robot.Ears(g, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.robot.get_status",
            "Get robot status: mode, front and rear obstacle distance in cm (400 = clear), battery "
            "voltage and percent of the motor pack (battery_*) and of the brain/logic pack (logic_*), "
            "wheel speeds. Use to answer 'how is your battery', 'what is in "
            "front of you', 'are you ok'.",
            PropertyList(), [&robot](const PropertyList&) -> ReturnValue { return robot.StatusJson(); });

        mcp.AddTool("self.robot.set_lights",
            "Set the row of 8 white LEDs. pattern: off | on | chase | blink | left | right | battery "
            "(battery shows charge level as a bar).",
            PropertyList({Property("pattern", kPropertyTypeString)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                auto s = p["pattern"].value<std::string>();
                MdeeLights l = MdeeLights::kOff;
                if (s == "on") l = MdeeLights::kOn;
                else if (s == "chase") l = MdeeLights::kChase;
                else if (s == "blink") l = MdeeLights::kBlink;
                else if (s == "left") l = MdeeLights::kSignalLeft;
                else if (s == "right") l = MdeeLights::kSignalRight;
                else if (s == "battery") l = MdeeLights::kBatteryBar;
                else if (s != "off") return false;
                robot.SetLights(l);
                return true;
            });

        mcp.AddTool("self.robot.set_color_lights",
            "Set the 2 RGB (NeoPixel) lights. effect: off | solid | breathe | rainbow. "
            "r, g, b are 0-255 (ignored for rainbow).",
            PropertyList({Property("effect", kPropertyTypeString, std::string("solid")),
                          Property("r", kPropertyTypeInteger, 0, 0, 255),
                          Property("g", kPropertyTypeInteger, 0, 0, 255),
                          Property("b", kPropertyTypeInteger, 0, 0, 255)}),
            [&robot](const PropertyList& p) -> ReturnValue {
                auto s = p["effect"].value<std::string>();
                MdeeNeo e = MdeeNeo::kSolid;
                if (s == "off") e = MdeeNeo::kOff;
                else if (s == "breathe") e = MdeeNeo::kBreathe;
                else if (s == "rainbow") e = MdeeNeo::kRainbow;
                robot.SetNeo(e, p["r"].value<int>(), p["g"].value<int>(), p["b"].value<int>());
                return true;
            });


        // ------------------------------------------------------------------ audio from the internet (MP3 only)
        mcp.AddTool("self.audio_url.play",
            "Play an MP3 file or MP3 internet-radio stream from a direct http/https link on the robot's "
            "speaker. It starts right after the robot finishes speaking and stops when the user talks to "
            "the robot or says stop. Only direct MP3 links work (not YouTube/Spotify pages). "
            "Arg `url`: the full link.",
            PropertyList({Property("url", kPropertyTypeString)}),
            [](const PropertyList& p) -> ReturnValue {
                std::string err;
                if (!AudioUrlPlayer::GetInstance().Play(p["url"].value<std::string>(), err))
                    return std::string("Cannot play: ") + err;
                return std::string("OK, the audio will start in a moment.");
            });

        mcp.AddTool("self.audio_url.play_favorite",
            "Play one of the saved favourite audio links by name (fuzzy match), e.g. 'lofi', 'my song'. "
            "Use self.audio_url.list_favorites to see the names.",
            PropertyList({Property("name", kPropertyTypeString)}),
            [](const PropertyList& p) -> ReturnValue {
                std::string want = p["name"].value<std::string>();
                for (auto& c : want) c = (char)tolower((unsigned char)c);
                for (const auto& f : kAudioFavorites) {
                    std::string n = f.name;
                    for (auto& c : n) c = (char)tolower((unsigned char)c);
                    if (n.find(want) != std::string::npos || want.find(n) != std::string::npos) {
                        std::string err;
                        if (!AudioUrlPlayer::GetInstance().Play(f.url, err)) return std::string("Cannot play: ") + err;
                        return std::string("OK, playing ") + f.name + " in a moment.";
                    }
                }
                return std::string("No favourite with that name.");
            });

        mcp.AddTool("self.audio_url.list_favorites",
            "List the names of the saved favourite audio links.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                std::string s;
                for (const auto& f : kAudioFavorites) { if (!s.empty()) s += ", "; s += f.name; }
                return s.empty() ? std::string("none") : s;
            });

        mcp.AddTool("self.audio_url.stop",
            "Stop the music / internet audio that is playing. Use when the user says stop the music, "
            "be quiet, turn it off.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                AudioUrlPlayer::GetInstance().Stop();
                return true;
            });

        mcp.AddTool("self.audio_url.status",
            "Tell whether internet audio is playing and its name.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                auto& a = AudioUrlPlayer::GetInstance();
                return a.IsPlaying() ? std::string("playing: ") + a.Title() : std::string("not playing");
            });

        mcp.AddTool("self.robot.dance",
            "Do a short happy dance (ear wiggle, spin, head moves, lights). Takes about 4 seconds.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                xTaskCreate([](void* arg) {
                    auto& r = MdeeRobot::GetInstance();
                    r.SetLights(MdeeLights::kChase);
                    r.SetNeo(MdeeNeo::kRainbow, 0, 0, 0);
                    for (int i = 0; i < 3; i++) {
                        r.Ears(MdeeEar::kWiggle, 4000); r.Head(60, 70); r.Move(MdeeDir::kLeft, MdeeSpeed::kMedium, 450);
                        vTaskDelay(pdMS_TO_TICKS(550));
                        r.Head(120, 110); r.Move(MdeeDir::kRight, MdeeSpeed::kMedium, 450);
                        vTaskDelay(pdMS_TO_TICKS(550));
                    }
                    r.Head(90, 90); r.Stop();
                    r.SetLights(MdeeLights::kOff);
                    r.SetNeo(MdeeNeo::kBreathe, 0, 120, 160);
                    vTaskDelete(nullptr);
                }, "dance", 3072, nullptr, 3, nullptr);
                return true;
            });
    }

public:
    MdeeBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        MdeeRobot::GetInstance().Init(i2c_bus_);
        LogicBattery::GetInstance().Init();
        OledStatus::GetInstance().Init(i2c_bus_);
        InitializeFace();
        InitializeButtons();
        InitializeTools();
        WebControl::StartWhenOnline();
    }

    virtual Led* GetLed() override {
        static NoLed led;
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    // The brain's own battery (the XiaoZhi status uses this); motor pack is shown on the OLED / get_status.
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        auto& lb = LogicBattery::GetInstance();
        if (!lb.Connected()) return false;
        level = lb.Percent();
        charging = false;
        discharging = true;
        return true;
    }
};

DECLARE_BOARD(MdeeBoard);
