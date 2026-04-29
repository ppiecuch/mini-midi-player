#pragma once

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Dial.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Light_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Roller.H>
#include <FL/Fl_Sys_Menu_Bar.H>

#include "ui/AnimationState.h"

namespace mmp {

class MmpEngine;
class SmfPlayer;
class MmpVuMeter;
class MmpSpectrum;
class MmpPianoRoll;
class MmpPianoKeyboard;
class MmpNoteGlyphs;
class MmpDigitalCounter;
class MmpEllipsisLabel;
class SfInspector;
class MidiInspector;

// Right-side switchable visualizer. PianoRoll, Glyphs, and Keyboard
// share the right slot beside the spectrum (half-width split);
// SpectrumOnly hides them and stretches the spectrum across the full
// available area.
enum class VisualizerMode { PianoRoll, Glyphs, Keyboard, SpectrumOnly };

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(MmpEngine& engine, SmfPlayer& player);
    ~MainWindow() override;

private:
    // Menu callbacks
    static void onOpenSf2     (Fl_Widget*, void* v);
    static void onOpenMid     (Fl_Widget*, void* v);
    static void onRenderWav   (Fl_Widget*, void* v);
    static void onDumpJson    (Fl_Widget*, void* v);
    static void onShowInspect (Fl_Widget*, void* v);
    static void onShowMidiInsp(Fl_Widget*, void* v);
    static void onConvertSf   (Fl_Widget*, void* v);
    static void onViewRoll    (Fl_Widget*, void* v);
    static void onViewGlyphs  (Fl_Widget*, void* v);
    static void onViewKbd     (Fl_Widget*, void* v);
    static void onViewSpectrum(Fl_Widget*, void* v);
    static void onPanelCycle  (Fl_Widget*, void* v);
    static void onSpectrumClick(Fl_Widget*, void* v);
    static void onLoop        (Fl_Widget*, void* v);
    static void onMetronome   (Fl_Widget*, void* v);
    static void onRecord      (Fl_Widget*, void* v);
    static void onMidiIn      (Fl_Widget*, void* v);
    static void onPickOutputWav(Fl_Widget*, void* v);

    // Status-row label clicks — branch on whether the corresponding
    // file is currently loaded. Loaded → open the matching inspector;
    // unloaded → fall through to the file picker.
    static void onMidNameClick(Fl_Widget*, void* v);
    static void onSfNameClick (Fl_Widget*, void* v);
    static void onAbout       (Fl_Widget*, void* v);
    static void onQuit        (Fl_Widget*, void* v);

    // Transport
    static void onPlay        (Fl_Widget*, void* v);
    static void onPause       (Fl_Widget*, void* v);
    static void onStop        (Fl_Widget*, void* v);

    // Mixer / FX
    static void onVolume      (Fl_Widget*, void* v);
    static void onReverb      (Fl_Widget*, void* v);
    static void onChorus      (Fl_Widget*, void* v);
    static void onPan         (Fl_Widget*, void* v);
    static void onTranspose   (Fl_Widget*, void* v);
    static void onTempoTrim   (Fl_Widget*, void* v);
    static void onChannelSolo (Fl_Widget*, void* v);

    static void onTimer(void* v);
    static void onRestorePersisted(void* v);   // deferred SF auto-load
    void tick();
    void refreshStatus();

    void openSoundFontFile(const char* path);
    void openMidiFile(const char* path);

    // Enable/disable the SoundFont Inspector entry points (menu item +
    // clickable SF status label). Both are inactive until a SoundFont
    // is loaded. The MIDI inspector mirrors the same pattern.
    void setSfInspectorEnabled(bool enabled);
    void setMidiInspectorEnabled(bool enabled);

    // Switch the right-column visualizer between Spectrum / PianoRoll
    // / Glyphs. Hides the others, syncs the View-menu radio state.
    void setVisualizerMode(VisualizerMode m);

    // Re-flow the status row: each MmpEllipsisLabel is resized to its
    // text's natural pixel width (capped) and the dividers / icons /
    // siblings slide along so the row stays packed. Called from
    // refreshStatus() whenever any displayed file name changes.
    void repackStatusRow();

    MmpEngine& engine_;
    SmfPlayer& player_;

    // Top widgets
    Fl_Sys_Menu_Bar* menu_;

    // Transport
    Fl_Progress* posBar_;

    // Mixer
    Fl_Roller*   volume_;
    Fl_Dial*     reverb_;
    Fl_Dial*     chorus_;
    Fl_Dial*     pan_;
    Fl_Counter*  transpose_;
    Fl_Counter*  tempoTrim_;
    Fl_Counter*  channelSolo_ = nullptr;

    // Custom widgets
    MmpVuMeter*    vu_;
    MmpSpectrum*   spectrum_;
    MmpPianoRoll*    pianoRoll_;
    MmpNoteGlyphs*   noteGlyphs_;
    MmpPianoKeyboard* keyboard_ = nullptr;
    MmpDigitalCounter* counter_ = nullptr;
    VisualizerMode vizMode_     = VisualizerMode::Keyboard;
    // The dual-panel mode we were in before going SpectrumOnly — so a
    // spectrum click can restore the previous look exactly.
    VisualizerMode vizPrevDual_ = VisualizerMode::Keyboard;

    // Status row widgets — pointers stored so repackStatusRow() can
    // resize the ellipsis labels to fit their current text and slide
    // the dividers / icons along.
    Fl_Box*           status_;
    Fl_Box*           statusDivMid_ = nullptr;
    Fl_Button*        statusOpenMid_ = nullptr;
    MmpEllipsisLabel* midName_;
    Fl_Box*           statusDivSf_  = nullptr;
    Fl_Button*        statusOpenSf_ = nullptr;
    MmpEllipsisLabel* sfName_;
    Fl_Box*           statusDivOut_  = nullptr;
    Fl_Button*        statusOpenOut_ = nullptr;
    MmpEllipsisLabel* outputWavName_ = nullptr;
    int               statusRowY_   = 0;
    int               statusRowH_   = 16;
    int               statusRowXL_  = 0;
    int               statusRowXR_  = 0;

    SfInspector*   inspector_       = nullptr;
    MidiInspector* midiInspector_   = nullptr;
    int            inspectMenuIdx_  = -1;
    int            midiInspectIdx_  = -1;
    int            menuViewRoll_    = -1;
    int            menuViewGlyph_   = -1;
    int            menuViewKbd_     = -1;
    int            menuViewSpec_    = -1;

    // UI-side latches; engine will read them once the corresponding
    // features land on the audio side.
    Fl_Light_Button* loop_       = nullptr;
    Fl_Light_Button* metronome_  = nullptr;
    Fl_Light_Button* record_     = nullptr;
    Fl_Light_Button* midiIn_     = nullptr;

    // Path of the current Record-output WAV (empty until Record is set).
    std::string      outputWavPath_;

    // Shared between the three animated visualizers (piano roll / note
    // glyphs / keyboard). The glyphs view drives it via its prev/next
    // arrows + pause button; the others read currentChannel() and
    // paused. soloChannel here mirrors what's in MmpEngine so the UI
    // can hide the per-animation channel selector when the engine is
    // solo'd to a single channel anyway.
    AnimationState   animState_;
};

} // namespace mmp
