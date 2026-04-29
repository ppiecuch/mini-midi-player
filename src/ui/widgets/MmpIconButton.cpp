#include "MmpIconButton.h"
#include "ui/IconAudio.h"

#include <FL/Fl.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/fl_draw.H>

namespace mmp {

namespace {
// Per-(glyph,size) cache so Fl_SVG_Image only re-rasterises when the
// requested pixel box actually changes — typically just at startup.
Fl_SVG_Image* sizedIcon(const char* glyph, int sz) {
    struct Key { const char* g; int sz; };
    static struct Cached {
        const char*    g;
        int            sz;
        Fl_SVG_Image*  img;
    } cache[64];
    static int count = 0;
    for (int i = 0; i < count; ++i) {
        if (cache[i].sz == sz && cache[i].g == glyph) return cache[i].img;
    }
    auto* base = iconAudio(glyph);
    auto* img  = (Fl_SVG_Image*)base->copy(sz, sz);
    img->resize(sz, sz);
    if (count < 64) {
        cache[count++] = { glyph, sz, img };
    }
    return img;
}
} // namespace

MmpIconButton::MmpIconButton(int x, int y, int w, int h,
                             const char* glyphName, int iconSize)
    : Fl_Button(x, y, w, h), glyph_(glyphName), iconSize_(iconSize) {
    box(FL_UP_BOX);
    down_box(FL_DOWN_BOX);
    clear_visible_focus();
}

void MmpIconButton::draw() {
    // Button frame — same rendering FLTK uses, just without its label
    // routing for the image.
    if (value())  fl_draw_box(down_box(),  x(), y(), w(), h(), color());
    else          fl_draw_box(box(),       x(), y(), w(), h(), color());

    auto* img = sizedIcon(glyph_, iconSize_);
    int cx = x() + (w() - iconSize_) / 2;
    int cy = y() + (h() - iconSize_) / 2;
    if (value()) { cx += 1; cy += 1; }   // mirror the down-state offset
    img->draw(cx, cy);
}

} // namespace mmp
