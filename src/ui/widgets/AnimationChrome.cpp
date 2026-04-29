#include "AnimationChrome.h"
#include "ui/AnimationState.h"
#include "ui/IconAudio.h"

#include <FL/Fl_SVG_Image.H>
#include <FL/fl_draw.H>

#include <cstdio>

namespace mmp {

namespace {

inline Fl_Color colText() { return fl_rgb_color(0x4a, 0x24, 0x08); }

const char* channelDisplayName(unsigned char ch) {
    static char buf[16];
    if (ch == 9) return "Drums";
    std::snprintf(buf, sizeof(buf), "Ch.%d", (int)ch + 1);
    return buf;
}

// Cached, sized rasters of the chrome glyphs. Same lazy-cache pattern
// MmpIconButton uses; sized once at first use, no per-frame copy().
Fl_SVG_Image* sizedIcon(const char* glyph, int sz) {
    static struct Cached {
        const char*    g;
        int            sz;
        Fl_SVG_Image*  img;
    } cache[8];
    static int count = 0;
    for (int i = 0; i < count; ++i) {
        if (cache[i].sz == sz && cache[i].g == glyph) return cache[i].img;
    }
    auto* base = iconAudio(glyph);
    auto* img  = (Fl_SVG_Image*)base->copy(sz, sz);
    img->resize(sz, sz);
    if (count < (int)(sizeof(cache) / sizeof(cache[0]))) {
        cache[count++] = { glyph, sz, img };
    }
    return img;
}

} // namespace

AnimationChromeRects drawAnimationChrome(int x, int y, int w, int h,
                                         const AnimationState& state) {
    AnimationChromeRects rects;

    // Single icon size for every glyph in the chrome row — pause, the
    // four channel triangles, and the optional Auto-track LED — all
    // share `iconSz` so the cluster reads as a uniform transport bar.
    constexpr int rowH      = 20;
    constexpr int iconSz    = 16;
    constexpr int triSz     = iconSz;     // hit-rect width per glyph
    constexpr int triPad    = 4;
    constexpr int rowBotPad = 2;

    const int rowY  = y + h - rowH - rowBotPad;
    const int rxR   = x + w - 6;
    const int cyRow = rowY + rowH / 2;

    // Pause / play button — always shown, anchored at the right edge.
    // Glyphs come from fontaudio (fad-pause / fad-play) via the embedded
    // SVG table.
    rects.pause = { rxR - triSz, rowY, triSz, rowH };
    {
        const int top = cyRow - iconSz / 2;
        auto* img = sizedIcon(
            state.paused ? icon_name::Play : icon_name::Pause, iconSz);
        img->draw(rects.pause.x, top);
    }

    // Channel selector chrome — hidden entirely when an engine-wide
    // solo is active (selection is locked, so prev/next/auto would be
    // no-ops and visually misleading). Layout, right-to-left from the
    // pause button:
    //
    //   [Auto] [⏪] [<] [Ch.X of N] [>] [⏩] [pause]
    //
    // The ⏪/⏩ skip buttons jump to the prev/next channel that's
    // currently producing notes; [Auto] is a toggle that delegates the
    // selection to the auto-track tick.
    if (state.soloChannel == 0) {
        char info[64];
        if (state.activeChannels.empty()) {
            std::snprintf(info, sizeof(info), "n/a");
        } else {
            std::snprintf(info, sizeof(info), "%s of %zu",
                          channelDisplayName(state.activeChannels[state.channelIdx]),
                          state.activeChannels.size());
        }
        fl_font(FL_HELVETICA, 10);
        const int infoW   = (int)fl_width(info) + 8;
        const int autoW   = (int)fl_width("Auto") + 10;

        // Right-to-left placement. Every glyph rect is iconSz wide so
        // the visible mass of each fontaudio transport icon is consistent.
        int sx = rects.pause.x - 6;
        rects.nextContent = { sx - triSz, rowY, triSz, rowH };
        sx = rects.nextContent.x - 2;
        rects.next = { sx - triSz, rowY, triSz, rowH };
        sx = rects.next.x - triPad;
        const int infoX = sx - infoW;
        sx = infoX - triPad;
        rects.prev = { sx - triSz, rowY, triSz, rowH };
        sx = rects.prev.x - 2;
        rects.prevContent = { sx - triSz, rowY, triSz, rowH };
        sx = rects.prevContent.x - 6;
        rects.autoTrack = { sx - autoW, rowY, autoW, rowH };

        auto drawSizedIcon = [&](const AnimationChromeRects::R& r,
                                 const char* slug) {
            const int top = cyRow - iconSz / 2;
            const int gx  = r.x + (r.w - iconSz) / 2;
            auto* img = sizedIcon(slug, iconSz);
            img->draw(gx, top);
        };

        // All four channel-step glyphs come from fontaudio so the
        // selector reads as a coherent transport-style cluster.
        drawSizedIcon(rects.prev,        "fad-prev");
        drawSizedIcon(rects.next,        "fad-next");
        drawSizedIcon(rects.prevContent, "fad-rew");
        drawSizedIcon(rects.nextContent, "fad-ffwd");

        fl_color(colText());
        fl_draw(info, infoX, rowY, infoW, rowH,
                FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

        // Auto-track toggle — flat label that fills its rect with the
        // text colour when on, draws the text in colText() when off.
        // No bevel: keeps the chrome row uncluttered.
        if (state.autoTrack) {
            fl_color(colText());
            fl_rectf(rects.autoTrack.x, rects.autoTrack.y + 2,
                     rects.autoTrack.w, rects.autoTrack.h - 4);
            fl_color(fl_rgb_color(0xfa, 0xf0, 0xe0));      // light cream
        } else {
            fl_color(fl_rgb_color(0x80, 0x80, 0x80));
            fl_rect(rects.autoTrack.x, rects.autoTrack.y + 2,
                    rects.autoTrack.w, rects.autoTrack.h - 4);
            fl_color(colText());
        }
        fl_draw("Auto",
                rects.autoTrack.x, rects.autoTrack.y,
                rects.autoTrack.w, rects.autoTrack.h,
                FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    }

    return rects;
}

bool handleAnimationChromeClick(int mx, int my, unsigned int nowMs,
                                AnimationState& state,
                                const AnimationChromeRects& r) {
    auto inside = [&](const AnimationChromeRects::R& rr) {
        return rr.w > 0 && rr.h > 0 &&
               mx >= rr.x && mx < rr.x + rr.w &&
               my >= rr.y && my < rr.y + rr.h;
    };
    if (inside(r.pause))       { state.paused = !state.paused; return true; }
    if (inside(r.autoTrack))   { state.autoTrack = !state.autoTrack; return true; }
    if (inside(r.prev))        { state.cycleChannel(-1); return true; }
    if (inside(r.next))        { state.cycleChannel(+1); return true; }
    if (inside(r.prevContent)) {
        state.cycleChannelWithContent(-1, nowMs, kAnimChromeStaleMs);
        return true;
    }
    if (inside(r.nextContent)) {
        state.cycleChannelWithContent(+1, nowMs, kAnimChromeStaleMs);
        return true;
    }
    return false;
}

} // namespace mmp
