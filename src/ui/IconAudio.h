#pragma once

// IconAudio — name-based lookup for every embedded SVG glyph. The
// generated headers below provide one `inline constexpr const char*`
// per shipped glyph in their respective `name` sub-namespaces:
//
//     mmp::svg::fontaudio::name::FadPlay  == "fad-play"
//     mmp::svg::opensymbols::name::DeepinOpenSymbolAirplane == "DeepinOpenSymbol/airplane"
//
// Pass any of those (or any free-form lookup key) to iconAudio() to get
// a cached Fl_SVG_Image*. The unified `mmp::icon` namespace re-exports
// both sets so callers can write `iconAudio(icon::FadPlay)` without
// having to remember which font a glyph lives in.

#include "fontaudio_svgs.h"
#include "opensymbols_svgs.h"
#include "notomusic_svgs.h"

class Fl_SVG_Image;

namespace mmp {

// Returns a cached Fl_SVG_Image for the named glyph. Looks in fontaudio
// first, then opensymbols. Always returns non-null — falls back to a
// 16×16 outline placeholder when nothing matches.
Fl_SVG_Image* iconAudio(const char* name);

// Convenience for the digital-counter widget. iconDigit(0)=fad-digital0,
// iconDigit(1)=fad-digital1, … iconDigit(9)=fad-digital9. Out-of-range
// values fall back to digit 0.
Fl_SVG_Image* iconDigit(int n);

// Unified namespace pulling in every glyph identifier. Use this from
// UI code so autocomplete shows every shipped icon at one location.
namespace icon {
    using namespace mmp::svg::fontaudio::name;
    using namespace mmp::svg::opensymbols::name;
    using namespace mmp::svg::notomusic::name;
} // namespace icon

// Short-form constants for the icons used most often (kept for
// readability — `icon::Play` reads better than `icon::FadPlay`).
namespace icon_name {
inline constexpr const char* Play         = svg::fontaudio::name::FadPlay;
inline constexpr const char* Pause        = svg::fontaudio::name::FadPause;
inline constexpr const char* Stop         = svg::fontaudio::name::FadStop;
inline constexpr const char* Loop         = svg::fontaudio::name::FadLoop;
inline constexpr const char* Record       = svg::fontaudio::name::FadRecord;
inline constexpr const char* ArmRecord    = svg::fontaudio::name::FadArmrecording;
inline constexpr const char* MidiPlug     = svg::fontaudio::name::FadMidiplug;
inline constexpr const char* Keyboard     = svg::fontaudio::name::FadKeyboard;
inline constexpr const char* Waveform     = svg::fontaudio::name::FadWaveform;
inline constexpr const char* Headphones   = svg::fontaudio::name::FadHeadphones;
inline constexpr const char* DigitColon   = svg::fontaudio::name::FadDigitalColon;
inline constexpr const char* DigitDot     = svg::fontaudio::name::FadDigitalDot;
} // namespace icon_name

} // namespace mmp
