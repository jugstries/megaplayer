# AdPlug con libbinio, compilati dai sorgenti dei tag git senza i loro
# script: qui si generano binio.h e version.h, che scriverebbe configure.
# Definisce il target `adplug`.

set(MP_LIBBINIO_DIR "${MP_THIRD_PARTY}/src/libbinio")
set(MP_ADPLUG_DIR "${MP_THIRD_PARTY}/src/adplug")
set(MP_ADPLUG_GENERATED "${CMAKE_BINARY_DIR}/adplug-generated")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(MP_ADPLUG_WARNINGS -w) # codice di terzi
else()
    set(MP_ADPLUG_WARNINGS)
endif()

# --- libbinio ---

set(ENABLE_STRING 1)
set(ENABLE_IOSTREAM 1)
set(ISO_STDLIB 1)
set(WITH_MATH 1)
set(TYPE_INT "long long")
set(TYPE_FLOAT "long double")
configure_file("${MP_LIBBINIO_DIR}/src/binio.h.in" "${MP_ADPLUG_GENERATED}/binio.h" @ONLY)

add_library(binio STATIC
    "${MP_LIBBINIO_DIR}/src/binfile.cpp"
    "${MP_LIBBINIO_DIR}/src/binio.cpp"
    "${MP_LIBBINIO_DIR}/src/binstr.cpp"
    "${MP_LIBBINIO_DIR}/src/binwrap.cpp")
# SYSTEM: i loro header non devono riempire di avvisi chi li include.
target_include_directories(binio SYSTEM PUBLIC "${MP_ADPLUG_GENERATED}" "${MP_LIBBINIO_DIR}/src")
target_compile_options(binio PRIVATE ${MP_ADPLUG_WARNINGS})
# Usa ancora `register`, che dal C++17 non c'è più.
set_target_properties(binio PROPERTIES CXX_STANDARD 14)

# --- AdPlug ---

# La versione da configure.ac: AC_INIT([adplug], [2.4]).
file(STRINGS "${MP_ADPLUG_DIR}/configure.ac" mp_adplug_init REGEX "^AC_INIT\\(")
string(REGEX MATCH "\\[([0-9.]+)\\]\\)" _ "${mp_adplug_init}")
set(VERSION "${CMAKE_MATCH_1}")
configure_file("${MP_ADPLUG_DIR}/src/version.h.in" "${MP_ADPLUG_GENERATED}/version.h" @ONLY)

# Tutti i sorgenti di src/, come la lista del suo Makefile.am.
file(GLOB MP_ADPLUG_SOURCES "${MP_ADPLUG_DIR}/src/*.c" "${MP_ADPLUG_DIR}/src/*.cpp")
add_library(adplug STATIC ${MP_ADPLUG_SOURCES})
target_include_directories(adplug SYSTEM PUBLIC "${MP_ADPLUG_DIR}/src" PRIVATE "${MP_ADPLUG_GENERATED}")
target_compile_options(adplug PRIVATE ${MP_ADPLUG_WARNINGS})
set_target_properties(adplug PROPERTIES CXX_STANDARD 17) # come il suo CMakeLists
if(WIN32)
    target_compile_definitions(adplug PRIVATE stricmp=_stricmp)
else()
    target_compile_definitions(adplug PRIVATE stricmp=strcasecmp)
endif()
target_link_libraries(adplug PUBLIC binio)

# libADLMIDI ha la sua copia di Nuked OPL3, di un'altra versione ma con le
# stesse funzioni: il linker ne terrebbe una sola, e AdPlug finirebbe a usare
# quella di libADLMIDI sulla sua struttura opl3_chip, più piccola, scrivendo
# oltre la fine. Le funzioni di AdPlug, prese dal suo nukedopl.h, si
# rinominano.
file(STRINGS "${MP_ADPLUG_DIR}/src/nukedopl.h" mp_nuked_declarations REGEX "OPL3_[A-Za-z0-9]+\\(")
string(REGEX MATCHALL "OPL3_[A-Za-z0-9]+" mp_nuked_functions "${mp_nuked_declarations}")
list(REMOVE_DUPLICATES mp_nuked_functions)
foreach(function ${mp_nuked_functions})
    target_compile_definitions(adplug PRIVATE ${function}=adplug_${function})
endforeach()
