# La parte di ZXTune (LGPL-3.0) che suona i formati dei chip AY: i tracker
# dello ZX Spectrum e del CPC (Pro Tracker 1-3, Vortex, Sound Tracker, ASC,
# SQ-Tracker, Global Tracker, Fast Tracker, Pro Sound Creator e Maker,
# TurboSound) e i dump PSG e VTX, col suo emulatore dell'AY-3-8910/YM2149.
# Senza i player esterni, gli archivi, i dispositivi audio e la
# localizzazione (lo stub). I sorgenti si compilano come nei suoi makefile:
# C++20 e char senza segno. Definisce la libreria zxtune e
# MP_ZXTUNE_ENABLED.

set(MP_ZXTUNE_ENABLED OFF)
set(MP_ZXTUNE_DIR "${MP_THIRD_PARTY}/src/zxtune")
if(NOT EXISTS "${MP_ZXTUNE_DIR}/src/core/service.h")
    message(WARNING "ZXTune non scaricato (scripts/build-deps.sh): backend zxtune disattivato")
    return()
endif()

set(zx "${MP_ZXTUNE_DIR}/src")
file(GLOB MP_ZXTUNE_SOURCES
    "${zx}/analysis/src/*.cpp"
    "${zx}/binary/src/*.cpp" "${zx}/binary/format/*.cpp" "${zx}/binary/format/full/*.cpp"
    "${zx}/core/plugins/*.cpp" "${zx}/core/plugins/players/plugin.cpp"
    "${zx}/core/plugins/players/ay/*.cpp" "${zx}/core/plugins/players/multi/plugins_factory.cpp"
    "${zx}/core/plugins/archives/stub/*.cpp" "${zx}/core/src/*.cpp"
    "${zx}/debug/src/*.cpp"
    "${zx}/devices/aym/src/*.cpp"
    "${zx}/formats/chiptune/common/*.cpp" "${zx}/formats/chiptune/aym/*.cpp"
    "${zx}/l10n/stub/*.cpp"
    "${zx}/module/players/*.cpp" "${zx}/module/players/aym/*.cpp" "${zx}/module/properties/src/*.cpp"
    "${zx}/parameters/src/*.cpp"
    "${zx}/sound/impl/*.cpp"
    "${zx}/strings/src/*.cpp"
    "${zx}/tools/src/*.cpp"
    "${MP_ZXTUNE_DIR}/3rdparty/fmt/src/format.cc")
# Il ricampionatore usa quello di lazyusf2 e serve solo ai player xSF e DAC;
# YM e VTX (compressi in LHA) e i file AY con codice Z80, che
# patches/zxtune.patch non registra, li suonano già AtariAudio e
# game-music-emu.
list(FILTER MP_ZXTUNE_SOURCES EXCLUDE REGEX
    "/sound/impl/resampler\\.cpp$|/aym/ym_vtx\\.cpp$|/aym/ymvtx\\.cpp$|/aym/ayemul\\.cpp$")
add_library(zxtune STATIC ${MP_ZXTUNE_SOURCES})
target_compile_definitions(zxtune PRIVATE MEGAPLAYER)
target_include_directories(zxtune SYSTEM PRIVATE
    "${MP_ZXTUNE_DIR}/include" "${zx}" "${MP_ZXTUNE_DIR}" "${MP_ZXTUNE_DIR}/3rdparty/fmt/include")
set_target_properties(zxtune PROPERTIES CXX_STANDARD 20 CXX_VISIBILITY_PRESET hidden)
# fmtlib 9 con i compilatori recenti: niente controllo dei formati a tempo di
# compilazione (consteval), che altrimenti non compila.
target_compile_definitions(zxtune PUBLIC "FMT_CONSTEVAL=")
target_compile_options(zxtune PRIVATE -funsigned-char)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(zxtune PRIVATE -w) # codice di terzi
endif()
set(MP_ZXTUNE_ENABLED ON)
