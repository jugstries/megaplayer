cmake_minimum_required(VERSION 3.21)
project(megaplayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Le librerie statiche finiscono anche nella libreria dinamica per le GUI.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(MP_WITH_OPENMPT "Backend libopenmpt (moduli tracker)" ON)
option(MP_WITH_UADE "Backend UADE (formati Amiga, solo UNIX)" ON)
option(MP_WITH_ARCHIVES "Apertura diretta degli archivi LHA/ZIP/7z/... (libarchive)" ON)

set(MP_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(MP_DEPS_PREFIX "${MP_THIRD_PARTY}/install")

if(NOT EXISTS "${MP_THIRD_PARTY}/src/ancient" OR NOT EXISTS "${MP_THIRD_PARTY}/src/miniaudio")
    message(FATAL_ERROR "Dipendenze mancanti: esegui prima scripts/build-deps.sh")
endif()

find_package(PkgConfig REQUIRED)

# --- ancient: decompressori per i packer d'epoca (compilato dai sorgenti) ---

file(GLOB MP_ANCIENT_SOURCES
    "${MP_THIRD_PARTY}/src/ancient/src/*.cpp"
    "${MP_THIRD_PARTY}/src/ancient/src/common/*.cpp")
add_library(ancient STATIC ${MP_ANCIENT_SOURCES})
target_include_directories(ancient
    PUBLIC "${MP_THIRD_PARTY}/src/ancient/api"
    PRIVATE "${MP_THIRD_PARTY}/src/ancient/src" "${MP_THIRD_PARTY}/src/ancient/api/ancient")
target_compile_features(ancient PUBLIC cxx_std_17)
set_target_properties(ancient PROPERTIES CXX_VISIBILITY_PRESET hidden)

# --- miniaudio: uscita audio ed encoder WAV ---

add_library(miniaudio STATIC "${MP_THIRD_PARTY}/src/miniaudio/miniaudio.c")
target_include_directories(miniaudio PUBLIC "${MP_THIRD_PARTY}/src/miniaudio")
# I decoder di miniaudio (WAV/FLAC/MP3) per ora non servono.
target_compile_definitions(miniaudio PUBLIC MA_NO_DECODING MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE MA_NO_GENERATION)
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()

# --- core ---

add_library(megaplayer_core STATIC
    src/core/Archive.cpp
    src/core/AudioOutput.cpp
    src/core/DecoderRegistry.cpp
    src/core/Player.cpp
    src/core/SourceFile.cpp
    src/core/TrackList.cpp
    src/core/WavWriter.cpp
    src/decoders/BuiltinDecoders.cpp)
target_include_directories(megaplayer_core PUBLIC src)
target_link_libraries(megaplayer_core PUBLIC ancient miniaudio)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(megaplayer_core PRIVATE -Wall -Wextra)
endif()

if(MP_WITH_ARCHIVES)
    if(APPLE)
        # Homebrew installa libarchive come keg-only, fuori dal percorso di
        # pkg-config.
        find_program(MP_BREW brew)
        if(MP_BREW)
            execute_process(COMMAND "${MP_BREW}" --prefix libarchive
                OUTPUT_VARIABLE MP_BREW_LIBARCHIVE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            set(ENV{PKG_CONFIG_PATH} "${MP_BREW_LIBARCHIVE}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        endif()
    endif()
    pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive)
    if(LIBARCHIVE_FOUND)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_ARCHIVES)
        target_link_libraries(megaplayer_core PUBLIC PkgConfig::LIBARCHIVE)
    else()
        message(WARNING "libarchive non trovata: gli archivi vanno estratti a mano")
    endif()
endif()

if(MP_WITH_OPENMPT)
    pkg_check_modules(OPENMPT IMPORTED_TARGET libopenmpt)
    if(OPENMPT_FOUND)
        target_sources(megaplayer_core PRIVATE src/decoders/OpenMptDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_OPENMPT)
        target_link_libraries(megaplayer_core PUBLIC PkgConfig::OPENMPT)
    else()
        message(WARNING "libopenmpt non trovata: backend openmpt disattivato")
    endif()
endif()

set(MP_UADE_ENABLED OFF)
if(MP_WITH_UADE)
    if(WIN32)
        message(WARNING "UADE non è ancora portato su Windows: backend uade disattivato")
    elseif(NOT EXISTS "${MP_DEPS_PREFIX}/share/uade/eagleplayer.conf")
        message(WARNING "UADE non preparato (scripts/build-deps.sh): backend uade disattivato")
    else()
        include(cmake/UadeInProcess.cmake)
        target_sources(megaplayer_core PRIVATE src/decoders/UadeDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_UADE)
        target_link_libraries(megaplayer_core PUBLIC uade_inprocess)
        set(MP_UADE_ENABLED ON)
    endif()
endif()

# --- CLI ---

add_executable(megaplayer src/cli/main.cpp src/cli/Terminal.cpp)
target_link_libraries(megaplayer PRIVATE megaplayer_core)
if(MP_UADE_ENABLED)
    # Su Linux libzakalwe e bencode-tools sono librerie condivise nel
    # prefisso privato.
    set_target_properties(megaplayer PROPERTIES BUILD_RPATH "${MP_DEPS_PREFIX}/lib")
endif()

# --- Libreria dinamica con l'API C, per l'app Flutter (app/) ---

add_library(megaplayer_capi SHARED src/capi/megaplayer.cpp)
target_link_libraries(megaplayer_capi PRIVATE megaplayer_core)
set_target_properties(megaplayer_capi PROPERTIES
    OUTPUT_NAME megaplayer
    CXX_VISIBILITY_PRESET hidden
    INSTALL_NAME_DIR "@rpath"
    BUILD_WITH_INSTALL_NAME_DIR ON)
if(MP_UADE_ENABLED)
    set_target_properties(megaplayer_capi PROPERTIES BUILD_RPATH "${MP_DEPS_PREFIX}/lib")
endif()

# --- Test (ctest --test-dir build) ---

enable_testing()
if(MP_UADE_ENABLED)
    add_executable(uade_inprocess_test tests/UadeInProcessTest.cpp)
    target_link_libraries(uade_inprocess_test PRIVATE megaplayer_core)
    set_target_properties(uade_inprocess_test PROPERTIES BUILD_RPATH "${MP_DEPS_PREFIX}/lib")
    add_test(NAME uade_inprocess
        COMMAND uade_inprocess_test "${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin")
endif()
