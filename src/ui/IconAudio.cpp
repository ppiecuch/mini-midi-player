// Pull in the heavy SVG-data tables (gated in the generated headers so
// IconAudio.h itself stays lightweight for every other consumer).
#define MMP_INCLUDE_SVG_TABLES 1

#include "IconAudio.h"

#include <FL/Fl_SVG_Image.H>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace mmp {

namespace {

// Build a single name→SVG-bytes map across all vendored sets, lazily
// on first call. fontaudio is consulted first so its short "fad-*"
// names take precedence over any same-named opensymbols glyph; notomusic
// is registered last (try_emplace) so its single-word slugs ("eighth-note")
// are available without shadowing more specific opensymbol entries.
const std::unordered_map<std::string, const char*>& nameTable() {
    static auto* m = []() {
        auto* map = new std::unordered_map<std::string, const char*>;
        for (const auto& e : svg::fontaudio::kTable)   map->emplace(e.name, e.data);
        for (const auto& e : svg::opensymbols::kTable) map->try_emplace(e.name, e.data);
        for (const auto& e : svg::notomusic::kTable)   map->try_emplace(e.name, e.data);
        return map;
    }();
    return *m;
}

constexpr const char* kPlaceholderSvg =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<rect width='16' height='16' fill='none' stroke='#999'/></svg>";

} // namespace

Fl_SVG_Image* iconAudio(const char* name) {
    static std::unordered_map<std::string, Fl_SVG_Image*> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    const auto& tab = nameTable();
    auto found = tab.find(name);
    const char* data = (found != tab.end()) ? found->second : kPlaceholderSvg;
    auto* img = new Fl_SVG_Image(nullptr, data);
    cache.emplace(name, img);
    return img;
}

Fl_SVG_Image* iconDigit(int n) {
    if (n < 0 || n > 9) n = 0;
    char name[20];
    std::snprintf(name, sizeof(name), "fad-digital%d", n);
    return iconAudio(name);
}

} // namespace mmp
