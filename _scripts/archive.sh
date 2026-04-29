#!/bin/bash
#
# archive.sh — Build, sign, notarize, and (optionally) DMG-package
# Mini MIDI Player for Developer ID distribution outside the Mac App Store.
#
# Usage:
#   ./_scripts/archive.sh                          # archive + export signed .app
#   ./_scripts/archive.sh +                        # bump build, then archive
#   ./_scripts/archive.sh + --notarize             # also submit to Apple notary
#   ./_scripts/archive.sh + --notarize --dmg       # also produce a .dmg
#
# One-time notarization setup:
#   xcrun notarytool store-credentials minimidiplayer-notary \
#       --apple-id piecuch.pawel@gmail.com \
#       --team-id NR5F9UD6RP \
#       --password APP_SPECIFIC_PASSWORD
#
# (Override the keychain profile name with NOTARY_PROFILE=…)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-xcode"
DIST_DIR="$PROJECT_DIR/dist"
SCHEME="MiniMidiPlayer"
APP_NAME="MiniMidiPlayer"
BUNDLE_ID="com.komsoft.minimidiplayer"
TEAM_ID="NR5F9UD6RP"
ENTITLEMENTS="$PROJECT_DIR/Resources/MiniMidiPlayer.entitlements"
FLTK_DIR="${FLTK_DIR:-/Volumes/FastUSB/Sources/fltk-dist/darwin}"
NOTARY_PROFILE="${NOTARY_PROFILE:-minimidiplayer-notary}"

# Parse build.info
VERSION=$(grep '^version=' "$PROJECT_DIR/build.info" | cut -d= -f2)
BUILD_NUM=$(grep '^build=' "$PROJECT_DIR/build.info" | cut -d= -f2)

# Args
DO_NOTARIZE=false
DO_DMG=false
BUMP_BUILD=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        +)          BUMP_BUILD=true;  shift ;;
        --notarize) DO_NOTARIZE=true; shift ;;
        --dmg)      DO_DMG=true;      shift ;;
        -h|--help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [+] [--notarize] [--dmg]"
            exit 1 ;;
    esac
done

if [ "$BUMP_BUILD" = true ]; then
    BUILD_NUM=$((BUILD_NUM + 1))
    sed -i '' "s/^build=.*/build=$BUILD_NUM/" "$PROJECT_DIR/build.info"
    echo "Build number incremented to $BUILD_NUM"
fi

echo "=== Mini MIDI Player Developer ID Builder ==="
echo "Version:   $VERSION (build $BUILD_NUM)"
echo "Bundle ID: $BUNDLE_ID"
echo "Team ID:   $TEAM_ID"
echo ""

if [ ! -d "$FLTK_DIR" ]; then
    echo "ERROR: FLTK not found at $FLTK_DIR"
    echo "Mount /Volumes/FastUSB or set FLTK_DIR=…"
    exit 1
fi

# --- Find Developer ID Application identity ---
echo "--- Checking signing identity ---"
IDENTITY=$(security find-identity -v -p codesigning \
    | grep "Developer ID Application" | head -1 \
    | sed -E 's/.*"([^"]+)".*/\1/' || true)
if [ -z "$IDENTITY" ]; then
    echo "ERROR: No 'Developer ID Application' certificate found in Keychain."
    echo ""
    echo "Available identities:"
    security find-identity -v -p codesigning
    echo ""
    echo "Create one of type 'Developer ID Application' at:"
    echo "  https://developer.apple.com/account/resources/certificates"
    exit 1
fi
echo "Using: $IDENTITY"
echo ""

# Strip quarantine flags so codesign doesn't complain about source files
echo "--- Stripping quarantine attributes ---"
xattr -dr com.apple.quarantine "$PROJECT_DIR" 2>/dev/null || true
echo ""

# --- Generate Xcode project from CMake ---
echo "--- Generating Xcode project ---"
rm -rf "$BUILD_DIR"
cmake -G Xcode -B "$BUILD_DIR" -S "$PROJECT_DIR" \
    -DFLTK_DIR="$FLTK_DIR" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64
echo ""

# --- Archive ---
mkdir -p "$DIST_DIR"
ARCHIVE_PATH="$DIST_DIR/${APP_NAME}-${VERSION}-${BUILD_NUM}.xcarchive"
rm -rf "$ARCHIVE_PATH"

echo "--- Building archive ---"
xcodebuild \
    -project "$BUILD_DIR/${APP_NAME}.xcodeproj" \
    -scheme "$SCHEME" \
    -configuration Release \
    -archivePath "$ARCHIVE_PATH" \
    CODE_SIGN_IDENTITY="$IDENTITY" \
    CODE_SIGN_STYLE="Manual" \
    CODE_SIGN_ENTITLEMENTS="$ENTITLEMENTS" \
    DEVELOPMENT_TEAM="$TEAM_ID" \
    ENABLE_HARDENED_RUNTIME=YES \
    OTHER_CODE_SIGN_FLAGS="--timestamp --options runtime" \
    archive 2>&1 | tail -8
echo ""

# Generate dSYM if Xcode didn't
DSYM_DIR="$ARCHIVE_PATH/dSYMs"
APP_BINARY="$ARCHIVE_PATH/Products/Applications/${APP_NAME}.app/Contents/MacOS/${APP_NAME}"
if [ -z "$(ls -A "$DSYM_DIR" 2>/dev/null)" ] && [ -f "$APP_BINARY" ]; then
    echo "--- Generating dSYM ---"
    dsymutil "$APP_BINARY" -o "$DSYM_DIR/${APP_NAME}.app.dSYM"
fi

CLI_BINARY="$ARCHIVE_PATH/Products/Applications/${APP_NAME}.app/Contents/MacOS/mmp"
if [ ! -f "$CLI_BINARY" ]; then
    echo "ERROR: embedded CLI not found at $CLI_BINARY"
    echo "Expected the CMake POST_BUILD step to copy mmp into the bundle."
    exit 1
fi

# --- Verify static-linking invariant (covers both bundle binaries) ---
# The project's hard rule is "no third-party dylibs". A regression here would
# silently ship a binary that breaks on machines without the dev's Homebrew
# tree, so fail the archive if `otool -L` lists anything outside the
# system-allowed prefixes — for either the GUI app or the embedded CLI.
echo "--- Verifying static linking (no third-party dylibs) ---"
ALLOWED='^(/usr/lib/|/System/Library/Frameworks/|@rpath/|@executable_path/)'
for bin in "$APP_BINARY" "$CLI_BINARY"; do
    BAD=$(otool -L "$bin" | tail -n +2 | awk '{print $1}' | grep -vE "$ALLOWED" || true)
    if [ -n "$BAD" ]; then
        echo "ERROR: $bin depends on non-system dylibs (static-linking rule violated):"
        echo "$BAD" | sed 's/^/  /'
        exit 1
    fi
done
echo "OK — only system dylibs referenced (GUI + CLI)."
echo ""

# --- Export signed .app ---
EXPORT_DIR="$DIST_DIR/${APP_NAME}-${VERSION}-${BUILD_NUM}"
EXPORT_PLIST="$SCRIPT_DIR/ExportOptions-DeveloperID.plist"
if [ ! -f "$EXPORT_PLIST" ]; then
    cat > "$EXPORT_PLIST" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>signingStyle</key>
    <string>manual</string>
    <key>teamID</key>
    <string>${TEAM_ID}</string>
    <key>signingCertificate</key>
    <string>Developer ID Application</string>
</dict>
</plist>
PLIST
fi

echo "--- Exporting signed .app ---"
rm -rf "$EXPORT_DIR"
xcodebuild -exportArchive \
    -archivePath "$ARCHIVE_PATH" \
    -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist "$EXPORT_PLIST" 2>&1 | tail -5
APP_DIST="$EXPORT_DIR/${APP_NAME}.app"
if [ ! -d "$APP_DIST" ]; then
    echo "ERROR: export did not produce $APP_DIST"
    exit 1
fi
echo "App: $APP_DIST"
echo ""

# --- Sign the embedded CLI binary ---
# xcodebuild's archive signing only seals the outer .app bundle; nested
# Mach-O executables under Contents/MacOS/ end up unsigned, which fails
# notarization. Sign mmp explicitly with hardened runtime + timestamp,
# then re-sign the outer bundle so its seal references the signed inner.
CLI_DIST="$APP_DIST/Contents/MacOS/mmp"
if [ ! -f "$CLI_DIST" ]; then
    echo "ERROR: embedded CLI missing from exported app at $CLI_DIST"
    exit 1
fi
echo "--- Signing embedded CLI (mmp) ---"
codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" \
    --identifier "${BUNDLE_ID}.mmp" \
    "$CLI_DIST"
echo "--- Re-sealing outer bundle ---"
codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" \
    --entitlements "$ENTITLEMENTS" \
    "$APP_DIST"
echo ""

# Quick sanity: verify the signature and that hardened runtime is on
echo "--- Verifying signature ---"
codesign --verify --strict --verbose=2 "$CLI_DIST" 2>&1 | tail -3
codesign --verify --deep --strict --verbose=2 "$APP_DIST" 2>&1 | tail -5
codesign -d --entitlements - --xml "$APP_DIST" >/dev/null 2>&1 \
    && echo "Entitlements: ok"
codesign -d --verbose=2 "$APP_DIST" 2>&1 | grep -E "flags=|Identifier=|Authority=" | head -5
echo ""

# --- Notarize ---
if [ "$DO_NOTARIZE" = true ]; then
    NOTARIZE_ZIP="$DIST_DIR/${APP_NAME}-${VERSION}-${BUILD_NUM}.zip"
    echo "--- Submitting .app to Apple notary (profile: $NOTARY_PROFILE) ---"
    /usr/bin/ditto -c -k --keepParent "$APP_DIST" "$NOTARIZE_ZIP"
    if ! xcrun notarytool submit "$NOTARIZE_ZIP" \
            --keychain-profile "$NOTARY_PROFILE" \
            --wait; then
        echo ""
        echo "Notarization failed. To inspect the most recent submission:"
        echo "  xcrun notarytool history --keychain-profile $NOTARY_PROFILE"
        echo "  xcrun notarytool log <submission-id> --keychain-profile $NOTARY_PROFILE"
        exit 1
    fi
    echo ""
    echo "--- Stapling notarization ticket ---"
    xcrun stapler staple "$APP_DIST"
    xcrun stapler validate "$APP_DIST"
    rm -f "$NOTARIZE_ZIP"
    echo ""
fi

# --- Optional DMG ---
if [ "$DO_DMG" = true ]; then
    DMG_PATH="$DIST_DIR/${APP_NAME}-${VERSION}-${BUILD_NUM}.dmg"
    echo "--- Building DMG ---"
    rm -f "$DMG_PATH"
    hdiutil create \
        -volname "Mini MIDI Player ${VERSION}" \
        -srcfolder "$APP_DIST" \
        -ov -format UDZO \
        "$DMG_PATH" >/dev/null
    codesign --sign "$IDENTITY" --timestamp "$DMG_PATH"
    if [ "$DO_NOTARIZE" = true ]; then
        echo "--- Notarizing DMG ---"
        if ! xcrun notarytool submit "$DMG_PATH" \
                --keychain-profile "$NOTARY_PROFILE" \
                --wait; then
            echo "DMG notarization failed."
            exit 1
        fi
        xcrun stapler staple "$DMG_PATH"
        xcrun stapler validate "$DMG_PATH"
    fi
    echo "DMG: $DMG_PATH"
    echo ""
fi

# --- Cleanup ---
echo "--- Cleaning up Xcode build dir ---"
rm -rf "$BUILD_DIR"
echo ""

echo "=== Done ==="
echo "Archive: $ARCHIVE_PATH"
echo "App:     $APP_DIST"
[ "$DO_DMG" = true ] && echo "DMG:     $DIST_DIR/${APP_NAME}-${VERSION}-${BUILD_NUM}.dmg"
if [ "$DO_NOTARIZE" = false ]; then
    echo ""
    echo "(not notarized — Gatekeeper will warn end users; re-run with --notarize)"
fi
