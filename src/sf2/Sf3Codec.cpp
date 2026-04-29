#include "Sf3Codec.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace mmp {

namespace {

// ---------- RIFF helpers ---------------------------------------------------

inline uint32_t le32(const unsigned char* p) {
    return  (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t le16(const unsigned char* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
inline void wle32(unsigned char* p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
inline void wle16(unsigned char* p, uint16_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
}
inline bool fourcc(const unsigned char* p, const char* tag) {
    return p[0] == (unsigned char)tag[0] && p[1] == (unsigned char)tag[1] &&
           p[2] == (unsigned char)tag[2] && p[3] == (unsigned char)tag[3];
}

bool readAll(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return !out.empty();
}

bool writeAll(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return (bool)f;
}

// Find a sub-chunk inside [start, end). Returns pointer to data, sets
// outSize. nullptr if not found.
const unsigned char* findChunk(const unsigned char* start,
                               const unsigned char* end,
                               const char* tag,
                               uint32_t* outSize) {
    const unsigned char* p = start;
    while (p + 8 <= end) {
        uint32_t sz = le32(p + 4);
        if (fourcc(p, tag)) { if (outSize) *outSize = sz; return p + 8; }
        p += 8 + sz + (sz & 1);
    }
    return nullptr;
}

// Locate the LIST chunk that wraps a particular form ("sdta", "pdta", …).
// Returns the position of the LIST header (so callers can read its size
// for re-packing) or nullptr if not present.
const unsigned char* findListChunk(const unsigned char* start,
                                   const unsigned char* end,
                                   const char* form,
                                   uint32_t* outListSize) {
    const unsigned char* p = start;
    while (p + 8 <= end) {
        uint32_t sz = le32(p + 4);
        if (fourcc(p, "LIST") && p + 12 <= end && fourcc(p + 8, form)) {
            if (outListSize) *outListSize = sz;
            return p;
        }
        p += 8 + sz + (sz & 1);
    }
    return nullptr;
}

// ---------- Vorbis encode (mono PCM int16 → OGG bytes) ---------------------

bool encodePcmMono16ToOgg(const int16_t* pcm, size_t frames, int sampleRate,
                          float quality, std::vector<unsigned char>& out) {
    vorbis_info vi; vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, 1, sampleRate, quality) != 0) {
        vorbis_info_clear(&vi);
        return false;
    }
    vorbis_comment vc; vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "mini-midi-player Sf3Codec");
    vorbis_dsp_state vd; vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, /*serialno=*/(int)((uintptr_t)pcm & 0x7fffffff));

    // Header packets.
    ogg_packet hdr, hdrComm, hdrCode;
    vorbis_analysis_headerout(&vd, &vc, &hdr, &hdrComm, &hdrCode);
    ogg_stream_packetin(&os, &hdr);
    ogg_stream_packetin(&os, &hdrComm);
    ogg_stream_packetin(&os, &hdrCode);
    ogg_page page;
    while (ogg_stream_flush(&os, &page) != 0) {
        out.insert(out.end(), page.header, page.header + page.header_len);
        out.insert(out.end(), page.body,   page.body   + page.body_len);
    }

    // Body — feed analysis in chunks of 1024 frames.
    constexpr size_t kBlock = 1024;
    size_t pos = 0;
    bool eos = false;
    while (!eos) {
        size_t frame = std::min(kBlock, frames - pos);
        if (frame == 0) {
            vorbis_analysis_wrote(&vd, 0);    // signal EOS
            eos = true;
        } else {
            float** buf = vorbis_analysis_buffer(&vd, (int)frame);
            for (size_t i = 0; i < frame; ++i) buf[0][i] = pcm[pos + i] / 32768.0f;
            vorbis_analysis_wrote(&vd, (int)frame);
            pos += frame;
            if (pos >= frames) { vorbis_analysis_wrote(&vd, 0); eos = true; }
        }

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet pkt;
            while (vorbis_bitrate_flushpacket(&vd, &pkt)) {
                ogg_stream_packetin(&os, &pkt);
                while (ogg_stream_pageout(&os, &page) != 0) {
                    out.insert(out.end(), page.header, page.header + page.header_len);
                    out.insert(out.end(), page.body,   page.body   + page.body_len);
                    if (ogg_page_eos(&page)) eos = true;
                }
            }
        }
    }
    while (ogg_stream_flush(&os, &page) != 0) {
        out.insert(out.end(), page.header, page.header + page.header_len);
        out.insert(out.end(), page.body,   page.body   + page.body_len);
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return true;
}

// ---------- Vorbis decode (memory OGG bytes → mono PCM int16) --------------

struct OvMem { const unsigned char* data; size_t size; size_t pos; };

size_t ovRead(void* ptr, size_t sz, size_t nmemb, void* user) {
    auto* m = static_cast<OvMem*>(user);
    size_t want = sz * nmemb;
    if (m->pos + want > m->size) want = m->size - m->pos;
    std::memcpy(ptr, m->data + m->pos, want);
    m->pos += want;
    return want / sz;
}
int ovSeek(void* user, ogg_int64_t off, int whence) {
    auto* m = static_cast<OvMem*>(user);
    size_t base = (whence == SEEK_CUR) ? m->pos : (whence == SEEK_END ? m->size : 0);
    long long target = (long long)base + off;
    if (target < 0 || target > (long long)m->size) return -1;
    m->pos = (size_t)target;
    return 0;
}
long ovTell(void* user) { return (long)static_cast<OvMem*>(user)->pos; }

bool decodeOggToPcmMono16(const unsigned char* data, size_t size,
                          std::vector<int16_t>& out, int& sampleRate) {
    OvMem mem{ data, size, 0 };
    ov_callbacks cb{ ovRead, ovSeek, nullptr, ovTell };
    OggVorbis_File vf;
    if (ov_open_callbacks(&mem, &vf, nullptr, 0, cb) != 0) return false;

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi || vi->channels < 1) { ov_clear(&vf); return false; }
    sampleRate = vi->rate;

    int section = 0;
    char buf[4096];
    while (true) {
        long got = ov_read(&vf, buf, sizeof(buf), /*bigendian=*/0,
                           /*word=*/2, /*signed=*/1, &section);
        if (got <= 0) break;
        // got is bytes; for stereo we'd downmix, but SF2 samples are mono.
        const int16_t* s = reinterpret_cast<const int16_t*>(buf);
        int frames       = (int)(got / sizeof(int16_t)) / vi->channels;
        for (int i = 0; i < frames; ++i) {
            // Mono pick (or left channel) — SF2 samples are always mono.
            out.push_back(s[i * vi->channels]);
        }
    }
    ov_clear(&vf);
    return !out.empty();
}

// ---------- shdr (Sample Header) record layout -----------------------------
//
//   byte  0..19 : sample name (null-padded)
//   byte 20..23 : start          (uint32 LE) — frame index into smpl/smpo
//   byte 24..27 : end            (uint32 LE)
//   byte 28..31 : startLoop
//   byte 32..35 : endLoop
//   byte 36..39 : sampleRate
//   byte 40     : originalKey
//   byte 41     : correction
//   byte 42..43 : sampleLink
//   byte 44..45 : sampleType
//
// 46 bytes per record. Last record is the EOS terminator with name "EOS".

constexpr size_t kShdrRecord = 46;

} // namespace


// ===========================================================================
// SF2 → SFO (compress)
// ===========================================================================

bool compressSf2ToSf3(const std::string& sf2Path,
                      const std::string& sfoPath,
                      const Sf3CompressOptions& opts,
                      Sf3CompressStats& stats) {
    std::vector<unsigned char> in;
    if (!readAll(sf2Path, in)) {
        stats.message = "cannot read input";
        return false;
    }
    if (in.size() < 12 || !fourcc(in.data(), "RIFF") ||
        !fourcc(in.data() + 8, "sfbk")) {
        stats.message = "not a SoundFont 2 file";
        return false;
    }
    const unsigned char* end = in.data() + in.size();

    // ---- Locate sdta/smpl ------------------------------------------------
    uint32_t sdtaListSz = 0;
    const unsigned char* sdtaList = findListChunk(in.data() + 12, end, "sdta", &sdtaListSz);
    if (!sdtaList) { stats.message = "missing sdta LIST"; return false; }
    const unsigned char* sdtaStart = sdtaList + 12;
    const unsigned char* sdtaEnd   = sdtaList + 8 + sdtaListSz;

    uint32_t smplSize = 0;
    const unsigned char* smpl = findChunk(sdtaStart, sdtaEnd, "smpl", &smplSize);
    if (!smpl) { stats.message = "no smpl chunk (already compressed?)"; return false; }
    stats.pcmBytes = smplSize;

    // ---- Locate pdta/shdr ------------------------------------------------
    uint32_t pdtaListSz = 0;
    const unsigned char* pdtaList = findListChunk(in.data() + 12, end, "pdta", &pdtaListSz);
    if (!pdtaList) { stats.message = "missing pdta LIST"; return false; }
    const unsigned char* pdtaStart = pdtaList + 12;
    const unsigned char* pdtaEnd   = pdtaList + 8 + pdtaListSz;
    uint32_t shdrSize = 0;
    const unsigned char* shdr = findChunk(pdtaStart, pdtaEnd, "shdr", &shdrSize);
    if (!shdr || shdrSize < kShdrRecord) {
        stats.message = "missing or invalid shdr chunk";
        return false;
    }
    size_t recCount = shdrSize / kShdrRecord;     // includes EOS

    // ---- Encode each sample, build the new smpo blob + new shdr offsets --
    std::vector<unsigned char> smpo;        // concatenated Ogg streams
    std::vector<unsigned char> shdrNew(shdrSize, 0);
    std::memcpy(shdrNew.data(), shdr, shdrSize);

    const int16_t* pcm = reinterpret_cast<const int16_t*>(smpl);
    const size_t   pcmFrames = smplSize / 2;

    for (size_t i = 0; i + 1 < recCount; ++i) {
        unsigned char* rec = shdrNew.data() + i * kShdrRecord;
        uint32_t startFrame = le32(rec + 20);
        uint32_t endFrame   = le32(rec + 24);
        uint32_t loopStart  = le32(rec + 28);
        uint32_t loopEnd    = le32(rec + 32);
        uint32_t sRate      = le32(rec + 36);
        if (endFrame > pcmFrames) endFrame = (uint32_t)pcmFrames;
        if (startFrame >= endFrame) continue;

        std::vector<unsigned char> ogg;
        if (!encodePcmMono16ToOgg(pcm + startFrame, endFrame - startFrame,
                                  (int)sRate, opts.quality, ogg)) {
            stats.message = "vorbis encode failed";
            return false;
        }
        // SFO: start = byte offset of this sample's Ogg stream in smpo.
        // end   = byte offset of next sample (= start + ogg.size()).
        uint32_t newStart = (uint32_t)smpo.size();
        smpo.insert(smpo.end(), ogg.begin(), ogg.end());
        uint32_t newEnd   = (uint32_t)smpo.size();

        // Loop points stay in PCM-frame space (TSF re-derives them
        // post-decode); store them in the shdr unchanged.
        wle32(rec + 20, newStart);
        wle32(rec + 24, newEnd);
        wle32(rec + 28, loopStart - startFrame);   // relative to sample start
        wle32(rec + 32, loopEnd   - startFrame);

        ++stats.samplesProcessed;
        stats.oggBytes += (uint32_t)ogg.size();
    }

    // ---- Re-pack the file: every chunk preserved, smpl→smpo replaced -----
    std::vector<unsigned char> outBytes;
    outBytes.reserve(in.size() / 2 + smpo.size() + 1024);

    // RIFF header (size patched at the end).
    outBytes.insert(outBytes.end(), in.begin(), in.begin() + 12);

    auto walkAndCopy = [&](const unsigned char* p, const unsigned char* pend) {
        // Copy everything until/excluding sdta LIST and pdta LIST; rewrite
        // those two ourselves.
        while (p + 8 <= pend) {
            uint32_t sz = le32(p + 4);
            const unsigned char* dataStart = p + 8;
            bool isList = fourcc(p, "LIST");
            const char* form = isList ? (const char*)dataStart : nullptr;
            size_t total = 8 + sz + (sz & 1);
            if (isList && form) {
                if (std::memcmp(form, "sdta", 4) == 0) {
                    // Write sdta LIST with our smpo chunk replacing smpl.
                    uint32_t newSdtaSz = 4 + 8 + (uint32_t)smpo.size()
                                       + ((smpo.size() & 1) ? 1 : 0);
                    unsigned char hdr[12] = {'L','I','S','T'};
                    wle32(hdr + 4, newSdtaSz);
                    std::memcpy(hdr + 8, "sdta", 4);
                    outBytes.insert(outBytes.end(), hdr, hdr + 12);

                    unsigned char smpoHdr[8] = {'s','m','p','o'};
                    wle32(smpoHdr + 4, (uint32_t)smpo.size());
                    outBytes.insert(outBytes.end(), smpoHdr, smpoHdr + 8);
                    outBytes.insert(outBytes.end(), smpo.begin(), smpo.end());
                    if (smpo.size() & 1) outBytes.push_back(0);
                } else if (std::memcmp(form, "pdta", 4) == 0) {
                    // Recompute pdta with the rewritten shdr.
                    std::vector<unsigned char> pdta;
                    pdta.reserve(sz);
                    pdta.insert(pdta.end(), dataStart, dataStart + 4);  // "pdta"
                    const unsigned char* pp = dataStart + 4;
                    const unsigned char* pe = dataStart + sz;
                    while (pp + 8 <= pe) {
                        uint32_t sub = le32(pp + 4);
                        if (fourcc(pp, "shdr")) {
                            unsigned char h[8] = {'s','h','d','r'};
                            wle32(h + 4, (uint32_t)shdrNew.size());
                            pdta.insert(pdta.end(), h, h + 8);
                            pdta.insert(pdta.end(), shdrNew.begin(), shdrNew.end());
                        } else {
                            pdta.insert(pdta.end(), pp, pp + 8 + sub + (sub & 1));
                        }
                        pp += 8 + sub + (sub & 1);
                    }
                    unsigned char hdr[8] = {'L','I','S','T'};
                    wle32(hdr + 4, (uint32_t)pdta.size());
                    outBytes.insert(outBytes.end(), hdr, hdr + 8);
                    outBytes.insert(outBytes.end(), pdta.begin(), pdta.end());
                    if (pdta.size() & 1) outBytes.push_back(0);
                } else {
                    outBytes.insert(outBytes.end(), p, p + total);
                }
            } else {
                outBytes.insert(outBytes.end(), p, p + total);
            }
            p += total;
        }
    };
    walkAndCopy(in.data() + 12, end);

    // Patch RIFF chunk size (file size minus 8).
    wle32(outBytes.data() + 4, (uint32_t)(outBytes.size() - 8));

    if (!writeAll(sfoPath, outBytes)) {
        stats.message = "cannot write output";
        return false;
    }
    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "OK — %d samples, %u → %u bytes (%.1f %%)",
                  stats.samplesProcessed, stats.pcmBytes, stats.oggBytes,
                  stats.pcmBytes > 0
                      ? 100.0 * stats.oggBytes / stats.pcmBytes
                      : 0.0);
    stats.message = msg;
    return true;
}


// ===========================================================================
// SFO → SF2 (decompress)
// ===========================================================================

bool decompressSf3ToSf2(const std::string& sfoPath,
                        const std::string& sf2Path,
                        Sf3DecompressStats& stats) {
    std::vector<unsigned char> in;
    if (!readAll(sfoPath, in)) {
        stats.message = "cannot read input";
        return false;
    }
    if (in.size() < 12 || !fourcc(in.data(), "RIFF") ||
        !fourcc(in.data() + 8, "sfbk")) {
        stats.message = "not a SoundFont file";
        return false;
    }
    const unsigned char* end = in.data() + in.size();

    uint32_t sdtaSz = 0;
    const unsigned char* sdtaList = findListChunk(in.data() + 12, end, "sdta", &sdtaSz);
    if (!sdtaList) { stats.message = "missing sdta LIST"; return false; }
    const unsigned char* sdtaStart = sdtaList + 12;
    const unsigned char* sdtaEnd   = sdtaList + 8 + sdtaSz;

    uint32_t smpoSize = 0;
    const unsigned char* smpo = findChunk(sdtaStart, sdtaEnd, "smpo", &smpoSize);
    if (!smpo) { stats.message = "no smpo chunk (already PCM?)"; return false; }
    stats.oggBytes = smpoSize;

    uint32_t pdtaSz = 0;
    const unsigned char* pdtaList = findListChunk(in.data() + 12, end, "pdta", &pdtaSz);
    if (!pdtaList) { stats.message = "missing pdta LIST"; return false; }
    const unsigned char* pdtaStart = pdtaList + 12;
    const unsigned char* pdtaEnd   = pdtaList + 8 + pdtaSz;
    uint32_t shdrSize = 0;
    const unsigned char* shdr = findChunk(pdtaStart, pdtaEnd, "shdr", &shdrSize);
    if (!shdr) { stats.message = "missing shdr chunk"; return false; }
    size_t recCount = shdrSize / kShdrRecord;

    // ---- Decode each sample, build new smpl PCM blob + rewritten shdr ----
    std::vector<int16_t>      pcm;
    std::vector<unsigned char> shdrNew(shdrSize, 0);
    std::memcpy(shdrNew.data(), shdr, shdrSize);

    for (size_t i = 0; i + 1 < recCount; ++i) {
        unsigned char* rec = shdrNew.data() + i * kShdrRecord;
        uint32_t oggStart = le32(rec + 20);
        uint32_t oggEnd   = le32(rec + 24);
        if (oggEnd <= oggStart || oggEnd > smpoSize) continue;

        std::vector<int16_t> samples;
        int sr = 0;
        if (!decodeOggToPcmMono16(smpo + oggStart, oggEnd - oggStart,
                                   samples, sr)) {
            stats.message = "vorbis decode failed";
            return false;
        }
        uint32_t newStart = (uint32_t)pcm.size();
        pcm.insert(pcm.end(), samples.begin(), samples.end());
        uint32_t newEnd   = (uint32_t)pcm.size();
        wle32(rec + 20, newStart);
        wle32(rec + 24, newEnd);
        // Loop points were stored relative to sample start in compress;
        // translate back to absolute frame indices.
        uint32_t loopStartRel = le32(rec + 28);
        uint32_t loopEndRel   = le32(rec + 32);
        wle32(rec + 28, newStart + loopStartRel);
        wle32(rec + 32, newStart + loopEndRel);
        wle32(rec + 36, (uint32_t)sr);
        ++stats.samplesProcessed;
    }
    stats.pcmBytes = (uint32_t)(pcm.size() * 2);

    // ---- Re-pack file with smpo → smpl + rewritten shdr ------------------
    std::vector<unsigned char> outBytes;
    outBytes.reserve(in.size() + stats.pcmBytes);
    outBytes.insert(outBytes.end(), in.begin(), in.begin() + 12);

    const unsigned char* p = in.data() + 12;
    while (p + 8 <= end) {
        uint32_t sz = le32(p + 4);
        size_t total = 8 + sz + (sz & 1);
        if (fourcc(p, "LIST") && p + 12 <= end) {
            if (fourcc(p + 8, "sdta")) {
                uint32_t bodySz = 4 + 8 + stats.pcmBytes
                                + ((stats.pcmBytes & 1) ? 1 : 0);
                unsigned char hdr[12] = {'L','I','S','T'};
                wle32(hdr + 4, bodySz);
                std::memcpy(hdr + 8, "sdta", 4);
                outBytes.insert(outBytes.end(), hdr, hdr + 12);
                unsigned char smplHdr[8] = {'s','m','p','l'};
                wle32(smplHdr + 4, stats.pcmBytes);
                outBytes.insert(outBytes.end(), smplHdr, smplHdr + 8);
                outBytes.insert(outBytes.end(),
                                reinterpret_cast<unsigned char*>(pcm.data()),
                                reinterpret_cast<unsigned char*>(pcm.data()) + stats.pcmBytes);
                if (stats.pcmBytes & 1) outBytes.push_back(0);
                p += total; continue;
            }
            if (fourcc(p + 8, "pdta")) {
                std::vector<unsigned char> pdta;
                pdta.reserve(sz);
                pdta.insert(pdta.end(), p + 8, p + 12);
                const unsigned char* pp = p + 12;
                const unsigned char* pe = p + 8 + sz;
                while (pp + 8 <= pe) {
                    uint32_t sub = le32(pp + 4);
                    if (fourcc(pp, "shdr")) {
                        unsigned char h[8] = {'s','h','d','r'};
                        wle32(h + 4, (uint32_t)shdrNew.size());
                        pdta.insert(pdta.end(), h, h + 8);
                        pdta.insert(pdta.end(), shdrNew.begin(), shdrNew.end());
                    } else {
                        pdta.insert(pdta.end(), pp, pp + 8 + sub + (sub & 1));
                    }
                    pp += 8 + sub + (sub & 1);
                }
                unsigned char hdr[8] = {'L','I','S','T'};
                wle32(hdr + 4, (uint32_t)pdta.size());
                outBytes.insert(outBytes.end(), hdr, hdr + 8);
                outBytes.insert(outBytes.end(), pdta.begin(), pdta.end());
                if (pdta.size() & 1) outBytes.push_back(0);
                p += total; continue;
            }
        }
        outBytes.insert(outBytes.end(), p, p + total);
        p += total;
    }
    wle32(outBytes.data() + 4, (uint32_t)(outBytes.size() - 8));

    if (!writeAll(sf2Path, outBytes)) {
        stats.message = "cannot write output";
        return false;
    }
    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "OK — %d samples, %u → %u bytes",
                  stats.samplesProcessed, stats.oggBytes, stats.pcmBytes);
    stats.message = msg;
    return true;
}

} // namespace mmp
