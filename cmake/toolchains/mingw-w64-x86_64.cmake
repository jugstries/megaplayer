# Cross-compilazione per Windows x64 con MinGW-w64 (da macOS: brew install
# mingw-w64; da Linux: il pacchetto mingw-w64). Esempio:
#
#   cmake -S . -B build-win -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#     -DMP_UADE_HOST_TOOLS=$PWD/build -DMP_WITH_OPENMPT=OFF -DMP_WITH_ARCHIVES=OFF -DMP_WITH_MP3=OFF
#
# MP_UADE_HOST_TOOLS indica una build nativa con uade_build68k e uade_gencpu.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
