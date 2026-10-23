# libuade e l'emulatore uadecore compilati in un'unica libreria statica, con
# uadecore in un thread invece che in un processo separato (vedi
# src/uade/InProcess.cpp e patches/uade-in-process.patch).
#
# Servono i sorgenti preparati da scripts/build-deps.sh: lo script di
# configurazione di UADE genera alcuni header e ossupport.c.

find_package(Threads REQUIRED)

set(UADE_SRC "${MP_THIRD_PARTY}/src/uade/src")
set(UADE_GEN "${CMAKE_CURRENT_BINARY_DIR}/uade-gen")
file(MAKE_DIRECTORY "${UADE_GEN}")

# La directory dei file generati viene prima: i sorgenti di UADE possono
# contenere copie vecchie degli stessi file, prodotte dalla build classica.
set(UADE_CORE_INCLUDES
    "${UADE_GEN}" "${UADE_SRC}" "${UADE_SRC}/include" "${UADE_SRC}/frontends/include"
    "${MP_DEPS_PREFIX}/include")
set(UADE_FRONTEND_INCLUDES
    "${UADE_SRC}/frontends/include" "${UADE_SRC}/frontends/common" "${MP_DEPS_PREFIX}/include")

# --- Emulatore 68000, generato dalla tabella delle istruzioni ---

add_executable(uade_build68k "${UADE_SRC}/build68k.c")
target_include_directories(uade_build68k PRIVATE ${UADE_CORE_INCLUDES})
target_compile_options(uade_build68k PRIVATE -w)

add_custom_command(
    OUTPUT "${UADE_GEN}/cpudefs.c"
    COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:uade_build68k> -DINPUT=${UADE_SRC}/table68k
            -DOUTPUT=${UADE_GEN}/cpudefs.c -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWithRedirect.cmake"
    DEPENDS uade_build68k "${UADE_SRC}/table68k"
    VERBATIM)

add_executable(uade_gencpu
    "${UADE_SRC}/gencpu.c" "${UADE_SRC}/readcpu.c" "${UADE_SRC}/missing.c" "${UADE_GEN}/cpudefs.c")
target_include_directories(uade_gencpu PRIVATE ${UADE_CORE_INCLUDES})
target_compile_options(uade_gencpu PRIVATE -w)

add_custom_command(
    OUTPUT "${UADE_GEN}/cpuemu.c" "${UADE_GEN}/cpustbl.c" "${UADE_GEN}/cputbl.h"
    COMMAND uade_gencpu
    WORKING_DIRECTORY "${UADE_GEN}"
    DEPENDS uade_gencpu
    VERBATIM)

# cpuemu.c è enorme e UADE lo compila in otto parti.
set(UADE_CPUEMU_PARTS)
foreach(part RANGE 1 8)
    file(CONFIGURE OUTPUT "${UADE_GEN}/cpuemu${part}.c"
        CONTENT "#define PART_${part}\n#include \"cpuemu.c\"\n")
    set_source_files_properties("${UADE_GEN}/cpuemu${part}.c" PROPERTIES OBJECT_DEPENDS "${UADE_GEN}/cpuemu.c")
    list(APPEND UADE_CPUEMU_PARTS "${UADE_GEN}/cpuemu${part}.c")
endforeach()

# --- uadecore e libuade ---

set(UADE_CORE_SOURCES
    newcpu.c memory.c custom.c cia.c audio.c compiler.c missing.c sd-sound.c md-support.c
    cfgfile.c fpp.c debug.c readcpu.c uade.c uademain.c sinctable.c text_scope.c
    write_audio.c state_detection.c uade_logging.c)
list(TRANSFORM UADE_CORE_SOURCES PREPEND "${UADE_SRC}/")

# uadeipc.c, uadeutils.c e ossupport.c sono condivisi: nella build classica
# uadecore ne compila una copia identica. unixatomic.c è sostituito da
# InProcess.cpp.
set(UADE_FRONTEND_SOURCES
    amifilemagic.c eagleplayer.c effects.c fifo.c md5.c ossupport.c rmc.c songdb.c songinfo.c
    support.c uadeconf.c uadecontrol.c uadeipc.c uadestate.c uadeutils.c unixwalkdir.c vparray.c)
list(TRANSFORM UADE_FRONTEND_SOURCES PREPEND "${UADE_SRC}/frontends/common/")

add_library(uade_core_objects OBJECT
    ${UADE_CORE_SOURCES} ${UADE_CPUEMU_PARTS}
    "${UADE_GEN}/cpudefs.c" "${UADE_GEN}/cpustbl.c" "${UADE_GEN}/cputbl.h")
target_include_directories(uade_core_objects PRIVATE ${UADE_CORE_INCLUDES})

add_library(uade_frontend_objects OBJECT ${UADE_FRONTEND_SOURCES})
target_include_directories(uade_frontend_objects PRIVATE ${UADE_FRONTEND_INCLUDES})

foreach(target uade_core_objects uade_frontend_objects)
    target_compile_definitions(${target} PRIVATE UADE_IN_PROCESS)
    # Codice di terze parti: niente warning, e exit() ridefinita.
    target_compile_options(${target} PRIVATE -w -include "${CMAKE_CURRENT_SOURCE_DIR}/src/uade/InProcessExit.h")
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()

add_library(uade_inprocess STATIC
    src/uade/InProcess.cpp
    $<TARGET_OBJECTS:uade_core_objects>
    $<TARGET_OBJECTS:uade_frontend_objects>)
target_include_directories(uade_inprocess PUBLIC "${UADE_SRC}/frontends/include")
target_link_libraries(uade_inprocess PUBLIC
    "${MP_DEPS_PREFIX}/lib/libbencodetools${CMAKE_SHARED_LIBRARY_SUFFIX}"
    "${MP_DEPS_PREFIX}/lib/libzakalwe.so"
    Threads::Threads)
if(UNIX)
    target_link_libraries(uade_inprocess PUBLIC m)
endif()
