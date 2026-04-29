#include "MmpEllipsisLabel.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace mmp {

namespace {
constexpr int kInsetPx = 3;     // matches FL_ALIGN_INSIDE | FL_ALIGN_LEFT visuals

std::string ellipsiseToFit(const std::string& s, int maxPx,
                           Fl_Font font, int sz) {
    fl_font(font, sz);
    if ((int)fl_width(s.c_str()) <= maxPx) return s;
    std::string trimmed = s;
    const char* dots = "…";
    while (!trimmed.empty() &&
           (int)fl_width((trimmed + dots).c_str()) > maxPx) {
        trimmed.pop_back();
    }
    return trimmed + dots;
}
} // namespace

MmpEllipsisLabel::MmpEllipsisLabel(int x, int y, int w, int h)
    : Fl_Box(x, y, w, h) {
    box(FL_NO_BOX);
    labelfont(FL_HELVETICA);
    labelsize(10);
    labelcolor(FL_FOREGROUND_COLOR);
    align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);
}

void MmpEllipsisLabel::setText(const std::string& s) {
    if (s == text_) return;
    text_ = s;
    redraw();
}

void MmpEllipsisLabel::clickable(bool b) {
    clickable_ = b;
    tooltip(b ? "Click for inspector" : nullptr);
}

int MmpEllipsisLabel::measureNaturalWidth() const {
    fl_font(labelfont(), labelsize());
    // Match the safety margin used elsewhere for placeholder text:
    // fl_width() can under-report by a few pixels for the trailing
    // glyph (descenders, anti-aliased serifs, accented chars), and
    // FL_ALIGN_INSIDE clips the last sub-pixel by default. +6 extra
    // beyond the symmetric inset prevents the visible "..sf2" / "..mid"
    // truncation that otherwise turned up on Helvetica at size 10.
    return (int)fl_width(text_.c_str()) + 2 * kInsetPx + 6;
}

void MmpEllipsisLabel::draw() {
    fl_color(color());
    fl_rectf(x(), y(), w(), h());
    if (text_.empty()) return;
    fl_color(labelcolor());
    fl_font(labelfont(), labelsize());
    int avail = w() - 2 * kInsetPx;
    if (avail < 1) return;
    std::string s = ellipsiseToFit(text_, avail, labelfont(), labelsize());
    fl_draw(s.c_str(),
            x() + kInsetPx, y(), w() - 2 * kInsetPx, h(),
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
}

int MmpEllipsisLabel::handle(int event) {
    if (clickable_ && event == FL_PUSH) { do_callback(); return 1; }
    return Fl_Box::handle(event);
}

} // namespace mmp
