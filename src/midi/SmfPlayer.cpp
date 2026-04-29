#include "SmfPlayer.h"
#include "core/MmpEngine.h"

#include <chrono>

// TinyMidiLoader: implementation in exactly one TU per binary.
#define TML_IMPLEMENTATION
#include "tml.h"

namespace mmp {

SmfPlayer::SmfPlayer(MmpEngine& engine) : engine_(engine) {}

SmfPlayer::~SmfPlayer() {
    stop();
    std::lock_guard<std::mutex> lk(msgMutex_);
    if (head_) {
        tml_free(head_);
        head_ = nullptr;
    }
}

bool SmfPlayer::load(const std::string& path) {
    stop();
    tml_message* msg = tml_load_filename(path.c_str());
    if (!msg) return false;

    unsigned int len = 0;
    tml_get_info(msg, nullptr, nullptr, nullptr, nullptr, &len);

    std::lock_guard<std::mutex> lk(msgMutex_);
    if (head_) tml_free(head_);
    head_ = msg;
    totalMs_ = len;
    path_ = path;
    positionMs_.store(0);
    return true;
}

bool SmfPlayer::isLoaded() const {
    std::lock_guard<std::mutex> lk(msgMutex_);
    return head_ != nullptr;
}

std::string SmfPlayer::filePath() const {
    std::lock_guard<std::mutex> lk(msgMutex_);
    return path_;
}

unsigned int SmfPlayer::lengthMs() const {
    std::lock_guard<std::mutex> lk(msgMutex_);
    return totalMs_;
}

unsigned int SmfPlayer::positionMs() const { return positionMs_.load(); }
bool SmfPlayer::isPlaying() const { return playing_.load(); }

void SmfPlayer::play() {
    if (playing_.exchange(true)) return;
    stopFlag_.store(false);
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this] { threadMain(); });
}

void SmfPlayer::pause() {
    // Pause = stop the worker but keep position so a subsequent play() resumes.
    if (!playing_.exchange(false)) return;
    stopFlag_.store(true);
    if (worker_.joinable()) worker_.join();
    engine_.allNotesOff();
}

void SmfPlayer::stop() {
    playing_.store(false);
    stopFlag_.store(true);
    if (worker_.joinable()) worker_.join();
    positionMs_.store(0);
    engine_.allNotesOff();
}

void SmfPlayer::dispatch(const tml_message* m) {
    switch (m->type) {
        case TML_NOTE_ON:
            engine_.noteOn(m->channel, m->key, m->velocity);
            break;
        case TML_NOTE_OFF:
            engine_.noteOff(m->channel, m->key);
            break;
        case TML_PROGRAM_CHANGE:
            engine_.programChange(m->channel, m->program);
            break;
        case TML_CONTROL_CHANGE:
            engine_.controlChange(m->channel, m->control, m->control_value);
            break;
        case TML_PITCH_BEND:
            engine_.pitchBend(m->channel, m->pitch_bend);
            break;
        case TML_CHANNEL_PRESSURE:
            engine_.channelPressure(m->channel, m->channel_pressure);
            break;
        default:
            break;
    }
}

void SmfPlayer::threadMain() {
    using clock = std::chrono::steady_clock;

    const tml_message* cur;
    unsigned int startMs;
    {
        std::lock_guard<std::mutex> lk(msgMutex_);
        cur = head_;
        startMs = positionMs_.load();
    }
    // Skip forward to startMs (resume).
    while (cur && cur->time < startMs) cur = cur->next;

    auto wallStart = clock::now();
    while (cur && !stopFlag_.load()) {
        auto target = wallStart + std::chrono::milliseconds(cur->time - startMs);
        auto now = clock::now();
        if (now < target) {
            std::this_thread::sleep_for(target - now);
            if (stopFlag_.load()) break;
        }
        dispatch(cur);
        positionMs_.store(cur->time);
        cur = cur->next;
    }
    if (!cur) {
        // Reached end naturally.
        playing_.store(false);
        positionMs_.store(0);
        engine_.allNotesOff();
    }
}

int SmfPlayer::renderToBuffer(float* out, int sampleRate) {
    // Walk the message list and intersperse audio renders to fill the
    // output buffer. Used by `mmp render` for headless WAV output.
    std::lock_guard<std::mutex> lk(msgMutex_);
    if (!head_ || totalMs_ == 0) return 0;

    const long long totalFrames = (long long)totalMs_ * sampleRate / 1000 + sampleRate; // +1s tail
    long long writtenFrames = 0;
    const tml_message* cur = head_;
    long long curMsFrames = 0;

    while (cur) {
        long long evtFrames = (long long)cur->time * sampleRate / 1000;
        long long delta = evtFrames - writtenFrames;
        if (delta > 0) {
            engine_.renderOffline(out + writtenFrames * 2, (int)delta);
            writtenFrames += delta;
            curMsFrames = evtFrames;
        }
        switch (cur->type) {
            case TML_NOTE_ON:        engine_.noteOn(cur->channel, cur->key, cur->velocity); break;
            case TML_NOTE_OFF:       engine_.noteOff(cur->channel, cur->key); break;
            case TML_PROGRAM_CHANGE: engine_.programChange(cur->channel, cur->program); break;
            case TML_CONTROL_CHANGE: engine_.controlChange(cur->channel, cur->control, cur->control_value); break;
            case TML_PITCH_BEND:     engine_.pitchBend(cur->channel, cur->pitch_bend); break;
            default: break;
        }
        cur = cur->next;
        (void)curMsFrames;
    }
    // Render the trailing tail so reverb/release fade naturally.
    if (writtenFrames < totalFrames) {
        engine_.renderOffline(out + writtenFrames * 2,
                              (int)(totalFrames - writtenFrames));
        writtenFrames = totalFrames;
    }
    return (int)writtenFrames;
}

} // namespace mmp
