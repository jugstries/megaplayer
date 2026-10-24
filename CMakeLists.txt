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
if(MINGW)
    # Eseguibili e DLL senza le DLL del runtime C++ di MinGW.
    add_link_options(-static-libgcc -static-libstdc++)
endif()

option(MP_WITH_OPENMPT "Backend libopenmpt (moduli tracker)" ON)
option(MP_WITH_UADE "Backend UADE (formati Amiga)" ON)
option(MP_WITH_ARCHIVES "Apertura diretta degli archivi LHA/ZIP/7z/... (libarchive)" ON)

set(MP_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

if(NOT EXISTS "${MP_THIRD_PARTY}/src/ancient" OR NOT EXISTS "${MP_THIRD_PARTY}/src/miniaudio")
    message(FATAL_ERROR "Dipendenze mancanti: esegui prima scripts/build-deps.sh")
endif()

# libopenmpt e libarchive arrivano dal sistema, trovate con pkg-config.
find_package(PkgConfig)

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
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive)
    endif()
    if(LIBARCHIVE_FOUND)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_ARCHIVES)
        target_link_libraries(megaplayer_core PUBLIC PkgConfig::LIBARCHIVE)
    else()
        message(WARNING "libarchive non trovata: gli archivi vanno estratti a mano")
    endif()
endif()

if(MP_WITH_OPENMPT)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(OPENMPT IMPORTED_TARGET libopenmpt)
    endif()
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
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/uade/eagleplayer.conf")
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

# --- Libreria dinamica con l'API C, per l'app Flutter (app/) ---

add_library(megaplayer_capi SHARED src/capi/megaplayer.cpp)
target_link_libraries(megaplayer_capi PRIVATE megaplayer_core)
set_target_properties(megaplayer_capi PROPERTIES
    OUTPUT_NAME megaplayer
    CXX_VISIBILITY_PRESET hidden
    INSTALL_NAME_DIR "@rpath"
    BUILD_WITH_INSTALL_NAME_DIR ON)

# --- Test (ctest --test-dir build) ---

enable_testing()
if(MP_UADE_ENABLED)
    add_executable(uade_inprocess_test tests/UadeInProcessTest.cpp)
    target_link_libraries(uade_inprocess_test PRIVATE megaplayer_core)
    if(WIN32)
        target_link_libraries(uade_inprocess_test PRIVATE psapi)
    endif()
    add_test(NAME uade_inprocess
        COMMAND uade_inprocess_test "${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin")

    # La CLI su un brano AHX, che solo UADE suona: deve esportarne 5 secondi.
    add_test(NAME cli_render_ahx
        COMMAND megaplayer render -t 5 -o "${CMAKE_BINARY_DIR}/test-ahx.wav"
                "${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin")
    set_tests_properties(cli_render_ahx PROPERTIES PASS_REGULAR_EXPRESSION "Scritti 0:05")
endif()

# Un MOD generato da tests/make_test_mod.py, suonato da entrambi i backend.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME make_test_mod
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_mod.py" "${CMAKE_BINARY_DIR}/test.mod")
    set_tests_properties(make_test_mod PROPERTIES FIXTURES_SETUP test_mod)
    set(MP_MOD_BACKENDS)
    if(OPENMPT_FOUND)
        list(APPEND MP_MOD_BACKENDS openmpt)
    endif()
    if(MP_UADE_ENABLED)
        list(APPEND MP_MOD_BACKENDS uade)
    endif()
    foreach(backend ${MP_MOD_BACKENDS})
        add_test(NAME cli_render_mod_${backend}
            COMMAND megaplayer render -b ${backend} -t 5 -o "${CMAKE_BINARY_DIR}/test-mod-${backend}.wav"
                    "${CMAKE_BINARY_DIR}/test.mod")
        set_tests_properties(cli_render_mod_${backend} PROPERTIES
            FIXTURES_REQUIRED test_mod PASS_REGULAR_EXPRESSION "Backend: +${backend}.*Scritti 0:05")
    endforeach()
endif()
