#include "SmfDump.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace mmp {

namespace {

inline uint16_t be16(const unsigned char* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
inline uint32_t be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
inline bool fourcc(const unsigned char* p, const char* tag) {
    return p[0] == (unsigned char)tag[0] && p[1] == (unsigned char)tag[1] &&
           p[2] == (unsigned char)tag[2] && p[3] == (unsigned char)tag[3];
}

// Read a Standard MIDI variable-length quantity. Advances `p`.
unsigned int readVlq(const unsigned char*& p, const unsigned char* end) {
    unsigned int v = 0;
    while (p < end) {
        unsigned char b = *p++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

bool readAll(const std::string& path, std::vector<unsigned char>& buf) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    buf.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return !buf.empty();
}

} // namespace

bool parseSmf(const std::string& path, SmfReport& r) {
    std::vector<unsigned char> buf;
    if (!readAll(path, buf)) return false;
    if (buf.size() < 14) return false;
    const unsigned char* p   = buf.data();
    const unsigned char* end = p + buf.size();

    if (!fourcc(p, "MThd")) return false;
    uint32_t hdrLen = be32(p + 4);
    if (hdrLen < 6) return false;
    r.filePath  = path;
    r.format    = be16(p + 8);
    r.numTracks = be16(p + 10);
    int16_t div = (int16_t)be16(p + 12);
    r.division  = div;
    p += 8 + hdrLen;

    int trackIdx = 0;
    unsigned int maxEndTick = 0;

    while (p + 8 <= end && trackIdx < r.numTracks) {
        if (!fourcc(p, "MTrk")) break;
        uint32_t trkLen = be32(p + 4);
        const unsigned char* tp  = p + 8;
        const unsigned char* tpEnd = tp + trkLen;
        if (tpEnd > end) tpEnd = end;

        SmfTrack track;
        track.index = trackIdx;
        char defaultName[24];
        std::snprintf(defaultName, sizeof(defaultName), "(track %d)", trackIdx + 1);
        track.name = defaultName;

        unsigned int absTick = 0;
        unsigned char runningStatus = 0;

        while (tp < tpEnd) {
            unsigned int delta = readVlq(tp, tpEnd);
            absTick += delta;
            if (tp >= tpEnd) break;

            unsigned char status = *tp;
            if (status & 0x80) { ++tp; runningStatus = status; }
            else                status = runningStatus;

            track.eventCount++;

            if (status == 0xFF) {
                if (tp >= tpEnd) break;
                unsigned char metaType = *tp++;
                unsigned int metaLen = readVlq(tp, tpEnd);
                if (tp + metaLen > tpEnd) break;

                switch (metaType) {
                case 0x03: { // sequence/track name
                    track.name.assign((const char*)tp, metaLen);
                    break;
                }
                case 0x04: { // instrument name
                    track.instrument.assign((const char*)tp, metaLen);
                    break;
                }
                case 0x51: { // set tempo: 24-bit microseconds per quarter
                    if (metaLen >= 3) {
                        unsigned int us = (tp[0] << 16) | (tp[1] << 8) | tp[2];
                        SmfTempo t;
                        t.tickAbs = absTick;
                        t.bpm     = us > 0 ? 60000000.0 / (double)us : 120.0;
                        r.tempoMap.push_back(t);
                    }
                    break;
                }
                case 0x58: { // time signature: nn dd cc bb
                    if (metaLen >= 4) {
                        SmfTimeSig ts;
                        ts.tickAbs    = absTick;
                        ts.numerator  = tp[0];
                        ts.denominator = 1 << tp[1];
                        r.timeSigs.push_back(ts);
                    }
                    break;
                }
                case 0x59: { // key signature: sf mi
                    if (metaLen >= 2) {
                        SmfKeySig ks;
                        ks.tickAbs    = absTick;
                        ks.sharpsFlats = (int8_t)tp[0];
                        ks.minor       = tp[1] ? 1 : 0;
                        r.keySigs.push_back(ks);
                    }
                    break;
                }
                default: break;
                }
                tp += metaLen;
            }
            else if (status == 0xF0 || status == 0xF7) {
                unsigned int sysLen = readVlq(tp, tpEnd);
                if (tp + sysLen > tpEnd) break;
                tp += sysLen;
            }
            else {
                // Channel voice message
                int ch = status & 0x0F;
                track.channels |= (1 << ch);
                int kind = status & 0xF0;
                int dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (kind == 0x90) {
                    if (tp + 1 < tpEnd && tp[1] != 0) {
                        track.noteCount++;
                        r.totalNotes++;
                    }
                }
                if (tp + dataBytes > tpEnd) break;
                tp += dataBytes;
            }
        }

        if (absTick > maxEndTick) maxEndTick = absTick;
        r.tracks.push_back(track);
        p += 8 + trkLen;
        ++trackIdx;
    }

    r.totalTicks = maxEndTick;
    return true;
}

void writeSmfReport(const SmfReport& r, std::ostream& out) {
    char buf[160];

    out << "File:     " << r.filePath << "\n";
    std::snprintf(buf, sizeof(buf),
                  "Format:   %d\nTracks:   %d\nDivision: %d %s\n",
                  r.format, r.numTracks, r.division,
                  r.division > 0 ? "PPQ" : "(SMPTE)");
    out << buf;
    std::snprintf(buf, sizeof(buf),
                  "Notes:    %d\nLength:   %u ticks\n\n",
                  r.totalNotes, r.totalTicks);
    out << buf;

    out << "TRACKS\n";
    out << "  IDX   EVENTS  NOTES  CH  NAME (instrument)\n";
    out << "  ---   ------  -----  --  -------------------------------\n";
    for (const auto& t : r.tracks) {
        char chBuf[32] = {0};
        int n = 0;
        for (int c = 0; c < 16; ++c) {
            if (t.channels & (1 << c)) {
                if (n) std::strcat(chBuf, ",");
                char tmp[6]; std::snprintf(tmp, sizeof(tmp), "%d", c + 1);
                std::strcat(chBuf, tmp);
                n++;
                if (std::strlen(chBuf) > 14) break;
            }
        }
        std::string label = t.name;
        if (!t.instrument.empty()) label += " (" + t.instrument + ")";
        std::snprintf(buf, sizeof(buf),
                      "  %3d   %6d  %5d  %-2s  %s\n",
                      t.index + 1, t.eventCount, t.noteCount,
                      n ? chBuf : "-", label.c_str());
        out << buf;
    }

    if (!r.tempoMap.empty()) {
        out << "\nTEMPO MAP\n";
        for (const auto& t : r.tempoMap) {
            std::snprintf(buf, sizeof(buf),
                          "  tick %8u   %6.2f BPM\n", t.tickAbs, t.bpm);
            out << buf;
        }
    }
    if (!r.timeSigs.empty()) {
        out << "\nTIME SIGNATURES\n";
        for (const auto& s : r.timeSigs) {
            std::snprintf(buf, sizeof(buf),
                          "  tick %8u   %d/%d\n",
                          s.tickAbs, s.numerator, s.denominator);
            out << buf;
        }
    }
    if (!r.keySigs.empty()) {
        out << "\nKEY SIGNATURES\n";
        for (const auto& s : r.keySigs) {
            std::snprintf(buf, sizeof(buf),
                          "  tick %8u   %+d %s\n",
                          s.tickAbs, s.sharpsFlats, s.minor ? "minor" : "major");
            out << buf;
        }
    }
}

} // namespace mmp
