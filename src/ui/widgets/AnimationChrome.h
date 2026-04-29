#pragma once

// AnimationChrome — bottom-row chrome shared by every animated
// visualizer (piano roll / note glyphs / piano keyboard). Renders a
// pause-or-play button at the right edge and, when no engine-wide
// solo is active, a [<] [Ch.X (i of N)] [>] selector to its left.
//
// Each widget reserves kAnimChromeRowH pixels at the bottom of its
// draw area, calls drawAnimationChrome() at the end of its draw(), and
// forwards FL_PUSH events through handleAnimationChromeClick() before
// falling back to its own (do_callback) view-switch behavior.

namespace mmp {

struct AnimationState;

struct AnimationChromeRects {
    struct R { int x = 0, y = 0, w = 0, h = 0; };
    R pause;
    R prev;
    R next;
    R prevContent;     // ⏪ skip to previous channel with content
    R nextContent;     // ⏩ skip to next channel with content
    R autoTrack;       // [Auto] toggle
};

inline constexpr int kAnimChromeRowH = 22;
// Window in ms used to decide whether a channel is "currently producing
// content". Affects both the ⏪/⏩ skip buttons and the Auto-track tick.
inline constexpr unsigned int kAnimChromeStaleMs = 1500;

AnimationChromeRects drawAnimationChrome(int x, int y, int w, int h,
                                         const AnimationState& state);

bool handleAnimationChromeClick(int mx, int my, unsigned int nowMs,
                                AnimationState& state,
                                const AnimationChromeRects& r);

} // namespace mmp
