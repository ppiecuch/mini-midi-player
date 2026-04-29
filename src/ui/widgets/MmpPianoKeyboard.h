#pragma once

// MmpPianoKeyboard — fourth right-column visualizer mode. Renders a
// standard piano keyboard with white + black keys and highlights any
// keys currently held by the engine. Driven by the same NoteEvent ring
// that feeds MmpPianoRoll / MmpNoteGlyphs, so it auto-tracks per-tick
// just like the others.

#include <FL/Fl_Widget.H>

#include <array>

#include "AnimationChrome.h"

namespace mmp {

class MmpEngine;
struct AnimationState;

class MmpPianoKeyboard : public Fl_Widget {
public:
    MmpPianoKeyboard(int x, int y, int w, int h, MmpEngine& engine,
                     AnimationState& state);
    void update();
    void draw() override;
    int  handle(int event) override;

private:
    MmpEngine&      engine_;
    AnimationState& state_;
    // For each MIDI key, whether currently held + which channel held it
    // (we use channel for the highlight colour). One byte per key.
    std::array<bool, 128>          pressed_  = {};
    std::array<unsigned char, 128> heldChan_ = {};
    AnimationChromeRects chromeRects_ = {};
};

} // namespace mmp
