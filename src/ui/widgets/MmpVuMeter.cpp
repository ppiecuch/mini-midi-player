#include "MmpVuMeter.h"
#include "core/MmpEngine.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmp {

namespace {
constexpr int kReadFrames     = 1024;
constexpr float kDecayPerTick = 0.92f;
constexpr int kHoldTicks      = 24;
constexpr float kFloorDb      = -60.0f;
constexpr float kHeadroomDb   =   3.0f;

// Subtle palette — VU sits on the window background (no panel/bezel) so
// it reads as a quiet meter rather than a heavy gauge. Colours are mid-
// tone so they show on the standard light-grey FLTK background.
inline Fl_Color colTrough() { return fl_rgb_color(0xc0, 0xc0, 0xc0); }
inline Fl_Color colSegLow() { return fl_rgb_color(0x18, 0x90, 0x30); }
inline Fl_Color colSegMid() { return fl_rgb_color(0xc8, 0xa0, 0x10); }
inline Fl_Color colSegHi()  { return fl_rgb_color(0xc8, 0x20, 0x18); }
inline Fl_Color colHold()   { return fl_rgb_color(0x40, 0x40, 0x40); }
inline Fl_Color colText()   { return fl_rgb_color(0x60, 0x60, 0x60); }
}

MmpVuMeter::MmpVuMeter(int x, int y, int w, int h, MmpEngine& engine)
    : Fl_Widget(x, y, w, h, nullptr), engine_(engine) {}

void MmpVuMeter::update() {
    static thread_local std::vector<float> buf;
    if ((int)buf.size() < kReadFrames * 2) buf.assign(kReadFrames * 2, 0.0f);
    int got = engine_.readAnalyzerTap(buf.data(), kReadFrames);
    if (got <= 0) return;

    float peakL = 0, peakR = 0;
    double sumL = 0, sumR = 0;
    for (int i = 0; i < got; ++i) {
        float l = buf[i * 2 + 0], r = buf[i * 2 + 1];
        peakL = std::max(peakL, std::fabs(l));
        peakR = std::max(peakR, std::fabs(r));
        sumL += (double)l * l;
        sumR += (double)r * r;
    }
    float rmsL = std::sqrt((float)(sumL / got));
    float rmsR = std::sqrt((float)(sumR / got));

    // Peak follows attack instantly, decays exponentially.
    peakL_ = std::max(peakL, peakL_ * kDecayPerTick);
    peakR_ = std::max(peakR, peakR_ * kDecayPerTick);
    rmsL_  = std::max(rmsL,  rmsL_  * kDecayPerTick);
    rmsR_  = std::max(rmsR,  rmsR_  * kDecayPerTick);

    // Peak-hold: latches the highest peak, releases after kHoldTicks.
    if (peakL_ >= holdL_) { holdL_ = peakL_; holdAgeL_ = 0; }
    else if (++holdAgeL_ > kHoldTicks) holdL_ = std::max(peakL_, holdL_ * 0.85f);
    if (peakR_ >= holdR_) { holdR_ = peakR_; holdAgeR_ = 0; }
    else if (++holdAgeR_ > kHoldTicks) holdR_ = std::max(peakR_, holdR_ * 0.85f);

    redraw();
}

namespace {
float sampleToBarFrac(float s) {
    // Linear amplitude → dBFS → 0..1 across [kFloorDb, kHeadroomDb].
    float db = (s <= 0.000001f) ? kFloorDb : 20.0f * std::log10(s);
    if (db < kFloorDb) db = kFloorDb;
    if (db > kHeadroomDb) db = kHeadroomDb;
    return (db - kFloorDb) / (kHeadroomDb - kFloorDb);
}
} // namespace

void MmpVuMeter::draw() {
    // Layout: bars run the full widget height; the small "L" / "R"
    // labels are drawn over the top of each bar (where the bar trough
    // shows through 99% of the time, since peaks rarely reach the top).
    const int padX     = 2;
    const int labelH   = 9;        // overlay band at the top of each bar
    const int centerW  = 16;       // gap between bars for dB scale numbers

    const int barAreaTop    = y();
    const int barAreaBottom = y() + h();
    const int barAreaH      = barAreaBottom - barAreaTop;

    const int barW = (w() - 2 * padX - centerW) / 2;
    const int xL   = x() + padX;
    const int xR   = xL + barW + centerW;
    const int cx   = xL + barW;     // start of the centre dB scale strip

    // LED-segment geometry. Each segment is segH px tall with a 1-px
    // dark gap below it; the trough fill behind the bar is what shows
    // through the gap so the segments read as a stack of emitters.
    constexpr int kSegH    = 3;
    constexpr int kGapH    = 1;
    constexpr int kStride  = kSegH + kGapH;

    auto drawChannel = [&](int bx, float peak, float rms, float hold, const char* label) {
        // Subtle 1-px outline — interior stays the window background so
        // the meter reads as transparent. The unlit gaps between LEDs
        // simply show the window through them.
        fl_color(colTrough());
        fl_rect(bx, barAreaTop, barW, barAreaH);

        float peakFrac = sampleToBarFrac(peak);
        float rmsFrac  = sampleToBarFrac(rms);
        float holdFrac = sampleToBarFrac(hold);

        const int totalSegs = barAreaH / kStride;
        const int litSegs   = (int)(peakFrac * totalSegs + 0.5f);
        const int midStart  = (int)(0.72f * totalSegs);
        const int hiStart   = (int)(0.88f * totalSegs);

        for (int s = 0; s < litSegs && s < totalSegs; ++s) {
            int sy = barAreaBottom - (s + 1) * kStride + kGapH;
            Fl_Color c = colSegLow();
            if      (s >= hiStart)  c = colSegHi();
            else if (s >= midStart) c = colSegMid();
            fl_color(c);
            fl_rectf(bx, sy, barW, kSegH);
        }

        // RMS marker — thin dark band inside the bar
        int rmsY = barAreaBottom - (int)(rmsFrac * barAreaH);
        if (rmsY < barAreaBottom) {
            fl_color(colHold());
            fl_rectf(bx + 1, rmsY, barW - 2, 1);
        }
        // Peak-hold cap — drawn aligned to the LED grid so it looks
        // like a single brighter segment.
        int holdSeg = (int)(holdFrac * totalSegs + 0.5f);
        if (holdSeg > 0 && holdSeg <= totalSegs) {
            int hy = barAreaBottom - holdSeg * kStride + kGapH;
            fl_color(colHold());
            fl_rectf(bx, hy, barW, kSegH);
        }
        // Channel label centered above this bar.
        fl_color(colText());
        fl_font(FL_HELVETICA_BOLD, 8);
        fl_draw(label, bx, y(), barW, labelH, FL_ALIGN_CENTER);
    };

    drawChannel(xL, peakL_, rmsL_, holdL_, "L");
    drawChannel(xR, peakR_, rmsR_, holdR_, "R");

    // dB scale in the centre strip between the two bars. Labels are tiny
    // (size 7) so the column can stay narrow.
    fl_color(colText());
    fl_font(FL_HELVETICA, 7);
    static const struct { int db; const char* s; } marks[] = {
        {  0, "0"  }, { -6, "-6" }, { -12, "-12" }, { -24, "-24" }, { -48, "-48" }
    };
    for (auto& m : marks) {
        float frac = ((float)m.db - kFloorDb) / (kHeadroomDb - kFloorDb);
        if (frac < 0.0f || frac > 1.0f) continue;
        int yy = barAreaBottom - (int)(frac * barAreaH);
        fl_draw(m.s, cx, yy - 4, centerW, 8, FL_ALIGN_CENTER);
    }
}

} // namespace mmp
