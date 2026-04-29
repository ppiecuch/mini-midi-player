#!/bin/bash
# Refresh deps/opensymbols/src/ — vendors the full upstream svg/ tree
# from the Deepin OpenSymbol fonts repo.
#
# OpenSymbol (https://github.com/leaeasy/deepin-opensymbol-fonts) is the
# OpenOffice / LibreOffice symbol font, GPL-2.0 / LGPL-2.1+. The repo
# stores per-variant glyph SVGs under svg/DeepinOpenSymbol*/. We mirror
# the lot here so we can pick out individual glyphs (musical notation,
# arrows, technical symbols, …) at our leisure with FLTK's Fl_SVG_Image.
#
# License caveat: GPL/LGPL applies to the glyphs. If/when we link these
# into the shipping binary we must keep the COPYING text in
# Resources/Credits.rtf and respect the (L)GPL terms — adding the
# vendored tree to deps/ doesn't trigger anything by itself.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET="$SCRIPT_DIR/src"
REPO="https://github.com/leaeasy/deepin-opensymbol-fonts.git"

echo "Library: opensymbols"
echo "Repo:    $REPO"
echo "Target:  $TARGET"

rm -rf "$TARGET"
mkdir -p "$TARGET"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

git clone --depth 1 "$REPO" "$tmp/repo" >/dev/null 2>&1

# Mirror the entire svg/ tree (its sub-dirs name the source variant).
[ -d "$tmp/repo/svg" ] && cp -R "$tmp/repo/svg" "$TARGET/svg"
[ -f "$tmp/repo/COPYING" ]   && cp "$tmp/repo/COPYING"   "$TARGET/COPYING"
[ -f "$tmp/repo/README.md" ] && cp "$tmp/repo/README.md" "$TARGET/README.md"

# Headline counts — each subdir is a variant (DeepinOpenSymbol, …).
if [ -d "$TARGET/svg" ]; then
    for d in "$TARGET/svg"/*/; do
        [ -d "$d" ] || continue
        cnt=$(/bin/ls "$d" 2>/dev/null | /usr/bin/wc -l | /usr/bin/tr -d ' ')
        echo "  $(basename "$d"): $cnt SVGs"
    done
fi

patches_dir="$SCRIPT_DIR/../patches/opensymbols"
if [ -d "$patches_dir" ]; then
    for p in "$patches_dir"/*.patch; do
        [ -e "$p" ] || continue
        echo "Applying $(basename "$p")"
        (cd "$TARGET" && patch -p1 --no-backup-if-mismatch < "$p")
    done
fi

# Rewrite font-glyph viewBoxes so the visible shape fills the bitmap.
# Upstream SVGs all carry viewBox="0 -410 2048 2048" + a vertical-flip
# matrix transform — meaning the glyph occupies only the lower ~33% of
# the raster and shrinks to a few pixels at icon sizes. Idempotent.
tighten="$SCRIPT_DIR/../../_scripts/tighten_svgs.py"
if [ -f "$tighten" ] && [ -d "$TARGET/svg" ]; then
    echo "Tightening viewBoxes…"
    python3 "$tighten" "$TARGET/svg"
fi
