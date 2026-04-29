#pragma once

// HistoryDialog — modal "recent files" picker. Shown only when shift-
// clicking a status-row label whose corresponding file is loaded *and*
// the persisted history holds more than one entry. Buttons:
//
//   Open    — proceed with the selected entry
//   Cancel  — close the dialog and fall through to fl_file_chooser
//   Close   — close the dialog and do nothing
//   Clear   — remove the selected entry from the history (does not close)

#include <string>
#include <vector>

namespace mmp {

struct HistoryDialogResult {
    enum class Action { Open, Cancel, Close } action = Action::Close;
    std::string selected;          // valid only when action == Open
};

// Run the dialog modally. The history vector is updated in place if the
// user removes entries via the Clear button. Returns when the user
// clicks Open / Cancel / Close (Clear stays in the dialog).
HistoryDialogResult runHistoryDialog(const char* title,
                                     std::vector<std::string>& history);

} // namespace mmp
