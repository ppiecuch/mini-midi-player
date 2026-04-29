#!/bin/bash
# Refresh deps/fontaudio/src/ — vendors the full upstream svgs/ tree
# (plus the rendered TTF and LICENSE for parity / fallback).
#
# Fontaudio (https://github.com/fefanto/fontaudio) is an MIT-licensed
# icon set for audio UIs (~150 glyphs at PUA codepoints U+F101..F19x).
# We ship the SVGs so FLTK's Fl_SVG_Image can render them on every
# platform with no runtime font registration.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/src"
REPO="https://github.com/fefanto/fontaudio.git"

echo "Library: fontaudio"
echo "Repo:    $REPO"
echo "Target:  $TARGET"

rm -rf "$TARGET"
mkdir -p "$TARGET"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

git clone --depth 1 "$REPO" "$tmp/repo" >/dev/null 2>&1

# Keep only the bits we care about: SVGs, the TTF, license, readme.
[ -d "$tmp/repo/svgs" ] && cp -R "$tmp/repo/svgs" "$TARGET/svgs"
[ -f "$tmp/repo/font/fontaudio.ttf" ] && cp "$tmp/repo/font/fontaudio.ttf" "$TARGET/fontaudio.ttf"
[ -f "$tmp/repo/LICENSE" ]   && cp "$tmp/repo/LICENSE"   "$TARGET/LICENSE"
[ -f "$tmp/repo/README.md" ] && cp "$tmp/repo/README.md" "$TARGET/README.md"

svg_count=$(/bin/ls "$TARGET/svgs" 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
ttf_size=$([ -f "$TARGET/fontaudio.ttf" ] && /usr/bin/wc -c < "$TARGET/fontaudio.ttf" | /usr/bin/tr -d ' ' || echo 0)
echo "  SVGs:  $svg_count"
echo "  TTF:   $ttf_size bytes"

patches_dir="$SCRIPT_DIR/../patches/fontaudio"
if [ -d "$patches_dir" ]; then
    for p in "$patches_dir"/*.patch; do
        [ -e "$p" ] || continue
        echo "Applying $(basename "$p")"
        (cd "$TARGET" && patch -p1 --no-backup-if-mismatch < "$p")
    done
fi
