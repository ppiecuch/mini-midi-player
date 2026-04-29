#include "MmpDigitalCounter.h"
#include "ui/CustomBoxes.h"
#include "ui/IconAudio.h"

#include <FL/Fl.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/fl_draw.H>

#include <cstdio>
#include <cstring>

namespace mmp {

namespace {
// Render-time per-glyph cache so we only ask each SVG to scale once
// per requested pixel size. Map key encodes (icon-name, height).
struct GlyphKey {
    const char* name;
    int h;
    bool operator==(const GlyphKey& o) const noexcept {
        return h == o.h && std::strcmp(name, o.name) == 0;
    }
};

// Returns a per-call cached, properly-rasterised digit image. Using
// Fl_SVG_Image::resize() rather than scale() so the glyph fills the
// requested pixel box exactly (scale() leaves the original 256×256
// viewport untouched, leaving the glyph as a tiny corner blob).
Fl_SVG_Image* sizedDigit(int n, int w, int h) {
    static Fl_SVG_Image* cache[10] = {};
    static int cachedW = 0, cachedH = 0;
    if (cachedW != w || cachedH != h) {
        for (auto*& p : cache) { delete p; p = nullptr; }
        cachedW = w; cachedH = h;
    }
    if (!cache[n]) {
        auto* img = (Fl_SVG_Image*)iconDigit(n)->copy();
        img->resize(w, h);
        cache[n] = img;
    }
    return cache[n];
}
Fl_SVG_Image* sizedColon(int w, int h) {
    static Fl_SVG_Image* cached = nullptr;
    static int cachedW = 0, cachedH = 0;
    if (!cached || cachedW != w || cachedH != h) {
        delete cached;
        cached = (Fl_SVG_Image*)iconAudio(icon_name::DigitColon)->copy();
        cached->resize(w, h);
        cachedW = w; cachedH = h;
    }
    return cached;
}
} // namespace

MmpDigitalCounter::MmpDigitalCounter(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h, nullptr) {
    tooltip("Click to toggle elapsed / remaining");
}

void MmpDigitalCounter::setTimes(unsigned int posMs, unsigned int totalMs) {
    if (posMs == posMs_ && totalMs == totalMs_) return;
    posMs_ = posMs;
    totalMs_ = totalMs;
    redraw();
}

void MmpDigitalCounter::setMode(Mode m) {
    if (m == mode_) return;
    mode_ = m;
    redraw();
}

int MmpDigitalCounter::handle(int event) {
    if (event == FL_PUSH) {
        setMode(mode_ == Mode::Forward ? Mode::Remaining : Mode::Forward);
        return 1;
    }
    return Fl_Widget::handle(event);
}

void MmpDigitalCounter::draw() {
    // Recessed LCD bevel + flat fill via the project-wide custom
    // boxtype (registered at startup). Lets us style the counter as a
    // proper inset display instead of a flat rectangle while still
    // routing through FLTK's box-drawing path so any later scheme
    // change touches it consistently.
    fl_draw_box(FL_USER_LCD, x(), y(), w(), h(), FL_BACKGROUND_COLOR);

    // Compose the 5-character display: M:SS for elapsed, -M:SS for remaining.
    int totalS = (int)(totalMs_ / 1000);
    int posS   = (int)(posMs_   / 1000);
    if (posS > totalS) posS = totalS;
    bool showMinus = false;
    int  shown;
    if (mode_ == Mode::Forward) {
        shown = posS;
    } else {
        shown = totalS - posS;
        showMinus = (shown > 0);
    }
    int mm = shown / 60;
    int ss = shown % 60;
    if (mm > 99) mm = 99;       // 2-digit minutes — clamp to 99:59

    // The fontaudio digit SVGs hold the visible glyph in the central
    // ~50 % of a 256-wide viewBox; overlapping the render boxes by 50 %
    // makes visible glyphs touch edge-to-edge. The colon SVG (128×256)
    // has its dots near the centre of the viewBox; rendered at colonW =
    // glyphH/2 it occupies ~25 % of its render width visibly, so we
    // place its render origin so the dots' centre sits halfway between
    // M2's and S1's visible content with a small gap on each side.
    const int pad         = 1;
    const int glyphH      = h() - 2 * pad;
    const int glyphW      = glyphH;            // square digit render
    const int colonW      = glyphH / 2;        // preserves SVG 1:2 aspect
    const int digitGap    = 2;                 // visible gap between digits
    const int digitStride = glyphW / 2 + digitGap;
    const int gapPx       = 2;                 // visible gap on each side of colon

    // Visible content of a digit lives in [origin + 0.25 W, origin + 0.75 W].
    // Visible content of the colon lives roughly in [origin + 0.383 colonW,
    // origin + 0.633 colonW]. Compute colon and S1 render origins so the
    // visible gaps before and after the colon are equal — and keep the
    // adjacent-digit gap consistent across MM, the colon split, and SS.
    const int M2_visEnd   = digitStride + (glyphW * 3) / 4;        // origin + 0.75W
    const int colonOrigin = M2_visEnd + gapPx                       // visible left
                          - (colonW * 49) / 128;                    // back-step to render origin
    const int colon_visR  = colonOrigin + (colonW * 81) / 128;      // visible right
    const int s1_visL     = colon_visR + gapPx;
    const int s1Origin    = s1_visL - glyphW / 4;
    const int s2Origin    = s1Origin + digitStride;
    const int totalW      = (showMinus ? digitStride : 0)
                          + s2Origin + glyphW;                       // last digit's full extent
    int cx = x() + (w() - totalW) / 2;
    int cy = y() + pad;

    auto blitDigitAt = [&](int n, int offX) {
        sizedDigit(n, glyphW, glyphH)->draw(cx + offX, cy);
    };
    auto blitColonAt = [&](int offX) {
        sizedColon(colonW, glyphH)->draw(cx + offX, cy);
    };

    if (showMinus) {
        fl_color(fl_rgb_color(0x40, 0x40, 0x40));
        int bx = cx + glyphW / 4;
        int by = cy + glyphH / 2;
        fl_line(bx, by, bx + glyphW / 2, by);
        cx += digitStride;
    }
    blitDigitAt(mm / 10, 0);
    blitDigitAt(mm % 10, digitStride);
    blitColonAt(colonOrigin);
    blitDigitAt(ss / 10, s1Origin);
    blitDigitAt(ss % 10, s2Origin);
}

} // namespace mmp
