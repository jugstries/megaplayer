# libuade e l'emulatore uadecore compilati in un'unica libreria statica, con
# uadecore in un thread invece che in un processo separato (vedi
# src/uade/InProcess.cpp e patches/uade-in-process.patch).
#
# Non usa lo script di configurazione né i Makefile di UADE, che funzionano
# solo su UNIX: gli header che generavano li scrive CMake, e libzakalwe e
# bencode-tools diventano librerie statiche. Servono solo i sorgenti scaricati
# da scripts/build-deps.sh.

include(CheckSymbolExists)
find_package(Threads REQUIRED)

set(UADE_ROOT "${MP_THIRD_PARTY}/src/uade")
set(UADE_SRC "${UADE_ROOT}/src")
set(ZAKALWE_ROOT "${MP_THIRD_PARTY}/src/libzakalwe")
set(BENCODE_ROOT "${MP_THIRD_PARTY}/src/bencodetools")
set(UADE_GEN "${CMAKE_CURRENT_BINARY_DIR}/uade-gen")
set(UADE_GEN_INCLUDE "${UADE_GEN}/include")
file(MAKE_DIRECTORY "${UADE_GEN_INCLUDE}/uade" "${UADE_GEN_INCLUDE}/zakalwe")

# Replayer e configurazione di UADE. La cartella si può spostare: le app la
# indicano a runtime (mp_set_uade_data_dir), questo è solo il valore
# predefinito per la CLI e i test.
set(MP_UADE_DATA_DIR "${CMAKE_BINARY_DIR}/uade-data")

# Scrive un file generato solo se cambia, per non ricompilare a ogni
# configurazione.
function(mp_write_if_changed path content)
    if(EXISTS "${path}")
        file(READ "${path}" old)
        if(old STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

# --- libzakalwe e bencode-tools ---

set(CMAKE_REQUIRED_DEFINITIONS -D_DEFAULT_SOURCE -D_GNU_SOURCE)
check_symbol_exists(reallocarray "stdlib.h" MP_HAVE_REALLOCARRAY)
check_symbol_exists(strlcpy "string.h" MP_HAVE_STRLCPY)
check_symbol_exists(memmem "string.h" MP_HAVE_MEMMEM)
check_symbol_exists(canonicalize_file_name "stdlib.h" MP_HAVE_CANONICALIZE_FILE_NAME)
unset(CMAKE_REQUIRED_DEFINITIONS)

set(zakalwe_config "#ifndef _Z_CONFIG_H_\n#define _Z_CONFIG_H_\n")
if(NOT MP_HAVE_REALLOCARRAY)
    string(APPEND zakalwe_config "#define _Z_NEED_REALLOCARRAY 1\n")
endif()
if(NOT MP_HAVE_STRLCPY)
    string(APPEND zakalwe_config "#define _Z_NEED_STRL 1\n")
endif()
string(APPEND zakalwe_config "#endif\n")
mp_write_if_changed("${UADE_GEN_INCLUDE}/zakalwe/config.h" "${zakalwe_config}")
# Il configure di libzakalwe usa sys/tree.h dei BSD se c'è: la sua copia
# portabile va bene ovunque.
mp_write_if_changed("${UADE_GEN_INCLUDE}/zakalwe/tree.h" "#include <zakalwe/ztree.h>\n")

set(ZAKALWE_SOURCES array.c base.c bitfield.c endianess.c math.c mem.c string.c str_array.c)
list(TRANSFORM ZAKALWE_SOURCES PREPEND "${ZAKALWE_ROOT}/")
add_library(zakalwe STATIC ${ZAKALWE_SOURCES})
# La directory generata viene prima: contiene il config.h giusto per questa
# piattaforma.
target_include_directories(zakalwe PUBLIC "${UADE_GEN_INCLUDE}" "${ZAKALWE_ROOT}/include")
target_compile_definitions(zakalwe PRIVATE _DEFAULT_SOURCE)
target_compile_options(zakalwe PRIVATE -w)

add_library(bencodetools STATIC "${BENCODE_ROOT}/bencode.c")
target_include_directories(bencodetools PUBLIC "${BENCODE_ROOT}/include")
target_compile_options(bencodetools PRIVATE -w)

# --- Header e sorgenti che generava lo script di configurazione di UADE ---

file(STRINGS "${UADE_ROOT}/version" UADE_VERSION LIMIT_COUNT 1)
set(uade_options "#ifndef _UADE_OPTIONS_H_\n#define _UADE_OPTIONS_H_\n")
string(APPEND uade_options "#define UADE_CONFIG_USER_MODE (0)\n")
string(APPEND uade_options "#define UADE_CONFIG_BASE_DIR \"${MP_UADE_DATA_DIR}\"\n")
# uadecore gira nel processo: il percorso dell'eseguibile non serve.
string(APPEND uade_options "#define UADE_CONFIG_UADE_CORE \"\"\n")
if(UNIX)
    string(APPEND uade_options "#define UADE_CONFIG_HAVE_URANDOM\n")
endif()
string(APPEND uade_options "#define UADE_VERSION \"${UADE_VERSION}\"\n#endif\n")
mp_write_if_changed("${UADE_GEN_INCLUDE}/uade/options.h" "${uade_options}")

mp_write_if_changed("${UADE_GEN_INCLUDE}/uade/compilersupport.h"
    "#ifndef _UADE_COMPILER_SUPPORT_H_\n#define _UADE_COMPILER_SUPPORT_H_\n#define likely(x) __builtin_expect(!!(x), 1)\n#define unlikely(x) __builtin_expect(!!(x), 0)\n#endif\n")

if(WIN32)
    mp_write_if_changed("${UADE_GEN_INCLUDE}/uade/sysincludes.h" "#include <winsock2.h>\n")
else()
    mp_write_if_changed("${UADE_GEN_INCLUDE}/uade/sysincludes.h" "#include <netinet/in.h>\n#include <sys/select.h>\n")
endif()

# ossupport.h/.c: il supporto UNIX di UADE più le funzioni che mancano alla
# piattaforma.
file(READ "${UADE_SRC}/frontends/common/unixsupport.c" uade_unixsupport)
set(uade_ossupport_h "#ifndef _UADE_OSSUPPORT_H_\n#define _UADE_OSSUPPORT_H_\n\n#include <uade/unixsupport.h>\n#include <zakalwe/string.h>\n\n")
set(uade_ossupport_c "#include <uade/ossupport.h>\n\n${uade_unixsupport}")
if(NOT MP_HAVE_MEMMEM)
    file(READ "${UADE_ROOT}/compat/memmemrep.h" header)
    file(READ "${UADE_ROOT}/compat/memmemrep.c" source)
    string(APPEND uade_ossupport_h "${header}")
    string(APPEND uade_ossupport_c "${source}")
endif()
if(NOT MP_HAVE_CANONICALIZE_FILE_NAME)
    file(READ "${UADE_ROOT}/compat/canonrep.h" header)
    file(READ "${UADE_ROOT}/compat/canonrep.c" source)
    string(APPEND uade_ossupport_h "${header}")
    string(APPEND uade_ossupport_c "${source}")
endif()
string(APPEND uade_ossupport_h "#endif\n")
mp_write_if_changed("${UADE_GEN_INCLUDE}/uade/ossupport.h" "${uade_ossupport_h}")
mp_write_if_changed("${UADE_GEN}/ossupport.c" "${uade_ossupport_c}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UADE_SRC}/frontends/common/unixsupport.c" "${UADE_ROOT}/version")

mp_write_if_changed("${UADE_GEN}/sd-sound.c" "#include \"sd-sound-generic.c\"\n")
mp_write_if_changed("${UADE_GEN}/sd-sound.h" "#include \"sd-sound-generic.h\"\n")

# --- Replayer e configurazione, copiati nella directory di build ---

add_custom_target(uade_data ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MP_UADE_DATA_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${UADE_ROOT}/players" "${MP_UADE_DATA_DIR}/players"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${UADE_ROOT}/amigasrc/score/score" "${UADE_ROOT}/uaerc" "${UADE_ROOT}/eagleplayer.conf"
            "${MP_UADE_DATA_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${UADE_ROOT}/uade.conf.in" "${MP_UADE_DATA_DIR}/uade.conf"
    VERBATIM)

# --- Emulatore 68000, generato dalla tabella delle istruzioni ---

# La directory generata viene prima: i sorgenti di UADE possono contenere
# copie vecchie degli stessi file, prodotte da una build classica.
set(UADE_CORE_INCLUDES
    "${UADE_GEN}" "${UADE_GEN_INCLUDE}" "${UADE_SRC}" "${UADE_SRC}/include" "${UADE_SRC}/frontends/include")
set(UADE_FRONTEND_INCLUDES
    "${UADE_GEN_INCLUDE}" "${UADE_SRC}/frontends/include" "${UADE_SRC}/frontends/common")

# I due generatori girano sulla macchina che compila. In cross-compilazione
# (Windows da macOS, WebAssembly) vanno presi da una build nativa:
# -DMP_UADE_HOST_TOOLS=<directory con uade_build68k e uade_gencpu>.
set(MP_UADE_HOST_TOOLS "" CACHE PATH "Directory con uade_build68k e uade_gencpu nativi (cross-compilazione)")
if(MP_UADE_HOST_TOOLS)
    set(UADE_BUILD68K "${MP_UADE_HOST_TOOLS}/uade_build68k")
    set(UADE_GENCPU "${MP_UADE_HOST_TOOLS}/uade_gencpu")
    set(UADE_BUILD68K_DEPENDS)
    set(UADE_GENCPU_DEPENDS)
else()
    add_executable(uade_build68k "${UADE_SRC}/build68k.c")
    target_include_directories(uade_build68k PRIVATE ${UADE_CORE_INCLUDES})
    target_compile_options(uade_build68k PRIVATE -w)
    add_executable(uade_gencpu
        "${UADE_SRC}/gencpu.c" "${UADE_SRC}/readcpu.c" "${UADE_SRC}/missing.c" "${UADE_GEN}/cpudefs.c")
    target_include_directories(uade_gencpu PRIVATE ${UADE_CORE_INCLUDES})
    target_compile_options(uade_gencpu PRIVATE -w)
    set(UADE_BUILD68K $<TARGET_FILE:uade_build68k>)
    set(UADE_GENCPU $<TARGET_FILE:uade_gencpu>)
    set(UADE_BUILD68K_DEPENDS uade_build68k)
    set(UADE_GENCPU_DEPENDS uade_gencpu)
endif()

add_custom_command(
    OUTPUT "${UADE_GEN}/cpudefs.c"
    COMMAND "${CMAKE_COMMAND}" -DTOOL=${UADE_BUILD68K} -DINPUT=${UADE_SRC}/table68k
            -DOUTPUT=${UADE_GEN}/cpudefs.c -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWithRedirect.cmake"
    DEPENDS ${UADE_BUILD68K_DEPENDS} "${UADE_SRC}/table68k"
    VERBATIM)

add_custom_command(
    OUTPUT "${UADE_GEN}/cpuemu.c" "${UADE_GEN}/cpustbl.c" "${UADE_GEN}/cputbl.h"
    COMMAND ${UADE_GENCPU}
    WORKING_DIRECTORY "${UADE_GEN}"
    DEPENDS ${UADE_GENCPU_DEPENDS}
    VERBATIM)

# cpuemu.c è enorme e UADE lo compila in otto parti.
set(UADE_CPUEMU_PARTS)
foreach(part RANGE 1 8)
    mp_write_if_changed("${UADE_GEN}/cpuemu${part}.c" "#define PART_${part}\n#include \"cpuemu.c\"\n")
    set_source_files_properties("${UADE_GEN}/cpuemu${part}.c" PROPERTIES OBJECT_DEPENDS "${UADE_GEN}/cpuemu.c")
    list(APPEND UADE_CPUEMU_PARTS "${UADE_GEN}/cpuemu${part}.c")
endforeach()

# --- uadecore e libuade ---

set(UADE_CORE_SOURCES
    newcpu.c memory.c custom.c cia.c audio.c compiler.c missing.c md-support.c
    cfgfile.c fpp.c debug.c readcpu.c uade.c uademain.c sinctable.c text_scope.c
    write_audio.c state_detection.c uade_logging.c)
list(TRANSFORM UADE_CORE_SOURCES PREPEND "${UADE_SRC}/")

# uadeipc.c e uadeutils.c sono condivisi: nella build classica uadecore ne
# compila una copia identica. unixatomic.c è sostituito da InProcess.cpp.
set(UADE_FRONTEND_SOURCES
    amifilemagic.c eagleplayer.c effects.c fifo.c md5.c rmc.c songdb.c songinfo.c
    support.c uadeconf.c uadecontrol.c uadeipc.c uadestate.c uadeutils.c unixwalkdir.c vparray.c)
list(TRANSFORM UADE_FRONTEND_SOURCES PREPEND "${UADE_SRC}/frontends/common/")

add_library(uade_core_objects OBJECT
    ${UADE_CORE_SOURCES} ${UADE_CPUEMU_PARTS} "${UADE_GEN}/sd-sound.c"
    "${UADE_GEN}/cpudefs.c" "${UADE_GEN}/cpustbl.c" "${UADE_GEN}/cputbl.h")
target_include_directories(uade_core_objects PRIVATE ${UADE_CORE_INCLUDES})
target_link_libraries(uade_core_objects PRIVATE zakalwe)

add_library(uade_frontend_objects OBJECT ${UADE_FRONTEND_SOURCES} "${UADE_GEN}/ossupport.c")
target_include_directories(uade_frontend_objects PRIVATE ${UADE_FRONTEND_INCLUDES})
target_link_libraries(uade_frontend_objects PRIVATE zakalwe bencodetools)

foreach(target uade_core_objects uade_frontend_objects)
    target_compile_definitions(${target} PRIVATE UADE_IN_PROCESS)
    if(NOT WIN32)
        # canonicalize_file_name, memmem e strlcpy di glibc.
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
    endif()
    if(WIN32)
        # Le funzioni POSIX che mancano a Windows (src/uade/windows).
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/uade/windows/include")
        target_compile_options(${target} PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/uade/windows/WindowsCompat.h")
    endif()
    # Codice di terze parti: niente warning, e exit() ridefinita.
    target_compile_options(${target} PRIVATE -w "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/uade/InProcessExit.h")
endforeach()

add_library(uade_inprocess STATIC
    src/uade/InProcess.cpp
    $<TARGET_OBJECTS:uade_core_objects>
    $<TARGET_OBJECTS:uade_frontend_objects>)
target_include_directories(uade_inprocess PUBLIC "${UADE_GEN_INCLUDE}" "${UADE_SRC}/frontends/include")
target_link_libraries(uade_inprocess PUBLIC zakalwe bencodetools Threads::Threads)
if(UNIX)
    target_link_libraries(uade_inprocess PUBLIC m)
elseif(WIN32)
    target_link_libraries(uade_inprocess PUBLIC ws2_32)
endif()
add_dependencies(uade_inprocess uade_data)
