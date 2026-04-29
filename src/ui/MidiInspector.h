#pragma once

// MidiInspector — modeless developer window showing the structure of a
// loaded Standard MIDI File: header info, per-track listing, tempo /
// time / key signature changes. Backed by mmp::parseSmf.

#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>

#include <string>

namespace mmp {

class MidiInspector : public Fl_Double_Window {
public:
    MidiInspector();
    ~MidiInspector() override;
    void loadFromFile(const std::string& midiPath);

private:
    Fl_Box*           header_;
    Fl_Text_Buffer*   buffer_;
    Fl_Text_Display*  display_;
};

} // namespace mmp
