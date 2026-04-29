#!/bin/bash
# Refresh deps/kissfft/src/ from upstream.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/../_lib-update.sh"

LIB_NAME=kissfft
LIB_REPO=https://github.com/mborgerding/kissfft.git
LIB_TAG=131.1.0
LIB_FETCH=git

refresh_lib
