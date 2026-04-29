#include "MidiInspector.h"
#include "midi/SmfDump.h"

#include <FL/fl_ask.H>

#include <cstdio>
#include <sstream>

namespace mmp {

MidiInspector::MidiInspector()
    : Fl_Double_Window(640, 480, "MIDI File Inspector") {
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

MidiInspector::~MidiInspector() {
    if (display_) display_->buffer(nullptr);
    delete buffer_;
}

void MidiInspector::loadFromFile(const std::string& midiPath) {
    SmfReport report;
    if (!parseSmf(midiPath, report)) {
        char hdr[512];
        std::snprintf(hdr, sizeof(hdr), "  Failed to parse:\n  %s",
                      midiPath.c_str());
        header_->copy_label(hdr);
        buffer_->text("");
        return;
    }

    char hdrBuf[640];
    std::snprintf(hdrBuf, sizeof(hdrBuf),
                  "  MIDI: %s\n  Format %d   Tracks %d   Division %d %s   Notes %d",
                  midiPath.c_str(),
                  report.format, report.numTracks, report.division,
                  report.division > 0 ? "PPQ" : "(SMPTE)",
                  report.totalNotes);
    header_->copy_label(hdrBuf);

    std::ostringstream oss;
    writeSmfReport(report, oss);
    buffer_->text(oss.str().c_str());
    redraw();
}

} // namespace mmp
