#include "Sf2Dump.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Minimal RIFF chunk walker. SoundFont 2 = RIFF "sfbk" with three list
// chunks: INFO, sdta, pdta. The preset table lives inside pdta as the
// "phdr" sub-chunk — fixed-width 38-byte records ending with a sentinel.
// We only need name+bank+program for the first dump cut, so the walker
// stays tiny.

namespace mmp {

namespace {

bool readAll(const std::string& path, std::vector<unsigned char>& buf) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    buf.resize((size_t)sz);
    size_t got = std::fread(buf.data(), 1, (size_t)sz, f);
    std::fclose(f);
    return got == (size_t)sz;
}

inline uint32_t le32(const unsigned char* p) {
    return  (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t le16(const unsigned char* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
inline bool fourcc(const unsigned char* p, const char* tag) {
    return p[0] == (unsigned char)tag[0] && p[1] == (unsigned char)tag[1] &&
           p[2] == (unsigned char)tag[2] && p[3] == (unsigned char)tag[3];
}

// Find a sub-chunk with `tag` inside [start, end). Returns pointer to its
// data or nullptr; *outSize receives the chunk's data size.
const unsigned char* findChunk(const unsigned char* start,
                               const unsigned char* end,
                               const char* tag,
                               uint32_t* outSize) {
    const unsigned char* p = start;
    while (p + 8 <= end) {
        uint32_t sz = le32(p + 4);
        if (fourcc(p, tag)) { if (outSize) *outSize = sz; return p + 8; }
        p += 8 + sz + (sz & 1);   // chunks are word-aligned
    }
    return nullptr;
}

void writeJsonString(std::ostream& out, const char* s) {
    out << '"';
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); out << b; }
                else          { out << (char)c; }
        }
    }
    out << '"';
}

} // namespace

bool dumpSoundFont(const std::string& path,
                   const Sf2DumpOptions& opts,
                   std::ostream& out) {
    std::vector<unsigned char> buf;
    if (!readAll(path, buf)) return false;
    if (buf.size() < 12) return false;

    // Top-level: "RIFF" <size> "sfbk"
    if (!fourcc(buf.data(), "RIFF") || !fourcc(buf.data() + 8, "sfbk")) return false;
    const unsigned char* end = buf.data() + buf.size();
    const unsigned char* p   = buf.data() + 12;

    // Walk the three top-level LIST chunks (INFO / sdta / pdta).
    const unsigned char* pdtaStart = nullptr;
    uint32_t pdtaSize = 0;
    while (p + 8 <= end) {
        uint32_t sz = le32(p + 4);
        if (fourcc(p, "LIST") && p + 12 <= end && fourcc(p + 8, "pdta")) {
            pdtaStart = p + 12;
            pdtaSize  = sz - 4;
            break;
        }
        p += 8 + sz + (sz & 1);
    }
    if (!pdtaStart) return false;

    uint32_t phdrSize = 0;
    const unsigned char* phdr = findChunk(pdtaStart, pdtaStart + pdtaSize,
                                          "phdr", &phdrSize);
    if (!phdr) return false;

    // phdr records are 38 bytes; last one is EOP terminator.
    constexpr size_t kPhdrRecord = 38;
    size_t recCount = phdrSize / kPhdrRecord;
    if (recCount > 0) recCount -= 1; // drop EOP

    if (opts.jsonOutput) {
        out << "{\n  \"path\": ";
        writeJsonString(out, path.c_str());
        out << ",\n  \"presets\": [\n";
        for (size_t i = 0; i < recCount; ++i) {
            const unsigned char* r = phdr + i * kPhdrRecord;
            char name[21]; std::memcpy(name, r, 20); name[20] = 0;
            uint16_t prog = le16(r + 20);
            uint16_t bank = le16(r + 22);
            out << "    { \"index\": " << i
                << ", \"bank\": " << bank
                << ", \"program\": " << prog
                << ", \"name\": ";
            writeJsonString(out, name);
            out << " }" << (i + 1 < recCount ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
    } else {
        // Nicely framed table with ASCII dividers (---+|).
        // Columns: IDX(5) BANK(6) PROG(6) NAME(28)
        constexpr int wIdx = 5, wBank = 6, wProg = 6, wName = 28;
        auto sep = [&]() {
            out << '+';
            for (int i = 0; i < wIdx + 2;  ++i) out << '-'; out << '+';
            for (int i = 0; i < wBank + 2; ++i) out << '-'; out << '+';
            for (int i = 0; i < wProg + 2; ++i) out << '-'; out << '+';
            for (int i = 0; i < wName + 2; ++i) out << '-'; out << "+\n";
        };
        out << "SoundFont: " << path << "\n";
        out << "Presets:   " << recCount << "\n\n";

        sep();
        char hdr[160];
        std::snprintf(hdr, sizeof(hdr),
                      "| %*s | %*s | %*s | %-*s |\n",
                      wIdx,  "IDX", wBank, "BANK", wProg, "PROG", wName, "NAME");
        out << hdr;
        sep();
        for (size_t i = 0; i < recCount; ++i) {
            const unsigned char* r = phdr + i * kPhdrRecord;
            char name[21]; std::memcpy(name, r, 20); name[20] = 0;
            uint16_t prog = le16(r + 20);
            uint16_t bank = le16(r + 22);
            char row[160];
            std::snprintf(row, sizeof(row),
                          "| %*zu | %*u | %*u | %-*s |\n",
                          wIdx, i, wBank, bank, wProg, prog, wName, name);
            out << row;
        }
        sep();
    }
    (void)opts.listPresets;
    return true;
}

} // namespace mmp
