#include "MmpEngine.h"

#include <algorithm>
#include <chrono>
#include <cstring>

// TinySoundFont: implementation goes in *exactly one* TU per linked binary.
// MmpEngine.cpp is the natural home — every executable links it once.
#define TSF_IMPLEMENTATION
#include "tsf.h"

#if defined(__APPLE__)
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace mmp {

namespace {
constexpr int kTapBufFrames = 8192;   // 8K frames stereo == ~186 ms @ 44.1k
}

MmpEngine::MmpEngine() {
    tapBuf_.assign(kTapBufFrames * 2, 0.0f);
    noteRing_.assign(kNoteRingCap, NoteEvent{});
}

MmpEngine::~MmpEngine() {
    stop();
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (tsf_) {
        tsf_close(tsf_);
        tsf_ = nullptr;
    }
}

bool MmpEngine::loadSoundFont(const std::string& path) {
    tsf* loaded = tsf_load_filename(path.c_str());
    if (!loaded) return false;

    tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
    tsf_set_volume(loaded, 1.0f);
    tsf_set_max_voices(loaded, 256);

    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (tsf_) tsf_close(tsf_);
    tsf_ = loaded;
    sfPath_ = path;
    return true;
}

bool MmpEngine::hasSoundFont() const {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    return tsf_ != nullptr;
}

std::string MmpEngine::soundFontPath() const {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    return sfPath_;
}

int MmpEngine::presetCount() const {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    return tsf_ ? tsf_get_presetcount(tsf_) : 0;
}

std::string MmpEngine::presetName(int index) const {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (!tsf_) return {};
    const char* n = tsf_get_presetname(tsf_, index);
    return n ? std::string(n) : std::string();
}

int MmpEngine::sampleRate() const { return sampleRate_; }
bool MmpEngine::isRunning() const { return running_.load(); }

bool MmpEngine::start(int sampleRate) {
    if (running_.exchange(true)) return true;
    sampleRate_ = sampleRate;
    {
        std::lock_guard<std::mutex> lk(tsfMutex_);
        if (tsf_) tsf_set_output(tsf_, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
    }
#if defined(__APPLE__)
    if (!startCoreAudio(sampleRate_)) {
        running_.store(false);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void MmpEngine::stop() {
    if (!running_.exchange(false)) return;
#if defined(__APPLE__)
    stopCoreAudio();
#endif
}

void MmpEngine::renderOffline(float* out, int frames) {
    renderAndTap(out, frames);
}

// --- MIDI input ------------------------------------------------------------

void MmpEngine::noteOn(int channel, int key, int velocity) {
    int solo = soloChannel_.load();
    if (solo > 0 && channel != solo - 1) return;       // muted channel
    {
        std::lock_guard<std::mutex> lk(tsfMutex_);
        if (!tsf_) return;
        if (velocity == 0) tsf_channel_note_off(tsf_, channel, key);
        else               tsf_channel_note_on(tsf_, channel, key, velocity / 127.0f);
    }
    NoteEvent e;
    e.t_ms     = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();
    e.on       = (velocity == 0) ? 0 : 1;
    e.channel  = (unsigned char)channel;
    e.key      = (unsigned char)key;
    e.velocity = (unsigned char)velocity;
    pushNoteEvent_(e);
}

void MmpEngine::noteOff(int channel, int key) {
    int solo = soloChannel_.load();
    if (solo > 0 && channel != solo - 1) return;
    {
        std::lock_guard<std::mutex> lk(tsfMutex_);
        if (tsf_) tsf_channel_note_off(tsf_, channel, key);
    }
    NoteEvent e;
    e.t_ms     = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();
    e.on       = 0;
    e.channel  = (unsigned char)channel;
    e.key      = (unsigned char)key;
    e.velocity = 0;
    pushNoteEvent_(e);
}

void MmpEngine::allNotesOff() {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (tsf_) tsf_note_off_all(tsf_);
}

void MmpEngine::programChange(int channel, int program) {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (!tsf_) return;
    // GM drum kit lives on channel 9 (0-based). Pass mididrums flag.
    tsf_channel_set_presetnumber(tsf_, channel, program, channel == 9 ? 1 : 0);
}

void MmpEngine::controlChange(int channel, int controller, int value) {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (tsf_) tsf_channel_midi_control(tsf_, channel, controller, value);
}

void MmpEngine::pitchBend(int channel, int pitch14bit) {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    if (tsf_) tsf_channel_set_pitchwheel(tsf_, channel, pitch14bit);
}

void MmpEngine::channelPressure(int channel, int value) {
    // TSF has no channel-pressure aftertouch path; ignore for now.
    (void)channel; (void)value;
}

void MmpEngine::setSoloChannel(int oneBasedChannel) {
    if (oneBasedChannel < 0)  oneBasedChannel = 0;
    if (oneBasedChannel > 16) oneBasedChannel = 16;
    int prev = soloChannel_.exchange(oneBasedChannel);
    if (prev != oneBasedChannel) {
        // Cut whatever was hanging on the now-muted channels.
        std::lock_guard<std::mutex> lk(tsfMutex_);
        if (tsf_) tsf_note_off_all(tsf_);
    }
}

int MmpEngine::activeVoiceCount() const {
    std::lock_guard<std::mutex> lk(tsfMutex_);
    return tsf_ ? tsf_active_voice_count(tsf_) : 0;
}

// --- Note-event ring -------------------------------------------------------

void MmpEngine::pushNoteEvent_(const NoteEvent& e) {
    const uint32_t cap = kNoteRingCap;
    uint32_t w = noteWrite_.load(std::memory_order_relaxed);
    noteRing_[w % cap] = e;
    noteWrite_.store(w + 1, std::memory_order_release);
    // If the consumer is way behind, drop oldest by advancing read.
    uint32_t r = noteRead_.load(std::memory_order_acquire);
    if ((w + 1) - r > cap) {
        noteRead_.store((w + 1) - cap, std::memory_order_relaxed);
    }
}

int MmpEngine::drainNoteEvents(NoteEvent* out, int max) {
    const uint32_t cap = kNoteRingCap;
    uint32_t w = noteWrite_.load(std::memory_order_acquire);
    uint32_t r = noteRead_.load(std::memory_order_relaxed);
    int n = 0;
    while (r != w && n < max) {
        out[n++] = noteRing_[r % cap];
        ++r;
    }
    noteRead_.store(r, std::memory_order_release);
    return n;
}

// --- Render + tap ----------------------------------------------------------

void MmpEngine::renderAndTap(float* out, int frames) {
    {
        std::lock_guard<std::mutex> lk(tsfMutex_);
        if (tsf_) {
            tsf_render_float(tsf_, out, frames, /*flag_mixing=*/0);
        } else {
            std::memset(out, 0, sizeof(float) * 2 * frames);
        }
    }
    // Push into analyzer tap (single-producer; UI thread is single-consumer).
    const uint32_t cap = kTapBufFrames;
    uint32_t w = tapWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        uint32_t idx = (w + i) % cap;
        tapBuf_[idx * 2 + 0] = out[i * 2 + 0];
        tapBuf_[idx * 2 + 1] = out[i * 2 + 1];
    }
    tapWrite_.store((w + frames) % cap, std::memory_order_release);
}

int MmpEngine::readAnalyzerTap(float* out, int frames) {
    const uint32_t cap = kTapBufFrames;
    if (frames <= 0 || frames > (int)cap) frames = cap;
    uint32_t w = tapWrite_.load(std::memory_order_acquire);
    // Read the most recent `frames` samples ending at write head.
    uint32_t start = (w + cap - (uint32_t)frames) % cap;
    for (int i = 0; i < frames; ++i) {
        uint32_t idx = (start + i) % cap;
        out[i * 2 + 0] = tapBuf_[idx * 2 + 0];
        out[i * 2 + 1] = tapBuf_[idx * 2 + 1];
    }
    tapRead_.store(w, std::memory_order_relaxed);
    return frames;
}

// --- CoreAudio backend -----------------------------------------------------
#if defined(__APPLE__)

namespace {
OSStatus mmpRenderCallback(void* refCon,
                           AudioUnitRenderActionFlags* /*flags*/,
                           const AudioTimeStamp* /*ts*/,
                           UInt32 /*busNumber*/,
                           UInt32 numFrames,
                           AudioBufferList* ioData) {
    auto* eng = static_cast<MmpEngine*>(refCon);
    float* buf = static_cast<float*>(ioData->mBuffers[0].mData);
    eng->renderOffline(buf, (int)numFrames);
    return noErr;
}
} // namespace

bool MmpEngine::startCoreAudio(int sampleRate) {
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) return false;
    AudioComponentInstance unit = nullptr;
    if (AudioComponentInstanceNew(comp, &unit) != noErr) return false;

    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = (Float64)sampleRate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket  = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = 8;
    fmt.mBytesPerPacket   = 8;

    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) != noErr) {
        AudioComponentInstanceDispose(unit);
        return false;
    }

    AURenderCallbackStruct cb{};
    cb.inputProc = mmpRenderCallback;
    cb.inputProcRefCon = this;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr) {
        AudioComponentInstanceDispose(unit);
        return false;
    }

    if (AudioUnitInitialize(unit) != noErr ||
        AudioOutputUnitStart(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return false;
    }
    audioUnit_ = unit;
    return true;
}

void MmpEngine::stopCoreAudio() {
    if (!audioUnit_) return;
    auto unit = static_cast<AudioComponentInstance>(audioUnit_);
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    audioUnit_ = nullptr;
}

#endif // __APPLE__

} // namespace mmp
