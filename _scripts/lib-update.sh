#!/bin/bash
#
# _scripts/lib-update.sh — Refresh every vendored dependency to its
# pinned version and re-apply any custom patches found under
# deps/patches/.
#
# Usage:
#   ./_scripts/lib-update.sh                       # refresh all
#   ./_scripts/lib-update.sh tinysoundfont kissfft # refresh selected libs
#
# Per-library scripts live at deps/<name>/update.sh and own the actual
# fetch/checkout logic so each lib can pin its own tag and apply its
# own patches independently. This top-level script just dispatches.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPS_DIR="$PROJECT_DIR/deps"

ALL_LIBS=(tinysoundfont tinymidiloader kissfft fontaudio opensymbols libogg libvorbis)
SELECTED=("$@")
if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${ALL_LIBS[@]}")
fi

for lib in "${SELECTED[@]}"; do
    script="$DEPS_DIR/$lib/update.sh"
    if [ ! -x "$script" ]; then
        echo "ERROR: no update script for '$lib' at $script"
        exit 1
    fi
    echo ""
    echo "=== Refreshing $lib ==="
    "$script"
done

echo ""
echo "=== All deps refreshed ==="
