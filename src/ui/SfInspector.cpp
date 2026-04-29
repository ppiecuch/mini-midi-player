#include "SfInspector.h"
#include "sf2/Sf2Dump.h"

#include <cstdio>
#include <sstream>

namespace mmp {

SfInspector::SfInspector()
    : Fl_Double_Window(620, 460, "SoundFont Inspector") {
    header_ = new Fl_Box(8, 8, w() - 16, 36, "");
    header_->box(FL_FLAT_BOX);
    header_->color(fl_rgb_color(0x18, 0x18, 0x18));
    header_->labelcolor(fl_rgb_color(0xff, 0xa8, 0x40));
    header_->labelfont(FL_COURIER);
    header_->labelsize(11);
    header_->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_WRAP);

    buffer_  = new Fl_Text_Buffer();
    display_ = new Fl_Text_Display(8, 52, w() - 16, h() - 60);
    display_->buffer(buffer_);
    display_->textfont(FL_COURIER);
    display_->textsize(11);

    end();
    resizable(display_);
}

SfInspector::~SfInspector() {
    if (display_) display_->buffer(nullptr);
    delete buffer_;
}

void SfInspector::loadFromFile(const std::string& sf2Path) {
    Sf2DumpOptions opts;
    opts.jsonOutput = false;

    std::ostringstream oss;
    if (!dumpSoundFont(sf2Path, opts, oss)) {
        char hdr[512];
        std::snprintf(hdr, sizeof(hdr),
                      "  Failed to read:\n  %s", sf2Path.c_str());
        header_->copy_label(hdr);
        buffer_->text("");
        return;
    }

    char hdrBuf[512];
    std::snprintf(hdrBuf, sizeof(hdrBuf),
                  "  SoundFont: %s", sf2Path.c_str());
    header_->copy_label(hdrBuf);

    buffer_->text(oss.str().c_str());
    redraw();
}

} // namespace mmp
