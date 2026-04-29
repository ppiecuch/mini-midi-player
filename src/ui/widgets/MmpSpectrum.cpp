#include "MmpSpectrum.h"
#include "core/MmpEngine.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include "kiss_fftr.h"

#include <algorithm>
#include <cmath>

namespace mmp {

namespace {
constexpr float kFloorDb     = -70.0f;
constexpr float kHeadroomDb  =   0.0f;
constexpr float kDecay       =  0.78f;
constexpr int   kHoldTicks   =  18;

// Sound Canvas-style amber LCD palette.
inline Fl_Color colPanel()  { return fl_rgb_color(0xee, 0xa0, 0x40); }
inline Fl_Color colGrid()   { return fl_rgb_color(0xd0, 0x88, 0x30); }
inline Fl_Color colBarLow() { return fl_rgb_color(0x35, 0x60, 0x18); }
inline Fl_Color colBarMid() { return fl_rgb_color(0x70, 0x40, 0x10); }
inline Fl_Color colBarHi()  { return fl_rgb_color(0x80, 0x18, 0x10); }
inline Fl_Color colPeak()   { return fl_rgb_color(0x20, 0x10, 0x08); }
inline Fl_Color colText()   { return fl_rgb_color(0x4a, 0x24, 0x08); }

// Map band index 0..(kBands-1) to a frequency range for log spacing.
float bandHz(int band, float sampleRate, int bands, bool upper) {
    float fLo = 40.0f;
    float fHi = sampleRate * 0.45f;
    float t   = ((float)band + (upper ? 1.0f : 0.0f)) / (float)bands;
    return fLo * std::pow(fHi / fLo, t);
}
} // namespace

int MmpSpectrum::handle(int event) {
    if (event == FL_PUSH) { do_callback(); return 1; }
    return Fl_Widget::handle(event);
}

MmpSpectrum::MmpSpectrum(int x, int y, int w, int h, MmpEngine& engine)
    : Fl_Widget(x, y, w, h, nullptr), engine_(engine),
      bandLevel_(kBands, 0.0f),
      bandPeak_(kBands, 0.0f),
      bandHoldAge_(kBands, 0) {
    cfg_ = kiss_fftr_alloc(kFftSize, /*inverse=*/0, nullptr, nullptr);
    window_.resize(kFftSize);
    for (int n = 0; n < kFftSize; ++n) {
        window_[n] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * n / (kFftSize - 1)));
    }
}

MmpSpectrum::~MmpSpectrum() {
    if (cfg_) { kiss_fftr_free(cfg_); cfg_ = nullptr; }
}

void MmpSpectrum::update() {
    if (!cfg_) return;
    static thread_local std::vector<float> buf;
    if ((int)buf.size() < kFftSize * 2) buf.assign(kFftSize * 2, 0.0f);
    int got = engine_.readAnalyzerTap(buf.data(), kFftSize);
    if (got <= 0) return;

    // Mono-mix and window.
    std::vector<float> mono(kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i) {
        float l = buf[i * 2 + 0], r = buf[i * 2 + 1];
        mono[i] = 0.5f * (l + r) * window_[i];
    }

    std::vector<kiss_fft_cpx> spec(kFftSize / 2 + 1);
    kiss_fftr(cfg_, mono.data(), spec.data());

    const int spectrumBins = kFftSize / 2;
    const float sr = (float)engine_.sampleRate();
    const float binHz = sr / (float)kFftSize;
    const float fftScale = 2.0f / (float)kFftSize;

    for (int b = 0; b < kBands; ++b) {
        float lo = bandHz(b, sr, kBands, false);
        float hi = bandHz(b, sr, kBands, true);
        int   lb = std::max(1, (int)std::floor(lo / binHz));
        int   hb = std::min(spectrumBins, (int)std::ceil(hi / binHz));
        if (hb <= lb) hb = lb + 1;

        float maxMag = 0.0f;
        for (int k = lb; k < hb; ++k) {
            float mag = std::sqrt(spec[k].r * spec[k].r + spec[k].i * spec[k].i) * fftScale;
            if (mag > maxMag) maxMag = mag;
        }
        float db = (maxMag <= 0.0000001f) ? kFloorDb : 20.0f * std::log10(maxMag);
        if (db < kFloorDb)    db = kFloorDb;
        if (db > kHeadroomDb) db = kHeadroomDb;
        float frac = (db - kFloorDb) / (kHeadroomDb - kFloorDb);

        bandLevel_[b] = std::max(frac, bandLevel_[b] * kDecay);
        if (bandLevel_[b] >= bandPeak_[b]) {
            bandPeak_[b]    = bandLevel_[b];
            bandHoldAge_[b] = 0;
        } else if (++bandHoldAge_[b] > kHoldTicks) {
            bandPeak_[b] = std::max(bandLevel_[b], bandPeak_[b] * 0.92f);
        }
    }

    redraw();
}

void MmpSpectrum::draw() {
    // Beveled inset panel with the Sound Canvas-style amber LCD interior.
    fl_draw_box(FL_DOWN_FRAME, x(), y(), w(), h(), FL_BACKGROUND_COLOR);
    fl_color(colPanel());
    fl_rectf(x() + 2, y() + 2, w() - 4, h() - 4);

    const int padX = 8, padTop = 6, padBot = 14;
    const int areaTop = y() + padTop;
    const int areaBot = y() + h() - padBot;
    const int areaH   = areaBot - areaTop;
    const int areaW   = w() - 2 * padX;
    const int barGap  = 2;
    const int barW    = (areaW - barGap * (kBands - 1)) / kBands;
    const int x0      = x() + padX;

    // Subtle horizontal grid — five evenly-spaced lines from bottom to
    // top (visual rule lines, not dB-tied). Even visual spacing reads
    // better than the previous fixed-dB ticks, which left empty space
    // below the lowest -60 dB line.
    fl_color(colGrid());
    constexpr int kGridLines = 5;
    for (int i = 0; i <= kGridLines; ++i) {
        int yy = areaBot - i * areaH / kGridLines;
        fl_line(x0, yy, x0 + areaW, yy);
    }

    // LED-segment geometry. Each segment is segH px tall with a 1-px
    // dark gap below it; the gap shows the (slightly darker) bar
    // background, giving every band the look of a stacked emitter row.
    constexpr int kSegH   = 2;
    constexpr int kGapH   = 1;
    constexpr int kStride = kSegH + kGapH;
    const int totalSegs   = areaH / kStride;
    const int midStart    = (int)(0.72f * totalSegs);
    const int hiStart     = (int)(0.88f * totalSegs);
    // Trough colour for the gaps — only a hair darker than the amber
    // LCD panel so the unlit dot grid reads as a faint shadow on the
    // body of the meter rather than a dark interruption.
    const Fl_Color colTrough = fl_rgb_color(0xe6, 0x98, 0x38);

    for (int b = 0; b < kBands; ++b) {
        int bx = x0 + b * (barW + barGap);

        // Bar trough — fills the inter-LED gaps for the entire vertical
        // extent so the bar always reads as a column of emitters.
        fl_color(colTrough);
        fl_rectf(bx, areaTop, barW, areaH);

        const int litSegs = (int)(bandLevel_[b] * totalSegs + 0.5f);
        for (int s = 0; s < litSegs && s < totalSegs; ++s) {
            int sy = areaBot - (s + 1) * kStride + kGapH;
            Fl_Color c = colBarLow();
            if      (s >= hiStart)  c = colBarHi();
            else if (s >= midStart) c = colBarMid();
            fl_color(c);
            fl_rectf(bx, sy, barW, kSegH);
        }

        // Peak hold cap aligned to the LED grid.
        const int peakSeg = (int)(bandPeak_[b] * totalSegs + 0.5f);
        if (peakSeg > 0 && peakSeg <= totalSegs) {
            int py = areaBot - peakSeg * kStride + kGapH;
            fl_color(colPeak());
            fl_rectf(bx, py, barW, kSegH);
        }
    }

    // Frequency tick labels along the bottom (decade marks).
    fl_color(colText());
    fl_font(FL_HELVETICA, 7);
    const float sr = (float)engine_.sampleRate();
    static const float tickHz[] = { 100, 500, 1000, 5000, 10000 };
    static const char* tickLbl[] = { "100", "500", "1k", "5k", "10k" };
    const float fLo = 40.0f;
    const float fHi = sr * 0.45f;
    for (int i = 0; i < 5; ++i) {
        if (tickHz[i] < fLo || tickHz[i] > fHi) continue;
        float t = std::log(tickHz[i] / fLo) / std::log(fHi / fLo);
        int xx = x0 + (int)(t * areaW);
        fl_draw(tickLbl[i], xx - 8, y() + h() - 3);
    }

    // ---- Info overlay (top-left) -------------------------------------
    // Transparent — just text directly on the amber panel. Uses a soft
    // dark-brown so it reads clearly without competing with the bars.
    // Skipped when the spectrum panel is too narrow for the text to
    // fit cleanly.
    {
        constexpr int padO = 6;
        constexpr int boxW = 220;
        if (w() < boxW + 2 * padO) return;

        const int innerX = x() + padO;
        int       ty     = y() + padO + 9;
        const Fl_Color soft = fl_rgb_color(0x5c, 0x36, 0x14);   // muted brown
        fl_color(soft);
        fl_font(FL_HELVETICA, 9);
        constexpr int lineH = 11;

        char l[128];
        std::snprintf(l, sizeof(l), "MIDI:  %s",
                      overlay_.fileName.empty() ? "(no MIDI file)"
                                                : overlay_.fileName.c_str());
        fl_draw(l, innerX, ty); ty += lineH;
        std::snprintf(l, sizeof(l), "SF2 :  %s",
                      overlay_.sfName.empty() ? "(no SoundFont)"
                                              : overlay_.sfName.c_str());
        fl_draw(l, innerX, ty); ty += lineH;
        std::snprintf(l, sizeof(l), "%02d:%02d / %02d:%02d   bpm %3d",
                      overlay_.posS / 60, overlay_.posS % 60,
                      overlay_.totalS / 60, overlay_.totalS % 60,
                      overlay_.bpm);
        fl_draw(l, innerX, ty); ty += lineH;
        std::snprintf(l, sizeof(l), "vox %3d   sr %5d   ch %2d/%2d",
                      overlay_.activeVoices, overlay_.sampleRate,
                      overlay_.channelsUsed, overlay_.channelsTotal);
        fl_draw(l, innerX, ty);
    }
}

} // namespace mmp
