#include "HistoryDialog.h"

#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>

#include <cstring>

namespace mmp {

namespace {

struct DialogState {
    Fl_Double_Window* win;
    Fl_Browser*       browser;
    std::vector<std::string>* history;
    HistoryDialogResult       result;
};

void rebuildBrowser(DialogState& s) {
    s.browser->clear();
    for (const auto& p : *s.history) {
        size_t slash = p.find_last_of('/');
        std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        std::string display = base + "  —  " + p;
        s.browser->add(display.c_str(), const_cast<char*>(p.c_str()));
    }
    if (!s.history->empty()) s.browser->select(1);
}

void onOpen(Fl_Widget*, void* v) {
    auto* s = static_cast<DialogState*>(v);
    int sel = s->browser->value();
    if (sel >= 1 && sel <= (int)s->history->size()) {
        s->result.action  = HistoryDialogResult::Action::Open;
        s->result.selected = (*s->history)[sel - 1];
        s->win->hide();
    }
}
void onCancel(Fl_Widget*, void* v) {
    auto* s = static_cast<DialogState*>(v);
    s->result.action = HistoryDialogResult::Action::Cancel;
    s->win->hide();
}
void onClose(Fl_Widget*, void* v) {
    auto* s = static_cast<DialogState*>(v);
    s->result.action = HistoryDialogResult::Action::Close;
    s->win->hide();
}
void onClear(Fl_Widget*, void* v) {
    auto* s = static_cast<DialogState*>(v);
    int sel = s->browser->value();
    if (sel >= 1 && sel <= (int)s->history->size()) {
        s->history->erase(s->history->begin() + (sel - 1));
        rebuildBrowser(*s);
        // If the list is now empty, treat that as Cancel — there's
        // nothing left to pick, so fall through to the file picker.
        if (s->history->empty()) {
            s->result.action = HistoryDialogResult::Action::Cancel;
            s->win->hide();
        }
    }
}

} // namespace

HistoryDialogResult runHistoryDialog(const char* title,
                                     std::vector<std::string>& history) {
    DialogState s{};
    s.history = &history;
    s.result.action = HistoryDialogResult::Action::Close;

    constexpr int W = 560, H = 320;
    constexpr int btnH = 24, btnW = 80, pad = 8, gap = 6;

    auto* win = new Fl_Double_Window(W, H, title);
    s.win = win;
    win->set_modal();

    auto* hint = new Fl_Box(pad, pad, W - 2 * pad, 18,
        "Recently used — Open to use, Clear to drop the selection, "
        "Cancel to pick a different file, Close to do nothing.");
    hint->labelsize(10);
    hint->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT);

    s.browser = new Fl_Browser(pad, pad + 22, W - 2 * pad, H - btnH - pad - 32);
    s.browser->textfont(FL_HELVETICA);
    s.browser->textsize(11);
    s.browser->callback([](Fl_Widget* w, void* v) {
        // Double-click → Open
        if (Fl::event_clicks() > 0) onOpen(w, v);
    }, &s);
    rebuildBrowser(s);

    int by = H - btnH - pad;
    int bx = W - pad - btnW;
    auto* bClose  = new Fl_Button(bx, by, btnW, btnH, "Close");
    bClose ->callback(onClose,  &s);
    bx -= btnW + gap;
    auto* bCancel = new Fl_Button(bx, by, btnW, btnH, "Cancel");
    bCancel->callback(onCancel, &s);
    bx -= btnW + gap;
    auto* bClear  = new Fl_Button(bx, by, btnW, btnH, "Clear");
    bClear ->callback(onClear,  &s);
    bx -= btnW + gap;
    auto* bOpen   = new Fl_Button(bx, by, btnW, btnH, "Open");
    bOpen  ->callback(onOpen,   &s);

    win->end();
    win->show();
    while (win->visible()) Fl::wait();
    delete win;        // own-and-free
    return s.result;
}

} // namespace mmp
