cmake_minimum_required(VERSION 3.20)
project(MiniMidiPlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static-linking-only invariant: ban accidental shared-library use.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# --- Version from build.info ---
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/build.info"
)
file(STRINGS "${CMAKE_SOURCE_DIR}/build.info" _build_info_lines)
foreach(_line IN LISTS _build_info_lines)
    if(_line MATCHES "^version=(.+)$")
        set(APP_VERSION "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^build=(.+)$")
        set(APP_BUILD_NUMBER "${CMAKE_MATCH_1}")
    endif()
endforeach()
set(APP_BUNDLE_ID "com.komsoft.minimidiplayer")
message(STATUS "Mini MIDI Player ${APP_VERSION} (build ${APP_BUILD_NUMBER})")

# --- FLTK (static, pre-built) ---
set(FLTK_DIR "/Volumes/FastUSB/Sources/fltk-dist/darwin"
    CACHE PATH "FLTK installation prefix")

add_library(fltk STATIC IMPORTED)
set_target_properties(fltk PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk.a"
    INTERFACE_INCLUDE_DIRECTORIES "${FLTK_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS
        "_LARGEFILE_SOURCE;_LARGEFILE64_SOURCE;_FILE_OFFSET_BITS=64;_THREAD_SAFE;_REENTRANT"
)
foreach(_lib fltk_images fltk_png fltk_jpeg fltk_z)
    add_library(${_lib} STATIC IMPORTED)
    set_target_properties(${_lib} PROPERTIES
        IMPORTED_LOCATION "${FLTK_DIR}/lib/lib${_lib}.a"
    )
endforeach()

# --- Vendored deps (header-only / single-TU; statically linked) ---
# Each upstream tree lives under deps/<libname>/src/ ; the per-lib update.sh
# wipes only that subdir, leaving the script itself intact.
# TinySoundFont (MIT, header-only) — primary synth engine
add_library(tsf INTERFACE)
target_include_directories(tsf INTERFACE
    "${CMAKE_SOURCE_DIR}/deps/tinysoundfont/src"
)

# TinyMidiLoader (MIT, header-only) — SMF parser
add_library(tml INTERFACE)
target_include_directories(tml INTERFACE
    "${CMAKE_SOURCE_DIR}/deps/tinymidiloader/src"
)

# KissFFT (BSD-3) — vendored as a tiny static lib; only the float build.
# 131.1.0 keeps kiss_fft.c and kiss_fftr.c at the src root (no tools/ split
# for the real-FFT helper as in older releases).
add_library(kissfft STATIC
    "${CMAKE_SOURCE_DIR}/deps/kissfft/src/kiss_fft.c"
    "${CMAKE_SOURCE_DIR}/deps/kissfft/src/kiss_fftr.c"
)
target_include_directories(kissfft PUBLIC
    "${CMAKE_SOURCE_DIR}/deps/kissfft/src"
)

# libogg + libvorbis (BSD-3) — static archive built straight from
# upstream sources for the SF2 ↔ SF3 converter.
include(${CMAKE_SOURCE_DIR}/cmake/libvorbis.cmake)

# Icon SVG embedding — every .svg under deps/<libname>/src/{svgs,svg}/ is
# compiled into the binary as a namespaced lookup table. FLTK's
# Fl_SVG_Image renders them on every platform with no runtime font
# registration. See cmake/embed_svgs.cmake for the generator.
function(_mmp_embed_svgs lib_name svg_dir namespace_name)
    set(header "${CMAKE_BINARY_DIR}/generated/${lib_name}_svgs.h")
    file(GLOB_RECURSE svg_files "${svg_dir}/*.svg")
    add_custom_command(
        OUTPUT  "${header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
        COMMAND ${CMAKE_COMMAND}
            -DSVG_DIR=${svg_dir}
            -DNAMESPACE=${namespace_name}
            -DHEADER_OUTPUT=${header}
            -P "${CMAKE_SOURCE_DIR}/cmake/embed_svgs.cmake"
        DEPENDS ${svg_files}
                "${CMAKE_SOURCE_DIR}/cmake/embed_svgs.cmake"
        COMMENT "Embedding ${lib_name} SVGs into ${header}"
        VERBATIM
    )
    add_custom_target(${lib_name}_header ALL DEPENDS "${header}")
endfunction()

# Fontaudio (MIT) — primary audio icon set.
_mmp_embed_svgs(fontaudio
    "${CMAKE_SOURCE_DIR}/deps/fontaudio/src/svgs"
    fontaudio)

# OpenSymbols (GPL/LGPL) — Deepin's port of the OpenOffice symbol font.
# Embedded for general musical-notation glyphs and assorted symbols.
_mmp_embed_svgs(opensymbols
    "${CMAKE_SOURCE_DIR}/deps/opensymbols/src/svg"
    opensymbols)

# Noto Music (SIL OFL 1.1) — Google's full coverage of Unicode music
# blocks (U+2669..U+266F + U+1D100..U+1D1FF). Per-glyph SVGs extracted
# from the rendered TTF by _scripts/extract_music_svgs.py at update time.
_mmp_embed_svgs(notomusic
    "${CMAKE_SOURCE_DIR}/deps/notomusic/src/svg"
    notomusic)

# --- Sources ---
# Each TU is listed once; both binaries share the engine/midi/sf2 cores but
# only one of them links the GUI entry point and only one links the CLI.
set(CORE_SOURCES
    src/core/MmpEngine.cpp
    src/midi/SmfPlayer.cpp
    src/midi/SmfDump.cpp
    src/sf2/Sf2Dump.cpp
    src/sf2/Sf3Codec.cpp
)
set(GUI_SOURCES
    ${CORE_SOURCES}
    src/ui/MainWindow.cpp
    src/ui/SfInspector.cpp
    src/ui/MidiInspector.cpp
    src/ui/HistoryDialog.cpp
    src/ui/IconAudio.cpp
    src/ui/CustomBoxes.cpp
    src/ui/widgets/MmpVuMeter.cpp
    src/ui/widgets/MmpSpectrum.cpp
    src/ui/widgets/MmpPianoRoll.cpp
    src/ui/widgets/MmpPianoKeyboard.cpp
    src/ui/widgets/MmpNoteGlyphs.cpp
    src/ui/widgets/AnimationChrome.cpp
    src/ui/widgets/MmpDigitalCounter.cpp
    src/ui/widgets/MmpEllipsisLabel.cpp
    src/ui/widgets/MmpIconButton.cpp
    src/main.cpp
)
set(CLI_SOURCES
    ${CORE_SOURCES}
    src/cli/main.cpp
)

# --- macOS App Bundle ---
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    file(GLOB_RECURSE OBJCXX_SOURCES src/*.mm)

    set(ICON_FILE "${CMAKE_SOURCE_DIR}/app.icns")

    add_executable(MiniMidiPlayer MACOSX_BUNDLE
        ${GUI_SOURCES} ${OBJCXX_SOURCES})

    if(EXISTS "${ICON_FILE}")
        target_sources(MiniMidiPlayer PRIVATE "${ICON_FILE}")
        set_source_files_properties("${ICON_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()

    set_target_properties(MiniMidiPlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Mini MIDI Player"
        MACOSX_BUNDLE_BUNDLE_VERSION "${APP_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 KomSoft Oprogramowanie"
        MACOSX_BUNDLE_INFO_STRING "Mini MIDI Player v${APP_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Resources/Info.plist.in"
        XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
        XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    )

    add_custom_command(TARGET MiniMidiPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Resources/Credits.rtf"
            "$<TARGET_BUNDLE_DIR:MiniMidiPlayer>/Contents/Resources/Credits.rtf"
        COMMENT "Copying Credits.rtf to bundle"
    )
else()
    add_executable(MiniMidiPlayer ${GUI_SOURCES})
endif()

target_include_directories(MiniMidiPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
add_dependencies(MiniMidiPlayer fontaudio_header opensymbols_header notomusic_header)
target_compile_definitions(MiniMidiPlayer PRIVATE
    BUILD_VERSION="${APP_VERSION}"
    BUILD_NUMBER=${APP_BUILD_NUMBER}
)
target_link_libraries(MiniMidiPlayer PRIVATE
    fltk fltk_images fltk_png fltk_jpeg fltk_z
    tsf tml kissfft mmp_vorbis
)
if(APPLE)
    target_link_libraries(MiniMidiPlayer PRIVATE
        "-framework Cocoa"
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreFoundation"
        -lpthread -lm
    )
endif()

# --- CLI binary (mmp) ---
# On macOS the CLI is embedded inside the .app bundle at
# Contents/MacOS/mmp. Users can run it via the bundle path or symlink it
# into PATH (a "Install command-line tools" menu action can do that later).
add_executable(mmp ${CLI_SOURCES})
target_include_directories(mmp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mmp PRIVATE
    BUILD_VERSION="${APP_VERSION}"
    BUILD_NUMBER=${APP_BUILD_NUMBER}
)
target_link_libraries(mmp PRIVATE tsf tml kissfft mmp_vorbis)
if(APPLE)
    target_link_libraries(mmp PRIVATE
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreFoundation"
        -lpthread -lm
    )

    # Embed mmp into the app bundle at Contents/MacOS/mmp.
    add_dependencies(MiniMidiPlayer mmp)
    add_custom_command(TARGET MiniMidiPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:mmp>"
            "$<TARGET_BUNDLE_DIR:MiniMidiPlayer>/Contents/MacOS/mmp"
        COMMENT "Embedding mmp CLI into MiniMidiPlayer.app/Contents/MacOS/"
        VERBATIM
    )
endif()
