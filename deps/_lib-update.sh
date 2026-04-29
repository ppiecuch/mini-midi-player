#!/bin/bash
#
# deps/_lib-update.sh — Shared helper sourced by every per-library update.sh
#
# Each per-lib script sets:
#   LIB_NAME=tinysoundfont
#   LIB_REPO=https://github.com/schellingb/TinySoundFont.git
#   LIB_TAG=HEAD                      # tag, branch, commit SHA, or HEAD
#   LIB_FETCH=git                     # git | tarball
#   LIB_TARBALL_URL=...               # required if LIB_FETCH=tarball
#   LIB_TARBALL_STRIP=1               # tar --strip-components level
# then calls:
#   refresh_lib
#
# Layout convention:
#   deps/<libname>/
#     ├── update.sh                   per-lib refresh script (kept)
#     └── src/                        cloned upstream contents (wiped & refilled)
#
# Keeping update.sh out of the wiped subtree means re-running this helper
# never deletes the script that's running.
#

set -euo pipefail

DEPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

refresh_lib() {
    local lib_dir="$DEPS_DIR/$LIB_NAME"
    local target="$lib_dir/src"
    local patches_dir="$DEPS_DIR/patches/$LIB_NAME"

    echo "Library: $LIB_NAME"
    echo "Pin:     $LIB_TAG"
    echo "Target:  $target"

    rm -rf "$target"
    mkdir -p "$target"

    case "${LIB_FETCH:-git}" in
        git)
            echo "--- Cloning $LIB_REPO @ $LIB_TAG ---"
            if [ "$LIB_TAG" = "HEAD" ] || [ -z "$LIB_TAG" ]; then
                # Track upstream default branch (used for libs without tagged releases)
                git clone --depth 1 "$LIB_REPO" "$target"
            else
                # Try shallow clone of the named tag/branch first; fall back to a
                # full clone + checkout so raw commit SHAs also work.
                git clone --depth 1 --branch "$LIB_TAG" "$LIB_REPO" "$target" 2>/dev/null \
                    || {
                        rm -rf "$target"
                        git clone "$LIB_REPO" "$target"
                        (cd "$target" && git checkout "$LIB_TAG")
                    }
            fi
            # Drop the upstream .git dir so the vendored tree is a clean snapshot.
            rm -rf "$target/.git"
            ;;
        tarball)
            echo "--- Fetching tarball $LIB_TARBALL_URL ---"
            curl -fsSL "$LIB_TARBALL_URL" \
                | tar -xz -C "$target" --strip-components="${LIB_TARBALL_STRIP:-1}"
            ;;
        *)
            echo "ERROR: unknown LIB_FETCH=$LIB_FETCH"
            return 1
            ;;
    esac

    # Apply patches in lexical order (00-foo.patch, 01-bar.patch, ...)
    if [ -d "$patches_dir" ]; then
        local applied=0
        for patch in "$patches_dir"/*.patch; do
            [ -e "$patch" ] || continue
            echo "--- Applying $(basename "$patch") ---"
            (cd "$target" && patch -p1 --no-backup-if-mismatch < "$patch")
            applied=$((applied + 1))
        done
        if [ "$applied" -gt 0 ]; then
            echo "Applied $applied patch(es)."
        else
            echo "No patches in $patches_dir."
        fi
    fi

    echo "Done: $LIB_NAME (src/ refreshed)"
}
