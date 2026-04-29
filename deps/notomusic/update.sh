#!/bin/bash
# Refresh deps/notomusic/src/ — vendor the upstream Noto Music font and
# extract per-glyph SVGs.
#
# Noto Music (https://github.com/notofonts/music) is Google's
# SIL OFL 1.1-licensed companion to the Noto family for the entire
# Unicode "Musical Symbols" block (U+1D100..U+1D1FF) plus the
# Miscellaneous-Symbols music glyphs (U+2669..U+266F).
#
# We don't ship the .ttf at runtime — instead, _scripts/extract_music_svgs.py
# walks the font's cmap and writes one SVG per music codepoint into
# src/svg/. That set drops straight into the project's existing
# embed_svgs() pipeline alongside fontaudio / opensymbols.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/src"

# Rendered fonts (incl. NotoMusic-Regular.ttf) live in the
# notofonts/notofonts.github.io repo. The build-from-source repo
# (notofonts/music.git) only carries glyph sources + scripts, and
# cloning the rendered repo is huge — so curl the file directly.
TTF_URL="https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoMusic/unhinted/ttf/NotoMusic-Regular.ttf"
OFL_URL="https://raw.githubusercontent.com/notofonts/music/main/OFL.txt"
README_URL="https://raw.githubusercontent.com/notofonts/music/main/README.md"

echo "Library: notomusic"
echo "TTF:     $TTF_URL"
echo "Target:  $TARGET"

rm -rf "$TARGET"
mkdir -p "$TARGET/svg"

# Source TTF kept on disk for reference / debugging only — the binary
# embeds only the extracted SVGs.
curl -fsSL "$TTF_URL"    -o "$TARGET/NotoMusic-Regular.ttf"
curl -fsSL "$OFL_URL"    -o "$TARGET/OFL.txt"   || true
curl -fsSL "$README_URL" -o "$TARGET/README.md" || true

# Extract per-glyph SVGs.
extractor="$SCRIPT_DIR/../../_scripts/extract_music_svgs.py"
if [ ! -f "$extractor" ]; then
    echo "  ERROR: extract_music_svgs.py not found at $extractor" >&2
    exit 1
fi
python3 "$extractor" "$TARGET/NotoMusic-Regular.ttf" "$TARGET/svg"

svg_count=$(/bin/ls "$TARGET/svg" 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
ttf_size=$(/usr/bin/wc -c < "$TARGET/NotoMusic-Regular.ttf" | /usr/bin/tr -d ' ')
echo "  SVGs:  $svg_count"
echo "  TTF:   $ttf_size bytes (kept for reference, not shipped)"

patches_dir="$SCRIPT_DIR/../patches/notomusic"
if [ -d "$patches_dir" ]; then
    for p in "$patches_dir"/*.patch; do
        [ -e "$p" ] || continue
        echo "Applying $(basename "$p")"
        (cd "$TARGET" && patch -p1 --no-backup-if-mismatch < "$p")
    done
fi
