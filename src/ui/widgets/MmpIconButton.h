#pragma once

// MmpIconButton — Fl_Button that draws a single SVG icon centered in
// its content area at a controlled pixel size. FLTK's default button
// drawing routes the image through draw_label(), which mis-aligns
// rasterised SVG icons (the rendered glyph ends up at top-left of the
// button content rect rather than centered). This widget bypasses that
// path and just blits the icon at a precomputed centered position.

#include <FL/Fl_Button.H>

namespace mmp {

class MmpIconButton : public Fl_Button {
public:
    MmpIconButton(int x, int y, int w, int h,
                  const char* glyphName, int iconSize);
    void draw() override;

private:
    const char* glyph_;
    int         iconSize_;
};

} // namespace mmp
