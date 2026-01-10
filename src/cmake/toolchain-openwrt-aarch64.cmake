# CMake toolchain file for OpenWrt aarch64 cross-compilation
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openwrt-aarch64.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler prefix
set(CROSS_PREFIX "aarch64-openwrt-linux-musl-")

# Find compiler (try common locations)
find_program(CMAKE_C_COMPILER
    NAMES ${CROSS_PREFIX}gcc
    PATHS
        /builder/staging_dir/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/bin
        $ENV{STAGING_DIR}/../toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/bin
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
