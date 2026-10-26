# FFmpeg ridotto ai decoder audio che servono, compilato col suo configure
# dentro la build (ExternalProject), con lo stesso compilatore del resto: per
# il backend ffmpeg (M4A, ALAC, Opus, AIFF, WMA, APE, WavPack...) e per i
# codec degli stream dei giochi in vgmstream (ATRAC3, XMA, AAC, Opus). Senza
# --enable-gpl né --enable-nonfree, quindi LGPL; niente programmi, rete,
# filtri, video, thread né assembly x86 (che vorrebbe nasm).
#
# Definisce la libreria ffmpeg (avformat, avcodec, swresample, avutil) e
# MP_FFMPEG_ENABLED.

set(MP_FFMPEG_ENABLED OFF)
set(MP_FFMPEG_SOURCE "${MP_THIRD_PARTY}/src/ffmpeg")
if(NOT EXISTS "${MP_FFMPEG_SOURCE}/configure")
    message(WARNING "FFmpeg non scaricato (scripts/build-deps.sh): backend ffmpeg disattivato")
    return()
endif()
find_program(MP_SH NAMES sh bash)
find_program(MP_MAKE NAMES gmake make)
if(NOT MP_SH OR NOT MP_MAKE)
    message(WARNING "per FFmpeg servono sh e make: backend ffmpeg disattivato")
    return()
endif()

# I decoder: i formati del backend, i PCM e i codec dell'AIFF-C, e quelli che
# vgmstream chiede a FFmpeg.
set(MP_FFMPEG_DECODERS
    aac aac_latm alac opus vorbis flac mp3float mp3 ape wavpack tta mpc7 mpc8 tak
    wmav1 wmav2 wmapro wmalossless
    pcm_s8 pcm_u8 pcm_s16be pcm_s16le pcm_s24be pcm_s24le pcm_s32be pcm_s32le
    pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_alaw pcm_mulaw adpcm_ima_qt mace3 mace6
    atrac3 atrac3al atrac3p atrac3pal atrac9 xma1 xma2)
# I contenitori: quelli del backend; wav, xwma, aac e mp3 solo per vgmstream,
# che dà a FFmpeg intestazioni RIFF fatte da lui.
set(MP_FFMPEG_DEMUXERS
    mov ogg aiff matroska asf ape wv tta mpc mpc8 tak caf au
    wav xwma aac mp3)
set(MP_FFMPEG_PARSERS aac aac_latm flac mpegaudio opus vorbis tak)

string(REPLACE ";" "," decoders "${MP_FFMPEG_DECODERS}")
string(REPLACE ";" "," demuxers "${MP_FFMPEG_DEMUXERS}")
string(REPLACE ";" "," parsers "${MP_FFMPEG_PARSERS}")
set(prefix "${CMAKE_BINARY_DIR}/ffmpeg")
set(args
    --prefix=${prefix}
    --enable-static --disable-shared --enable-pic
    --disable-programs --disable-doc --disable-debug
    --disable-autodetect --disable-network --disable-everything
    --disable-avdevice --disable-avfilter --disable-swscale
    --enable-avcodec --enable-avformat --enable-swresample
    --disable-pthreads --disable-w32threads --disable-os2threads
    --enable-decoder=${decoders} --enable-demuxer=${demuxers} --enable-parser=${parsers})

if(EMSCRIPTEN)
    # Le pthread anche qui: il modulo usa la memoria condivisa.
    list(APPEND args --enable-cross-compile --target-os=none --arch=x86_32
        --cc=${CMAKE_C_COMPILER} --ar=${CMAKE_AR} --ranlib=${CMAKE_RANLIB} --nm=${CMAKE_NM}
        --disable-asm --disable-inline-asm --disable-stripping --extra-cflags=-pthread)
elseif(CMAKE_CROSSCOMPILING AND WIN32)
    # x86_64-w64-mingw32-gcc → x86_64-w64-mingw32-
    string(REGEX REPLACE "gcc(\\.exe)?$" "" cross "${CMAKE_C_COMPILER}")
    list(APPEND args --enable-cross-compile --target-os=mingw32 --arch=${CMAKE_SYSTEM_PROCESSOR}
        --cross-prefix=${cross} --disable-x86asm)
elseif(WIN32)
    # MSYS2: il gcc di MinGW che c'è nel PATH.
    list(APPEND args --disable-x86asm)
else()
    list(APPEND args --cc=${CMAKE_C_COMPILER} --disable-x86asm)
    if(APPLE)
        # Il compilatore di Xcode chiamato direttamente vuole l'SDK.
        set(flags)
        if(CMAKE_OSX_SYSROOT)
            list(APPEND flags -isysroot ${CMAKE_OSX_SYSROOT})
            # Anche per i programmi che FFmpeg compila e lancia durante la build.
            list(APPEND args "--host-cflags=-isysroot ${CMAKE_OSX_SYSROOT}"
                "--host-ldflags=-isysroot ${CMAKE_OSX_SYSROOT}")
        endif()
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND flags -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
        endif()
        if(flags)
            string(REPLACE ";" " " flags "${flags}")
            list(APPEND args "--extra-cflags=${flags}" "--extra-ldflags=${flags}")
        endif()
    endif()
endif()

set(libraries avformat avcodec swresample avutil)
set(byproducts)
foreach(library IN LISTS libraries)
    list(APPEND byproducts "${prefix}/lib/lib${library}.a")
endforeach()
include(ProcessorCount)
ProcessorCount(jobs)
if(jobs EQUAL 0)
    set(jobs 4)
endif()

include(ExternalProject)
ExternalProject_Add(ffmpeg_build
    SOURCE_DIR "${MP_FFMPEG_SOURCE}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/ffmpeg-build"
    CONFIGURE_COMMAND "${MP_SH}" "${MP_FFMPEG_SOURCE}/configure" ${args}
    BUILD_COMMAND "${MP_MAKE}" -j${jobs}
    INSTALL_COMMAND "${MP_MAKE}" install
    BUILD_BYPRODUCTS ${byproducts}
    USES_TERMINAL_BUILD ON)

file(MAKE_DIRECTORY "${prefix}/include")
add_library(ffmpeg INTERFACE)
add_dependencies(ffmpeg ffmpeg_build)
target_include_directories(ffmpeg SYSTEM INTERFACE "${prefix}/include")
target_link_libraries(ffmpeg INTERFACE ${byproducts})
if(WIN32)
    target_link_libraries(ffmpeg INTERFACE bcrypt) # av_random_bytes
elseif(UNIX)
    target_link_libraries(ffmpeg INTERFACE m)
endif()
set(MP_FFMPEG_ENABLED ON)
