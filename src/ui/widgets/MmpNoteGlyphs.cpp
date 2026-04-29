#include "MmpNoteGlyphs.h"
#include "core/MmpEngine.h"
#include "ui/AnimationState.h"
#include "ui/IconAudio.h"

#include <FL/Fl.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/fl_draw.H>

#include <chrono>
#include <cstdio>

namespace mmp {

namespace {

inline Fl_Color colPanel() { return fl_rgb_color(0xee, 0xa0, 0x40); }
inline Fl_Color colText()  { return fl_rgb_color(0x4a, 0x24, 0x08); }

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

constexpr unsigned int kWindowMs = 4000;

unsigned int nowMs() {
    return (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Pick a glyph based on velocity bucket — louder hits get a heavier symbol.
// Single eighth-note for soft, beamed pair for medium, beamed sixteenths
// for forte; the quarter note is reserved for "low velocity / sustained".
// Glyphs come from the embedded Noto Music SVG set so rendering is
// pixel-identical across platforms (vs. the old Unicode literals which
// fell back to whatever music-symbol font the OS happened to ship).
const char* glyphForVel(unsigned char v) {
    if (v <  40) return "quarter-note";          // ♩ U+2669
    if (v <  80) return "eighth-note";           // ♪ U+266A
    if (v < 110) return "beamed-eighth-notes";   // ♫ U+266B
    return                "beamed-sixteenth-notes"; // ♬ U+266C
}

// Sized-glyph cache — same pattern as MmpIconButton / AnimationChrome.
// Keyed by (slug, size) so a re-rasterise only happens when the bucket
// changes height. Eight slots is enough for our (4 velocity buckets
// × ~2 distinct sizes per session) typical churn.
Fl_SVG_Image* sizedGlyph(const char* slug, int sz) {
    static struct Cached {
        const char*    g;
        int            sz;
        Fl_SVG_Image*  img;
    } cache[16];
    static int count = 0;
    for (int i = 0; i < count; ++i) {
        if (cache[i].sz == sz && cache[i].g == slug) return cache[i].img;
    }
    auto* base = iconAudio(slug);
    auto* img  = (Fl_SVG_Image*)base->copy(sz, sz);
    img->resize(sz, sz);
    if (count < (int)(sizeof(cache) / sizeof(cache[0]))) {
        cache[count++] = { slug, sz, img };
    }
    return img;
}

// MIDI key → diatonic step (C-major scale). Black keys share the step
// of the natural key below them — close enough for a notation feel
// without drawing accidentals.
int midiToStaffStep(int key) {
    static const int diatonic[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    int oct = key / 12 - 1;          // MIDI octave (C4 = octave 4)
    return oct * 7 + diatonic[key % 12];
}

} // namespace

MmpNoteGlyphs::MmpNoteGlyphs(int x, int y, int w, int h,
                             MmpEngine& engine, AnimationState& state)
    : Fl_Widget(x, y, w, h, nullptr), engine_(engine), state_(state) {
    tooltip("Click to switch view (Note glyphs ↔ Piano roll ↔ Keyboard)");
}

void MmpNoteGlyphs::update() {
    if (state_.paused) return;
    unsigned int prevNow = nowMs_;
    nowMs_ = nowMs();
    state_.autoTrackTick(nowMs_, /*holdMs=*/800);
    static thread_local MmpEngine::NoteEvent buf[512];
    int n = engine_.drainNoteEvents(buf, 512);
    int added = 0, popped = 0;
    for (int i = 0; i < n; ++i) {
        if (!buf[i].on) continue;
        hits_.push_back({ buf[i].t_ms, buf[i].channel, buf[i].key, buf[i].velocity });
        ++added;
        state_.note(buf[i].channel, nowMs_);
    }
    while (!hits_.empty() && (nowMs_ - hits_.front().t_ms) > kWindowMs) {
        hits_.pop_front();
        ++popped;
    }
    constexpr size_t kMaxHits = 4096;
    while (hits_.size() > kMaxHits) { hits_.pop_front(); ++popped; }

    if (added == 0 && popped == 0 && nowMs_ == prevNow) return;
    redraw();
}

void MmpNoteGlyphs::draw() {
    // Same rounded bevel as the spectrum panel, transparent interior.
    fl_draw_box(FL_DOWN_FRAME, x(), y(), w(), h(), FL_BACKGROUND_COLOR);

    // Reserve exactly enough room for the bottom selector row; the
    // staff above gets every other pixel so it reads as large as
    // possible. (padTop is small — there's no top label in this view.)
    const int padX = 4, padTop = 2, padBot = kAnimChromeRowH;
    const int areaX = x() + padX;
    const int areaY = y() + padTop;
    const int areaW = w() - 2 * padX;
    const int areaH = h() - padTop - padBot;

    const float pxPerMs = (float)areaW / (float)kWindowMs;

    // ---- Grand-staff layout ------------------------------------------
    // Treble lines: E4(30) G4(32) B4(34) D5(36) F5(38)
    // Bass   lines: G2(18) B2(20) D3(22) F3(24) A3(26)
    // Each step = ~1/22 of areaH; lines are 2 steps apart. The y of
    // every visible note (and the staff lines) flows from a single
    // mapping: y = areaY + ((topStep - step) * pxPerStep) + topMargin.
    constexpr int topStep    = 40;      // a couple of steps above F5
    constexpr int botStep    = 16;      // a couple of steps below G2
    const     int totalSteps = topStep - botStep;        // 24
    const     float pxPerStep = (float)areaH / (float)totalSteps;
    auto stepToY = [&](int s) {
        return areaY + (int)((topStep - s) * pxPerStep);
    };

    // Faint staff-area background — slightly darker than the window
    // so it reads as a panel without being eye-catching.
    fl_color(fl_rgb_color(0xd8, 0xd2, 0xc6));
    int trebleTop = stepToY(38) - 2;
    int trebleBot = stepToY(30) + 2;
    int bassTop   = stepToY(26) - 2;
    int bassBot   = stepToY(18) + 2;
    fl_rectf(areaX, trebleTop, areaW, trebleBot - trebleTop);
    fl_rectf(areaX, bassTop,   areaW, bassBot   - bassTop);

    // The five lines of each clef
    fl_color(fl_rgb_color(0x80, 0x80, 0x80));
    for (int s : {30, 32, 34, 36, 38}) {
        int yy = stepToY(s);
        fl_line(areaX, yy, areaX + areaW, yy);
    }
    for (int s : {18, 20, 22, 24, 26}) {
        int yy = stepToY(s);
        fl_line(areaX, yy, areaX + areaW, yy);
    }

    // Clef glyphs anchored at the start of each staff. Treble (G clef)
    // wraps line 30 (= G4); bass (F clef) sits between lines 24 (F3)
    // and 26.
    {
        const int trebleH = stepToY(30) - stepToY(38) + 4;       // 5 lines tall
        const int bassH   = stepToY(18) - stepToY(26) + 4;
        const int trebleY = stepToY(38) - 2;
        const int bassY   = stepToY(26) - 2;

        fl_color(colText());
        sizedGlyph("musical-symbol-g-clef", trebleH)->draw(areaX + 2, trebleY);
        sizedGlyph("musical-symbol-f-clef", bassH  )->draw(areaX + 2, bassY);
    }
    // Middle-C ledger hint (single dotted line at step 28)
    fl_line_style(FL_DOT, 1);
    fl_color(fl_rgb_color(0xc0, 0xc0, 0xc0));
    int midC = stepToY(28);
    fl_line(areaX, midC, areaX + areaW, midC);
    fl_line_style(0);

    // Beat / measure grid (vertical) — 1 s ticks, stronger every 4 s.
    for (int t_ms = 0; t_ms <= (int)kWindowMs; t_ms += 1000) {
        int xx = areaX + areaW - (int)(t_ms * pxPerMs);
        if (xx <= areaX) continue;
        bool measure = (t_ms % 4000) == 0;
        fl_color(measure ? fl_rgb_color(0x9a, 0x9a, 0x9a)
                         : fl_rgb_color(0xd8, 0xd8, 0xd8));
        fl_line(xx, areaY, xx, areaY + areaH);
    }

    // "Now" line on the right
    fl_color(colText());
    fl_line(areaX + areaW - 1, areaY, areaX + areaW - 1, areaY + areaH);

    // ---- Notes — only the currently-selected channel -----------------
    int selChan = state_.currentChannel();
    for (const auto& hit : hits_) {
        if ((nowMs_ - hit.t_ms) > kWindowMs) continue;
        if (selChan >= 0 && hit.channel != selChan) continue;

        int gx = areaX + areaW - (int)((nowMs_ - hit.t_ms) * pxPerMs);
        if (gx < areaX) continue;

        int step = midiToStaffStep(hit.key);
        if (step < botStep || step > topStep) continue;       // outside staff
        int gy = stepToY(step);

        int sz = 12 + (hit.velocity * 10) / 127;
        // Note glyphs render in the channel colour. Fl_SVG_Image
        // interprets `fill="currentColor"` as the foreground colour set
        // before draw(); set fl_color() to the channel hue so each
        // glyph reads as belonging to its MIDI channel.
        fl_color(channelColor(hit.channel));
        auto* img = sizedGlyph(glyphForVel(hit.velocity), sz);
        img->draw(gx - sz / 2, gy - sz / 2);
    }

    // ---- Bottom-row chrome (pause + channel selector) ---------------
    chromeRects_ = drawAnimationChrome(x(), y(), w(), h(), state_);

    // ---- Bottom-left: most recent note info on the visible channel --
    // Walk the deque tail-first so we land on the most recent hit that
    // actually belongs to the currently-selected channel.
    const Hit* recent = nullptr;
    if (!hits_.empty() && selChan >= 0) {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (it->channel == selChan) { recent = &*it; break; }
        }
    } else if (!hits_.empty()) {
        recent = &hits_.back();
    }

    static const char* kNoteName[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    char nbuf[64];
    if (recent) {
        int oct = recent->key / 12 - 1;
        std::snprintf(nbuf, sizeof(nbuf),
                      "♪ %s%d  key %3d  vel %3d",
                      kNoteName[recent->key % 12], oct,
                      (int)recent->key, (int)recent->velocity);
    } else {
        std::snprintf(nbuf, sizeof(nbuf), "♪ —");
    }
    fl_color(colText());
    fl_font(FL_HELVETICA, 10);
    const int infoRowY = y() + h() - kAnimChromeRowH;
    fl_draw(nbuf, x() + 8, infoRowY, 220, kAnimChromeRowH,
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

int MmpNoteGlyphs::handle(int event) {
    if (event == FL_PUSH) {
        if (handleAnimationChromeClick(Fl::event_x(), Fl::event_y(),
                                       nowMs_, state_, chromeRects_)) {
            redraw();
            return 1;
        }
        do_callback();
        return 1;
    }
    return Fl_Widget::handle(event);
}

} // namespace mmp
