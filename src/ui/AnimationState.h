#pragma once

// Shared state that all three animated visualizers (piano roll, note
// glyphs, piano keyboard) read from. Lives in MainWindow; widgets get
// a non-owning pointer in their constructor.
//
//   * activeChannels   set of channels that have actually fired since
//                      load — used to drive the prev/next selector.
//   * channelIdx       index into activeChannels; selected channel =
//                      activeChannels[channelIdx] (or -1 if empty).
//   * paused           when true, every animated visualizer skips its
//                      per-tick update and redraw.
//   * soloChannel      0 = no solo, 1..16 = solo that channel at the
//                      engine level (gated in MmpEngine::noteOn etc.).
//                      When > 0 the per-animation channel filter is
//                      irrelevant (the engine only emits one channel)
//                      and the in-glyphs selector hides itself.

#include <algorithm>
#include <array>
#include <vector>

namespace mmp {

struct AnimationState {
    std::vector<unsigned char> activeChannels;
    // Per-channel last-note timestamp (ms since epoch). Read by the
    // double-triangle skip controls and the Auto-track tick to decide
    // whether a channel currently has visible content.
    std::array<unsigned int, 16> lastNoteMs = {};
    int  channelIdx  = 0;
    bool paused      = false;
    int  soloChannel = 0;
    // When true, the currently-displayed channel auto-jumps to whichever
    // channel is producing notes. Driven from the visible widget's
    // update tick via autoTrackTick().
    bool autoTrack   = false;

    int currentChannel() const {
        if (soloChannel > 0) return soloChannel - 1;        // engine solo wins
        if (activeChannels.empty()) return -1;
        return (int)activeChannels[channelIdx];
    }

    // Register that `ch` has produced a note at time `nowMs`. Idempotent
    // for the active-channel set; the timestamp is always refreshed.
    void note(unsigned char ch, unsigned int nowMs) {
        if (ch < 16) lastNoteMs[ch] = nowMs;
        auto it = std::lower_bound(activeChannels.begin(),
                                   activeChannels.end(), ch);
        if (it == activeChannels.end() || *it != ch) {
            activeChannels.insert(it, ch);
        }
    }

    // Step the animation channel selection. dir = +1 / -1. No-op when
    // soloed (soloChannel takes over) or when no channels are active.
    void cycleChannel(int dir) {
        if (soloChannel > 0 || activeChannels.empty()) return;
        int n = (int)activeChannels.size();
        channelIdx = (channelIdx + dir + n) % n;
    }

    // Skip to the prev/next channel in `activeChannels` whose last note
    // was within `staleMs` of `nowMs` — i.e. one currently producing
    // visible content. Falls back to a plain cycle when no channel
    // qualifies (so the button never feels dead).
    void cycleChannelWithContent(int dir, unsigned int nowMs,
                                 unsigned int staleMs) {
        if (soloChannel > 0 || activeChannels.empty()) return;
        int n = (int)activeChannels.size();
        int i = channelIdx;
        for (int step = 0; step < n; ++step) {
            i = (i + dir + n) % n;
            unsigned char ch = activeChannels[i];
            if (ch < 16 && nowMs - lastNoteMs[ch] < staleMs) {
                channelIdx = i;
                return;
            }
        }
        cycleChannel(dir);
    }

    // Auto-track tick: when `autoTrack` is on and the current channel
    // has been silent for `holdMs`, jump to the channel whose last
    // note is freshest. Returns true if the selection actually moved.
    bool autoTrackTick(unsigned int nowMs, unsigned int holdMs) {
        if (!autoTrack || soloChannel > 0 || activeChannels.empty())
            return false;
        int curCh = currentChannel();
        if (curCh >= 0 && curCh < 16 && nowMs - lastNoteMs[curCh] < holdMs)
            return false;
        int bestIdx = -1;
        unsigned int bestT = 0;
        for (size_t i = 0; i < activeChannels.size(); ++i) {
            unsigned char ch = activeChannels[i];
            if (ch >= 16) continue;
            unsigned int t = lastNoteMs[ch];
            if (t > bestT) { bestT = t; bestIdx = (int)i; }
        }
        if (bestIdx < 0 || bestIdx == channelIdx) return false;
        channelIdx = bestIdx;
        return true;
    }
};

} // namespace mmp
