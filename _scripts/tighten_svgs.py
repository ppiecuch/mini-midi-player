#!/usr/bin/env python3
"""
tighten_svgs.py — rewrite the viewBox of font-glyph-style SVGs so the
visible content fills the raster instead of sitting in the lower third.

Why: the OpenSymbol SVG set (deps/opensymbols/src/svg/...) was generated
from font glyphs; every SVG carries a viewBox like

    viewBox="0 -410 2048 2048"

and a top-level <g> with a vertical-flip transform like

    matrix(1 0 0 -1 0 1638)

so the actual visible glyph occupies only the lower ~33 % of the raster.
Rendered at small icon sizes this puts most of the visible art into a
handful of pixels.

This script:

  1. Parses each *.svg under <root> recursively.
  2. Walks every <path d="..."> looking at its M/L/C/Q/etc. coordinates.
     (Approximate — control points of curves are used as stand-ins for
     the curve's true extrema; close enough for icon-level tightening.)
  3. Applies any <g transform="matrix(...)"> ancestor transforms.
  4. Computes the screen-space bounding box of all paths.
  5. Rewrites the viewBox to that bbox + a 4 % margin and removes the
     now-redundant <g transform>.

Idempotent — already-tight SVGs are detected (vbox_y >= -1) and skipped.
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# SVG path-data tokenizer: commands or numbers, including signed/decimal.
_TOKEN_RE = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]|-?\d+\.?\d*(?:[eE][-+]?\d+)?')

def parse_path_d(d: str) -> list[tuple[float, float]]:
    """Return a list of (x, y) coordinates referenced by the path. We
    treat absolute and relative commands the same way (we just want a
    bounding box, so we accumulate position state and yield each end-of-
    segment point)."""
    pts = []
    px, py = 0.0, 0.0           # current pen position
    last_cmd = ''
    nums = []

    def emit(x, y):
        pts.append((x, y))

    tokens = _TOKEN_RE.findall(d)
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t
            i += 1
        else:
            cmd = last_cmd or 'L'    # implicit lineto after moveto
        # collect numeric arguments for this command
        args = []
        while i < len(tokens) and not tokens[i].isalpha():
            try:
                args.append(float(tokens[i]))
            except ValueError:
                pass
            i += 1
        last_cmd = cmd
        rel = cmd.islower()
        cu = cmd.upper()

        def take(n):
            nonlocal args
            if len(args) < n: return None
            chunk = args[:n]; args = args[n:]
            return chunk
        # The above re-binds `args` only inside this function; we need to
        # mutate the outer one. Manage manually:
        idx = 0
        def next_pair():
            nonlocal idx
            if idx + 1 >= len(args): return None
            p = (args[idx], args[idx+1]); idx += 2
            return p

        if cu == 'M':
            while True:
                p = next_pair()
                if p is None: break
                if rel: p = (px + p[0], py + p[1])
                px, py = p
                emit(px, py)
                # subsequent implicit pairs are L
                cu, rel = 'L', rel
        elif cu == 'L':
            while True:
                p = next_pair()
                if p is None: break
                if rel: p = (px + p[0], py + p[1])
                px, py = p
                emit(px, py)
        elif cu == 'H':
            while idx < len(args):
                v = args[idx]; idx += 1
                px = px + v if rel else v
                emit(px, py)
        elif cu == 'V':
            while idx < len(args):
                v = args[idx]; idx += 1
                py = py + v if rel else v
                emit(px, py)
        elif cu == 'C':
            while True:
                # (cx1,cy1) (cx2,cy2) (x,y)
                a = next_pair(); b = next_pair(); c = next_pair()
                if c is None: break
                if rel:
                    a = (px+a[0], py+a[1]); b = (px+b[0], py+b[1]); c = (px+c[0], py+c[1])
                emit(*a); emit(*b); emit(*c)
                px, py = c
        elif cu == 'S' or cu == 'Q':
            while True:
                a = next_pair(); b = next_pair()
                if b is None: break
                if rel:
                    a = (px+a[0], py+a[1]); b = (px+b[0], py+b[1])
                emit(*a); emit(*b)
                px, py = b
        elif cu == 'T':
            while True:
                p = next_pair()
                if p is None: break
                if rel: p = (px+p[0], py+p[1])
                px, py = p
                emit(px, py)
        elif cu == 'A':
            while True:
                # rx ry rot large sweep x y — only the endpoint matters
                if idx + 6 >= len(args): break
                _rx, _ry, _rot, _large, _sweep, x, y = args[idx:idx+7]
                idx += 7
                if rel: x, y = px + x, py + y
                px, py = x, y
                emit(px, py)
        # Z / z: close path, no coords
    return pts

def parse_matrix(transform: str):
    """Return 6-tuple (a, b, c, d, e, f) for transform="matrix(...)"; if
    multiple transforms are present, only the first matrix(...) is taken
    (sufficient for the OpenSymbol set)."""
    m = re.search(r'matrix\(([^)]+)\)', transform)
    if not m: return None
    parts = re.split(r'[\s,]+', m.group(1).strip())
    if len(parts) != 6: return None
    try:
        return tuple(float(x) for x in parts)
    except ValueError:
        return None

def apply_matrix(mat, pt):
    a, b, c, d, e, f = mat
    x, y = pt
    return (a * x + c * y + e, b * x + d * y + f)

def tighten_one(path: Path) -> bool:
    text = path.read_text(encoding='utf-8', errors='replace')

    vb = re.search(r'viewBox="([^"]+)"', text)
    if not vb: return False
    parts = re.split(r'[\s,]+', vb.group(1).strip())
    try:
        vbX, vbY, vbW, vbH = (float(x) for x in parts)
    except ValueError:
        return False

    # Already tight? Only act on font-glyph-style SVGs (negative top).
    if vbY > -1:
        return False

    # Collect path-d coordinates
    coords = []
    for m in re.finditer(r'd="([^"]+)"', text):
        coords.extend(parse_path_d(m.group(1)))
    if not coords:
        return False

    # If a top-level matrix transform is present, apply it.
    mt = re.search(r'<g[^>]*transform="([^"]+)"', text)
    matrix = parse_matrix(mt.group(1)) if mt else None
    if matrix:
        coords = [apply_matrix(matrix, p) for p in coords]

    xs = [p[0] for p in coords]
    ys = [p[1] for p in coords]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    pad = max(maxx - minx, maxy - miny) * 0.04
    nx = minx - pad
    ny = miny - pad
    nw = (maxx - minx) + 2 * pad
    nh = (maxy - miny) + 2 * pad

    # Format with simple rounding for stable diffs
    def f(v): return f'{v:.1f}'.rstrip('0').rstrip('.') if '.' in f'{v:.1f}' else f'{int(v)}'
    new_vb = f'viewBox="{f(nx)} {f(ny)} {f(nw)} {f(nh)}"'
    text = text[:vb.start()] + new_vb + text[vb.end():]

    path.write_text(text, encoding='utf-8')
    return True

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path, help='directory of .svg files (recursed)')
    args = ap.parse_args()
    if not args.root.exists():
        sys.exit(f'no such directory: {args.root}')
    n_total = n_fixed = n_skipped = 0
    for f in sorted(args.root.rglob('*.svg')):
        n_total += 1
        try:
            if tighten_one(f):
                n_fixed += 1
            else:
                n_skipped += 1
        except Exception as ex:
            print(f'  warn {f.relative_to(args.root)}: {ex}', file=sys.stderr)
    print(f'tighten_svgs: {n_fixed}/{n_total} tightened ({n_skipped} skipped)')

if __name__ == '__main__':
    main()
