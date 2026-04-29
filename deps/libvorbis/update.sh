#!/bin/bash
# Refresh deps/libvorbis/src/ — Xiph libvorbis snapshot (BSD-3-Clause).
# Provides Vorbis encode (vorbisenc) and decode (vorbisfile / synthesis)
# used by the SF2 ↔ SF3 converter (src/sf2/Sf3Codec.cpp). Built directly
# from the upstream .c files via a custom CMake fragment so we don't
# inherit autotools / pkg-config plumbing.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/../_lib-update.sh"

LIB_NAME=libvorbis
LIB_REPO=https://github.com/xiph/vorbis.git
LIB_TAG=v1.3.7
LIB_FETCH=git

refresh_lib
