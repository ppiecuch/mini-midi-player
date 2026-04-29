# cmake/libvorbis.cmake — build libogg + libvorbis statically from the
# upstream source we vendored under deps/libogg and deps/libvorbis,
# bypassing their autotools / CMake build systems.
#
# Output: a single INTERFACE-style static archive `mmp_vorbis` exposing
# the public Ogg + Vorbis APIs (ogg/ogg.h, vorbis/codec.h, vorbis/vorbisenc.h,
# vorbis/vorbisfile.h). Used by src/sf2/Sf3Codec.cpp for both encode
# (PCM → Vorbis-in-Ogg) and decode.
#
# Skipped from upstream:
#   * barkmel.c / psytune.c / tone.c — diagnostic CLI tools, not library code.
#   * lib/lookups.h / *.cpp / windows builds — handled by upstream headers.

set(_OGG_DIR     "${CMAKE_SOURCE_DIR}/deps/libogg/src")
set(_VORBIS_DIR  "${CMAKE_SOURCE_DIR}/deps/libvorbis/src")

# libogg sources — exactly two .c files.
set(_OGG_SRCS
    "${_OGG_DIR}/src/bitwise.c"
    "${_OGG_DIR}/src/framing.c"
)

# libvorbis sources — every .c except the diagnostic-tool entry points.
set(_VORBIS_SRCS
    "${_VORBIS_DIR}/lib/analysis.c"
    "${_VORBIS_DIR}/lib/bitrate.c"
    "${_VORBIS_DIR}/lib/block.c"
    "${_VORBIS_DIR}/lib/codebook.c"
    "${_VORBIS_DIR}/lib/envelope.c"
    "${_VORBIS_DIR}/lib/floor0.c"
    "${_VORBIS_DIR}/lib/floor1.c"
    "${_VORBIS_DIR}/lib/info.c"
    "${_VORBIS_DIR}/lib/lookup.c"
    "${_VORBIS_DIR}/lib/lpc.c"
    "${_VORBIS_DIR}/lib/lsp.c"
    "${_VORBIS_DIR}/lib/mapping0.c"
    "${_VORBIS_DIR}/lib/mdct.c"
    "${_VORBIS_DIR}/lib/misc.c"
    "${_VORBIS_DIR}/lib/psy.c"
    "${_VORBIS_DIR}/lib/registry.c"
    "${_VORBIS_DIR}/lib/res0.c"
    "${_VORBIS_DIR}/lib/sharedbook.c"
    "${_VORBIS_DIR}/lib/smallft.c"
    "${_VORBIS_DIR}/lib/synthesis.c"
    "${_VORBIS_DIR}/lib/vorbisenc.c"
    "${_VORBIS_DIR}/lib/vorbisfile.c"
    "${_VORBIS_DIR}/lib/window.c"
)

add_library(mmp_vorbis STATIC ${_OGG_SRCS} ${_VORBIS_SRCS})
target_include_directories(mmp_vorbis
    PUBLIC
        "${_OGG_DIR}/include"
        "${_VORBIS_DIR}/include"
    PRIVATE
        "${_VORBIS_DIR}/lib"
)
# Silence the upstream's mountain of legacy warnings — we don't own
# this code and they don't break correctness.
target_compile_options(mmp_vorbis PRIVATE
    -Wno-unused-parameter
    -Wno-implicit-function-declaration
    -Wno-unused-variable
    -Wno-uninitialized
    -Wno-unused-but-set-variable
    -Wno-sign-compare
)
