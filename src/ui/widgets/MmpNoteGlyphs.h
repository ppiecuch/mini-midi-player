#pragma once

// MmpNoteGlyphs — playful sibling of MmpPianoRoll. Same engine event
// source, but each note is drawn as a Unicode music glyph (♩ ♪ ♫ ♬)
// scrolling from right to left, colored by MIDI channel.

#include <FL/Fl_Widget.H>

#include <deque>

#include "AnimationChrome.h"

namespace mmp {

class MmpEngine;
struct AnimationState;

class MmpNoteGlyphs : public Fl_Widget {
public:
    MmpNoteGlyphs(int x, int y, int w, int h, MmpEngine& engine,
                  AnimationState& state);
    void update();
    void draw() override;
    int  handle(int event) override;

private:
    struct Hit {
        unsigned int t_ms;
        unsigned char channel;
        unsigned char key;
        unsigned char velocity;
    };

    MmpEngine&            engine_;
    AnimationState&       state_;
    std::deque<Hit>       hits_;
    unsigned int          nowMs_ = 0;
    AnimationChromeRects  chromeRects_ = {};
};

} // namespace mmp
