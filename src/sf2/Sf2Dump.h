#pragma once

// Sf2Dump — developer tools for inspecting SoundFont 2 files.
//
// First-cut implementation reuses TSF's already-parsed preset table to list
// presets (bank/program/name). A future expansion will add a full RIFF
// chunk walker for instruments / samples / generators / modulators.

#include <ostream>
#include <string>

namespace mmp {

struct Sf2DumpOptions {
    bool listPresets = true;
    bool jsonOutput  = false;
};

// Load the SF2 from `path` and write a human- or machine-readable dump to
// `out`. Returns true on success, false if the file couldn't be opened.
bool dumpSoundFont(const std::string& path,
                   const Sf2DumpOptions& opts,
                   std::ostream& out);

} // namespace mmp
