#pragma once

// MmpSpectrum — log-spaced FFT histogram. Custom-drawn (no FLTK
// equivalent). Owns a KissFFT real-FFT plan and reads the engine's
// analyzer tap on every update().

#include <FL/Fl_Widget.H>

#include <string>
#include <vector>

struct kiss_fftr_state;
typedef struct kiss_fftr_state* kiss_fftr_cfg;

namespace mmp {

class MmpEngine;

class MmpSpectrum : public Fl_Widget {
public:
    // Snapshot of playback / engine state shown as an LCD-style text
    // overlay in the top-left corner of the spectrum panel. The panel
    // owner (MainWindow) refreshes this once per tick.
    struct Overlay {
        std::string fileName;          // basename of the loaded MIDI
        std::string sfName;            // basename of the loaded SF
        int          posS         = 0; // playback position (seconds)
        int          totalS       = 0; // file length (seconds)
        int          bpm          = 0; // current tempo (0 = unknown)
        int          activeVoices = 0;
        int          sampleRate   = 0;
        int          channelsUsed = 0; // count, not bitmask
        int          channelsTotal= 0; // 16 normally
    };

    MmpSpectrum(int x, int y, int w, int h, MmpEngine& engine);
    ~MmpSpectrum() override;
    void update();
    void draw() override;
    int  handle(int event) override;
    void setOverlay(const Overlay& info) { overlay_ = info; }

private:
    static constexpr int kFftSize  = 1024;
    static constexpr int kBands    = 32;

    MmpEngine& engine_;
    kiss_fftr_cfg cfg_ = nullptr;
    std::vector<float> window_;     // Hann
    std::vector<float> bandLevel_;  // 0..1, smoothed
    std::vector<float> bandPeak_;   // peak hold
    std::vector<int>   bandHoldAge_;
    Overlay            overlay_;
};

} // namespace mmp
