#include "audio_url_player.h"
#include "mdee_robot.h"
#include "oled_status.h"

#include "application.h"
#include "board.h"
#include "http.h"

#include <esp_audio_dec.h>
#include <esp_mp3_dec.h>
#include <esp_ae_rate_cvt.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <vector>

#define TAG "AudioUrl"

static constexpr size_t kInBufSize = 8192;          // compressed bytes held at once
static constexpr size_t kReadChunk = 2048;
static constexpr size_t kPcmBufSize = 6144;         // >= one MP3 frame (1152 stereo samples = 4608 B)
static constexpr uint32_t kMinFreeInternal = 45 * 1024;
static constexpr int kMaxWaitIdleMs = 30000;

AudioUrlPlayer& AudioUrlPlayer::GetInstance() {
    static AudioUrlPlayer inst;
    return inst;
}

std::string AudioUrlPlayer::Title() {
    std::lock_guard<std::mutex> l(mutex_);
    return title_;
}

void AudioUrlPlayer::SetTitle(const std::string& t) {
    std::lock_guard<std::mutex> l(mutex_);
    title_ = t;
}

static std::string MakeTitle(const std::string& url) {
    std::string s = url;
    auto scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    auto q = s.find_first_of("?#");
    if (q != std::string::npos) s = s.substr(0, q);
    std::string host = s.substr(0, s.find('/'));
    std::string file;
    auto slash = s.rfind('/');
    if (slash != std::string::npos && slash + 1 < s.size()) file = s.substr(slash + 1);
    std::string t = file.empty() ? host : file;
    for (auto& c : t) if (c < 32 || c > 126) c = '?';
    return t.substr(0, 40);
}

bool AudioUrlPlayer::Play(const std::string& url, std::string& error) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        error = "URL must start with http:// or https://";
        return false;
    }
    if (running_) {                                    // replace whatever is playing
        stop_ = true;
        for (int i = 0; i < 100 && running_; i++) vTaskDelay(pdMS_TO_TICKS(50));
        if (running_) { error = "previous audio is still stopping, try again"; return false; }
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < kMinFreeInternal) {
        error = "not enough free memory";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        url_ = url;
        title_ = MakeTitle(url);
    }
    stop_ = false;
    running_ = true;
    uint32_t session = ++session_;
    if (xTaskCreate(&AudioUrlPlayer::TaskEntry, "audio_url", 8192, (void*)(uintptr_t)session, 4, nullptr) != pdPASS) {
        running_ = false;
        error = "could not start the player task";
        return false;
    }
    return true;
}

void AudioUrlPlayer::Stop() {
    stop_ = true;
    ++session_;                                        // invalidates a running session
    for (int i = 0; i < 60 && running_; i++) vTaskDelay(pdMS_TO_TICKS(50));
}

void AudioUrlPlayer::TaskEntry(void* arg) {
    auto& self = GetInstance();
    self.Run((uint32_t)(uintptr_t)arg);
    self.running_ = false;
    vTaskDelete(nullptr);
}

static void ShowNote(const std::string& text) {
    OledStatus::GetInstance().SetChat("assistant", text.c_str());
}

void AudioUrlPlayer::Run(uint32_t session) {
    auto& app = Application::GetInstance();
    auto& robot = MdeeRobot::GetInstance();
    std::string url;
    {
        std::lock_guard<std::mutex> l(mutex_);
        url = url_;
    }

    // 1) never talk over the assistant: wait until the robot is idle
    int waited = 0;
    while (app.GetDeviceState() != kDeviceStateIdle) {
        if (ShouldStop(session) || waited >= kMaxWaitIdleMs) { ESP_LOGW(TAG, "not idle, giving up"); return; }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }
    if (ShouldStop(session)) return;

    ShowNote("Loading: " + Title());
    auto codec = Board::GetInstance().GetAudioCodec();
    const int out_rate = codec->output_sample_rate();
    const int out_ch = codec->output_channels();

    // 2) open the stream
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http) { ShowNote("Audio: no network"); return; }
    http->SetTimeout(8000);
    http->SetHeader("Accept", "audio/mpeg, audio/*");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetHeader("User-Agent", "MDEE-robot/1.0");
    auto opened = http->Open("GET", url);
    if (!opened) { ESP_LOGE(TAG, "open failed"); ShowNote("Audio: cannot open URL"); return; }
    auto status = http->GetStatusCode();
    if (!status || *status < 200 || *status >= 300) {
        ESP_LOGE(TAG, "HTTP status %d", status ? *status : -1);
        ShowNote("Audio: HTTP error " + std::to_string(status ? *status : -1) + " (use a direct link)");
        http->Close();
        return;
    }

    // 3) MP3 decoder
    esp_mp3_dec_register();                            // safe if already registered
    esp_audio_dec_cfg_t dcfg = {};
    dcfg.type = ESP_AUDIO_TYPE_MP3;
    esp_audio_dec_handle_t dec = nullptr;
    if (esp_audio_dec_open(&dcfg, &dec) != ESP_AUDIO_ERR_OK || dec == nullptr) {
        ShowNote("Audio: decoder failed");
        http->Close();
        return;
    }

    std::vector<uint8_t> in(kInBufSize);
    std::vector<uint8_t> pcm_buf(kPcmBufSize);
    std::vector<int16_t> pending;                       // codec-rate PCM waiting to be pushed
    std::vector<int16_t> work;                          // downmixed / converted frame
    std::vector<int16_t> resampled;
    esp_ae_rate_cvt_handle_t rs = nullptr;
    int rs_src = 0;
    size_t have = 0, id3_skip = 0;
    bool sniffed = false, eof = false, started = false;
    int push_fail = 0;
    int64_t next_ear_ms = 0;
    auto& audio = app.GetAudioService();

    while (!ShouldStop(session)) {
        if (app.GetDeviceState() != kDeviceStateIdle) {  // someone woke the robot / it speaks: music ends
            ESP_LOGI(TAG, "robot active, stopping audio");
            break;
        }
        // ---- read more compressed data ----
        if (!eof && have + kReadChunk <= in.size()) {
            auto n = http->Read(reinterpret_cast<char*>(in.data() + have), kReadChunk);
            if (!n) { ESP_LOGE(TAG, "read failed"); ShowNote("Audio: connection lost"); break; }
            if (*n == 0) eof = true; else have += *n;
        }
        // ---- skip ID3v2 tag ----
        if (!sniffed && have >= 10) {
            if (memcmp(in.data(), "OggS", 4) == 0 || in[0] == '<' || memcmp(in.data(), "RIFF", 4) == 0 ||
                memcmp(in.data(), "fLaC", 4) == 0) {
                ShowNote("Audio: not an MP3 (MP3 links only)");
                break;
            }
            if (memcmp(in.data(), "ID3", 3) == 0) {
                id3_skip = 10 + (((size_t)(in[6] & 0x7F) << 21) | ((size_t)(in[7] & 0x7F) << 14) |
                                 ((size_t)(in[8] & 0x7F) << 7) | (size_t)(in[9] & 0x7F));
            }
            sniffed = true;
        }
        if (!sniffed) { if (eof) break; continue; }
        if (id3_skip > 0) {
            size_t d = id3_skip < have ? id3_skip : have;
            memmove(in.data(), in.data() + d, have - d);
            have -= d;
            id3_skip -= d;
            if (id3_skip > 0 && eof) break;
            continue;
        }

        // ---- decode all complete frames in the buffer ----
        bool progressed = true;
        while (progressed && !ShouldStop(session) && (have >= 1500 || (eof && have > 0))) {
            progressed = false;
            esp_audio_dec_in_raw_t raw = {};
            raw.buffer = in.data();
            raw.len = (uint32_t)have;
            esp_audio_dec_out_frame_t out = {};
            out.buffer = pcm_buf.data();
            out.len = (uint32_t)pcm_buf.size();
            auto ret = esp_audio_dec_process(dec, &raw, &out);
            size_t used = raw.consumed;
            if (ret != ESP_AUDIO_ERR_OK && used == 0) used = 1;      // resync: drop one byte
            if (used > have) used = have;
            if (used > 0) {
                memmove(in.data(), in.data() + used, have - used);
                have -= used;
                progressed = true;
            }
            if (ret != ESP_AUDIO_ERR_OK || out.decoded_size == 0) continue;

            esp_audio_dec_info_t info = {};
            esp_audio_dec_get_info(dec, &info);
            int src_rate = info.sample_rate > 0 ? info.sample_rate : 44100;
            int src_ch = info.channel > 0 ? info.channel : 2;
            const int16_t* s = reinterpret_cast<const int16_t*>(pcm_buf.data());
            size_t frames = out.decoded_size / (sizeof(int16_t) * src_ch);

            // channel conversion to the speaker's channel count
            work.resize(frames * out_ch);
            for (size_t i = 0; i < frames; i++) {
                if (out_ch == 1) {
                    int acc = 0;
                    for (int c = 0; c < src_ch; c++) acc += s[i * src_ch + c];
                    work[i] = (int16_t)(acc / src_ch);
                } else {
                    for (int c = 0; c < out_ch; c++) work[i * out_ch + c] = s[i * src_ch + (c < src_ch ? c : 0)];
                }
            }
            // sample-rate conversion
            if (src_rate != out_rate) {
                if (rs == nullptr || rs_src != src_rate) {
                    if (rs) esp_ae_rate_cvt_close(rs);
                    esp_ae_rate_cvt_cfg_t cfg = {};
                    cfg.src_rate = (uint32_t)src_rate;
                    cfg.dest_rate = (uint32_t)out_rate;
                    cfg.channel = (uint8_t)out_ch;
                    cfg.bits_per_sample = ESP_AUDIO_BIT16;
                    cfg.complexity = 2;
                    cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                    rs = nullptr;
                    esp_ae_rate_cvt_open(&cfg, &rs);
                    rs_src = src_rate;
                }
                if (rs == nullptr) { ShowNote("Audio: resampler failed"); eof = true; have = 0; break; }
                uint32_t max_out = 0;
                esp_ae_rate_cvt_get_max_out_sample_num(rs, (uint32_t)frames, &max_out);
                resampled.resize((size_t)max_out * out_ch);
                uint32_t got = max_out;
                esp_ae_rate_cvt_process(rs, (esp_ae_sample_t)work.data(), (uint32_t)frames,
                                        (esp_ae_sample_t)resampled.data(), &got);
                pending.insert(pending.end(), resampled.begin(), resampled.begin() + (size_t)got * out_ch);
            } else {
                pending.insert(pending.end(), work.begin(), work.end());
            }

            // push ~100 ms chunks (blocks while the speaker queue is full = natural flow control)
            const size_t chunk = (size_t)(out_rate / 10) * out_ch;
            while (pending.size() >= chunk && !ShouldStop(session)) {
                std::vector<int16_t> block(pending.begin(), pending.begin() + chunk);
                pending.erase(pending.begin(), pending.begin() + chunk);
                if (!audio.PushPcmToPlayback(std::move(block))) {
                    if (++push_fail > 50) { stop_ = true; break; }
                } else {
                    push_fail = 0;
                }
                if (!started) {
                    started = true;
                    ShowNote("Playing: " + Title());
                    robot.SetNeo(MdeeNeo::kRainbow, 0, 0, 0);
                }
            }
            int64_t now = esp_timer_get_time() / 1000;
            if (started && now > next_ear_ms) {          // ears dance to the music
                next_ear_ms = now + 3500;
                robot.Ears(MdeeEar::kWiggle, 1500);
            }
        }
        if (eof && have == 0 && pending.empty()) break;
        if (eof && !progressed) break;
    }

    if (!ShouldStop(session) && !pending.empty()) audio.PushPcmToPlayback(std::move(pending));   // tail
    if (rs) esp_ae_rate_cvt_close(rs);
    esp_audio_dec_close(dec);
    http->Close();
    http.reset();

    if (started) {                                       // let the speaker finish its queue
        for (int i = 0; i < 40 && !audio.IsPlaybackIdle() && !stop_; i++) vTaskDelay(pdMS_TO_TICKS(100));
        ShowNote("");
        robot.SetNeo(MdeeNeo::kBreathe, 0, 120, 160);
        robot.Ears(MdeeEar::kAuto);
    }
    ESP_LOGI(TAG, "finished");
}
