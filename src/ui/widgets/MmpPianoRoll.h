#pragma once

// MmpPianoRoll — scrolling piano-roll visualizer. Drains the engine's
// note-event ring on every update() and tracks active+recent notes,
// drawing each as a horizontal rectangle (x = time relative to "now",
// y = pitch within an auto-windowed range, color = MIDI channel).

#include <FL/Fl_Widget.H>

#include <deque>
#include <unordered_map>

#include "AnimationChrome.h"

namespace mmp {

class MmpEngine;
struct AnimationState;

class MmpPianoRoll : public Fl_Widget {
public:
    MmpPianoRoll(int x, int y, int w, int h, MmpEngine& engine,
                 AnimationState& state);
    void update();
    void draw() override;
    int  handle(int event) override;

private:
    struct NoteSpan {
        unsigned int onMs;
        unsigned int offMs;     // 0 while still held
        unsigned char channel;
        unsigned char key;
        unsigned char velocity;
    };

    MmpEngine&      engine_;
    AnimationState& state_;
    std::deque<NoteSpan> notes_;          // newest at the back
    // (channel, key) → index into notes_ for the currently-held span
    std::unordered_map<unsigned, size_t> active_;
    unsigned int nowMs_ = 0;
    // Smoothed pitch-window centre. Recomputed each tick from the
    // notes currently visible on the selected channel; lerped towards
    // its target so the view doesn't snap when notes pop in/out.
    float        centerKey_ = 60.0f;      // start on Middle C
    AnimationChromeRects chromeRects_ = {};
};

} // namespace mmp
