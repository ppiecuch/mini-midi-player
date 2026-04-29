#include "MmpPianoKeyboard.h"
#include "core/MmpEngine.h"
#include "ui/AnimationState.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <chrono>
#include <cstdio>

namespace mmp {

namespace {

// Keyboard range: 5 octaves, C2 (MIDI 36) through C7 (MIDI 96). 36
// white keys total ((5 * 7) + 1 trailing C). Wide enough to catch most
// general MIDI material without crowding individual keys.
constexpr int kRangeLo = 36;       // C2
constexpr int kRangeHi = 96;       // C7
constexpr int kNumWhites = 36;     // 5 octaves * 7 + 1

// MIDI key → semitone offset within the chromatic scale.
inline bool isBlack(int key) {
    static const bool b[12] = { false, true, false, true, false,
                                false, true, false, true, false, true, false };
    return b[key % 12];
}

// Index of the white key that appears immediately to the LEFT of the
// given black key — used to position the black key over the gap.
inline int whiteIndexLeft(int key) {
    static const int leftWhiteOffset[12] = {
        0,           // C   (white)
        0,           //   C# (between C and D — left = C)
        1,           // D
        1,           //   D# (between D and E — left = D)
        2,           // E
        3,           // F
        3,           //   F# (between F and G — left = F)
        4,           // G
        4,           //   G# (between G and A — left = G)
        5,           // A
        5,           //   A# (between A and B — left = A)
        6,           // B
    };
    int oct = key / 12 - kRangeLo / 12;
    return oct * 7 + leftWhiteOffset[key % 12];
}

inline int whiteIndex(int key) {
    static const int whiteSlot[12] = {
        0,    // C
        -1,   // C# (black)
        1,    // D
        -1,
        2,    // E
        3,    // F
        -1,
        4,    // G
        -1,
        5,    // A
        -1,
        6,    // B
    };
    int oct = key / 12 - kRangeLo / 12;
    return oct * 7 + whiteSlot[key % 12];
}

Fl_Color channelColor(int ch) {
    static const Fl_Color t[16] = {
        fl_rgb_color(0x18, 0x60, 0x90), fl_rgb_color(0x18, 0x80, 0x50),
        fl_rgb_color(0x90, 0x30, 0x18), fl_rgb_color(0x60, 0x18, 0x80),
        fl_rgb_color(0x80, 0x60, 0x18), fl_rgb_color(0x18, 0x70, 0x80),
        fl_rgb_color(0x80, 0x40, 0x60), fl_rgb_color(0x40, 0x40, 0x18),
        fl_rgb_color(0x30, 0x40, 0x90), fl_rgb_color(0x20, 0x20, 0x20),
        fl_rgb_color(0x60, 0x80, 0x18), fl_rgb_color(0x80, 0x18, 0x60),
        fl_rgb_color(0x18, 0x40, 0x40), fl_rgb_color(0x60, 0x30, 0x18),
        fl_rgb_color(0x18, 0x18, 0x60), fl_rgb_color(0x60, 0x60, 0x20),
    };
    return t[ch & 15];
}

} // namespace

MmpPianoKeyboard::MmpPianoKeyboard(int x, int y, int w, int h,
                                   MmpEngine& engine, AnimationState& state)
    : Fl_Widget(x, y, w, h, nullptr), engine_(engine), state_(state) {
    tooltip("Click to switch view");
}

int MmpPianoKeyboard::handle(int event) {
    if (event == FL_PUSH) {
        const unsigned int nowMs = (unsigned int)std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        if (handleAnimationChromeClick(Fl::event_x(), Fl::event_y(),
                                       nowMs, state_, chromeRects_)) {
            redraw();
            return 1;
        }
        do_callback();
        return 1;
    }
    return Fl_Widget::handle(event);
}

void MmpPianoKeyboard::update() {
    if (state_.paused) return;
    const unsigned int nowMs = (unsigned int)std::chrono::duration_cast<
        std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    static thread_local MmpEngine::NoteEvent buf[512];
    int n = engine_.drainNoteEvents(buf, 512);
    bool changed = false;
    state_.autoTrackTick(nowMs, /*holdMs=*/800);
    int selChan = state_.currentChannel();
    for (int i = 0; i < n; ++i) {
        const auto& e = buf[i];
        if (e.key >= 128) continue;
        state_.note(e.channel, nowMs);
        if (selChan >= 0 && e.channel != selChan) continue;
        if (e.on) {
            if (!pressed_[e.key] || heldChan_[e.key] != e.channel) {
                pressed_[e.key]  = true;
                heldChan_[e.key] = e.channel;
                changed = true;
            }
        } else {
            if (pressed_[e.key]) { pressed_[e.key] = false; changed = true; }
        }
    }
    if (changed) redraw();
}

void MmpPianoKeyboard::draw() {
    // Beveled inset frame (matches the spectrum / piano-roll panels)
    fl_draw_box(FL_DOWN_FRAME, x(), y(), w(), h(), FL_BACKGROUND_COLOR);

    const int padX = 6, padTop = 6, padBot = kAnimChromeRowH;
    const int areaX = x() + padX;
    const int areaY = y() + padTop;
    const int areaW = w() - 2 * padX;
    const int areaH = h() - padTop - padBot;

    // White-key geometry
    const float whiteW = (float)areaW / (float)kNumWhites;
    const int   whiteH = areaH;
    // Black keys are ~60 % white width and ~62 % white height.
    const int   blackW = (int)(whiteW * 0.60f);
    const int   blackH = (int)(whiteH * 0.62f);

    // ---- White keys (background) -------------------------------------
    for (int wi = 0; wi < kNumWhites; ++wi) {
        int wx = areaX + (int)(wi * whiteW);
        int ww = (int)((wi + 1) * whiteW) - (int)(wi * whiteW);

        // Determine the MIDI key for this white index (start at kRangeLo,
        // then walk the C-D-E-F-G-A-B pattern).
        static const int whiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int oct = wi / 7;
        int key = kRangeLo + oct * 12 + whiteSemi[wi % 7];

        bool pressed = pressed_[key];
        Fl_Color fill = pressed ? channelColor(heldChan_[key])
                                 : fl_rgb_color(0xfa, 0xfa, 0xf2);
        fl_color(fill);
        fl_rectf(wx, areaY, ww - 1, whiteH);

        // Subtle shadow on right edge so adjacent whites don't blur
        fl_color(fl_rgb_color(0xb0, 0xb0, 0xa8));
        fl_yxline(wx + ww - 1, areaY, areaY + whiteH - 1);

        // C-octave label below each C white key
        if (key % 12 == 0) {
            fl_color(fl_rgb_color(0x80, 0x80, 0x80));
            fl_font(FL_HELVETICA, 7);
            char lbl[8];
            std::snprintf(lbl, sizeof(lbl), "C%d", key / 12 - 1);
            fl_draw(lbl, wx + 1, areaY + whiteH - 2);
        }
    }
    // Bottom border under the whites
    fl_color(fl_rgb_color(0x80, 0x80, 0x80));
    fl_xyline(areaX, areaY + whiteH - 1, areaX + areaW - 1);

    // ---- Black keys (overlay) ----------------------------------------
    // Three-band fill (top-cap → body → front-cap) + a 1-px highlight
    // line and outer rim, giving the keys a subtly molded "round-front
    // ebony" feel without committing to a heavy gradient. The bottom
    // band reads as the catch-light along the front of a real key; the
    // top band is the gentle reflection on the slope toward the rear.
    for (int key = kRangeLo; key <= kRangeHi; ++key) {
        if (!isBlack(key)) continue;
        int wiL = whiteIndexLeft(key);
        int boundaryX = areaX + (int)((wiL + 1) * whiteW);
        int bx = boundaryX - blackW / 2;

        bool pressed = pressed_[key];

        // Body / cap colours. Pressed keys borrow the channel hue
        // (slightly darker than the white-key version so blacks still
        // read against their neighbours); unpressed use a deep ebony
        // with two subtle accent bands.
        Fl_Color body, capTop, capBot;
        if (pressed) {
            Fl_Color base = channelColor(heldChan_[key]);
            body   = base;
            capTop = fl_color_average(base, FL_BLACK,  0.55f);
            capBot = fl_color_average(base, FL_WHITE,  0.85f);
        } else {
            body   = fl_rgb_color(0x18, 0x18, 0x1a);
            capTop = fl_rgb_color(0x28, 0x28, 0x2c);   // gentle slope highlight
            capBot = fl_rgb_color(0x52, 0x52, 0x58);   // front catch-light
        }

        const int topCapH = std::max(2, blackH / 6);
        const int botCapH = std::max(2, blackH / 5);

        fl_color(body);
        fl_rectf(bx, areaY + topCapH, blackW, blackH - topCapH - botCapH);
        fl_color(capTop);
        fl_rectf(bx, areaY,                         blackW, topCapH);
        fl_color(capBot);
        fl_rectf(bx, areaY + blackH - botCapH,      blackW, botCapH);

        // Bright 1-px catch-light along the very front edge — this is
        // the strongest cue that the key is rounded.
        if (!pressed) {
            fl_color(fl_rgb_color(0x88, 0x88, 0x90));
            fl_xyline(bx + 1, areaY + blackH - 1, bx + blackW - 2);
        }

        // Top-and-left raised highlight on un-pressed keys so the key
        // still reads as raised against its body colour.
        if (!pressed) {
            fl_color(fl_rgb_color(0x40, 0x40, 0x44));
            fl_xyline(bx, areaY, bx + blackW - 1);
            fl_yxline(bx, areaY, areaY + blackH - 1);
        }

        // Outer rim — keeps the key edge crisp against the channel
        // colour when pressed and against the white keys when idle.
        fl_color(fl_rgb_color(0x08, 0x08, 0x08));
        fl_rect(bx, areaY, blackW, blackH);
    }

    // Bottom-row chrome (pause + channel selector)
    chromeRects_ = drawAnimationChrome(x(), y(), w(), h(), state_);
}

} // namespace mmp
