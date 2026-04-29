#pragma once

// SmfPlayer — drives an MmpEngine from a Standard MIDI File using
// TinyMidiLoader. Runs its own scheduling thread that walks the message
// linked list in real time. Safe to load/play/stop/seek from any thread.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace mmp { class MmpEngine; }

struct tml_message;

namespace mmp {

class SmfPlayer {
public:
    explicit SmfPlayer(MmpEngine& engine);
    ~SmfPlayer();

    SmfPlayer(const SmfPlayer&) = delete;
    SmfPlayer& operator=(const SmfPlayer&) = delete;

    // Load a Standard MIDI File. Replaces any previously loaded file.
    bool load(const std::string& path);
    bool isLoaded() const;
    std::string filePath() const;

    // Total length in milliseconds (0 if nothing loaded).
    unsigned int lengthMs() const;
    // Current playback position in milliseconds.
    unsigned int positionMs() const;

    void play();
    void pause();
    void stop();
    bool isPlaying() const;

    // Render the entire loaded SMF into a stereo-interleaved float buffer
    // sized exactly `lengthMs()` worth of samples. Used by `mmp render`.
    // Caller-owned buffer of size 2 * frames; returns frames rendered.
    int renderToBuffer(float* out, int sampleRate);

private:
    void threadMain();
    void dispatch(const tml_message* msg);

    MmpEngine& engine_;
    std::string path_;

    mutable std::mutex msgMutex_;
    tml_message* head_ = nullptr;          // owned
    unsigned int totalMs_ = 0;

    std::atomic<bool> playing_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<unsigned int> positionMs_{0};
    std::thread worker_;
};

} // namespace mmp
