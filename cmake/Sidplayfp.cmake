# libsidplayfp con l'emulatore reSIDfp (libresidfp), compilati dai sorgenti
# senza i loro script autotools: qui si generano gli header che scriverebbe
# configure. Restano fuori i SID hardware (exSID, USBSID) e STILview.
# Definisce i target `residfp` e `sidplayfp`.

set(MP_RESIDFP_DIR "${MP_THIRD_PARTY}/src/libresidfp")
set(MP_SIDPLAYFP_DIR "${MP_THIRD_PARTY}/src/libsidplayfp")
set(MP_SID_GENERATED "${CMAKE_BINARY_DIR}/sid-generated")

# La versione dalle prime righe di configure.ac: m4_define([lib_major], [3]).
function(mp_autoconf_version configure prefix)
    file(STRINGS "${configure}" lines REGEX "^m4_define\\(\\[lib_(major|minor|level)\\]")
    foreach(line ${lines})
        string(REGEX MATCH "lib_([a-z]+)\\], \\[([0-9]+)" _ "${line}")
        string(TOUPPER "${CMAKE_MATCH_1}" part)
        set(${prefix}_${part} "${CMAKE_MATCH_2}" PARENT_SCOPE)
    endforeach()
endfunction()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(MP_SID_BUILTIN_EXPECT 1)
    set(MP_SID_WARNINGS -w) # codice di terzi
else()
    set(MP_SID_BUILTIN_EXPECT 0)
    set(MP_SID_WARNINGS)
endif()

# --- libresidfp ---

mp_autoconf_version("${MP_RESIDFP_DIR}/configure.ac" RESIDFP)
set(LIB_MAJOR ${RESIDFP_MAJOR})
set(LIB_MINOR ${RESIDFP_MINOR})
set(LIB_LEVEL ${RESIDFP_LEVEL})
set(PACKAGE_VERSION "${RESIDFP_MAJOR}.${RESIDFP_MINOR}.${RESIDFP_LEVEL}")
set(HAVE_BUILTIN_EXPECT ${MP_SID_BUILTIN_EXPECT})
set(RESIDFP_BRANCH_HINTS 1)
set(RESIDFP_INLINING 1)
set(RESIDFP_INLINE inline)
configure_file("${MP_RESIDFP_DIR}/src/siddefs-fp.h.in" "${MP_SID_GENERATED}/residfp-src/siddefs-fp.h" @ONLY)
configure_file("${MP_RESIDFP_DIR}/src/residfp/sidversion.h.in"
               "${MP_SID_GENERATED}/residfp-include/residfp/sidversion.h" @ONLY)
# Ai suoi utenti libresidfp mostra solo l'API pubblica: i suoi header interni
# (SID.h, Filter.h...) si scontrerebbero con quelli di libsidplayfp.
foreach(header residfp.h residfp_defs.h)
    configure_file("${MP_RESIDFP_DIR}/src/residfp/${header}"
                   "${MP_SID_GENERATED}/residfp-include/residfp/${header}" COPYONLY)
endforeach()

file(GLOB MP_RESIDFP_SOURCES "${MP_RESIDFP_DIR}/src/*.cpp")
add_library(residfp STATIC
    ${MP_RESIDFP_SOURCES}
    "${MP_RESIDFP_DIR}/src/resample/SincResampler.cpp"
    "${MP_RESIDFP_DIR}/src/version.cc"
    "${MP_RESIDFP_DIR}/src/residfp/residfp.cpp")
target_include_directories(residfp
    PUBLIC "${MP_SID_GENERATED}/residfp-include"
    PRIVATE "${MP_SID_GENERATED}/residfp-src" "${MP_RESIDFP_DIR}/src" "${MP_RESIDFP_DIR}/src/residfp")
target_compile_options(residfp PRIVATE ${MP_SID_WARNINGS})
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(residfp PRIVATE Threads::Threads)
endif()
if(WIN32)
    target_compile_definitions(residfp PRIVATE WIN32_LEAN_AND_MEAN)
endif()

# --- libsidplayfp ---

mp_autoconf_version("${MP_SIDPLAYFP_DIR}/configure.ac" SIDPLAYFP)
set(LIB_MAJOR ${SIDPLAYFP_MAJOR})
set(LIB_MINOR ${SIDPLAYFP_MINOR})
set(LIB_LEVEL ${SIDPLAYFP_LEVEL})
set(MP_SIDPLAYFP_VERSION "${SIDPLAYFP_MAJOR}.${SIDPLAYFP_MINOR}.${SIDPLAYFP_LEVEL}")
configure_file("${MP_SIDPLAYFP_DIR}/src/sidplayfp/sidversion.h.in"
               "${MP_SID_GENERATED}/sidplayfp/sidplayfp/sidversion.h" @ONLY)
configure_file("${MP_SIDPLAYFP_DIR}/src/builders/sidlite-builder/sidlite/sl_defs.h.in"
               "${MP_SID_GENERATED}/sidplayfp/sl_defs.h" @ONLY)
file(WRITE "${MP_SID_GENERATED}/sidplayfp/config.h.new"
"/* Generato da cmake/Sidplayfp.cmake al posto di configure. */
#define HAVE_CXX11 1
#define HAVE_CXX14 1
#define HAVE_CXX17 1
#define HAVE_CXX20 1
#define HAVE_RESIDFP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define SIZEOF_INT 4
#define SIZEOF_SHORT 2
#define PACKAGE_NAME \"libsidplayfp\"
#define PACKAGE_VERSION \"${MP_SIDPLAYFP_VERSION}\"
#define PACKAGE_STRING \"libsidplayfp ${MP_SIDPLAYFP_VERSION}\"
#define PACKAGE_URL \"https://github.com/libsidplayfp/libsidplayfp/\"
#define VERSION \"${MP_SIDPLAYFP_VERSION}\"
")
# Riscritto solo se cambia, per non ricompilare tutto a ogni configurazione.
file(COPY_FILE "${MP_SID_GENERATED}/sidplayfp/config.h.new" "${MP_SID_GENERATED}/sidplayfp/config.h" ONLY_IF_DIFFERENT)

set(MP_SIDPLAYFP_SRC "${MP_SIDPLAYFP_DIR}/src")
add_library(sidplayfp STATIC
    "${MP_SIDPLAYFP_SRC}/EventScheduler.cpp"
    "${MP_SIDPLAYFP_SRC}/player.cpp"
    "${MP_SIDPLAYFP_SRC}/psiddrv.cpp"
    "${MP_SIDPLAYFP_SRC}/reloc65.cpp"
    "${MP_SIDPLAYFP_SRC}/simpleMixer.cpp"
    "${MP_SIDPLAYFP_SRC}/sidemu.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/c64.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/mmu.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/VIC_II/mos656x.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CPU/mos6510.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CPU/mos6510debug.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CIA/interrupt.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CIA/mos652x.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CIA/SerialPort.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CIA/timer.cpp"
    "${MP_SIDPLAYFP_SRC}/c64/CIA/tod.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/sidplayfp.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/sidbuilder.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/SidConfig.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/SidInfo.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/SidTune.cpp"
    "${MP_SIDPLAYFP_SRC}/sidplayfp/SidTuneInfo.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/MUS.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/p00.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/prg.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/PSID.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/SidTuneBase.cpp"
    "${MP_SIDPLAYFP_SRC}/sidtune/SidTuneTools.cpp"
    "${MP_SIDPLAYFP_SRC}/utils/iniParser.cpp"
    "${MP_SIDPLAYFP_SRC}/utils/SidDatabase.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/residfp-builder/residfp-builder.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/residfp-builder/residfp-emu.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite-builder.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite-emu.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite/ADSR.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite/Filter.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite/SID.cpp"
    "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite/WavGen.cpp")
# Come gli AM_CPPFLAGS di libsidplayfp; i suoi header pubblici si includono
# come <sidplayfp/sidplayfp.h> e <sidplayfp/builders/residfp.h>.
target_include_directories(sidplayfp
    PUBLIC "${MP_SIDPLAYFP_SRC}" "${MP_SID_GENERATED}/sidplayfp"
    PRIVATE "${MP_SIDPLAYFP_SRC}/sidtune" "${MP_SIDPLAYFP_SRC}/builders/residfp-builder"
            "${MP_SIDPLAYFP_SRC}/builders/sidlite-builder/sidlite")
target_compile_definitions(sidplayfp PRIVATE HAVE_CONFIG_H)
target_compile_options(sidplayfp PRIVATE ${MP_SID_WARNINGS})
target_link_libraries(sidplayfp PUBLIC residfp)
if(WIN32)
    target_compile_definitions(sidplayfp PRIVATE WIN32_LEAN_AND_MEAN)
endif()
