#pragma once

// MmpEllipsisLabel — flat label-styled clickable widget that draws its
// stored text truncated with "…" to fit the current widget width.
// Behaves like an Fl_Button (clickable, fires its callback on FL_PUSH)
// but draws its own ellipsised text via fl_width-based measurement so
// the visible glyph count adapts to whatever pixel width the layout
// gives it. Setting `clickable(false)` makes it a non-interactive
// label (used for static slots like the output-WAV name).

#include <FL/Fl_Box.H>

#include <string>

namespace mmp {

class MmpEllipsisLabel : public Fl_Box {
public:
    MmpEllipsisLabel(int x, int y, int w, int h);

    void setText(const std::string& s);     // raw text (not ellipsised)
    const std::string& text() const { return text_; }

    void clickable(bool b);                 // toggle FL_PUSH handling
    bool clickable() const { return clickable_; }

    int  measureNaturalWidth() const;       // px width of the full string
    void draw() override;
    int  handle(int event) override;

private:
    std::string text_;
    bool        clickable_ = false;
};

} // namespace mmp
