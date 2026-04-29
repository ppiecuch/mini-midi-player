#include "core/MmpEngine.h"
#include "midi/SmfPlayer.h"
#include "ui/CustomBoxes.h"
#include "ui/MainWindow.h"

#include <FL/Fl.H>
#include <FL/Enumerations.H>

int main(int argc, char** argv) {
    // Compact UI: smaller global font for every widget that defaults to
    // FL_NORMAL_SIZE. Custom widgets (VU, spectrum) set their own sizes
    // and are unaffected.
    FL_NORMAL_SIZE = 11;
    Fl::scheme("gtk+");

    // Project-specific box types (FL_USER_LCD, …). Must run after
    // Fl::scheme() — switching schemes resets FLTK's box-type table.
    mmp::registerCustomBoxes();

    mmp::MmpEngine engine;
    mmp::SmfPlayer player(engine);
    mmp::MainWindow win(engine, player);
    win.show(argc, argv);
    int rc = Fl::run();
    player.stop();
    engine.stop();
    return rc;
}
