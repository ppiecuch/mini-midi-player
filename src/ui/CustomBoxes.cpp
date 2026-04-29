#include "CustomBoxes.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace mmp {

Fl_Boxtype FL_USER_LCD = FL_NO_BOX;   // populated by registerCustomBoxes()

namespace {
// Recessed 1-px bevel around a flat-fill interior. Top/left edge in
// FL_DARK3 (shadow falling into the recess), bottom/right in FL_LIGHT3
// (light catching the lower lip). The interior is filled with the
// supplied colour so callers can pick the LCD background tint.
void mmpLcdBoxDraw(int x, int y, int w, int h, Fl_Color c) {
    if (w <= 2 || h <= 2) {
        fl_color(c);
        fl_rectf(x, y, w, h);
        return;
    }
    // Outer rim
    fl_color(FL_DARK3);
    fl_xyline(x,         y,         x + w - 2);     // top
    fl_yxline(x,         y,         y + h - 2);     // left
    fl_color(FL_LIGHT3);
    fl_xyline(x + 1,     y + h - 1, x + w - 1);     // bottom
    fl_yxline(x + w - 1, y + 1,     y + h - 1);     // right

    // Interior fill — leaves the bevel visible.
    fl_color(c);
    fl_rectf(x + 1, y + 1, w - 2, h - 2);
}
} // namespace

void registerCustomBoxes() {
    static bool done = false;
    if (done) return;
    done = true;

    // FL_FREE_BOXTYPE is the first user-allocatable index in FLTK's
    // box-type table. Our LCD type takes that slot; future custom
    // boxes use FL_FREE_BOXTYPE+1, +2, …
    FL_USER_LCD = (Fl_Boxtype)(FL_FREE_BOXTYPE + 0);
    Fl::set_boxtype(FL_USER_LCD, mmpLcdBoxDraw, 1, 1, 2, 2);
}

} // namespace mmp
