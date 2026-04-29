#!/usr/bin/env python3
"""
extract_music_svgs.py — dump per-glyph SVGs from a music font.

Built for Noto Music: walks the font's `cmap`, picks the codepoints we
care about (the BMP "Miscellaneous Symbols" music block + the SMP
"Musical Symbols" block), draws each glyph through `SVGPathPen`, and
writes one tight-viewBox SVG per glyph into <out>/<slug>.svg.

The slug is the Unicode name lower-cased and hyphenated:

    U+2669 MUSIC NATURAL → music-natural.svg
    U+1D11E MUSICAL SYMBOL G CLEF → musical-symbol-g-clef.svg

Output is shaped to drop straight into the project's SVG embedding
pipeline (cmake/embed_svgs.cmake): every file references `currentColor`
for its fill, has a snug viewBox, and uses no font-glyph y-flip.

Usage:
    extract_music_svgs.py NotoMusic-Regular.ttf  out_dir/
"""

import argparse
import re
import sys
import unicodedata
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen  import BoundsPen


# Codepoint ranges to extract. Inclusive on both ends.
RANGES = [
    (0x2669, 0x266F),   # ♩ ♪ ♫ ♬ ♭ ♮ ♯ — Miscellaneous Symbols (music)
    (0x1D100, 0x1D1FF), # 𝄀 .. 𝇇 — Musical Symbols
]


def slugify(name: str) -> str:
    """Make a filesystem-safe lowercase slug from a Unicode name."""
    s = name.lower()
    s = re.sub(r'[^a-z0-9]+', '-', s)
    return s.strip('-') or 'glyph'


def extract(ttf_path: Path, out_dir: Path) -> int:
    font = TTFont(str(ttf_path))
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    upm = font['head'].unitsPerEm

    # Resolve every (codepoint → glyph-name) inside the chosen ranges.
    targets: list[tuple[int, str]] = []
    for cp, gname in cmap.items():
        if any(lo <= cp <= hi for lo, hi in RANGES):
            targets.append((cp, gname))
    targets.sort()

    out_dir.mkdir(parents=True, exist_ok=True)
    written = 0
    for cp, gname in targets:
        try:
            uname = unicodedata.name(chr(cp))
        except ValueError:
            uname = f'u{cp:04x}'
        slug = slugify(uname)

        # Drive the glyph through two pens: one collecting bounds,
        # one collecting the SVG path commands.
        bounds = BoundsPen(glyphs)
        glyphs[gname].draw(bounds)
        if bounds.bounds is None:
            continue          # blank/zero-area glyph
        xMin, yMin, xMax, yMax = bounds.bounds

        path_pen = SVGPathPen(glyphs)
        glyphs[gname].draw(path_pen)
        path_d = path_pen.getCommands()

        # Pad the bbox by ~4 % so descenders / serifs don't kiss the
        # raster edge once Fl_SVG_Image scales the file down to 14 px.
        margin = max(xMax - xMin, yMax - yMin) * 0.04
        vx = xMin - margin
        vy = -yMax - margin           # font Y is up; SVG Y is down → flip
        vw = (xMax - xMin) + 2 * margin
        vh = (yMax - yMin) + 2 * margin

        svg = (
            '<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="{vx:.1f} {vy:.1f} {vw:.1f} {vh:.1f}">'
            '<g transform="scale(1 -1)">'
            f'<path fill="currentColor" d="{path_d}"/>'
            '</g></svg>\n'
        )
        (out_dir / f'{slug}.svg').write_text(svg, encoding='utf-8')
        written += 1

    print(f'extract_music_svgs: {written}/{len(targets)} glyphs '
          f'(unitsPerEm={upm}) → {out_dir}')
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('ttf',     type=Path, help='input .ttf file')
    ap.add_argument('out_dir', type=Path, help='output directory for .svg files')
    args = ap.parse_args()
    if not args.ttf.exists():
        sys.exit(f'no such font: {args.ttf}')
    extract(args.ttf, args.out_dir)


if __name__ == '__main__':
    main()
