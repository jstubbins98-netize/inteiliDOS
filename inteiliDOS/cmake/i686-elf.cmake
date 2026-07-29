# cmake/i686-elf.cmake
# CMake toolchain file for i686-elf bare-metal cross-compiler.
# Pass this to cmake with: -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i686)

# Prefer toolchain from /opt/cross or /usr/local if present
find_program(CROSS_CC  i686-elf-gcc  HINTS /opt/cross/bin /usr/local/bin /usr/bin REQUIRED)
find_program(CROSS_CXX i686-elf-g++  HINTS /opt/cross/bin /usr/local/bin /usr/bin)
find_program(CROSS_AR  i686-elf-ar   HINTS /opt/cross/bin /usr/local/bin /usr/bin)

set(CMAKE_C_COMPILER   ${CROSS_CC})
set(CMAKE_CXX_COMPILER ${CROSS_CXX})
set(CMAKE_AR           ${CROSS_AR})

# Prevent CMake from trying to test the compiler against the host system
set(CMAKE_C_COMPILER_WORKS   1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# No sysroot — pure freestanding
set(CMAKE_SYSROOT "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
