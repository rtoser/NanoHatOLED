# CMake toolchain file for OpenWrt aarch64 cross-compilation
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openwrt-aarch64.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler prefix
set(CROSS_PREFIX "aarch64-openwrt-linux-musl-")

# Find compiler: any SDK toolchain (gcc version differs per OpenWrt release)
file(GLOB _OPENWRT_TOOLCHAIN_BIN_DIRS
    /builder/staging_dir/toolchain-*/bin
    $ENV{STAGING_DIR}/../toolchain-*/bin
)
find_program(CMAKE_C_COMPILER
    NAMES ${CROSS_PREFIX}gcc
    PATHS ${_OPENWRT_TOOLCHAIN_BIN_DIRS}
    NO_DEFAULT_PATH
)

if(NOT CMAKE_C_COMPILER)
    # Fallback to PATH
    set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
endif()

# Target sysroot
set(TARGET_DIR "/builder/staging_dir/target-aarch64_cortex-a53_musl" CACHE PATH "Target sysroot")
set(CMAKE_SYSROOT ${TARGET_DIR})
set(CMAKE_FIND_ROOT_PATH ${TARGET_DIR})

# Search settings
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
