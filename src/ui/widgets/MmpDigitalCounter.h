#pragma once

// MmpDigitalCounter — playback timer rendered with fontaudio's 7-segment
// LCD digit SVGs. Two modes, toggled by clicking the widget:
//
//   Forward   — time elapsed from start         "0:42"
//   Remaining — time until end (with leading -) "-2:38"

#include <FL/Fl_Widget.H>

namespace mmp {

class MmpDigitalCounter : public Fl_Widget {
public:
    enum class Mode { Forward, Remaining };

    MmpDigitalCounter(int x, int y, int w, int h);
    void setTimes(unsigned int posMs, unsigned int totalMs);
    void setMode(Mode m);
    Mode mode() const { return mode_; }

    void draw() override;
    int  handle(int event) override;

private:
    unsigned int posMs_   = 0;
    unsigned int totalMs_ = 0;
    Mode         mode_    = Mode::Forward;
};

} // namespace mmp
