#pragma once

// MmpVuMeter — stereo peak + RMS bargraph. No FLTK equivalent, so custom
// drawn. Reads samples from MmpEngine's analyzer tap on every update();
// the host (MainWindow) drives update() from a 30 Hz timer.

#include <FL/Fl_Widget.H>

namespace mmp {

class MmpEngine;

class MmpVuMeter : public Fl_Widget {
public:
    MmpVuMeter(int x, int y, int w, int h, MmpEngine& engine);
    void update();          // refresh peak/rms from the engine tap
    void draw() override;

private:
    MmpEngine& engine_;
    float peakL_ = 0.0f, peakR_ = 0.0f;
    float rmsL_  = 0.0f, rmsR_  = 0.0f;
    float holdL_ = 0.0f, holdR_ = 0.0f;
    int   holdAgeL_ = 0, holdAgeR_ = 0;   // timer ticks since hold latched
};

} // namespace mmp
