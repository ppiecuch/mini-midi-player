#pragma once

// SmfDump — comprehensive Standard MIDI File inspector. Walks the
// MThd / MTrk chunks directly so it can surface details TML's flat
// linked list discards (per-track names, instrument names, the tempo
// map, time / key signature changes, sysex, etc.).

#include <ostream>
#include <string>
#include <vector>

namespace mmp {

struct SmfTrack {
    int          index;
    std::string  name;            // FF 03 meta or "(track N)"
    std::string  instrument;      // FF 04 meta if present
    int          eventCount   = 0;
    int          noteCount    = 0;
    int          channels     = 0;   // bitmask of channels used
};

struct SmfTempo {
    unsigned int tickAbs;
    double       bpm;
};

struct SmfTimeSig {
    unsigned int tickAbs;
    int numerator, denominator;
};

struct SmfKeySig {
    unsigned int tickAbs;
    int sharpsFlats;     // -7..+7, negative = flats
    int minor;           // 0 = major, 1 = minor
};

struct SmfReport {
    std::string filePath;
    int format         = -1;           // 0/1/2
    int numTracks      = 0;
    int division       = 0;            // PPQ if positive
    int totalNotes     = 0;
    unsigned int totalTicks = 0;       // length in ticks
    std::vector<SmfTrack>    tracks;
    std::vector<SmfTempo>    tempoMap;
    std::vector<SmfTimeSig>  timeSigs;
    std::vector<SmfKeySig>   keySigs;
};

// Parse `path`. Returns true on success, false if the file isn't a SMF.
bool parseSmf(const std::string& path, SmfReport& out);

// Pretty-print the report to a stream (used by the GUI inspector and
// can be hooked up to the CLI later).
void writeSmfReport(const SmfReport& r, std::ostream& out);

} // namespace mmp
