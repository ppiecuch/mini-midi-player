#pragma once

// SfInspector — modeless developer window listing every preset in the
// loaded SoundFont (bank/program/name) plus summary stats. Stock FLTK
// widgets only.

#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>

#include <string>

namespace mmp {

class SfInspector : public Fl_Double_Window {
public:
    SfInspector();
    ~SfInspector() override;
    void loadFromFile(const std::string& sf2Path);

private:
    Fl_Box*           header_;
    Fl_Text_Buffer*   buffer_;
    Fl_Text_Display*  display_;
};

} // namespace mmp
