#include "core/MmpEngine.h"
#include "midi/SmfPlayer.h"
#include "sf2/Sf2Dump.h"
#include "sf2/Sf3Codec.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.0.0"
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

namespace {

void printUsage(FILE* f) {
    std::fprintf(f,
        "\n"
        "    ##::::''##:'####:'##::: ##:'####::'##::::'##:'####:'########:'####:\n"
        "    ###::'####:. ##:: ###:: ##:. ##::: ###::'###:. ##:: ##.... ##. ##::\n"
        "    ####'####::: ##:: ####: ##:: ##::: ####'####:: ##:: ##:::: ##: ##::\n"
        "    ## ### ##::: ##:: ## ## ##:: ##::: ## ### ##:: ##:: ##:::: ##: ##::\n"
        "    ##. #: ##::: ##:: ##. ####:: ##::: ##. #: ##:: ##:: ##:::: ##: ##::\n"
        "    ##:.:: ##::: ##:: ##:. ###:: ##::: ##:.:: ##:: ##:: ##:::: ##: ##::\n"
        "    ##:::: ##:'####: ##::. ##:'####:: ##:::: ##:'####: ########:'####:\n"
        "    ..:::::..::....::..::::..::....:::..:::::..::....::........::....::\n"
        "                            P L A Y E R                v%s (build %d)\n"
        "\n"
        "USAGE\n"
        "  mmp play   <file.mid> -sf <file.sf2>\n"
        "      Play a MIDI file via the default audio device.\n"
        "\n"
        "  mmp render <file.mid> -sf <file.sf2> -o <out.wav> [-r 44100]\n"
        "      Render a MIDI file to WAV without opening the audio device.\n"
        "\n"
        "  mmp dump   <file.sf2> [--json]\n"
        "      Inspect a SoundFont — preset table (bank/program/name).\n"
        "\n"
        "  mmp probe\n"
        "      Enumerate audio output and MIDI input devices.\n"
        "\n"
        "  mmp -h | --help | -v | --version\n"
        "\n"
        "OPTIONS\n"
        "  -sf, --soundfont <path>    SoundFont (.sf2) file used by play/render\n"
        "  -o,  --output    <path>    output WAV path (render)\n"
        "  -r,  --rate      <hz>      sample rate (default 44100)\n"
        "       --json                JSON output for `dump`\n"
        "\n"
        "Embedded inside MiniMidiPlayer.app/Contents/MacOS/mmp on macOS.\n"
        "\n",
        BUILD_VERSION, (int)BUILD_NUMBER);
}

bool nextArg(int& i, int argc, char** argv, const char* longF, const char* shortF,
             std::string& out) {
    if (std::strcmp(argv[i], longF) == 0 || std::strcmp(argv[i], shortF) == 0) {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "mmp: %s requires an argument\n", argv[i]);
            return false;
        }
        out = argv[++i];
        return true;
    }
    return false;
}

// ---------------- WAV writer ----------------------------------------------

bool writeWav(const std::string& path, const float* samples, int frames, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int channels = 2;
    int bytesPerSample = 2;
    int dataBytes = frames * channels * bytesPerSample;
    auto w32 = [&](uint32_t v){ f.put(v); f.put(v>>8); f.put(v>>16); f.put(v>>24); };
    auto w16 = [&](uint16_t v){ f.put(v); f.put(v>>8); };

    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16);
    w16(1);                       // PCM
    w16(channels);
    w32(rate);
    w32(rate * channels * bytesPerSample);
    w16(channels * bytesPerSample);
    w16(bytesPerSample * 8);
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

// ---------------- subcommands ---------------------------------------------

int cmdPlay(int argc, char** argv) {
    if (argc < 2) { printUsage(stderr); return 2; }
    std::string mid = argv[1];
    std::string sf2;
    for (int i = 2; i < argc; ++i) {
        std::string v;
        if (nextArg(i, argc, argv, "--soundfont", "-sf", v)) sf2 = v;
        else { std::fprintf(stderr, "mmp: unknown option %s\n", argv[i]); return 2; }
    }
    if (sf2.empty()) { std::fprintf(stderr, "mmp: --soundfont is required\n"); return 2; }

    mmp::MmpEngine engine;
    if (!engine.loadSoundFont(sf2)) {
        std::fprintf(stderr, "mmp: failed to load %s\n", sf2.c_str()); return 1;
    }
    if (!engine.start(44100)) {
        std::fprintf(stderr, "mmp: could not start audio device\n"); return 1;
    }

    mmp::SmfPlayer player(engine);
    if (!player.load(mid)) {
        std::fprintf(stderr, "mmp: failed to load %s\n", mid.c_str()); return 1;
    }
    std::printf("Playing %s (%u ms) with %s …\n",
                mid.c_str(), player.lengthMs(), sf2.c_str());
    player.play();

    while (player.isPlaying()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    player.stop();
    engine.stop();
    std::printf("Done.\n");
    return 0;
}

int cmdRender(int argc, char** argv) {
    if (argc < 2) { printUsage(stderr); return 2; }
    std::string mid = argv[1];
    std::string sf2, out;
    int rate = 44100;
    for (int i = 2; i < argc; ++i) {
        std::string v;
        if (nextArg(i, argc, argv, "--soundfont", "-sf", v)) sf2 = v;
        else if (nextArg(i, argc, argv, "--output", "-o", v)) out = v;
        else if (nextArg(i, argc, argv, "--rate",   "-r", v)) rate = std::atoi(v.c_str());
        else { std::fprintf(stderr, "mmp: unknown option %s\n", argv[i]); return 2; }
    }
    if (sf2.empty() || out.empty()) {
        std::fprintf(stderr, "mmp: --soundfont and --output are required\n"); return 2;
    }

    mmp::MmpEngine engine;
    if (!engine.loadSoundFont(sf2)) {
        std::fprintf(stderr, "mmp: failed to load %s\n", sf2.c_str()); return 1;
    }
    // Render at the requested rate without opening an audio device.
    mmp::SmfPlayer player(engine);
    if (!player.load(mid)) {
        std::fprintf(stderr, "mmp: failed to load %s\n", mid.c_str()); return 1;
    }
    long long frames = (long long)player.lengthMs() * rate / 1000 + rate;
    std::vector<float> buf((size_t)frames * 2, 0.0f);
    int rendered = player.renderToBuffer(buf.data(), rate);
    if (!writeWav(out, buf.data(), rendered, rate)) {
        std::fprintf(stderr, "mmp: failed to write %s\n", out.c_str()); return 1;
    }
    std::printf("Rendered %d frames to %s\n", rendered, out.c_str());
    return 0;
}

int cmdDump(int argc, char** argv) {
    if (argc < 2) { printUsage(stderr); return 2; }
    std::string sf = argv[1];
    mmp::Sf2DumpOptions opts;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) opts.jsonOutput = true;
        else { std::fprintf(stderr, "mmp: unknown option %s\n", argv[i]); return 2; }
    }
    if (!mmp::dumpSoundFont(sf, opts, std::cout)) {
        std::fprintf(stderr, "mmp: failed to read %s\n", sf.c_str()); return 1;
    }
    return 0;
}

int cmdConvert(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "mmp convert <in.sf2|in.sfo> <out.sfo|out.sf2> [--quality 0..1]\n");
        return 2;
    }
    std::string in = argv[1];
    std::string out = argv[2];
    float quality = 0.4f;
    for (int i = 3; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--quality") == 0 || std::strcmp(argv[i], "-q") == 0)
            && i + 1 < argc) {
            quality = (float)std::atof(argv[++i]);
        } else {
            std::fprintf(stderr, "mmp convert: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    auto endsWith = [](const std::string& s, const char* suf) {
        size_t n = std::strlen(suf);
        return s.size() >= n &&
               std::equal(s.end() - n, s.end(), suf,
                          [](char a, char b){ return std::tolower((unsigned char)a) == b; });
    };
    if (endsWith(out, ".sfo")) {
        mmp::Sf3CompressOptions opts; opts.quality = quality;
        mmp::Sf3CompressStats st;
        if (!mmp::compressSf2ToSf3(in, out, opts, st)) {
            std::fprintf(stderr, "mmp convert: %s\n", st.message.c_str());
            return 1;
        }
        std::printf("compress: %s\n", st.message.c_str());
        return 0;
    }
    if (endsWith(out, ".sf2")) {
        mmp::Sf3DecompressStats st;
        if (!mmp::decompressSf3ToSf2(in, out, st)) {
            std::fprintf(stderr, "mmp convert: %s\n", st.message.c_str());
            return 1;
        }
        std::printf("decompress: %s\n", st.message.c_str());
        return 0;
    }
    std::fprintf(stderr,
        "mmp convert: output extension must be .sfo (compress) or .sf2 (decompress)\n");
    return 2;
}

int cmdProbe(int /*argc*/, char** /*argv*/) {
    std::printf("mmp probe: audio + MIDI device enumeration not yet implemented.\n");
    std::printf("Default audio output (CoreAudio) is used by `mmp play`.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(stdout); return 0; }
    std::string sub = argv[1];
    if (sub == "-h" || sub == "--help")    { printUsage(stdout); return 0; }
    if (sub == "-v" || sub == "--version") {
        std::printf("mmp %s (build %d)\n", BUILD_VERSION, (int)BUILD_NUMBER);
        return 0;
    }
    // Shift argv so subcommand handlers see argv[0]=subname, argv[1]=first arg.
    int subArgc = argc - 1;
    char** subArgv = argv + 1;
    if (sub == "play")    return cmdPlay   (subArgc, subArgv);
    if (sub == "render")  return cmdRender (subArgc, subArgv);
    if (sub == "dump")    return cmdDump   (subArgc, subArgv);
    if (sub == "convert") return cmdConvert(subArgc, subArgv);
    if (sub == "probe")   return cmdProbe  (subArgc, subArgv);

    std::fprintf(stderr, "mmp: unknown subcommand '%s'\n", sub.c_str());
    printUsage(stderr);
    return 2;
}
