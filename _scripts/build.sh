#!/bin/bash
#
# build.sh — Configure and build Mini MIDI Player
#
# Usage:
#   ./_scripts/build.sh       # Build
#   ./_scripts/build.sh +     # Bump build number, then build
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
BUILD_INFO="$PROJECT_DIR/build.info"
FLTK_DIR="${FLTK_DIR:-/Volumes/FastUSB/Sources/fltk-dist/darwin}"

# Parse arguments
BUMP_BUILD=false
if [[ "${1:-}" == "+" ]]; then
    BUMP_BUILD=true
fi

# Bump build number if requested
if [ "$BUMP_BUILD" = true ]; then
    BUILD_NUM=$(grep '^build=' "$BUILD_INFO" | cut -d= -f2)
    BUILD_NUM=$((BUILD_NUM + 1))
    sed -i '' "s/^build=.*/build=$BUILD_NUM/" "$BUILD_INFO"
    echo "Build number incremented to $BUILD_NUM"
    # Force reconfigure so CMake picks up new build number
    rm -f "$BUILD_DIR/CMakeCache.txt"
fi

VERSION=$(grep '^version=' "$BUILD_INFO" | cut -d= -f2)
BUILD_NUM=$(grep '^build=' "$BUILD_INFO" | cut -d= -f2)

echo "=== Mini MIDI Player Build ==="
echo "Version: $VERSION (build $BUILD_NUM)"
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"
echo "FLTK:    $FLTK_DIR"
echo ""

if [ ! -d "$FLTK_DIR" ]; then
    echo "ERROR: FLTK not found at $FLTK_DIR"
    echo "Mount /Volumes/FastUSB or set FLTK_DIR=…"
    exit 1
fi

# Configure
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "--- Configuring ---"
    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DFLTK_DIR="$FLTK_DIR"
    echo ""
fi

# Build
echo "--- Building ---"
cmake --build "$BUILD_DIR" -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "=== Build complete ==="
echo "App: $BUILD_DIR/MiniMidiPlayer.app"
echo "CLI: $BUILD_DIR/mmp"
