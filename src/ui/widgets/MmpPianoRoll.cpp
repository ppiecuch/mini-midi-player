#include "MmpPianoRoll.h"
#include "core/MmpEngine.h"
#include "ui/AnimationState.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <chrono>
#include <vector>

namespace mmp {

namespace {

// Same Sound-Canvas amber palette as the spectrum widget.
inline Fl_Color colPanel()  { return fl_rgb_color(0xee, 0xa0, 0x40); }
inline Fl_Color colGrid()   { return fl_rgb_color(0xd0, 0x88, 0x30); }
inline Fl_Color colText()   { return fl_rgb_color(0x4a, 0x24, 0x08); }

// 16-channel palette — distinguishable hues, deep enough to read on amber.
Fl_Color channelColor(int ch) {
    static const Fl_Color t[16] = {
        fl_rgb_color(0x18, 0x60, 0x90),  // ch1 blue
        fl_rgb_color(0x18, 0x80, 0x50),  // ch2 green
        fl_rgb_color(0x90, 0x30, 0x18),  // ch3 red
        fl_rgb_color(0x60, 0x18, 0x80),  // ch4 violet
        fl_rgb_color(0x80, 0x60, 0x18),  // ch5 olive
        fl_rgb_color(0x18, 0x70, 0x80),  // ch6 teal
        fl_rgb_color(0x80, 0x40, 0x60),  // ch7 magenta-brown
        fl_rgb_color(0x40, 0x40, 0x18),  // ch8 dark olive
        fl_rgb_color(0x30, 0x40, 0x90),  // ch9 indigo
        fl_rgb_color(0x20, 0x20, 0x20),  // ch10 (drums) near-black
        fl_rgb_color(0x60, 0x80, 0x18),  // ch11 lime
        fl_rgb_color(0x80, 0x18, 0x60),  // ch12 pink
        fl_rgb_color(0x18, 0x40, 0x40),  // ch13 deep teal
        fl_rgb_color(0x60, 0x30, 0x18),  // ch14 brown
        fl_rgb_color(0x18, 0x18, 0x60),  // ch15 navy
        fl_rgb_color(0x60, 0x60, 0x20),  // ch16 mustard
    };
    return t[ch & 15];
}

constexpr unsigned int kWindowMs = 4000;   // 4 s of recent history

unsigned int nowMs() {
    return (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline unsigned activeKey(unsigned char ch, unsigned char key) {
    return (unsigned)ch * 128u + (unsigned)key;
}

} // namespace

MmpPianoRoll::MmpPianoRoll(int x, int y, int w, int h,
                           MmpEngine& engine, AnimationState& state)
    : Fl_Widget(x, y, w, h, nullptr), engine_(engine), state_(state) {
    tooltip("Click to switch view");
}

int MmpPianoRoll::handle(int event) {
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

void MmpPianoRoll::update() {
    if (state_.paused) return;
    unsigned int prevNow = nowMs_;
    nowMs_ = nowMs();
    state_.autoTrackTick(nowMs_, /*holdMs=*/800);

    // Drain note events. Each pop_front shifts every index in active_,
    // so when ANY pops happen we rebuild active_ from scratch — but
    // only then. Previously we rebuilt unconditionally every tick which
    // was the dominant cost during fast-tempo playback.
    static thread_local MmpEngine::NoteEvent buf[512];
    int n = engine_.drainNoteEvents(buf, 512);
    int popped = 0;

    for (int i = 0; i < n; ++i) {
        const auto& e = buf[i];
        unsigned k = activeKey(e.channel, e.key);
        state_.note(e.channel, nowMs_);
        if (e.on) {
            notes_.push_back({ e.t_ms, 0, e.channel, e.key, e.velocity });
            active_[k] = notes_.size() - 1;
        } else {
            auto it = active_.find(k);
            if (it != active_.end() && it->second < notes_.size()) {
                notes_[it->second].offMs = e.t_ms;
                active_.erase(it);
            }
        }
    }

    // Drop spans whose off-time is older than the window.
    while (!notes_.empty()) {
        const NoteSpan& s = notes_.front();
        unsigned int end = s.offMs ? s.offMs : nowMs_;
        if ((nowMs_ - end) > kWindowMs) { notes_.pop_front(); ++popped; }
        else break;
    }

    // Hard cap so a runaway-tempo file can't blow memory: keep at most
    // kMaxSpans entries; if exceeded, drop oldest in bulk and force
    // an active_ rebuild.
    constexpr size_t kMaxSpans = 4096;
    if (notes_.size() > kMaxSpans) {
        size_t excess = notes_.size() - kMaxSpans;
        for (size_t i = 0; i < excess; ++i) notes_.pop_front();
        popped += (int)excess;
    }

    // Only rebuild the active-note map when its indices have actually
    // shifted — i.e. when at least one pop happened this tick.
    if (popped > 0) {
        active_.clear();
        for (size_t i = 0; i < notes_.size(); ++i) {
            if (notes_[i].offMs == 0) {
                active_[activeKey(notes_[i].channel, notes_[i].key)] = i;
            }
        }
    }

    // Smoothed pitch-window centring. Target = median of notes still on
    // screen (within the 4-second window) on the currently-selected
    // channel; if nothing matches we keep the previous target so the
    // view doesn't jitter to far-away or off-screen events. The lerp
    // factor is tuned for ~150-250 ms settle at 30 Hz update.
    const int selChan = state_.currentChannel();
    std::vector<int> visKeys;
    visKeys.reserve(notes_.size());
    for (const auto& s : notes_) {
        if (selChan >= 0 && s.channel != selChan) continue;
        unsigned int end = s.offMs ? s.offMs : nowMs_;
        if (nowMs_ - end > kWindowMs) continue;
        visKeys.push_back(s.key);
    }
    if (!visKeys.empty()) {
        size_t mid = visKeys.size() / 2;
        std::nth_element(visKeys.begin(), visKeys.begin() + mid, visKeys.end());
        float target = (float)visKeys[mid];
        centerKey_ += 0.08f * (target - centerKey_);
    }

    // Skip the redraw when nothing visible has actually changed:
    // no new events drained, no pops, and "now" advanced by < 1 ms
    // (less than a sub-pixel scroll at 4 s / areaW).
    if (n == 0 && popped == 0 && nowMs_ == prevNow) return;
    redraw();
}

void MmpPianoRoll::draw() {
    // Same beveled frame as the spectrum (matches its rounded corners),
    // but skip the inner fill so the panel stays transparent — VU-meter
    // style, just with a defined edge.
    fl_draw_box(FL_DOWN_FRAME, x(), y(), w(), h(), FL_BACKGROUND_COLOR);

    const int padX = 4, padTop = 4, padBot = kAnimChromeRowH;
    const int areaX  = x() + padX;
    const int areaY  = y() + padTop;
    const int areaW  = w() - 2 * padX;
    const int areaH  = h() - padTop - padBot;

    // Pitch window: 36 semitones centred on the smoothed median of
    // visible notes on the selected channel. centerKey_ is updated in
    // update() with a low-pass lerp so the panel pans rather than
    // snapping when the median jumps around.
    int mid = (int)(centerKey_ + 0.5f);
    int minKey = std::max(0,   mid - 18);
    int maxKey = std::min(127, mid + 18);
    const int keyRange = maxKey - minKey + 1;
    const float pxPerKey = (float)areaH / (float)keyRange;
    const float pxPerMs  = (float)areaW / (float)kWindowMs;

    // Octave-band striping (very subtle). Two alternating shades over
    // each 12-semitone slab so the eye locks onto octave boundaries
    // without the grid getting busy.
    for (int k = ((minKey + 11) / 12) * 12 - 12; k <= maxKey; k += 12) {
        if (k > maxKey) break;
        int yTop = areaY + areaH - (int)((k + 12 - minKey) * pxPerKey);
        int yBot = areaY + areaH - (int)((k      - minKey) * pxPerKey);
        if (yTop < areaY) yTop = areaY;
        if (yBot > areaY + areaH) yBot = areaY + areaH;
        if (yBot <= yTop) continue;
        bool altBand = ((k / 12) & 1) == 0;
        fl_color(altBand ? fl_rgb_color(0xe4, 0xe4, 0xe4)
                         : fl_rgb_color(0xee, 0xee, 0xee));
        fl_rectf(areaX, yTop, areaW, yBot - yTop);
    }

    // Octave grid (horizontal). C notes get a stronger line and a
    // pitch label in the left gutter; semitone divisions stay implicit.
    fl_font(FL_HELVETICA, 8);
    for (int k = ((minKey + 11) / 12) * 12; k <= maxKey; k += 12) {
        int yy = areaY + areaH - (int)((k - minKey) * pxPerKey);
        fl_color(fl_rgb_color(0x70, 0x70, 0x70));
        fl_line(areaX, yy, areaX + areaW, yy);
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "C%d", (k / 12) - 1);
        fl_color(colText());
        fl_draw(lbl, areaX + 2, yy - 2);
    }

    // Beat / measure grid (vertical). 1-second tick lines, with a
    // stronger line every 4 seconds (≈ one bar at 60 BPM 4/4).
    for (int t_ms = 0; t_ms <= (int)kWindowMs; t_ms += 1000) {
        int xx = areaX + areaW - (int)(t_ms * pxPerMs);
        if (xx <= areaX) continue;
        bool measure = (t_ms % 4000) == 0;
        fl_color(measure ? fl_rgb_color(0x80, 0x80, 0x80)
                         : fl_rgb_color(0xb0, 0xb0, 0xb0));
        fl_line(xx, areaY, xx, areaY + areaH);
    }

    // Vertical "now" line at the right edge.
    fl_color(colText());
    fl_line(areaX + areaW - 1, areaY, areaX + areaW - 1, areaY + areaH);

    // Draw each span as a coloured rectangle. Filter by the shared
    // animation channel selection (or pass-through when no channel
    // selected / the engine has soloed one).
    int selChan = state_.currentChannel();
    for (const auto& s : notes_) {
        if (selChan >= 0 && s.channel != selChan) continue;
        if (s.key < (unsigned)minKey || s.key > (unsigned)maxKey) continue;
        unsigned int end = s.offMs ? s.offMs : nowMs_;
        if (end < nowMs_ - kWindowMs) continue;
        int x0 = areaX + areaW - (int)((nowMs_ - end)    * pxPerMs);
        int x1 = areaX + areaW - (int)((nowMs_ - s.onMs) * pxPerMs);
        if (x1 < areaX) continue;
        if (x1 > x0)    std::swap(x0, x1);
        int rx = x0;
        // Drum-channel hits (and any zero-duration notes) collapse to
        // 0 px wide; bump every visible event to a minimum 4-px block
        // so percussion isn't invisible.
        int rw = std::max(4, x1 - x0);
        if (rx < areaX) { rw -= (areaX - rx); rx = areaX; }
        if (rw < 1)  continue;

        // Each note rectangle fills its own semitone slot. The previous
        // 1.2× height pushed every note ~20 % into the slot above and
        // made the on-screen content visually drift upward; sticking
        // to the slot height plus a 3-px floor keeps notes centred on
        // their pitch row even when the window is tall.
        int yy = areaY + areaH - (int)((s.key - minKey + 1) * pxPerKey);
        int rh = std::max(3, (int)pxPerKey);

        Fl_Color base = channelColor(s.channel);
        fl_color(base);
        fl_rectf(rx, yy, rw, rh);
        // Subtle 1-px bevel for a raised-key look (skip if too small).
        if (rw > 2 && rh > 2) {
            fl_color(fl_lighter(base));
            fl_xyline(rx, yy, rx + rw - 1);
            fl_yxline(rx, yy, yy + rh - 1);
            fl_color(fl_darker(base));
            fl_xyline(rx, yy + rh - 1, rx + rw - 1);
            fl_yxline(rx + rw - 1, yy, yy + rh - 1);
        }
    }

    // Bottom-row chrome (pause + channel selector) — drawn last so its
    // hit rects reflect the latest layout.
    chromeRects_ = drawAnimationChrome(x(), y(), w(), h(), state_);

    // Bottom-left: pitch range + active count, sharing the chrome row.
    fl_color(colText());
    fl_font(FL_HELVETICA, 10);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "C%d…C%d   active: %d",
                  (minKey / 12) - 1, (maxKey / 12) - 1, (int)active_.size());
    const int infoRowY = y() + h() - kAnimChromeRowH;
    fl_draw(buf, x() + 8, infoRowY, 220, kAnimChromeRowH,
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

} // namespace mmp
