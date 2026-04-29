#pragma once

// MmpEngine — thread-safe wrapper around TinySoundFont + a CoreAudio output
// thread (macOS) or stub (other platforms). Owns SoundFont lifetime, accepts
// MIDI events from any thread, and exposes a polling API for the analyzer
// tap (most-recent block of mixed float samples).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct tsf;

namespace mmp {

class MmpEngine {
public:
    MmpEngine();
    ~MmpEngine();

    MmpEngine(const MmpEngine&) = delete;
    MmpEngine& operator=(const MmpEngine&) = delete;

    // Load .sf2 from disk. Replaces any previously loaded font. Returns
    // true on success; on failure the engine is left with no font loaded.
    bool loadSoundFont(const std::string& path);
    bool hasSoundFont() const;
    std::string soundFontPath() const;

    // Number of presets in the loaded SoundFont (0 if none loaded).
    int presetCount() const;
    std::string presetName(int index) const;

    // Audio I/O. start() opens CoreAudio at the given sample rate and
    // begins rendering; stop() closes it. start() is a no-op if already
    // running. sampleRate() returns the rate the engine is configured for
    // (defaults to 44100 if start() hasn't been called).
    bool start(int sampleRate = 44100);
    void stop();
    bool isRunning() const;
    int sampleRate() const;

    // Render a fixed number of stereo-interleaved float frames into `out`
    // without going through the audio device. Used by `mmp render` for
    // headless WAV output and by tests. `out` length must be 2 * frames.
    void renderOffline(float* out, int frames);

    // MIDI event submission — channel is 0..15. These are safe to call
    // from any thread (TSF mutates state on call but our wrapper guards
    // it with a mutex shared with the audio render thread).
    void noteOn(int channel, int key, int velocity);
    void noteOff(int channel, int key);
    void allNotesOff();
    void programChange(int channel, int program);
    void controlChange(int channel, int controller, int value);
    void pitchBend(int channel, int pitch14bit);   // 0..16383, 8192 = center
    void channelPressure(int channel, int value);

    // Channel solo: 0 = no solo (all channels play); 1..16 = only that
    // channel reaches the synth. Setting it sends an all-notes-off so
    // previously-held notes on the now-muted channels don't hang.
    void setSoloChannel(int oneBasedChannel);
    int  soloChannel() const { return soloChannel_.load(); }

    // Analyzer tap: copy the most recent `frames` stereo-interleaved
    // samples from the engine's ring buffer into `out`. Returns the
    // number of frames actually copied. Lock-free; safe to call from
    // the UI thread.
    int readAnalyzerTap(float* out, int frames);

    // Active voice count (cheap, no lock).
    int activeVoiceCount() const;

    // ----- Note-event tap --------------------------------------------
    // Lightweight record of every channel note-on/note-off the engine
    // sees, captured into a bounded SPSC ring. UI visualizers
    // (piano-roll, note glyphs) drain the ring at ~30 Hz and animate.
    struct NoteEvent {
        unsigned int t_ms;     // host-clock timestamp at submission
        unsigned char on;      // 1 = note-on, 0 = note-off
        unsigned char channel; // 0..15
        unsigned char key;     // MIDI note number 0..127
        unsigned char velocity;// 0..127
    };
    // Drain up to `max` events; returns the count written into `out`.
    int drainNoteEvents(NoteEvent* out, int max);

private:
#if defined(__APPLE__)
    bool startCoreAudio(int sampleRate);
    void stopCoreAudio();
#endif

    // Render `frames` stereo-interleaved floats into `out`, also feeding
    // the analyzer tap. Called from the audio thread (or renderOffline).
    void renderAndTap(float* out, int frames);

    mutable std::mutex tsfMutex_;     // guards tsf_ and channel-set ops
    tsf* tsf_ = nullptr;
    std::string sfPath_;
    int sampleRate_ = 44100;
    std::atomic<bool> running_{false};

    // Lock-free SPSC ring for analyzer (audio-thread writer, UI-thread reader)
    std::vector<float> tapBuf_;       // stereo-interleaved
    std::atomic<uint32_t> tapWrite_{0};
    std::atomic<uint32_t> tapRead_{0};

    // SPSC ring for note events (sequencer/UI writer → UI reader).
    // Plenty of slack — large MIDI files can fire thousands of notes
    // per second during transient passages.
    static constexpr int kNoteRingCap = 4096;
    std::vector<NoteEvent>    noteRing_;
    std::atomic<uint32_t>     noteWrite_{0};
    std::atomic<uint32_t>     noteRead_{0};
    void pushNoteEvent_(const NoteEvent& e);

    // 0 = no solo; otherwise 1-based channel that's allowed through.
    std::atomic<int>          soloChannel_{0};

#if defined(__APPLE__)
    // AudioComponentInstance is `struct ComponentInstanceRecord*` from
    // AudioToolbox; kept as void* here so this header doesn't drag in
    // the framework just for the type.
    void* audioUnit_ = nullptr;
#endif
};

} // namespace mmp
