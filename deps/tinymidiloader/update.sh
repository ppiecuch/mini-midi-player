#!/bin/bash
# Refresh deps/tinymidiloader/src/ from upstream.
# TML is bundled in the same repo as TinySoundFont — but we vendor it
# separately so the two can be pinned and patched independently.
# We keep only tml.h (and license/readme); everything else is dropped.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/../_lib-update.sh"

LIB_NAME=tinymidiloader
LIB_REPO=https://github.com/schellingb/TinySoundFont.git
LIB_TAG=HEAD
LIB_FETCH=git

refresh_lib

# Trim everything except the TML header + license/readme.
src="$SCRIPT_DIR/src"
find "$src" -mindepth 1 -maxdepth 1 \
    ! -name 'tml.h' \
    ! -name 'LICENSE*' \
    ! -name 'README*' \
    -exec rm -rf {} +
