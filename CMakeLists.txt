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
if(EMSCRIPTEN)
    # WebAssembly: uadecore e il motore di riproduzione girano in thread (web
    # worker), quindi tutto va compilato con le pthread. Le eccezioni del core
    # e il longjmp con cui uadecore esce dagli errori usano il meccanismo
    # nativo di WebAssembly; lo stack predefinito (64 KB) non basta a UADE.
    add_compile_options(-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
    add_link_options(-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sPTHREAD_POOL_SIZE=4
        -sSTACK_SIZE=1048576 -sDEFAULT_PTHREAD_STACK_SIZE=1048576)
endif()

option(MP_WITH_OPENMPT "Backend libopenmpt (moduli tracker)" ON)
option(MP_WITH_UADE "Backend UADE (formati Amiga)" ON)
option(MP_WITH_GME "Backend game-music-emu (console a 8 e 16 bit)" ON)
option(MP_WITH_SID "Backend libsidplayfp (Commodore 64)" ON)
option(MP_WITH_ATARI "Backend AtariAudio (Atari ST: SNDH e YM)" ON)
option(MP_WITH_PSF "Backend Highly Experimental (PlayStation: PSF con OpenBIOS; PlayStation 2: PSF2)" ON)
option(MP_WITH_SSF "Backend Highly Theoretical (Saturn: SSF, Dreamcast: DSF)" ON)
option(MP_WITH_QSF "Backend Highly Quixotic (QSound di Capcom: QSF)" ON)
option(MP_WITH_2SF "Backend vio2sf (Nintendo DS: 2SF)" ON)
option(MP_WITH_GSF "Backend viogsf (Game Boy Advance: GSF)" ON)
option(MP_WITH_USF "Backend lazyusf2 (Nintendo 64: USF)" ON)
option(MP_WITH_VGMSTREAM "Backend vgmstream (audio in streaming dei giochi: GENH, ADS, VAG, DSP, ADX...)" ON)
option(MP_WITH_FFMPEG "Backend FFmpeg (M4A/AAC, ALAC, Opus, AIFF, WMA, APE, WavPack...) e i codec di vgmstream" ON)
option(MP_WITH_ADLMIDI "Backend libADLMIDI (MIDI su chip FM OPL3)" ON)
option(MP_WITH_88EMU "Backend 88emu (MIDI su Roland Sound Canvas e MT-32 emulati, con le ROM dell'utente)" ON)
option(MP_WITH_FLUIDSYNTH "Backend FluidSynth (MIDI su un SoundFont, per chi non ha le ROM)" ON)
option(MP_WITH_MP3 "Esportazione in MP3 (LAME)" ON)
option(MP_WITH_ADPLUG "Backend AdPlug (formati AdLib dei giochi DOS)" ON)
option(MP_WITH_ARCHIVES "Apertura diretta degli archivi LHA/ZIP/7z/... (libarchive)" ON)
option(MP_WITH_LIBRARY "Libreria musicale: indice SQLite di una cartella, con ricerca (non per WebAssembly)" ON)

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

# --- miniaudio: uscita audio, encoder WAV e decoder MP3/FLAC/WAV/Ogg Vorbis ---

add_library(miniaudio STATIC src/core/Miniaudio.c)
target_include_directories(miniaudio PUBLIC "${MP_THIRD_PARTY}/src/miniaudio")
target_compile_definitions(miniaudio PUBLIC MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE MA_NO_GENERATION)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(miniaudio PRIVATE -w) # codice di terzi (stb_vorbis)
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()

# --- core ---

add_library(megaplayer_core STATIC
    src/core/Archive.cpp
    src/core/AudioFileWriter.cpp
    src/core/AudioOutput.cpp
    src/core/DecoderRegistry.cpp
    src/core/Exporter.cpp
    src/core/Platform.cpp
    src/core/Player.cpp
    src/core/Resampler.cpp
    src/core/SourceFile.cpp
    src/core/TrackList.cpp
    src/core/WavWriter.cpp
    src/decoders/AudioTags.cpp
    src/decoders/BuiltinDecoders.cpp
    src/decoders/MiniaudioDecoder.cpp
    src/decoders/RolandDevices.cpp
    src/decoders/TfmxFiles.cpp
    src/decoders/XaDecoder.cpp
    src/dsp/MegaSound.cpp)
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

# LAME per esportare in MP3, dal sistema come libopenmpt: con pkg-config
# dove c'è il suo file (Homebrew, MSYS2), altrimenti cercato a mano (Debian e
# Ubuntu non lo danno).
set(MP_MP3_ENABLED OFF)
if(MP_WITH_MP3 AND NOT EMSCRIPTEN)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LAME IMPORTED_TARGET lame)
    endif()
    if(LAME_FOUND)
        target_link_libraries(megaplayer_core PUBLIC PkgConfig::LAME)
        set(MP_MP3_ENABLED ON)
    else()
        find_path(MP_LAME_INCLUDE_DIR lame.h PATH_SUFFIXES lame)
        find_library(MP_LAME_LIBRARY mp3lame)
        if(MP_LAME_INCLUDE_DIR AND MP_LAME_LIBRARY)
            target_include_directories(megaplayer_core PRIVATE "${MP_LAME_INCLUDE_DIR}")
            target_link_libraries(megaplayer_core PUBLIC "${MP_LAME_LIBRARY}")
            set(MP_MP3_ENABLED ON)
        endif()
    endif()
    if(MP_MP3_ENABLED)
        target_sources(megaplayer_core PRIVATE src/core/Mp3Writer.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_MP3)
    else()
        message(WARNING "LAME non trovata: niente esportazione in MP3")
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

set(MP_GME_ENABLED OFF)
if(MP_WITH_GME)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/game-music-emu/CMakeLists.txt")
        message(WARNING "game-music-emu non scaricato (scripts/build-deps.sh): backend gme disattivato")
    else()
        # Solo la libreria statica. I .vgz arrivano già decompressi da ancient
        # (gzip): zlib non serve.
        set(GME_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(GME_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(GME_BUILD_FRAMEWORK OFF CACHE BOOL "" FORCE)
        set(GME_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(GME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GME_ZLIB OFF CACHE BOOL "" FORCE)
        add_subdirectory("${MP_THIRD_PARTY}/src/game-music-emu" "${CMAKE_BINARY_DIR}/game-music-emu" EXCLUDE_FROM_ALL)
        target_sources(megaplayer_core PRIVATE src/decoders/GmeDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_GME)
        target_link_libraries(megaplayer_core PUBLIC gme::gme)
        set(MP_GME_ENABLED ON)
    endif()
endif()

set(MP_SID_ENABLED OFF)
if(MP_WITH_SID)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/libsidplayfp/configure.ac" OR NOT EXISTS "${MP_THIRD_PARTY}/src/libresidfp/configure.ac")
        message(WARNING "libsidplayfp non scaricata (scripts/build-deps.sh): backend sid disattivato")
    else()
        include(cmake/Sidplayfp.cmake)
        target_sources(megaplayer_core PRIVATE src/decoders/SidDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_SID)
        target_link_libraries(megaplayer_core PUBLIC sidplayfp)
        set(MP_SID_ENABLED ON)
    endif()
endif()

set(MP_ATARI_ENABLED OFF)
if(MP_WITH_ATARI)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/atariaudio/src/AtariAudio.h")
        message(WARNING "AtariAudio non scaricato (scripts/build-deps.sh): backend atariaudio disattivato")
    else()
        # Nessuna dipendenza e nessun CMake suo: i sorgenti, con il 68000 di
        # Musashi e i decompressori ICE! e LHA.
        set(MP_ATARIAUDIO_DIR "${MP_THIRD_PARTY}/src/atariaudio/src")
        file(GLOB MP_ATARIAUDIO_SOURCES
            "${MP_ATARIAUDIO_DIR}/*.cpp"
            "${MP_ATARIAUDIO_DIR}/external/*.cpp"
            "${MP_ATARIAUDIO_DIR}/external/Musashi/*.cpp")
        add_library(atariaudio STATIC ${MP_ATARIAUDIO_SOURCES})
        target_include_directories(atariaudio SYSTEM PUBLIC "${MP_ATARIAUDIO_DIR}")
        set_target_properties(atariaudio PROPERTIES CXX_VISIBILITY_PRESET hidden)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(atariaudio PRIVATE -w) # codice di terzi
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/AtariAudioDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_ATARI)
        target_link_libraries(megaplayer_core PUBLIC atariaudio)
        set(MP_ATARI_ENABLED ON)
    endif()
endif()

# --- La famiglia PSF: psflib (file, librerie _lib e tag), il codice comune
# dei decoder (src/decoders/Xsf.cpp) e i nuclei di Neill Corlett, con le
# piattaforme che emulano. Tutte le piattaforme di megaplayer sono little
# endian. ---

set(MP_PSFLIB_DIR "${MP_THIRD_PARTY}/src/psflib")
set(MP_XSF_ENABLED OFF)
if(MP_WITH_PSF OR MP_WITH_SSF OR MP_WITH_QSF OR MP_WITH_2SF OR MP_WITH_GSF OR MP_WITH_USF)
    if(EMSCRIPTEN)
        set(ZLIB_FOUND ON) # la porta di Emscripten, vedi sotto
    else()
        find_package(ZLIB)
    endif()
    if(NOT EXISTS "${MP_PSFLIB_DIR}/psflib.c")
        message(WARNING "psflib non scaricata (scripts/build-deps.sh): backend della famiglia PSF disattivati")
    elseif(NOT ZLIB_FOUND)
        message(WARNING "zlib non trovata: backend della famiglia PSF disattivati")
    else()
        add_library(psflib STATIC "${MP_PSFLIB_DIR}/psflib.c" "${MP_PSFLIB_DIR}/psf2fs.c")
        target_include_directories(psflib SYSTEM PUBLIC "${MP_PSFLIB_DIR}")
        if(EMSCRIPTEN)
            target_compile_options(psflib PUBLIC -sUSE_ZLIB=1)
            target_link_options(psflib PUBLIC -sUSE_ZLIB=1)
        else()
            target_link_libraries(psflib PUBLIC ZLIB::ZLIB)
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/Xsf.cpp)
        target_link_libraries(megaplayer_core PUBLIC psflib)
        set(MP_XSF_ENABLED ON)
    endif()
endif()

# mp_neill_core(<target> <cartella> <sorgenti>...): un nucleo di Neill Corlett
# come libreria statica.
function(mp_neill_core target directory)
    list(TRANSFORM ARGN PREPEND "${directory}/")
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} SYSTEM PUBLIC "${directory}")
    target_compile_definitions(${target} PUBLIC EMU_LITTLE_ENDIAN HAVE_STDINT_H PRIVATE EMU_COMPILE)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -w) # codice di terzi
    endif()
    target_link_libraries(megaplayer_core PUBLIC ${target})
endfunction()

set(MP_PSF_ENABLED OFF)
if(MP_WITH_PSF AND MP_XSF_ENABLED)
    set(MP_HE_DIR "${MP_THIRD_PARTY}/src/highly_experimental/Core")
    set(MP_OPENBIOS "${MP_THIRD_PARTY}/src/openbios/openbios.bin")
    if(NOT EXISTS "${MP_HE_DIR}/psx.c" OR NOT EXISTS "${MP_OPENBIOS}")
        message(WARNING "Highly Experimental o OpenBIOS non scaricati (scripts/build-deps.sh): backend psf disattivato")
    else()
        # Il nucleo PS1/PS2, senza disassembler, assembler e mkhebios (che
        # ricava hebios da una BIOS della PS2).
        mp_neill_core(highly_experimental "${MP_HE_DIR}"
            bios.c iop.c ioptimer.c psx.c r3000.c spu.c spucore.c vfs.c)
        # OpenBIOS dentro la libreria: niente file da cercare a runtime. Per
        # il PSF2 i moduli IOP liberi (src/ps2/irx, rifatti da
        # scripts/build-ps2-iop.sh), nell'ordine in cui loadcore li avvia.
        include(cmake/EmbedBinary.cmake)
        mp_embed_binary("${MP_OPENBIOS}" mp_openbios "${CMAKE_BINARY_DIR}/openbios.c")
        set(MP_IOP_MODULES sysmem loadcore excepman intrman ssbusc dmacman timrman sysclib heaplib
            threadman vblank ioman modload stdio sifman sifcmd loadfile sifinit fileio hefile)
        list(TRANSFORM MP_IOP_MODULES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/ps2/irx/")
        list(TRANSFORM MP_IOP_MODULES APPEND ".irx")
        mp_embed_binaries("${CMAKE_BINARY_DIR}/iop_modules.c" mp_iop_modules ${MP_IOP_MODULES})
        target_sources(megaplayer_core PRIVATE src/decoders/HighlyExperimental.cpp src/decoders/PsfDecoder.cpp
            src/decoders/Psf2Decoder.cpp "${CMAKE_BINARY_DIR}/openbios.c" "${CMAKE_BINARY_DIR}/iop_modules.c")
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_PSF)
        set(MP_PSF_ENABLED ON)
    endif()
endif()

set(MP_SSF_ENABLED OFF)
if(MP_WITH_SSF AND MP_XSF_ENABLED)
    set(MP_HT_DIR "${MP_THIRD_PARTY}/src/highly_theoretical/Core")
    if(NOT EXISTS "${MP_HT_DIR}/sega.c")
        message(WARNING "Highly Theoretical non scaricato (scripts/build-deps.sh): backend ssf disattivato")
    else()
        # Il 68000 del Saturn è C68K di Yabause (GPL-2.0 o successiva), con
        # gli opcode in uno switch: il Musashi che c'è accanto ha una licenza
        # solo non commerciale. Il ricompilatore del DSP è solo per x86 a 32
        # bit, e resta spento.
        mp_neill_core(highly_theoretical "${MP_HT_DIR}"
            sega.c satsound.c dcsound.c yam.c arm.c c68k/c68k.c c68k/c68kexec.c)
        target_compile_definitions(highly_theoretical PRIVATE LSB_FIRST C68K_NO_JUMP_TABLE)
        target_sources(megaplayer_core PRIVATE src/decoders/SsfDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_SSF)
        set(MP_SSF_ENABLED ON)
    endif()
endif()

set(MP_QSF_ENABLED OFF)
if(MP_WITH_QSF AND MP_XSF_ENABLED)
    set(MP_HQ_DIR "${MP_THIRD_PARTY}/src/highly_quixotic/Core")
    if(NOT EXISTS "${MP_HQ_DIR}/qsound.c")
        message(WARNING "Highly Quixotic non scaricato (scripts/build-deps.sh): backend qsf disattivato")
    else()
        mp_neill_core(highly_quixotic "${MP_HQ_DIR}" qsound.c qsound_ctr.c qmix.c z80.c kabuki.c)
        target_sources(megaplayer_core PRIVATE src/decoders/QsfDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_QSF)
        set(MP_QSF_ENABLED ON)
    endif()
endif()

# mp_xsf_library(<target> <cartella> <sorgenti>...): un emulatore della
# famiglia PSF che non è di Neill Corlett, come libreria statica.
function(mp_xsf_library target directory)
    list(TRANSFORM ARGN PREPEND "${directory}/")
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} SYSTEM PUBLIC "${directory}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -w) # codice di terzi
    endif()
    target_link_libraries(megaplayer_core PUBLIC ${target})
endfunction()

set(MP_2SF_ENABLED OFF)
if(MP_WITH_2SF AND MP_XSF_ENABLED)
    set(MP_VIO2SF_DIR "${MP_THIRD_PARTY}/src/vio2sf/src/vio2sf/desmume")
    if(NOT EXISTS "${MP_VIO2SF_DIR}/state.c")
        message(WARNING "vio2sf non scaricato (scripts/build-deps.sh): backend 2sf disattivato")
    else()
        # Il DS di DeSmuME ridotto al suono, con le chiamate della BIOS
        # emulate.
        mp_xsf_library(vio2sf "${MP_VIO2SF_DIR}"
            FIFO.c GPU.c MMU.c NDSSystem.c SPU.cpp arm_instructions.c armcpu.c barray.c bios.c cp15.c isqrt.c
            matrix.c mc.c resampler.c state.c thumb_instructions.c)
        target_sources(megaplayer_core PRIVATE src/decoders/TwosfDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_2SF)
        set(MP_2SF_ENABLED ON)
    endif()
endif()

set(MP_GSF_ENABLED OFF)
if(MP_WITH_GSF AND MP_XSF_ENABLED)
    set(MP_VIOGSF_DIR "${MP_THIRD_PARTY}/src/viogsf/vbam")
    if(NOT EXISTS "${MP_VIOGSF_DIR}/gba/GBA.cpp")
        message(WARNING "viogsf non scaricato (scripts/build-deps.sh): backend gsf disattivato")
    else()
        # Il GBA di VBA-M, con le chiamate della BIOS emulate, e l'APU di
        # Blargg.
        mp_xsf_library(viogsf "${MP_VIOGSF_DIR}"
            apu/Blip_Buffer.cpp apu/Effects_Buffer.cpp apu/Gb_Apu.cpp apu/Gb_Oscs.cpp apu/Multi_Buffer.cpp
            gba/GBA.cpp gba/GBA-arm.cpp gba/GBA-thumb.cpp gba/Sound.cpp gba/bios.cpp)
        target_sources(megaplayer_core PRIVATE src/decoders/GsfDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_GSF)
        set(MP_GSF_ENABLED ON)
    endif()
endif()

set(MP_USF_ENABLED OFF)
if(MP_WITH_USF AND MP_XSF_ENABLED)
    set(MP_LAZYUSF2_DIR "${MP_THIRD_PARTY}/src/lazyusf2")
    if(NOT EXISTS "${MP_LAZYUSF2_DIR}/usf/usf.c")
        message(WARNING "lazyusf2 non scaricato (scripts/build-deps.sh): backend usf disattivato")
    else()
        # Mupen64Plus ridotto al suono: l'interprete con la cache, senza i
        # ricompilatori (solo x86), e l'RSP di Iconoclast.
        mp_xsf_library(lazyusf2 "${MP_LAZYUSF2_DIR}"
            ai/ai_controller.c api/callbacks.c debugger/dbg_decoder.c main/main.c main/rom.c main/savestates.c
            main/util.c memory/memory.c pi/cart_rom.c pi/pi_controller.c r4300/cached_interp.c r4300/cp0.c
            r4300/cp1.c r4300/empty_dynarec.c r4300/exception.c r4300/interupt.c r4300/mi_controller.c
            r4300/pure_interp.c r4300/r4300.c r4300/r4300_core.c r4300/recomp.c r4300/reset.c r4300/tlb.c
            rdp/rdp_core.c ri/rdram.c ri/rdram_detection_hack.c ri/ri_controller.c rsp/rsp_core.c
            rsp_hle/alist.c rsp_hle/alist_audio.c rsp_hle/alist_naudio.c rsp_hle/alist_nead.c rsp_hle/audio.c
            rsp_hle/cicx105.c rsp_hle/hle.c rsp_hle/hvqm.c rsp_hle/jpeg.c rsp_hle/memory.c rsp_hle/mp3.c
            rsp_hle/musyx.c rsp_hle/plugin.c rsp_hle/re2.c rsp_lle/rsp.c si/cic.c si/game_controller.c
            si/n64_cic_nus_6105.c si/pif.c si/si_controller.c usf/barray.c usf/resampler.c usf/usf.c
            vi/vi_controller.c)
        # Per crc32.
        if(EMSCRIPTEN)
            target_compile_options(lazyusf2 PRIVATE -sUSE_ZLIB=1)
        else()
            target_link_libraries(lazyusf2 PRIVATE ZLIB::ZLIB)
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/UsfDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_USF)
        set(MP_USF_ENABLED ON)
    endif()
endif()

set(MP_FFMPEG_ENABLED OFF)
if(MP_WITH_FFMPEG)
    include(cmake/FFmpeg.cmake)
    if(MP_FFMPEG_ENABLED)
        target_sources(megaplayer_core PRIVATE src/decoders/FfmpegDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_FFMPEG)
        target_link_libraries(megaplayer_core PUBLIC ffmpeg)
    endif()
endif()

set(MP_VGMSTREAM_ENABLED OFF)
if(MP_WITH_VGMSTREAM)
    set(MP_VGMSTREAM_DIR "${MP_THIRD_PARTY}/src/vgmstream")
    if(NOT EXISTS "${MP_VGMSTREAM_DIR}/src/libvgmstream.h")
        message(WARNING "vgmstream non scaricato (scripts/build-deps.sh): backend vgmstream disattivato")
    else()
        # I codec che vgmstream ha dentro (PS-ADPCM, DSP, IMA, ADX, HCA, PCM,
        # G.722.1...) e, se c'è, il nostro FFmpeg (ATRAC3 e ATRAC3plus, XMA,
        # WMA, AAC, Opus); niente mpg123, Vorbis, ATRAC9, CELT e Speex, che
        # sarebbero altre dipendenze da portare su ogni piattaforma. Il suo
        # CMake le scaricherebbe da solo: si compilano i sorgenti.
        set(MP_VGMSTREAM_SRC "${MP_VGMSTREAM_DIR}/src")
        file(GLOB MP_VGMSTREAM_SOURCES
            "${MP_VGMSTREAM_SRC}/*.c"
            "${MP_VGMSTREAM_SRC}/base/*.c"
            "${MP_VGMSTREAM_SRC}/coding/*.c"
            "${MP_VGMSTREAM_SRC}/coding/libs/*.c"
            "${MP_VGMSTREAM_SRC}/layout/*.c"
            "${MP_VGMSTREAM_SRC}/meta/*.c"
            "${MP_VGMSTREAM_SRC}/util/*.c")
        add_library(vgmstream STATIC ${MP_VGMSTREAM_SOURCES})
        target_include_directories(vgmstream SYSTEM PUBLIC "${MP_VGMSTREAM_SRC}")
        target_include_directories(vgmstream PRIVATE "${MP_VGMSTREAM_DIR}/ext_includes" "${MP_VGMSTREAM_SRC}/base"
            "${MP_VGMSTREAM_SRC}/coding" "${MP_VGMSTREAM_SRC}/layout" "${MP_VGMSTREAM_SRC}/meta" "${MP_VGMSTREAM_SRC}/util")
        target_compile_definitions(vgmstream PRIVATE VGM_USE_G7221)
        if(MP_FFMPEG_ENABLED)
            target_compile_definitions(vgmstream PRIVATE VGM_USE_FFMPEG)
            target_link_libraries(vgmstream PUBLIC ffmpeg)
        endif()
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(vgmstream PRIVATE -w) # codice di terzi
        endif()
        if(UNIX)
            target_link_libraries(vgmstream PUBLIC m)
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/VgmstreamDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_VGMSTREAM)
        target_link_libraries(megaplayer_core PUBLIC vgmstream)
        set(MP_VGMSTREAM_ENABLED ON)
    endif()
endif()

set(MP_ADLMIDI_ENABLED OFF)
if(MP_WITH_ADLMIDI)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/libadlmidi/CMakeLists.txt")
        message(WARNING "libADLMIDI non scaricata (scripts/build-deps.sh): backend adlmidi disattivato")
    else()
        # Solo la libreria statica, con il sequencer MIDI e i banchi di
        # strumenti incorporati. Degli emulatori del chip resta Nuked OPL3, il
        # più fedele, e DOSBox, leggero.
        set(libADLMIDI_STATIC ON CACHE BOOL "" FORCE)
        set(libADLMIDI_SHARED OFF CACHE BOOL "" FORCE)
        set(WITH_MIDI_SEQUENCER ON CACHE BOOL "" FORCE)
        set(WITH_EMBEDDED_BANKS ON CACHE BOOL "" FORCE)
        set(USE_NUKED_EMULATOR ON CACHE BOOL "" FORCE)
        set(USE_DOSBOX_EMULATOR ON CACHE BOOL "" FORCE)
        foreach(emulator OPAL JAVA ESFMU MAME YMFM NUKED_OPL2_LLE NUKED_OPL3_LLE)
            set(USE_${emulator}_EMULATOR OFF CACHE BOOL "" FORCE)
        endforeach()
        set(USE_HW_SERIAL OFF CACHE BOOL "" FORCE)
        add_subdirectory("${MP_THIRD_PARTY}/src/libadlmidi" "${CMAKE_BINARY_DIR}/libadlmidi" EXCLUDE_FROM_ALL)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(ADLMIDI_static PRIVATE -w) # codice di terzi
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/AdlMidiDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_ADLMIDI)
        target_link_libraries(megaplayer_core PUBLIC ADLMIDI_static)
        set(MP_ADLMIDI_ENABLED ON)
    endif()
endif()

# Le dipendenze compilate a parte (ExternalProject) con lo stesso compilatore
# e la stessa piattaforma del core.
set(MP_EXTERNAL_TOOLCHAIN_ARGS)
foreach(variable CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_MAKE_PROGRAM
        CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_ARCHITECTURES)
    if(${variable})
        list(APPEND MP_EXTERNAL_TOOLCHAIN_ARGS "-D${variable}=${${variable}}")
    endif()
endforeach()

# 88emu di gearmulator (The Usual Suspects): i moduli Roland, emulati
# facendo girare il loro firmware. Si compila a parte, con il suo CMake e solo
# quello che serve a 88lib, in un'unica libreria statica (il target
# 88emu_bundle) da usare con c_interface.h. Gli eventi li ordina il sequencer
# di libADLMIDI. Le ROM le fornisce l'utente; senza, i MIDI restano ad adlmidi.
set(MP_88EMU_ENABLED OFF)
if(MP_WITH_88EMU AND MP_ADLMIDI_ENABLED AND NOT EMSCRIPTEN)
    set(MP_GEARMULATOR "${MP_THIRD_PARTY}/src/gearmulator")
    if(NOT EXISTS "${MP_GEARMULATOR}/CMakeLists.txt")
        message(WARNING "gearmulator non scaricato (scripts/build-deps.sh): backend 88emu disattivato")
    else()
        include(ExternalProject)
        set(MP_88EMU_BINARY "${CMAKE_BINARY_DIR}/gearmulator")
        set(MP_88EMU_LIBRARY "${MP_88EMU_BINARY}/source/ronaldo/88emu/88lib/lib88emu.a")
        set(MP_88EMU_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -Dgearmulator_BUILD_JUCEPLUGIN=OFF -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF
            -Dgearmulator_COMPONENT_DSPBRIDGE=OFF -Dgearmulator_SYNTH_88EMU=ON)
        foreach(synth OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086)
            list(APPEND MP_88EMU_ARGS -Dgearmulator_SYNTH_${synth}=OFF)
        endforeach()
        list(APPEND MP_88EMU_ARGS ${MP_EXTERNAL_TOOLCHAIN_ARGS})
        ExternalProject_Add(gearmulator_88emu
            SOURCE_DIR "${MP_GEARMULATOR}"
            BINARY_DIR "${MP_88EMU_BINARY}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            CMAKE_ARGS ${MP_88EMU_ARGS}
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target 88emu_bundle
            BUILD_BYPRODUCTS "${MP_88EMU_LIBRARY}"
            INSTALL_COMMAND "")
        add_library(emu88 STATIC IMPORTED GLOBAL)
        set_target_properties(emu88 PROPERTIES IMPORTED_LOCATION "${MP_88EMU_LIBRARY}")
        target_include_directories(emu88 INTERFACE "${MP_GEARMULATOR}/source/ronaldo/88emu/88lib")
        add_dependencies(emu88 gearmulator_88emu)
        find_package(Threads REQUIRED)
        target_link_libraries(emu88 INTERFACE Threads::Threads)
        if(APPLE)
            target_link_libraries(emu88 INTERFACE "-framework CoreFoundation")
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/Emu88Decoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_88EMU)
        target_link_libraries(megaplayer_core PUBLIC emu88)
        set(MP_88EMU_ENABLED ON)
    endif()
endif()

# FluidSynth: i MIDI su un SoundFont, per chi non ha le ROM dei moduli Roland.
# Si compila a parte, come libreria statica e con spento tutto quello che il
# suo CMake cercherebbe sulla macchina (driver audio e MIDI, rete,
# libsndfile, OpenMP...): qui serve solo il sintetizzatore. Il SoundFont che
# viaggia con le app è GeneralUser GS; il core lo prende da third_party
# finché l'app non gliene indica un altro (mp_set_soundfont).
set(MP_FLUIDSYNTH_ENABLED OFF)
if(MP_WITH_FLUIDSYNTH AND MP_ADLMIDI_ENABLED AND NOT EMSCRIPTEN)
    set(MP_FLUIDSYNTH_SOURCE "${MP_THIRD_PARTY}/src/fluidsynth")
    if(NOT EXISTS "${MP_FLUIDSYNTH_SOURCE}/gcem/CMakeLists.txt")
        message(WARNING "FluidSynth non scaricato (scripts/build-deps.sh): backend fluidsynth disattivato")
    else()
        include(ExternalProject)
        set(MP_FLUIDSYNTH_BINARY "${CMAKE_BINARY_DIR}/fluidsynth")
        # Su Windows FluidSynth mette nel nome la versione dell'ABI.
        if(WIN32)
            set(MP_FLUIDSYNTH_LIBRARY "${MP_FLUIDSYNTH_BINARY}/src/libfluidsynth-3.a")
        else()
            set(MP_FLUIDSYNTH_LIBRARY "${MP_FLUIDSYNTH_BINARY}/src/libfluidsynth.a")
        endif()
        set(MP_FLUIDSYNTH_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            ${MP_EXTERNAL_TOOLCHAIN_ARGS})
        foreach(feature alsa aufile dbus ipv6 jack ladspa libsndfile midishare network oss dsound wasapi waveout
                winmidi sdl3 pulseaudio pipewire readline openmp native-dls signalsmith systemd coreaudio coremidi
                framework dart kai libinstpatch portaudio opensles oboe)
            list(APPEND MP_FLUIDSYNTH_ARGS -Denable-${feature}=OFF)
        endforeach()
        ExternalProject_Add(fluidsynth_static
            SOURCE_DIR "${MP_FLUIDSYNTH_SOURCE}"
            BINARY_DIR "${MP_FLUIDSYNTH_BINARY}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            CMAKE_ARGS ${MP_FLUIDSYNTH_ARGS}
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target libfluidsynth
            BUILD_BYPRODUCTS "${MP_FLUIDSYNTH_LIBRARY}"
            INSTALL_COMMAND "")
        # fluidsynth.h lo genera la sua configurazione: la cartella deve
        # esserci già quando si configura il core.
        file(MAKE_DIRECTORY "${MP_FLUIDSYNTH_BINARY}/include")
        add_library(fluidsynth STATIC IMPORTED GLOBAL)
        set_target_properties(fluidsynth PROPERTIES IMPORTED_LOCATION "${MP_FLUIDSYNTH_LIBRARY}")
        target_include_directories(fluidsynth INTERFACE "${MP_FLUIDSYNTH_SOURCE}/include" "${MP_FLUIDSYNTH_BINARY}/include")
        target_compile_definitions(fluidsynth INTERFACE FLUIDSYNTH_NOT_A_DLL)
        add_dependencies(fluidsynth fluidsynth_static)
        find_package(Threads REQUIRED)
        target_link_libraries(fluidsynth INTERFACE Threads::Threads)
        if(NOT WIN32 AND NOT APPLE)
            target_link_libraries(fluidsynth INTERFACE m)
        endif()
        target_sources(megaplayer_core PRIVATE src/decoders/FluidSynthDecoder.cpp)
        set(MP_DEFAULT_SOUNDFONT "${MP_THIRD_PARTY}/src/generaluser-gs/GeneralUser-GS.sf2")
        set_source_files_properties(src/decoders/FluidSynthDecoder.cpp PROPERTIES
            COMPILE_DEFINITIONS "MP_DEFAULT_SOUNDFONT=\"${MP_DEFAULT_SOUNDFONT}\"")
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_FLUIDSYNTH)
        target_link_libraries(megaplayer_core PUBLIC fluidsynth)
        set(MP_FLUIDSYNTH_ENABLED ON)
    endif()
endif()

# Il sequencer di libADLMIDI per i backend MIDI con un sintetizzatore
# esterno, compilato una volta sola e con un altro nome di classe.
if(MP_88EMU_ENABLED OR MP_FLUIDSYNTH_ENABLED)
    target_sources(megaplayer_core PRIVATE src/decoders/SequencedMidiDecoder.cpp)
    target_include_directories(megaplayer_core PRIVATE "${MP_THIRD_PARTY}/src/libadlmidi/src")
endif()

set(MP_ADPLUG_ENABLED OFF)
if(MP_WITH_ADPLUG)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/adplug/configure.ac" OR NOT EXISTS "${MP_THIRD_PARTY}/src/libbinio/configure.ac")
        message(WARNING "AdPlug non scaricato (scripts/build-deps.sh): backend adplug disattivato")
    else()
        include(cmake/AdPlug.cmake)
        target_sources(megaplayer_core PRIVATE src/decoders/AdplugDecoder.cpp)
        target_compile_definitions(megaplayer_core PRIVATE MP_WITH_ADPLUG)
        target_link_libraries(megaplayer_core PUBLIC adplug)
        set(MP_ADPLUG_ENABLED ON)
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

# --- Libreria (indice SQLite) ---

set(MP_LIBRARY_ENABLED OFF)
if(MP_WITH_LIBRARY AND NOT EMSCRIPTEN)
    if(NOT EXISTS "${MP_THIRD_PARTY}/src/sqlite/sqlite3.c")
        message(WARNING "SQLite non scaricato (scripts/build-deps.sh): libreria disattivata")
    else()
        # L'amalgamation, senza estensioni caricabili (niente dlopen) e senza
        # le stringhe tra virgolette doppie, un vecchio errore di SQL.
        add_library(sqlite3 STATIC "${MP_THIRD_PARTY}/src/sqlite/sqlite3.c")
        # Solo l'header: la cartella dell'archivio ha anche un file VERSION,
        # che sui dischi senza maiuscole passerebbe per <version> del C++.
        configure_file("${MP_THIRD_PARTY}/src/sqlite/sqlite3.h" "${CMAKE_BINARY_DIR}/sqlite-include/sqlite3.h" COPYONLY)
        target_include_directories(sqlite3 SYSTEM PUBLIC "${CMAKE_BINARY_DIR}/sqlite-include")
        target_compile_definitions(sqlite3 PRIVATE SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(sqlite3 PRIVATE -w) # codice di terzi
        endif()
        find_package(Threads REQUIRED)
        target_link_libraries(sqlite3 PUBLIC Threads::Threads)
        target_sources(megaplayer_core PRIVATE src/library/Library.cpp src/library/M3u.cpp)
        target_compile_definitions(megaplayer_core PUBLIC MP_WITH_LIBRARY)
        target_link_libraries(megaplayer_core PUBLIC sqlite3)
        set(MP_LIBRARY_ENABLED ON)
    endif()
endif()

# --- CLI ---

add_executable(megaplayer src/cli/main.cpp src/cli/Terminal.cpp)
target_link_libraries(megaplayer PRIVATE megaplayer_core)

# In WebAssembly la CLI e i test girano con Node (ctest lo usa da solo): con
# accesso diretto ai file e main() in un thread, così il thread principale
# resta libero per le operazioni che i worker gli delegano.
set(MP_NODE_LINK_OPTIONS -sNODERAWFS=1 -sPROXY_TO_PTHREAD=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
if(EMSCRIPTEN)
    target_link_options(megaplayer PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()

if(NOT EMSCRIPTEN)
    # --- Libreria dinamica con l'API C, per l'app Flutter (app/) ---

    add_library(megaplayer_capi SHARED src/capi/megaplayer.cpp src/capi/Json.cpp)
    if(MP_LIBRARY_ENABLED)
        target_sources(megaplayer_capi PRIVATE src/capi/library.cpp)
    endif()
    target_link_libraries(megaplayer_capi PRIVATE megaplayer_core)
    set_target_properties(megaplayer_capi PROPERTIES
        OUTPUT_NAME megaplayer
        CXX_VISIBILITY_PRESET hidden
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_NAME_DIR ON)
else()
    # --- Modulo WebAssembly con l'API C, per il browser (web/) ---

    set(MP_WEB_EXPORTS
        _mp_set_uade_data_dir _mp_add_roland_rom_dir _mp_set_roland_device _mp_engine_roland_devices _mp_set_soundfont _mp_engine_soundfont _mp_engine_export _mp_engine_export_status _mp_engine_export_error _mp_engine_export_cancel _mp_can_export_mp3 _mp_engine_create _mp_engine_destroy _mp_engine_last_error
        _mp_engine_backends _mp_engine_collect _mp_engine_play _mp_engine_stop _mp_engine_set_paused
        _mp_engine_seek _mp_engine_select_subsong _mp_engine_set_volume _mp_engine_set_megasound
        _mp_engine_set_channel_muted _mp_engine_status _mp_engine_track_info _mp_engine_scope _malloc _free)
    list(JOIN MP_WEB_EXPORTS "," MP_WEB_EXPORTS)
    add_executable(megaplayer_web src/capi/megaplayer.cpp src/capi/Json.cpp)
    target_link_libraries(megaplayer_web PRIVATE megaplayer_core)
    target_link_options(megaplayer_web PRIVATE
        --no-entry
        -sMODULARIZE=1 -sEXPORT_NAME=createMegaplayer
        # Memoria fissa: le viste JavaScript sulla memoria restano valide.
        -sINITIAL_MEMORY=268435456
        "-sEXPORTED_FUNCTIONS=${MP_WEB_EXPORTS}"
        "-sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToNewUTF8,HEAPF32,HEAPF64,HEAP32"
        # I replayer di UADE nel filesystem virtuale, in /uade, e un brano
        # di prova che UADE distribuisce con i suoi sorgenti.
        "--preload-file=${MP_UADE_DATA_DIR}@/uade"
        "--preload-file=${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin@/demo/AHX.Cruisin")
    add_dependencies(megaplayer_web uade_data)
    foreach(file index.html megaplayer-web.js)
        configure_file("web/${file}" "${CMAKE_BINARY_DIR}/${file}" COPYONLY)
    endforeach()
endif()

# --- Test (ctest --test-dir build) ---

enable_testing()

add_executable(megasound_test tests/MegaSoundTest.cpp)
target_link_libraries(megasound_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(megasound_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
add_test(NAME megasound COMMAND megasound_test)

add_executable(audio_decoder_test tests/AudioDecoderTest.cpp)
target_link_libraries(audio_decoder_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(audio_decoder_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
add_test(NAME audio_decoders COMMAND audio_decoder_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

add_executable(tfmx_files_test tests/TfmxFilesTest.cpp)
target_link_libraries(tfmx_files_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(tfmx_files_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
add_test(NAME tfmx_files COMMAND tfmx_files_test)

# La CLI su un FLAC: deve esportarne il secondo che dura.
add_test(NAME cli_render_flac
    COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-flac.wav" "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tono.flac")
set_tests_properties(cli_render_flac PROPERTIES PASS_REGULAR_EXPRESSION "Backend: +miniaudio.*Scritti 0:01")
if(MP_UADE_ENABLED)
    add_executable(uade_inprocess_test tests/UadeInProcessTest.cpp)
    target_link_libraries(uade_inprocess_test PRIVATE megaplayer_core)
    if(EMSCRIPTEN)
        target_link_options(uade_inprocess_test PRIVATE ${MP_NODE_LINK_OPTIONS})
    endif()
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

    add_test(NAME cli_render_megasound
        COMMAND megaplayer render -m 70 -t 5 -o "${CMAKE_BINARY_DIR}/test-megasound.wav"
                "${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin")
    set_tests_properties(cli_render_megasound PROPERTIES
        PASS_REGULAR_EXPRESSION "MegaSound: +70%.*Scritti 0:05")
endif()

# I brani di prova di game-music-emu: un NSF e un VGM compresso con gzip, che
# arriva a gme già decompresso. Il VGM va esportato tutto: deve finire alla
# durata dichiarata.
if(MP_GME_ENABLED)
    add_test(NAME cli_render_nsf
        COMMAND megaplayer render -t 5 -o "${CMAKE_BINARY_DIR}/test-nsf.wav"
                "${MP_THIRD_PARTY}/src/game-music-emu/test.nsf")
    set_tests_properties(cli_render_nsf PROPERTIES PASS_REGULAR_EXPRESSION "Backend: +gme.*Scritti 0:05")
    add_test(NAME cli_render_vgz
        COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-vgz.wav"
                "${MP_THIRD_PARTY}/src/game-music-emu/test.vgz")
    set_tests_properties(cli_render_vgz PROPERTIES
        PASS_REGULAR_EXPRESSION "Packer: +gzip.*Backend: +gme.*Durata: +1:37.*Scritti 1:37")
endif()

# I brani d'esempio di AtariAudio: un SNDH compresso con ICE!, che arriva
# già decompresso da ancient, con più sottobrani, e un YM compresso con LHA,
# da suonare com'è e non come un archivio (sarebbe "... > SYNTHD7.BIN").
if(MP_ATARI_ENABLED)
    set(MP_ATARI_SONGS "${MP_THIRD_PARTY}/src/atariaudio/example/AtariAudio2Wav/musics")
    add_test(NAME cli_render_sndh
        COMMAND megaplayer render -s 3 -t 5 -o "${CMAKE_BINARY_DIR}/test-sndh.wav" "${MP_ATARI_SONGS}/Enchanted_Land.sndh")
    set_tests_properties(cli_render_sndh PROPERTIES
        PASS_REGULAR_EXPRESSION "Packer: +ICE.*Backend: +atariaudio.*Sottobrano: 3 di 11.*Scritti 0:05")
    add_test(NAME cli_render_ym
        COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-ym.wav" "${MP_ATARI_SONGS}/synthdream7.ym")
    set_tests_properties(cli_render_ym PROPERTIES
        PASS_REGULAR_EXPRESSION "Backend: +atariaudio.*Formato: +YM 5.*Durata: +1:03.*Scritti 1:03"
        FAIL_REGULAR_EXPRESSION " > ")
endif()

# Un PSID costruito dal test stesso, poi esportato dalla CLI.
if(MP_SID_ENABLED)
    add_executable(sid_decoder_test tests/SidDecoderTest.cpp)
    target_link_libraries(sid_decoder_test PRIVATE megaplayer_core)
    if(EMSCRIPTEN)
        target_link_options(sid_decoder_test PRIVATE ${MP_NODE_LINK_OPTIONS})
    endif()
    add_test(NAME sid_decoder COMMAND sid_decoder_test "${CMAKE_BINARY_DIR}/test.sid")
    set_tests_properties(sid_decoder PROPERTIES FIXTURES_SETUP test_sid)
    add_test(NAME cli_render_sid
        COMMAND megaplayer render -t 5 -o "${CMAKE_BINARY_DIR}/test-sid.wav" "${CMAKE_BINARY_DIR}/test.sid")
    set_tests_properties(cli_render_sid PROPERTIES
        FIXTURES_REQUIRED test_sid PASS_REGULAR_EXPRESSION "Backend: +sid.*Sottobrano: 1 di 2.*Scritti 0:05")
endif()

# La libreria sui toni di tests/data, e l'API C con la scansione in
# background (compilata nel test, che non usa la libreria dinamica).
if(MP_LIBRARY_ENABLED)
    add_executable(library_test tests/LibraryTest.cpp src/capi/library.cpp src/capi/Json.cpp)
    target_link_libraries(library_test PRIVATE megaplayer_core)
    set(MP_LIBRARY_TEST_ARGS "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    if(LIBARCHIVE_FOUND)
        # Lo zip di prova lo crea cmake -E tar.
        target_compile_definitions(library_test PRIVATE MP_TEST_ARCHIVES)
        list(APPEND MP_LIBRARY_TEST_ARGS "${CMAKE_COMMAND}")
    endif()
    add_test(NAME library COMMAND library_test ${MP_LIBRARY_TEST_ARGS})
endif()

# Un MIDI e un IMF costruiti dal test stesso, poi esportati dalla CLI.
if(MP_ADLMIDI_ENABLED OR MP_ADPLUG_ENABLED)
    add_executable(opl_decoders_test tests/OplDecodersTest.cpp)
    target_link_libraries(opl_decoders_test PRIVATE megaplayer_core)
    if(MP_ADLMIDI_ENABLED)
        target_compile_definitions(opl_decoders_test PRIVATE MP_TEST_ADLMIDI)
    endif()
    if(MP_ADPLUG_ENABLED)
        target_compile_definitions(opl_decoders_test PRIVATE MP_TEST_ADPLUG)
    endif()
    if(EMSCRIPTEN)
        target_link_options(opl_decoders_test PRIVATE ${MP_NODE_LINK_OPTIONS})
    endif()
    set(MP_OPL_TEST_ARGS "${CMAKE_BINARY_DIR}")
    if(MP_ADPLUG_ENABLED)
        list(APPEND MP_OPL_TEST_ARGS "${MP_THIRD_PARTY}/src/adplug/test/testmus/DUNE19.ADL")
    endif()
    add_test(NAME opl_decoders COMMAND opl_decoders_test ${MP_OPL_TEST_ARGS})
    set_tests_properties(opl_decoders PROPERTIES FIXTURES_SETUP test_opl)
    if(MP_ADLMIDI_ENABLED)
        add_test(NAME cli_render_midi
            COMMAND megaplayer render -b adlmidi -o "${CMAKE_BINARY_DIR}/test-midi.wav" "${CMAKE_BINARY_DIR}/test.mid")
        set_tests_properties(cli_render_midi PROPERTIES
            FIXTURES_REQUIRED test_opl PASS_REGULAR_EXPRESSION "Backend: +adlmidi.*Titolo: +Prova MIDI.*Scritti 0:0[23]")
    endif()
    if(MP_ADPLUG_ENABLED)
        add_test(NAME cli_render_imf
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-imf.wav" "${CMAKE_BINARY_DIR}/test.imf")
        set_tests_properties(cli_render_imf PROPERTIES
            FIXTURES_REQUIRED test_opl PASS_REGULAR_EXPRESSION "Backend: +adplug.*Scritti 0:02")
    endif()
endif()

# Un MOD generato da tests/make_test_mod.py, suonato da entrambi i backend.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME make_test_mod
        # Il percorso e non il target Python3::Interpreter: in cross-compilazione
        # CTest lancerebbe anche lui con l'emulatore (Node per WebAssembly).
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_mod.py" "${CMAKE_BINARY_DIR}/test.mod")
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

# I file PlayStation generati da tests/make_test_psf.py: un PSF il cui
# programma suona solo se la BIOS (OpenBIOS) è partita, un MiniPSF con la sua
# libreria, che ha un nome dalle maiuscole diverse, un XA con due canali, e
# per la PS2 un PSF2 il cui psf2.irx suona solo se i moduli IOP sono partiti e
# un MiniPSF2 che sostituisce il psf2.irx della sua libreria.
if(Python3_FOUND)
    set(MP_PSX_TEST_DIR "${CMAKE_BINARY_DIR}/psx-test")
    add_test(NAME make_test_psf
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_psf.py" "${MP_PSX_TEST_DIR}")
    set_tests_properties(make_test_psf PROPERTIES FIXTURES_SETUP test_psx)
    add_test(NAME cli_render_xa
        COMMAND megaplayer render -s 2 -o "${CMAKE_BINARY_DIR}/test-xa.wav" "${MP_PSX_TEST_DIR}/test.xa")
    set_tests_properties(cli_render_xa PROPERTIES FIXTURES_REQUIRED test_psx
        PASS_REGULAR_EXPRESSION "Backend: +xa.*Sottobrano: 2 di 2.*Durata: +0:32.*Scritti 0:32")
    if(MP_PSF_ENABLED)
        add_test(NAME cli_render_psf
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-psf.wav" "${MP_PSX_TEST_DIR}/test.psf")
        set_tests_properties(cli_render_psf PROPERTIES FIXTURES_REQUIRED test_psx
            PASS_REGULAR_EXPRESSION "Backend: +psf.*Titolo: +Onda quadra.*Durata: +0:06.*Scritti 0:06")
        add_test(NAME cli_render_minipsf
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-minipsf.wav" "${MP_PSX_TEST_DIR}/test.minipsf")
        set_tests_properties(cli_render_minipsf PROPERTIES FIXTURES_REQUIRED test_psx
            PASS_REGULAR_EXPRESSION "Formato: +MiniPSF.*Titolo: +Onda larga.*Scritti 0:06")
        add_test(NAME cli_render_psf2
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-psf2.wav" "${MP_PSX_TEST_DIR}/test.psf2")
        set_tests_properties(cli_render_psf2 PROPERTIES FIXTURES_REQUIRED test_psx
            PASS_REGULAR_EXPRESSION "Backend: +psf2.*Formato: +PSF2.*Titolo: +Onda quadra.*Durata: +0:06.*Scritti 0:06")
        add_test(NAME cli_render_minipsf2
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-minipsf2.wav" "${MP_PSX_TEST_DIR}/test.minipsf2")
        set_tests_properties(cli_render_minipsf2 PROPERTIES FIXTURES_REQUIRED test_psx
            PASS_REGULAR_EXPRESSION "Formato: +MiniPSF2.*Titolo: +Onda larga.*Scritti 0:06")
    endif()
endif()

# Gli stream generati da tests/make_test_streams.py: un GENH in PS-ADPCM col
# loop, uno in PCM mono e un .int senza intestazione, controllati campione
# per campione dal test e suonati dalla CLI; un VAG e uno SVAG col loop, solo
# dalla CLI (la durata dice che il loop c'è: due giri e la dissolvenza).
if(Python3_FOUND AND MP_VGMSTREAM_ENABLED)
    set(MP_STREAMS_TEST_DIR "${CMAKE_BINARY_DIR}/streams-test")
    add_test(NAME make_test_streams
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_streams.py" "${MP_STREAMS_TEST_DIR}")
    set_tests_properties(make_test_streams PROPERTIES FIXTURES_SETUP test_streams)
    add_executable(vgmstream_decoder_test tests/VgmstreamDecoderTest.cpp)
    target_link_libraries(vgmstream_decoder_test PRIVATE megaplayer_core)
    if(EMSCRIPTEN)
        target_link_options(vgmstream_decoder_test PRIVATE ${MP_NODE_LINK_OPTIONS})
    endif()
    add_test(NAME vgmstream_decoder COMMAND vgmstream_decoder_test "${MP_STREAMS_TEST_DIR}")
    set_tests_properties(vgmstream_decoder PROPERTIES FIXTURES_REQUIRED test_streams)
    if(MP_FFMPEG_ENABLED)
        # Con FFmpeg dentro, vgmstream decodifica anche l'Opus dei giochi:
        # qui un Ogg Opus qualunque, chiesto a lui invece che al backend ffmpeg.
        add_test(NAME cli_vgmstream_ffmpeg
            COMMAND megaplayer render -b vgmstream -o "${CMAKE_BINARY_DIR}/test-vgmstream-opus.wav"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tono.opus")
        set_tests_properties(cli_vgmstream_ffmpeg PROPERTIES
            PASS_REGULAR_EXPRESSION "Backend: +vgmstream.*Formato: +Ogg Opus header.*Scritti 0:01")
    endif()
    add_test(NAME cli_render_genh
        COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-genh.wav" "${MP_STREAMS_TEST_DIR}/test.genh")
    set_tests_properties(cli_render_genh PROPERTIES FIXTURES_REQUIRED test_streams
        PASS_REGULAR_EXPRESSION "Backend: +vgmstream.*Formato: +GENH.*Durata: +0:15.*Scritti 0:15")
    add_test(NAME cli_render_int
        COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-int.wav" "${MP_STREAMS_TEST_DIR}/test.int")
    set_tests_properties(cli_render_int PROPERTIES FIXTURES_REQUIRED test_streams
        PASS_REGULAR_EXPRESSION "Backend: +vgmstream.*Formato: +PS2 \\.int.*Durata: +0:02.*Scritti 0:02")
    foreach(entry "vag:Sony VAG:0:13" "svag:Konami SVAG:0:18")
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 extension)
        list(GET parts 1 format)
        list(GET parts 2 minutes)
        list(GET parts 3 seconds)
        add_test(NAME cli_render_${extension}
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-${extension}.wav" "${MP_STREAMS_TEST_DIR}/test.${extension}")
        set_tests_properties(cli_render_${extension} PROPERTIES FIXTURES_REQUIRED test_streams
            PASS_REGULAR_EXPRESSION "Backend: +vgmstream.*Formato: +${format}.*Durata: +${minutes}:${seconds}.*Scritti ${minutes}:${seconds}")
    endforeach()
endif()

# I file di Saturn, Dreamcast, QSound, GBA e DS generati da
# tests/make_test_xsf.py: ognuno un programma per la CPU del suono che suona
# un'onda quadra sul chip.
if(Python3_FOUND AND (MP_SSF_ENABLED OR MP_QSF_ENABLED OR MP_GSF_ENABLED OR MP_2SF_ENABLED))
    set(MP_XSF_TEST_DIR "${CMAKE_BINARY_DIR}/xsf-test")
    add_test(NAME make_test_xsf
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_test_xsf.py" "${MP_XSF_TEST_DIR}")
    set_tests_properties(make_test_xsf PROPERTIES FIXTURES_SETUP test_xsf)
    set(MP_XSF_TESTS)
    if(MP_SSF_ENABLED)
        list(APPEND MP_XSF_TESTS "ssf:ssf:SSF" "dsf:ssf:DSF")
    endif()
    if(MP_QSF_ENABLED)
        list(APPEND MP_XSF_TESTS "qsf:qsf:QSF")
    endif()
    if(MP_GSF_ENABLED)
        list(APPEND MP_XSF_TESTS "gsf:gsf:GSF")
    endif()
    if(MP_2SF_ENABLED)
        list(APPEND MP_XSF_TESTS "2sf:2sf:2SF")
    endif()
    foreach(entry ${MP_XSF_TESTS})
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 extension)
        list(GET parts 1 backend)
        list(GET parts 2 format)
        add_test(NAME cli_render_${extension}
            COMMAND megaplayer render -o "${CMAKE_BINARY_DIR}/test-${extension}.wav" "${MP_XSF_TEST_DIR}/test.${extension}")
        set_tests_properties(cli_render_${extension} PROPERTIES FIXTURES_REQUIRED test_xsf
            PASS_REGULAR_EXPRESSION "Backend: +${backend}.*Formato: +${format}.*Titolo: +Onda quadra.*Scritti 0:06")
    endforeach()
endif()

# Il mute dei canali, con un brano di prova per ogni backend che lo permette.
# L'esportazione su un tono dei dati di prova (e sul modulo generato, per il
# mute, se c'è libopenmpt).
add_executable(export_test tests/ExportTest.cpp)
target_link_libraries(export_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(export_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
set(MP_EXPORT_TEST_ARGS "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tono.flac")
if(OPENMPT_FOUND AND Python3_FOUND)
    list(APPEND MP_EXPORT_TEST_ARGS "${CMAKE_BINARY_DIR}/test.mod")
endif()
add_test(NAME export COMMAND export_test ${MP_EXPORT_TEST_ARGS})
if(OPENMPT_FOUND AND Python3_FOUND)
    set_tests_properties(export PROPERTIES FIXTURES_REQUIRED test_mod)
endif()

add_executable(roland_devices_test tests/RolandDevicesTest.cpp)
target_link_libraries(roland_devices_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(roland_devices_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
add_test(NAME roland_devices COMMAND roland_devices_test)

add_executable(channels_test tests/ChannelsTest.cpp)
target_link_libraries(channels_test PRIVATE megaplayer_core)
if(EMSCRIPTEN)
    target_link_options(channels_test PRIVATE ${MP_NODE_LINK_OPTIONS})
endif()
set(MP_CHANNELS_TEST_ARGS "nessuno=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tono.flac")
set(MP_CHANNELS_FIXTURES)
if(OPENMPT_FOUND AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "openmpt=${CMAKE_BINARY_DIR}/test.mod")
    list(APPEND MP_CHANNELS_FIXTURES test_mod)
endif()
if(MP_UADE_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "uade=${MP_THIRD_PARTY}/src/uade/songs/AHX.Cruisin")
endif()
if(MP_GME_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "gme=${MP_THIRD_PARTY}/src/game-music-emu/test.nsf")
endif()
if(MP_SID_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "sid=${CMAKE_BINARY_DIR}/test.sid")
    list(APPEND MP_CHANNELS_FIXTURES test_sid)
endif()
if(MP_ATARI_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "atariaudio=${MP_ATARI_SONGS}/Enchanted_Land.sndh")
endif()
if(MP_PSF_ENABLED AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "psf=${MP_PSX_TEST_DIR}/test.psf" "psf2=${MP_PSX_TEST_DIR}/test.psf2")
    list(APPEND MP_CHANNELS_FIXTURES test_psx)
endif()
if(MP_SSF_ENABLED AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "ssf=${MP_XSF_TEST_DIR}/test.ssf" "ssf=${MP_XSF_TEST_DIR}/test.dsf")
    list(APPEND MP_CHANNELS_FIXTURES test_xsf)
endif()
if(MP_QSF_ENABLED AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "qsf=${MP_XSF_TEST_DIR}/test.qsf")
    list(APPEND MP_CHANNELS_FIXTURES test_xsf)
endif()
if(MP_GSF_ENABLED AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "gsf=${MP_XSF_TEST_DIR}/test.gsf")
    list(APPEND MP_CHANNELS_FIXTURES test_xsf)
endif()
if(MP_2SF_ENABLED AND Python3_FOUND)
    list(APPEND MP_CHANNELS_TEST_ARGS "2sf=${MP_XSF_TEST_DIR}/test.2sf")
    list(APPEND MP_CHANNELS_FIXTURES test_xsf)
endif()
if(MP_ADLMIDI_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "adlmidi=${CMAKE_BINARY_DIR}/test.mid")
    list(APPEND MP_CHANNELS_FIXTURES test_opl)
endif()
if(MP_ADPLUG_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "adplug=${CMAKE_BINARY_DIR}/test.imf")
    list(APPEND MP_CHANNELS_FIXTURES test_opl)
endif()
# FluidSynth con il SoundFont di serie, che build-deps scarica.
if(MP_FLUIDSYNTH_ENABLED)
    list(APPEND MP_CHANNELS_TEST_ARGS "fluidsynth=${CMAKE_BINARY_DIR}/test.mid")
endif()
# 88emu suona solo con le ROM, che non sono nel repo: si prova se
# MEGAPLAYER_ROLAND_ROMS (o MP_TEST_ROLAND_ROMS) indica una cartella che le ha.
set(MP_TEST_ROLAND_ROMS "$ENV{MEGAPLAYER_ROLAND_ROMS}" CACHE PATH "Cartella con le ROM dei moduli Roland per i test di 88emu")
if(MP_88EMU_ENABLED AND MP_TEST_ROLAND_ROMS)
    list(APPEND MP_CHANNELS_TEST_ARGS "roms=${MP_TEST_ROLAND_ROMS}" "88emu=${CMAKE_BINARY_DIR}/test.mid")
endif()
add_test(NAME channels COMMAND channels_test ${MP_CHANNELS_TEST_ARGS})
if(MP_CHANNELS_FIXTURES)
    list(REMOVE_DUPLICATES MP_CHANNELS_FIXTURES)
    set_tests_properties(channels PROPERTIES FIXTURES_REQUIRED "${MP_CHANNELS_FIXTURES}")
endif()
