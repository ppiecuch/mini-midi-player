#pragma once

// Sf3Codec — convert between standard SoundFont 2 (smpl chunk = raw
// 16-bit PCM, mono, 22050 Hz typical) and the TSF "SFO" variant
// (smpo chunk = each sample compressed as its own Ogg-Vorbis stream,
// concatenated, with the per-sample shdr offsets re-written to point
// at the new byte positions).
//
// Both directions preserve every other RIFF chunk (info / pdta /
// preset / instrument / generator / modulator records) byte-for-byte
// so the resulting font behaves identically when loaded by TSF.
//
// Only TSF's SFO format is targeted — *not* the standard SF3 used by
// FluidSynth / Polyphone (those use chunk id "smpl" with OGG content
// and per-sample byte offsets, an incompatible layout). If/when SF3
// support is wanted, the encode/decode primitives below are reusable;
// only the chunk re-pack changes.

#include <ostream>
#include <string>

namespace mmp {

struct Sf3CompressOptions {
    // Vorbis encoder quality, -0.1..1.0. Around 0.4 ≈ 128 kbit/s on
    // typical SF2 sample material; 0.6 ≈ 192 kbit/s. Default is a
    // conservative middle ground.
    float quality = 0.4f;
};

struct Sf3CompressStats {
    int          samplesProcessed = 0;
    unsigned int pcmBytes         = 0;
    unsigned int oggBytes         = 0;
    std::string  message;          // human-readable error / status
};

// SF2 → SFO. Returns true on success; on failure `stats.message` is
// populated. The output file is created/overwritten on success.
bool compressSf2ToSf3(const std::string& sf2Path,
                      const std::string& sfoPath,
                      const Sf3CompressOptions& opts,
                      Sf3CompressStats& stats);

struct Sf3DecompressStats {
    int          samplesProcessed = 0;
    unsigned int oggBytes         = 0;
    unsigned int pcmBytes         = 0;
    std::string  message;
};

// SFO → SF2. Returns true on success; populates stats either way.
bool decompressSf3ToSf2(const std::string& sfoPath,
                        const std::string& sf2Path,
                        Sf3DecompressStats& stats);

} // namespace mmp
