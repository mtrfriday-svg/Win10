#ifndef AUDIO_URL_PLAYER_H
#define AUDIO_URL_PLAYER_H

#include <atomic>
#include <mutex>
#include <string>

// Streams an MP3 from an http(s) URL through the robot's speaker (music, radio, podcast files).
// Memory-light: ~50 KB while playing, nothing when stopped. MP3 only (no video, no other formats).
// Starts only when the robot is idle (so it never talks over the assistant) and stops when you
// wake the robot, press the button, or use Stop.
class AudioUrlPlayer {
public:
    static AudioUrlPlayer& GetInstance();

    // Returns false (and a reason) if it cannot start. Starts a background task.
    bool Play(const std::string& url, std::string& error);
    void Stop();
    bool IsPlaying() const { return running_; }
    std::string Title();

private:
    AudioUrlPlayer() = default;
    static void TaskEntry(void* arg);
    void Run(uint32_t session);
    bool ShouldStop(uint32_t session) const { return stop_ || session != session_; }
    void SetTitle(const std::string& t);

    std::atomic<uint32_t> session_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::string url_, title_;
};

#endif
