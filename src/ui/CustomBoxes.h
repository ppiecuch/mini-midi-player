#pragma once

// Custom FLTK box types registered at startup. Lets us style elements
// with a project-specific bevel without forking widget classes —
// callers just pass the box-type enum to fl_draw_box() (or set it via
// widget->box()).

#include <FL/Enumerations.H>

namespace mmp {

// Inset LCD-style bevel: 1-px dark top/left + 1-px light bottom/right
// outer rim and a flat-fill interior in the supplied colour. Used by
// MmpDigitalCounter; suitable for any "screen" widget that wants to
// read as a recessed display rather than a raised button.
extern Fl_Boxtype FL_USER_LCD;

// Call once at startup, after Fl::scheme() but before any widget
// creation. Idempotent.
void registerCustomBoxes();

} // namespace mmp
