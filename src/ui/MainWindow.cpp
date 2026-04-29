#include "MainWindow.h"

#include "core/MmpEngine.h"
#include "midi/SmfPlayer.h"
#include "sf2/Sf2Dump.h"
#include "sf2/Sf3Codec.h"
#include "ui/HistoryDialog.h"
#include "ui/IconAudio.h"
#include "ui/MidiInspector.h"
#include "ui/SfInspector.h"
#include "ui/widgets/MmpDigitalCounter.h"
#include "ui/widgets/MmpEllipsisLabel.h"
#include "ui/widgets/MmpIconButton.h"
#include "ui/widgets/MmpNoteGlyphs.h"
#include "ui/widgets/MmpPianoKeyboard.h"
#include "ui/widgets/MmpPianoRoll.h"
#include "ui/widgets/MmpSpectrum.h"
#include "ui/widgets/MmpVuMeter.h"

#include <FL/Fl_SVG_Image.H>

#include <FL/Fl.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Preferences.H>
#include <FL/fl_ask.H>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.0.0"
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

namespace mmp {

namespace {

// macOS keeps the menu in the system bar; other platforms draw it in-window.
#if defined(__APPLE__)
constexpr int kMenuH = 0;
#else
constexpr int kMenuH = 24;
#endif

// Default starting path for fl_file_chooser — the user's home directory.
const char* defaultBrowsePath() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? h : ".";
}

// ---- Persisted "recently used" history ----------------------------
// Tiny inline helpers around Fl_Preferences. Each file type ("sf",
// "mid") keeps up to kHistMax paths under its own preferences group.
constexpr int kHistMax = 10;

std::vector<std::string> readHistory(const char* type) {
    Fl_Preferences root(Fl_Preferences::USER, "KomSoft", "MiniMidiPlayer");
    Fl_Preferences hist(root, type);
    std::vector<std::string> out;
    char key[8], buf[2048];
    for (int i = 0; i < kHistMax; ++i) {
        std::snprintf(key, sizeof(key), "%d", i);
        buf[0] = 0;
        hist.get(key, buf, "", sizeof(buf));
        if (buf[0] == 0) break;
        out.emplace_back(buf);
    }
    return out;
}

void writeHistory(const char* type, const std::vector<std::string>& list) {
    Fl_Preferences root(Fl_Preferences::USER, "KomSoft", "MiniMidiPlayer");
    Fl_Preferences hist(root, type);
    char key[8];
    for (int i = 0; i < kHistMax; ++i) {
        std::snprintf(key, sizeof(key), "%d", i);
        if ((size_t)i < list.size()) hist.set(key, list[i].c_str());
        else                          hist.deleteEntry(key);
    }
    root.flush();
}

void addToHistory(const char* type, const std::string& path) {
    auto list = readHistory(type);
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if ((int)list.size() > kHistMax) list.resize(kHistMax);
    writeHistory(type, list);
}

// Write WAV (shared with the CLI; small enough to inline here).
bool writeWav16(const std::string& path, const float* samples, int frames, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int channels = 2, bps = 2;
    int dataBytes = frames * channels * bps;
    auto w32 = [&](uint32_t v){ f.put(v); f.put(v>>8); f.put(v>>16); f.put(v>>24); };
    auto w16 = [&](uint16_t v){ f.put(v); f.put(v>>8); };
    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(channels); w32(rate);
    w32(rate * channels * bps); w16(channels * bps); w16(bps * 8);
    f.write("data", 4); w32(dataBytes);
    for (int i = 0; i < frames * channels; ++i) {
        float s = samples[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32767.0f);
        f.put((char)(v & 0xFF));
        f.put((char)((v >> 8) & 0xFF));
    }
    return (bool)f;
}

} // namespace

MainWindow::MainWindow(MmpEngine& engine, SmfPlayer& player)
    : Fl_Double_Window(760, 200 + kMenuH, ""),
      engine_(engine), player_(player) {

    {
        char title[96];
        std::snprintf(title, sizeof(title),
                      "Mini MIDI Player  (ver. %s, build %d)",
                      BUILD_VERSION, (int)BUILD_NUMBER);
        copy_label(title);
    }

    // Compact margins; every row's Y is computed from the previous +
    // a fixed gap so widgets never overlap.
    constexpr int pad      = 4;
    constexpr int colGap   = 6;
    constexpr int rowGap   = 3;
    constexpr int sectGap  = 6;
    constexpr int btnH     = 22;

    // Three-column layout, then a position slider, then a status line.
    //   ┌─────┬─────────────────────┬────────────┐
    //   │     │ Open SF2 │ Open MIDI│            │
    //   │ VU  │ ▶ ⏸ ■                            │ Spectrum
    //   │     │ VOL  REV  CHO  PAN  TRANS  TEMPO │
    //   ├─────┴────────────────────────────────  ┤
    //   │ ├─ position slider ────────────────────┤
    //   │ ── separator                           │
    //   │ status line                            │
    //   └────────────────────────────────────────┘

    // Menubar (system on macOS, in-window elsewhere).
    menu_ = new Fl_Sys_Menu_Bar(0, 0, w(), kMenuH ? kMenuH : 1);
    menu_->add("&File/&Open SoundFont…",      FL_COMMAND + 'o', onOpenSf2,      this);
    menu_->add("&File/Open &MIDI file…",      FL_COMMAND + 'm', onOpenMid,      this);
    menu_->add("&File/Render to &WAV…",       0,                onRenderWav,    this, FL_MENU_DIVIDER);
    menu_->add("&File/&Quit",                 FL_COMMAND + 'q', onQuit,         this);
    menu_->add("&Playback/&Play",             FL_COMMAND + 'p', onPlay,         this);
    menu_->add("&Playback/Pa&use",            0,                onPause,        this);
    menu_->add("&Playback/&Stop",             FL_COMMAND + '.', onStop,         this);
    menu_->add("&Developer/&SoundFont Inspector…",  FL_COMMAND + 'i', onShowInspect,   this);
    menu_->add("&Developer/&MIDI Inspector…",       FL_COMMAND + 'j', onShowMidiInsp,  this);
    menu_->add("&Developer/&Dump SoundFont (JSON)…", 0,               onDumpJson,      this, FL_MENU_DIVIDER);
    menu_->add("&Developer/&Convert SoundFont…",     0,               onConvertSf,     this);
    menu_->add("&View/&Piano roll",                  0,
               onViewRoll,     this, FL_MENU_RADIO | FL_MENU_VALUE);
    menu_->add("&View/Note &glyphs",                 0,
               onViewGlyphs,   this, FL_MENU_RADIO);
    menu_->add("&View/&Keyboard",                    0,
               onViewKbd,      this, FL_MENU_RADIO);
    menu_->add("&View/Spectrum &only",               0,
               onViewSpectrum, this, FL_MENU_RADIO);
    menu_->add("&Help/&About",                       0,    onAbout,    this);

    inspectMenuIdx_ = menu_->find_index("&Developer/&SoundFont Inspector…");
    midiInspectIdx_ = menu_->find_index("&Developer/&MIDI Inspector…");
    menuViewRoll_   = menu_->find_index("&View/&Piano roll");
    menuViewGlyph_  = menu_->find_index("&View/Note &glyphs");
    menuViewKbd_    = menu_->find_index("&View/&Keyboard");
    menuViewSpec_   = menu_->find_index("&View/Spectrum &only");

    const int yTop  = kMenuH + pad;
    const int xL    = pad;
    const int xR    = w() - pad;
    const int vuW   = 44;
    const int ctlX  = xL + vuW + colGap;

    // -------- Single-row controls: 2-row transport + Loop, then mixer
    //          cluster (volume / rev / cho / pan / trans / tempo).
    const int trBtnW   = 32;
    const int trBtnH   = 22;
    const int trGap    = 2;
    const int trBlockW = 3 * trBtnW + 2 * trGap;     // top row dictates width
    const int trBlockH = 2 * trBtnH + trGap;
    int y = yTop;

    // Top row: ▶ ⏸ ■  — MmpIconButton handles centered SVG rendering
    // (FLTK's default button image routing mis-aligns rasterised SVGs).
    auto* bPlay  = new MmpIconButton(ctlX,                        y, trBtnW, trBtnH, icon_name::Play,  14);
    bPlay->callback(onPlay, this);    bPlay->tooltip("Play");
    auto* bPause = new MmpIconButton(ctlX + trBtnW + trGap,       y, trBtnW, trBtnH, icon_name::Pause, 14);
    bPause->callback(onPause, this);  bPause->tooltip("Pause");
    auto* bStop  = new MmpIconButton(ctlX + 2 * (trBtnW + trGap), y, trBtnW, trBtnH, icon_name::Stop,  14);
    bStop->callback(onStop, this);    bStop->tooltip("Stop");

    // Bottom row: Loop + Metronome (Aux replaced).
    const int trBotW = (trBlockW - trGap) / 2;
    loop_ = new Fl_Light_Button(ctlX, y + trBtnH + trGap, trBotW, trBtnH, "Loop");
    loop_->callback(onLoop, this);
    loop_->tooltip("Loop playback (toggle)");
    loop_->labelsize(10);
    metronome_ = new Fl_Light_Button(ctlX + trBotW + trGap, y + trBtnH + trGap,
                                     trBlockW - trBotW - trGap, trBtnH, "Metro");
    metronome_->callback(onMetronome, this);
    metronome_->tooltip("Metronome (toggle)");
    metronome_->labelsize(10);

    // ---- New column: Record (top) + MIDI input (bottom) ----
    const int newColX = ctlX + trBlockW + 6;
    const int newColW = 70;
    record_ = new Fl_Light_Button(newColX, y, newColW, trBtnH, "Record");
    record_->callback(onRecord, this);
    record_->tooltip("Record output to WAV (toggle)");
    record_->labelsize(10);
    record_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    midiIn_ = new Fl_Light_Button(newColX, y + trBtnH + trGap, newColW, trBtnH, "MIDI in");
    midiIn_->callback(onMidiIn, this);
    midiIn_->tooltip("Enable MIDI input (toggle)");
    midiIn_->labelsize(10);
    midiIn_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    // Match the Loop / Metronome buttons too so all four toggles read
    // alike — labels centered after the LED indicator.
    loop_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    metronome_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    // ---- Digital playback counter, between transport block and knobs.
    // Wide enough for "MM:SS" with the digit-overlap layout (each digit
    // square at the widget's full content height, ~half-overlapped) and
    // a small visible gap on each side of the colon's dot pair.
    const int dcX = newColX + newColW + 8;
    const int dcW = 116;
    counter_ = new MmpDigitalCounter(dcX, y, dcW, trBlockH);

    // Mixer cluster — laid out to the right of the transport block.
    const int dialSize  = 30;
    const int rollerW   = 84;
    const int counterW  = 56;
    const int cellLblH  = 11;
    const int cellGap   = 8;
    const int mixerY    = y;            // top of label row
    const int controlAreaY = mixerY + cellLblH;
    const int controlAreaH = trBlockH - cellLblH;
    auto centeredY = [&](int hCtl) {
        return controlAreaY + (controlAreaH - hCtl) / 2;
    };
    auto labelBox = [&](int x, int ww, const char* text) {
        auto* l = new Fl_Box(x, mixerY, ww, cellLblH, text);
        l->labelsize(9); l->labelfont(FL_HELVETICA_BOLD);
        l->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    };

    // Mixer cluster starts after the digital counter.
    int cx = dcX + dcW + 12;
    labelBox(cx, rollerW, "VOLUME");
    volume_ = new Fl_Roller(cx, centeredY(18), rollerW, 18);
    volume_->type(FL_HORIZONTAL);
    volume_->minimum(0.0); volume_->maximum(1.5);
    volume_->value(1.0);   volume_->step(0.01);
    volume_->callback(onVolume, this);
    volume_->tooltip("Master volume (CC7 broadcast)");
    cx += rollerW + cellGap;

    auto addDial = [&](const char* lbl, double mn, double mx_, double v0,
                       Fl_Callback* cb, const char* tip) -> Fl_Dial* {
        labelBox(cx, dialSize, lbl);
        auto* d = new Fl_Dial(cx, centeredY(dialSize), dialSize, dialSize);
        d->type(FL_LINE_DIAL);
        d->minimum(mn); d->maximum(mx_); d->value(v0);
        d->callback(cb, this); d->tooltip(tip);
        cx += dialSize + cellGap;
        return d;
    };
    reverb_ = addDial("REV",  0.0, 1.0, 0.30, onReverb, "Reverb send (CC91)");
    chorus_ = addDial("CHO",  0.0, 1.0, 0.10, onChorus, "Chorus send (CC93)");
    pan_    = addDial("PAN", -1.0, 1.0, 0.0,  onPan,    "Pan (CC10)");

    auto addCounter = [&](const char* lbl, double mn, double mx_, double v0,
                          double step, Fl_Callback* cb, const char* tip) -> Fl_Counter* {
        labelBox(cx, counterW, lbl);
        auto* c = new Fl_Counter(cx, centeredY(22), counterW, 22);
        c->type(FL_SIMPLE_COUNTER);
        c->minimum(mn); c->maximum(mx_); c->value(v0); c->step(step);
        c->callback(cb, this); c->tooltip(tip);
        cx += counterW + cellGap;
        return c;
    };
    transpose_   = addCounter("TRANS",  -24, 24,   0, 1, onTranspose, "Pitch transpose");
    tempoTrim_   = addCounter("TEMPO%", 50, 200, 100, 5, onTempoTrim, "Tempo trim (placeholder)");
    channelSolo_ = addCounter("CHNL",    0,  16,   0, 1, onChannelSolo,
                              "Solo a single MIDI channel (0 = all)");

    y += trBlockH + sectGap;

    // -------- Spectrum (always shown) + switchable Piano/Notes --------
    const int vizH    = 86;
    const int vizGap  = 4;
    const int vizW    = (xR - ctlX - vizGap) / 2;
    const int specX_  = ctlX;
    const int pianoX_ = specX_ + vizW + vizGap;

    spectrum_   = new MmpSpectrum     (specX_,  y, vizW,         vizH, engine_);
    pianoRoll_  = new MmpPianoRoll    (pianoX_, y, xR - pianoX_, vizH, engine_, animState_);
    noteGlyphs_ = new MmpNoteGlyphs   (pianoX_, y, xR - pianoX_, vizH, engine_, animState_);
    keyboard_   = new MmpPianoKeyboard(pianoX_, y, xR - pianoX_, vizH, engine_, animState_);
    spectrum_  ->callback(onSpectrumClick, this);
    spectrum_  ->tooltip("Click to expand to full width / restore both panels");
    pianoRoll_ ->callback(onPanelCycle, this);
    noteGlyphs_->callback(onPanelCycle, this);
    keyboard_  ->callback(onPanelCycle, this);
    pianoRoll_ ->hide();
    noteGlyphs_->hide();
    y += vizH;

    const int columnsBottom = y;

    // -------- Position progress bar (thin, non-interactive) -----------
    y = columnsBottom + sectGap;
    posBar_ = new Fl_Progress(ctlX, y, xR - ctlX, 6);
    posBar_->minimum(0); posBar_->maximum(1); posBar_->value(0);
    posBar_->box(FL_FLAT_BOX);
    posBar_->color(FL_DARK3);            // unfilled track
    posBar_->selection_color(FL_DARK_BLUE);  // filled portion
    posBar_->labeltype(FL_NO_LABEL);
    y += 6 + 4;

    // -------- Separator (visually divides the work area from status) --
    auto* sep = new Fl_Box(xL, y, xR - xL, 1, nullptr);
    sep->box(FL_FLAT_BOX);
    sep->color(FL_DARK3);
    y += 4;

    // -------- Status row -------------------------------------------
    // Layout is rebuilt by repackStatusRow() whenever the file labels
    // change, so the dividers / icons / labels slide with the actual
    // text widths instead of sitting in fixed slots. The widget pointers
    // live as members so the repack pass can read+resize them.
    const int icoSz = 16;
    statusRowH_  = icoSz;
    statusRowY_  = y;
    statusRowXL_ = xL;
    statusRowXR_ = xR;

    auto makeDivider = [&](int yy) {
        auto* d = new Fl_Box(0, yy, 8, icoSz, "|");
        d->box(FL_NO_BOX);
        d->labelfont(FL_HELVETICA);
        d->labelsize(10);
        d->labelcolor(FL_DARK2);
        d->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        return d;
    };
    auto makeOpenIcon = [&](int yy, Fl_Callback* cb, const char* tip) -> Fl_Button* {
        auto* b = new MmpIconButton(0, yy, icoSz, icoSz, "fad-save", icoSz - 4);
        b->callback(cb, this);
        b->tooltip(tip);
        return b;
    };

    // State info (state | time | vox | sr) — fixed width, populated by
    // refreshStatus().
    status_ = new Fl_Box(xL, y, 0, icoSz);
    status_->box(FL_NO_BOX);
    status_->labelfont(FL_HELVETICA);
    status_->labelsize(10);
    status_->labelcolor(FL_FOREGROUND_COLOR);
    status_->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP);

    statusDivMid_  = makeDivider(y);
    statusOpenMid_ = makeOpenIcon(y, onOpenMid, "Open MIDI file…");
    midName_ = new MmpEllipsisLabel(0, y, 0, icoSz);
    midName_->clickable(true);
    midName_->callback(onMidNameClick, this);
    midName_->tooltip("Click to inspect (when loaded) or pick a MIDI file");

    statusDivSf_  = makeDivider(y);
    statusOpenSf_ = makeOpenIcon(y, onOpenSf2, "Open SoundFont…");
    sfName_ = new MmpEllipsisLabel(0, y, 0, icoSz);
    sfName_->clickable(true);
    sfName_->callback(onSfNameClick, this);
    sfName_->tooltip("Click to inspect (when loaded) or pick a SoundFont");

    statusDivOut_  = makeDivider(y);
    statusOpenOut_ = makeOpenIcon(y, onPickOutputWav, "Choose output WAV…");
    outputWavName_ = new MmpEllipsisLabel(0, y, 0, icoSz);
    outputWavName_->clickable(true);
    outputWavName_->callback(onPickOutputWav, this);
    outputWavName_->tooltip("Click to choose output WAV");

    y += icoSz + pad;

    // -------- Left column: VU meter — flush from top of controls to
    // bottom of the position progress bar; no internal vertical margin.
    const int posBarBottom = posBar_->y() + posBar_->h();
    vu_ = new MmpVuMeter(xL, yTop, vuW, posBarBottom - yTop, engine_);

    size(w(), y);
    end();
    // Fixed-size window — no resize handle, no resize behaviour.
    size_range(w(), h(), w(), h());
    resizable(nullptr);

    // Inspectors start disabled; enabled when their respective files load.
    setSfInspectorEnabled(false);
    setMidiInspectorEnabled(false);

    // 30 Hz UI tick drives meters, spectrum and status.
    Fl::add_timeout(0.033, onTimer, this);
    // Auto-restore the last-loaded SoundFont once the event loop is up
    // and the window has shown — so the user sees the UI before the
    // (potentially slow) SF parse blocks.
    Fl::add_timeout(0.05, onRestorePersisted, this);
    refreshStatus();
}

MainWindow::~MainWindow() {
    Fl::remove_timeout(onTimer, this);
    delete inspector_;
    delete midiInspector_;
}

// ---------------- Timer & status ------------------------------------------

void MainWindow::onTimer(void* v) {
    static_cast<MainWindow*>(v)->tick();
    Fl::repeat_timeout(0.033, onTimer, v);
}

void MainWindow::tick() {
    // Skip update() for any hidden visualizer — pumping events into a
    // hidden widget still costs the per-tick walk + redraw schedule.
    vu_->update();
    spectrum_->update();
    switch (vizMode_) {
        case VisualizerMode::PianoRoll: pianoRoll_ ->update(); break;
        case VisualizerMode::Glyphs:    noteGlyphs_->update(); break;
        case VisualizerMode::Keyboard:  keyboard_  ->update(); break;
        case VisualizerMode::SpectrumOnly: break;          // nothing else to update
    }

    if (player_.isLoaded() && player_.lengthMs() > 0) {
        posBar_->maximum((float)player_.lengthMs());
        posBar_->value((float)player_.positionMs());
    } else {
        posBar_->value(0);
    }
    if (counter_) counter_->setTimes(player_.positionMs(), player_.lengthMs());

    // Refresh the spectrum's LCD info overlay each tick — small struct,
    // copy-by-value, no allocation in steady state.
    if (spectrum_) {
        auto tail = [](const std::string& s) {
            size_t slash = s.find_last_of('/');
            return slash == std::string::npos ? s : s.substr(slash + 1);
        };
        MmpSpectrum::Overlay ov;
        ov.fileName     = player_.isLoaded()     ? tail(player_.filePath())     : "";
        ov.sfName       = engine_.hasSoundFont() ? tail(engine_.soundFontPath()) : "";
        ov.posS         = (int)(player_.positionMs() / 1000);
        ov.totalS       = (int)(player_.lengthMs()   / 1000);
        ov.bpm          = 0;             // tempo not yet exposed by SmfPlayer
        ov.activeVoices = engine_.activeVoiceCount();
        ov.sampleRate   = engine_.sampleRate();
        ov.channelsUsed = 0;             // populated when MIDI inspector data is plumbed
        ov.channelsTotal= 16;
        spectrum_->setOverlay(ov);
    }

    refreshStatus();
}

namespace {
// Truncate s with a trailing ellipsis so it fits in maxPx pixels under
// the given font/size. fl_width is per-pixel correct; this beats fixed
// character cap-and-suffix.
std::string ellipsize(const std::string& s, int maxPx,
                      Fl_Font font = FL_HELVETICA, int size = 10) {
    fl_font(font, size);
    if ((int)fl_width(s.c_str()) <= maxPx) return s;
    std::string t = s;
    const char* dots = "…";
    while (!t.empty() &&
           (int)fl_width((t + dots).c_str()) > maxPx) {
        t.pop_back();
    }
    return t + dots;
}
}

void MainWindow::refreshStatus() {
    auto tail = [](const std::string& s) {
        size_t slash = s.find_last_of('/');
        return slash == std::string::npos ? s : s.substr(slash + 1);
    };

    std::string sfBase = engine_.hasSoundFont() ? tail(engine_.soundFontPath())
                                                : std::string("(no SoundFont)");
    std::string mfBase = player_.isLoaded()     ? tail(player_.filePath())
                                                : std::string("(no MIDI file)");
    std::string outBase = outputWavPath_.empty() ? std::string("(no output WAV)")
                                                 : tail(outputWavPath_);

    midName_      ->setText(mfBase);
    sfName_       ->setText(sfBase);
    outputWavName_->setText(outBase);

    unsigned int posMs = player_.positionMs();
    unsigned int totMs = player_.lengthMs();
    int posS = posMs / 1000, totS = totMs / 1000;

    const char* state = !engine_.hasSoundFont()  ? "READY"
                      : !player_.isLoaded()      ? "LOADED"
                      :  player_.isPlaying()     ? "PLAY"
                                                 : "STOP";

    static char line[160];
    std::snprintf(line, sizeof(line),
                  " %-6s | %02d:%02d / %02d:%02d | vox %3d | sr %5d",
                  state,
                  posS / 60, posS % 60, totS / 60, totS % 60,
                  engine_.activeVoiceCount(),
                  engine_.sampleRate());
    status_->label(line);

    repackStatusRow();
}

void MainWindow::repackStatusRow() {
    constexpr int gapTiny = 2;
    const int y = statusRowY_;
    const int h = statusRowH_;

    // Measure the state-info string's width — that's its natural slot.
    fl_font(status_->labelfont(), status_->labelsize());
    int stateW = (int)fl_width(status_->label() ? status_->label() : "") + 8;

    // Cap any single ellipsis label so a long path doesn't crowd out
    // the others; minimum is the placeholder width so unloaded slots
    // never collapse to nothing.
    const int kMaxLabel = 240;
    auto pickLabelW = [&](MmpEllipsisLabel* lbl, const std::string& placeholder) {
        fl_font(lbl->labelfont(), lbl->labelsize());
        // +12 px margin so the placeholder text (and any closing
        // parenthesis or descender) doesn't get clipped by the label's
        // own draw inset. Native FLTK fl_width() under-reports for
        // some glyphs near the right edge.
        int placeholderW = (int)fl_width(placeholder.c_str()) + 12;
        int natural      = lbl->measureNaturalWidth();
        int wantW = (natural < placeholderW) ? placeholderW : natural;
        if (wantW > kMaxLabel) wantW = kMaxLabel;
        return wantW;
    };
    int midW = pickLabelW(midName_,       "(no MIDI file)");
    int sfW  = pickLabelW(sfName_,        "(no SoundFont)");
    int outW = pickLabelW(outputWavName_, "(no output WAV)");

    int sx = statusRowXL_;
    auto place = [&](Fl_Widget* w, int width) {
        w->resize(sx, y, width, h);
        sx += width + gapTiny;
    };

    place(status_, stateW);

    place(statusDivMid_,  8);
    place(statusOpenMid_, statusOpenMid_->h());
    place(midName_,       midW);

    place(statusDivSf_,   8);
    place(statusOpenSf_,  statusOpenSf_->h());
    place(sfName_,        sfW);

    place(statusDivOut_,  8);
    place(statusOpenOut_, statusOpenOut_->h());
    place(outputWavName_, outW);

    redraw();
}

// ---------------- File / Menu callbacks -----------------------------------

void MainWindow::openSoundFontFile(const char* path) {
    if (!engine_.loadSoundFont(path)) {
        fl_alert("Failed to load SoundFont:\n%s", path);
        return;
    }
    if (!engine_.isRunning()) engine_.start(44100);
    if (inspector_ && inspector_->visible()) inspector_->loadFromFile(path);
    setSfInspectorEnabled(true);

    // Persist the path so the next launch auto-restores it, and push
    // it to the recent-files list used by shift-click history.
    Fl_Preferences prefs(Fl_Preferences::USER, "KomSoft", "MiniMidiPlayer");
    prefs.set("last_soundfont", path);
    prefs.flush();
    addToHistory("sf", path);

    refreshStatus();
}

void MainWindow::onRestorePersisted(void* v) {
    auto* self = static_cast<MainWindow*>(v);
    Fl_Preferences prefs(Fl_Preferences::USER, "KomSoft", "MiniMidiPlayer");
    char buf[2048] = {0};
    prefs.get("last_soundfont", buf, "", sizeof(buf));
    if (buf[0] == 0) return;
    // Silently no-op if the path no longer exists or fails to load —
    // user didn't initiate this so we shouldn't pop an alert.
    std::ifstream f(buf);
    if (!f.good()) return;
    self->engine_.loadSoundFont(buf);
    if (self->engine_.hasSoundFont()) {
        if (!self->engine_.isRunning()) self->engine_.start(44100);
        self->setSfInspectorEnabled(true);
        self->refreshStatus();
    }
}

void MainWindow::setSfInspectorEnabled(bool enabled) {
    // Only the menu item tracks "a SoundFont is loaded". The status-row
    // label stays active so clicking it always opens the file picker —
    // FLTK's event router skips inactive widgets, which would silently
    // swallow the click.
    if (inspectMenuIdx_ >= 0) {
        int m = menu_->mode(inspectMenuIdx_);
        m = enabled ? (m & ~FL_MENU_INACTIVE) : (m | FL_MENU_INACTIVE);
        menu_->mode(inspectMenuIdx_, m);
    }
}

void MainWindow::setMidiInspectorEnabled(bool enabled) {
    if (midiInspectIdx_ >= 0) {
        int m = menu_->mode(midiInspectIdx_);
        m = enabled ? (m & ~FL_MENU_INACTIVE) : (m | FL_MENU_INACTIVE);
        menu_->mode(midiInspectIdx_, m);
    }
}

void MainWindow::setVisualizerMode(VisualizerMode m) {
    vizMode_ = m;
    if (m != VisualizerMode::SpectrumOnly) vizPrevDual_ = m;

    // Recompute the right-column geometry so the spectrum can claim the
    // full slot when SpectrumOnly is active. Constants here mirror the
    // ones used in the constructor; if those move, update here too.
    constexpr int pad    = 4;
    constexpr int colGap = 6;
    constexpr int vuW    = 44;
    constexpr int vizGap = 4;
    const int xR    = w() - pad;
    const int ctlX  = pad + vuW + colGap;
    const int sy    = spectrum_->y();
    const int sh    = spectrum_->h();
    const int halfW = (xR - ctlX - vizGap) / 2;
    const int fullW = xR - ctlX;

    if (m == VisualizerMode::SpectrumOnly) {
        spectrum_ ->resize(ctlX, sy, fullW, sh);
        pianoRoll_->hide();
        noteGlyphs_->hide();
        keyboard_  ->hide();
    } else {
        spectrum_->resize(ctlX, sy, halfW, sh);
        const int px = ctlX + halfW + vizGap;
        pianoRoll_ ->resize(px, sy, xR - px, sh);
        noteGlyphs_->resize(px, sy, xR - px, sh);
        keyboard_  ->resize(px, sy, xR - px, sh);
        pianoRoll_ ->hide();
        noteGlyphs_->hide();
        keyboard_  ->hide();
        switch (m) {
            case VisualizerMode::PianoRoll: pianoRoll_ ->show(); break;
            case VisualizerMode::Glyphs:    noteGlyphs_->show(); break;
            case VisualizerMode::Keyboard:  keyboard_  ->show(); break;
            default: break;
        }
    }

    // Sync the View-menu radio cluster.
    auto setRadio = [&](int idx, bool on) {
        if (idx < 0) return;
        menu_->mode(idx, on ? (FL_MENU_RADIO | FL_MENU_VALUE) : FL_MENU_RADIO);
    };
    setRadio(menuViewRoll_,  m == VisualizerMode::PianoRoll);
    setRadio(menuViewGlyph_, m == VisualizerMode::Glyphs);
    setRadio(menuViewKbd_,   m == VisualizerMode::Keyboard);
    setRadio(menuViewSpec_,  m == VisualizerMode::SpectrumOnly);

    redraw();
}

void MainWindow::onConvertSf(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    (void)self;
    // 1) Pick the input file. Accept either .sf2 or .sfo.
    const char* in = fl_file_chooser("Convert SoundFont — pick input",
                                     "*.{sf2,SF2,sfo,SFO}",
                                     defaultBrowsePath(), 0);
    if (!in) return;
    std::string inPath = in;
    auto endsWith = [](const std::string& s, const char* suf) {
        size_t n = std::strlen(suf);
        return s.size() >= n &&
               std::equal(s.end() - n, s.end(), suf,
                          [](char a, char b){ return std::tolower((unsigned char)a) == b; });
    };
    bool toSfo = endsWith(inPath, ".sf2");

    // 2) Suggest an output filename based on the direction.
    std::string outSeed = inPath;
    auto dot = outSeed.find_last_of('.');
    if (dot != std::string::npos) outSeed.resize(dot);
    outSeed += toSfo ? ".sfo" : ".sf2";

    const char* out = fl_file_chooser(toSfo ? "Compress to SFO (Vorbis)"
                                            : "Decompress to SF2 (PCM)",
                                      toSfo ? "*.{sfo,SFO}" : "*.{sf2,SF2}",
                                      outSeed.c_str(), 0);
    if (!out) return;

    if (toSfo) {
        Sf3CompressOptions opts;
        // Default Vorbis quality 0.4 ≈ 128 kbit/s — good enough for SF
        // sample material; user can edit via the CLI for finer control.
        Sf3CompressStats st;
        if (!compressSf2ToSf3(inPath, out, opts, st)) {
            fl_alert("Compression failed:\n%s", st.message.c_str());
            return;
        }
        fl_message("Compressed %d samples\n%u → %u bytes (%.1f %%)\n\n%s",
                   st.samplesProcessed, st.pcmBytes, st.oggBytes,
                   st.pcmBytes ? 100.0 * st.oggBytes / st.pcmBytes : 0.0,
                   out);
    } else {
        Sf3DecompressStats st;
        if (!decompressSf3ToSf2(inPath, out, st)) {
            fl_alert("Decompression failed:\n%s", st.message.c_str());
            return;
        }
        fl_message("Decompressed %d samples\n%u → %u bytes\n\n%s",
                   st.samplesProcessed, st.oggBytes, st.pcmBytes, out);
    }
}

void MainWindow::onShowMidiInsp(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (!self->player_.isLoaded()) return;
    if (!self->midiInspector_) self->midiInspector_ = new MidiInspector();
    self->midiInspector_->loadFromFile(self->player_.filePath());
    self->midiInspector_->show();
}

void MainWindow::onViewRoll(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->setVisualizerMode(VisualizerMode::PianoRoll);
}
void MainWindow::onViewGlyphs(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->setVisualizerMode(VisualizerMode::Glyphs);
}
void MainWindow::onViewKbd(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->setVisualizerMode(VisualizerMode::Keyboard);
}
void MainWindow::onViewSpectrum(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->setVisualizerMode(VisualizerMode::SpectrumOnly);
}
void MainWindow::onPanelCycle(Fl_Widget*, void* v) {
    // Click on the keyboard / glyphs / piano-roll panel cycles the
    // right slot: Keyboard → Glyphs → PianoRoll → SpectrumOnly. Return
    // from SpectrumOnly via clicking the spectrum panel.
    auto* self = static_cast<MainWindow*>(v);
    auto next = VisualizerMode::SpectrumOnly;
    switch (self->vizMode_) {
        case VisualizerMode::Keyboard:  next = VisualizerMode::Glyphs;       break;
        case VisualizerMode::Glyphs:    next = VisualizerMode::PianoRoll;    break;
        case VisualizerMode::PianoRoll: next = VisualizerMode::SpectrumOnly; break;
        default: break;
    }
    self->setVisualizerMode(next);
}
void MainWindow::onSpectrumClick(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (self->vizMode_ != VisualizerMode::SpectrumOnly) {
        self->setVisualizerMode(VisualizerMode::SpectrumOnly);
        return;
    }
    // Wrap into the dual mode *after* the one we entered from, so the
    // full panel-cycle continues to advance instead of bouncing.
    auto next = VisualizerMode::Keyboard;
    switch (self->vizPrevDual_) {
        case VisualizerMode::Keyboard:  next = VisualizerMode::Glyphs;    break;
        case VisualizerMode::Glyphs:    next = VisualizerMode::PianoRoll; break;
        case VisualizerMode::PianoRoll: next = VisualizerMode::Keyboard;  break;
        default: break;
    }
    self->setVisualizerMode(next);
}
void MainWindow::onLoop(Fl_Widget*, void*) {
    // SmfPlayer doesn't yet expose a loop flag — once it does, hand the
    // toggle state through with player_.setLoop(loop_->value()).
}
void MainWindow::onMetronome(Fl_Widget*, void*) {
    // Engine doesn't yet emit click samples; this latch is read once
    // the metronome generator lands in MmpEngine.
}
void MainWindow::onRecord(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (self->record_->value()) {
        if (self->outputWavPath_.empty()) {
            std::string seed = std::string(defaultBrowsePath()) + "/out.wav";
            const char* p = fl_file_chooser("Record output WAV",
                                            "*.{wav,WAV}", seed.c_str(), 0);
            if (!p) {
                self->record_->value(0);
                return;
            }
            self->outputWavPath_ = p;
        }
    }
    self->refreshStatus();
    // TODO: wire up the actual recording stream — feed engine tap into
    // a streaming WAV writer when record_->value()==1.
}
void MainWindow::onMidiIn(Fl_Widget*, void*) {
    // CoreMIDI input isn't wired yet — this is a UI latch for now.
}
// Click routing for the SF / MIDI status-row labels:
//
//   File NOT loaded:
//       * history empty           → open-file dialog
//       * history has any entries → show the history dialog
//
//   File IS loaded:
//       * plain click             → matching inspector
//       * shift-click + ≥ 2 items → history dialog (≥2 because the
//                                    currently-loaded file is itself
//                                    in the list — only its presence
//                                    plus another entry makes the
//                                    dialog actually useful)
//       * shift-click + ≤ 1 items → open-file dialog
//
// The dialog itself routes the user's choice: Open → load picked path,
// Cancel → fall through to fl_file_chooser, Close → no-op, Clear →
// drop the selected entry from history (stays in dialog).
namespace {
struct ClickActions {
    const char*           historyType;
    void (*openFn)(Fl_Widget*, void*);
    void (MainWindow::*loader)(const char*);
    const char*           dialogTitle;
};

void runHistoryFlow(MainWindow* self, Fl_Widget* w, const ClickActions& a) {
    auto hist = readHistory(a.historyType);
    if (hist.empty()) { a.openFn(w, self); return; }
    auto res = runHistoryDialog(a.dialogTitle, hist);
    writeHistory(a.historyType, hist);     // persist any Clear edits
    if (res.action == HistoryDialogResult::Action::Open) {
        (self->*(a.loader))(res.selected.c_str());
    } else if (res.action == HistoryDialogResult::Action::Cancel) {
        a.openFn(w, self);
    }
    // Close → nothing
}
} // namespace

void MainWindow::onMidNameClick(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    bool shift  = (Fl::event_state() & FL_SHIFT) != 0;
    bool loaded = self->player_.isLoaded();
    ClickActions a{ "mid", &MainWindow::onOpenMid,
                    &MainWindow::openMidiFile, "Recent MIDI files" };

    if (!loaded) { runHistoryFlow(self, w, a); return; }
    if (!shift)  { onShowMidiInsp(w, v); return; }
    if (readHistory("mid").size() < 2) { onOpenMid(w, v); return; }
    runHistoryFlow(self, w, a);
}

void MainWindow::onSfNameClick(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    bool shift  = (Fl::event_state() & FL_SHIFT) != 0;
    bool loaded = self->engine_.hasSoundFont();
    ClickActions a{ "sf", &MainWindow::onOpenSf2,
                    &MainWindow::openSoundFontFile, "Recent SoundFonts" };

    if (!loaded) { runHistoryFlow(self, w, a); return; }
    if (!shift)  { onShowInspect(w, v); return; }
    if (readHistory("sf").size() < 2) { onOpenSf2(w, v); return; }
    runHistoryFlow(self, w, a);
}
void MainWindow::onPickOutputWav(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    std::string seed = self->outputWavPath_.empty()
                        ? std::string(defaultBrowsePath()) + "/out.wav"
                        : self->outputWavPath_;
    const char* p = fl_file_chooser("Output WAV", "*.{wav,WAV}", seed.c_str(), 0);
    if (!p) return;
    self->outputWavPath_ = p;
    self->refreshStatus();
}

void MainWindow::openMidiFile(const char* path) {
    if (!player_.load(path)) {
        fl_alert("Failed to load MIDI file:\n%s", path);
        return;
    }
    posBar_->value(0);
    posBar_->maximum((float)player_.lengthMs());
    if (midiInspector_ && midiInspector_->visible())
        midiInspector_->loadFromFile(path);
    setMidiInspectorEnabled(true);
    addToHistory("mid", path);
    refreshStatus();
}

void MainWindow::onOpenSf2(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    const char* file = fl_file_chooser("Open SoundFont", "*.{sf2,SF2}",
                                       defaultBrowsePath(), 0);
    if (file) self->openSoundFontFile(file);
}

void MainWindow::onOpenMid(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    const char* file = fl_file_chooser("Open MIDI file",
                                       "*.{mid,MID,midi,MIDI}",
                                       defaultBrowsePath(), 0);
    if (file) self->openMidiFile(file);
}

void MainWindow::onRenderWav(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (!self->engine_.hasSoundFont() || !self->player_.isLoaded()) {
        fl_alert("Load a SoundFont and a MIDI file first."); return;
    }
    std::string seed = std::string(defaultBrowsePath()) + "/out.wav";
    const char* out = fl_file_chooser("Render to WAV", "*.{wav,WAV}", seed.c_str(), 0);
    if (!out) return;
    int rate = 44100;
    long long frames = (long long)self->player_.lengthMs() * rate / 1000 + rate;
    std::vector<float> buf((size_t)frames * 2, 0.0f);
    int rendered = self->player_.renderToBuffer(buf.data(), rate);
    if (!writeWav16(out, buf.data(), rendered, rate)) {
        fl_alert("Failed to write %s", out); return;
    }
    fl_message("Rendered %d frames (%.1f s) to:\n%s",
               rendered, rendered / (float)rate, out);
}

void MainWindow::onDumpJson(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (!self->engine_.hasSoundFont()) { fl_alert("Load a SoundFont first."); return; }
    std::string seed2 = std::string(defaultBrowsePath()) + "/soundfont.json";
    const char* out = fl_file_chooser("Dump SoundFont as JSON",
                                      "*.{json,JSON}", seed2.c_str(), 0);
    if (!out) return;
    std::ofstream f(out);
    if (!f) { fl_alert("Failed to open %s", out); return; }
    Sf2DumpOptions opts; opts.jsonOutput = true;
    if (!dumpSoundFont(self->engine_.soundFontPath(), opts, f)) {
        fl_alert("Dump failed."); return;
    }
    fl_message("Wrote JSON dump to:\n%s", out);
}

void MainWindow::onShowInspect(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (!self->inspector_) self->inspector_ = new SfInspector();
    if (self->engine_.hasSoundFont())
        self->inspector_->loadFromFile(self->engine_.soundFontPath());
    self->inspector_->show();
}

#if defined(__APPLE__)
void showStandardAboutPanel();   // defined in MacAbout.mm (same namespace)
#endif

void MainWindow::onAbout(Fl_Widget*, void*) {
#if defined(__APPLE__)
    // macOS: invoke the system About panel which automatically reads
    // Contents/Resources/Credits.rtf for its body text. The Info.plist
    // supplies app name / version / icon.
    showStandardAboutPanel();
#else
    fl_message(
        "Mini MIDI Player %s (build %d)\n"
        "SoundFont MIDI player + developer tools.\n"
        "TinySoundFont · TinyMidiLoader · KissFFT · FLTK\n"
        "© 2026 KomSoft Oprogramowanie",
        BUILD_VERSION, (int)BUILD_NUMBER);
#endif
}

void MainWindow::onQuit(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->hide();
}

// ---------------- Transport / mixer ---------------------------------------

void MainWindow::onPlay(Fl_Widget*, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    if (!self->engine_.hasSoundFont()) { fl_alert("Load a SoundFont first."); return; }
    if (!self->player_.isLoaded())     { fl_alert("Load a MIDI file first."); return; }
    if (!self->engine_.isRunning())    self->engine_.start(44100);
    self->player_.play();
}
void MainWindow::onPause(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->player_.pause();
}
void MainWindow::onStop(Fl_Widget*, void* v) {
    static_cast<MainWindow*>(v)->player_.stop();
}

void MainWindow::onVolume(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    auto* r = static_cast<Fl_Roller*>(w);
    int val = (int)std::clamp(r->value() * 100.0 / 1.5, 0.0, 127.0);
    for (int ch = 0; ch < 16; ++ch) self->engine_.controlChange(ch, 7, val);
}

void MainWindow::onReverb(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    int val = (int)std::clamp(static_cast<Fl_Dial*>(w)->value() * 127.0, 0.0, 127.0);
    for (int ch = 0; ch < 16; ++ch) self->engine_.controlChange(ch, 91, val);
}

void MainWindow::onChorus(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    int val = (int)std::clamp(static_cast<Fl_Dial*>(w)->value() * 127.0, 0.0, 127.0);
    for (int ch = 0; ch < 16; ++ch) self->engine_.controlChange(ch, 93, val);
}

void MainWindow::onPan(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    // -1..+1 → 0..127 (64 = center)
    int val = (int)std::clamp(64.0 + static_cast<Fl_Dial*>(w)->value() * 63.0,
                              0.0, 127.0);
    for (int ch = 0; ch < 16; ++ch) self->engine_.controlChange(ch, 10, val);
}

void MainWindow::onTranspose(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    int semi = (int)static_cast<Fl_Counter*>(w)->value();
    // ±2 semis = full pitch wheel range by default; broadcast scaled.
    int pw = std::clamp(8192 + (int)(semi * 8191.0 / 24.0), 0, 16383);
    for (int ch = 0; ch < 16; ++ch) self->engine_.pitchBend(ch, pw);
}

void MainWindow::onChannelSolo(Fl_Widget* w, void* v) {
    auto* self = static_cast<MainWindow*>(v);
    int ch = (int)static_cast<Fl_Counter*>(w)->value();
    self->animState_.soloChannel = ch;
    self->engine_.setSoloChannel(ch);
    // Force a redraw so the glyphs view picks up the channel-selector
    // hide/show state immediately.
    if (self->noteGlyphs_) self->noteGlyphs_->redraw();
}

void MainWindow::onTempoTrim(Fl_Widget*, void*) {
    // TODO: feed into SmfPlayer's scheduler once it exposes a tempo
    // multiplier. Placeholder so the control is visible during UI design.
}

} // namespace mmp
