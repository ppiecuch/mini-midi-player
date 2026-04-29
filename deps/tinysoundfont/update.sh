#!/bin/bash
# Refresh deps/tinysoundfont/src/ from upstream.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/../_lib-update.sh"

LIB_NAME=tinysoundfont
LIB_REPO=https://github.com/schellingb/TinySoundFont.git
# TinySoundFont doesn't tag releases — track upstream default branch.
LIB_TAG=HEAD
LIB_FETCH=git

refresh_lib
