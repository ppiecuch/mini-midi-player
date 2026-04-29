# deps/

Vendored third-party sources. Each subdirectory is a snapshot of an upstream
project at a pinned tag, optionally with custom patches from
`deps/patches/<libname>/*.patch` applied on top.

## Layout

    _scripts/lib-update.sh    dispatch — refreshes every lib (or a subset)
    deps/
    ├── _lib-update.sh         shared helper sourced by every per-lib update.sh
    ├── patches/
    │   ├── tinysoundfont/*.patch
    │   ├── tinymidiloader/*.patch
    │   ├── kissfft/*.patch
    │   ├── fontaudio/*.patch
    │   └── opensymbols/*.patch
    ├── tinysoundfont/         primary synth engine (MIT, header-only)
    │   ├── update.sh
    │   └── src/               cloned upstream contents (wiped + refilled)
    ├── tinymidiloader/        SMF parser (MIT, header-only)
    │   ├── update.sh
    │   └── src/
    ├── kissfft/               FFT for spectrum analyzer (BSD-3)
    │   ├── update.sh
    │   └── src/
    ├── fontaudio/             icon SVGs for transport / FX (MIT)
    │   ├── update.sh
    │   └── src/svgs/
    └── opensymbols/           Deepin OpenSymbol musical glyphs (GPL/LGPL)
        ├── update.sh
        └── src/svg/

The shared helper sourced by every per-lib `update.sh` lives outside this
tree at `_scripts/lib-update.sh` so it isn't deleted by its own callers.

## Refreshing

    ./_scripts/lib-update.sh                       # all libs
    ./_scripts/lib-update.sh tinysoundfont kissfft # selected libs

Each per-lib `update.sh` clones the upstream repo, checks out the pinned
tag, drops the `.git` directory so the tree is a clean snapshot, and then
applies every `*.patch` from `deps/patches/<libname>/` in lexical order.

## Adding a custom patch

1.  Make the change inside `deps/<libname>/`.
2.  Run a regular `diff -u` against a clean checkout (or `git diff` if you
    re-init a temporary git repo inside the dir).
3.  Save the diff as `deps/patches/<libname>/NN-short-name.patch`.
4.  Re-run `./_scripts/lib-update.sh <libname>` and confirm the patch applies cleanly.

## Linking policy

Only **permissive-licensed** sources are linked into the shipping binary,
in line with the project's static-linking-only constraint:

  - **TinySoundFont** — MIT, primary synth engine
  - **TinyMidiLoader** — MIT, SMF parser
  - **KissFFT** — BSD-3, spectrum analyzer FFT
  - **fontaudio** SVGs — MIT, transport / record / digital-display icons

**OpenSymbol** is GPL-2.0 / LGPL-2.1+. Glyphs from this set are
available via the `mmp::svg::opensymbols::*` lookup table but should be
used carefully — distributing them with this app means the project must
keep the COPYING text in `Resources/Credits.rtf` and respect the
(L)GPL terms for those specific glyphs.
