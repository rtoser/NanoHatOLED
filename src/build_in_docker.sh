#!/bin/sh
set -e

# Change to script's directory (the source directory)
cd "$(dirname "$0")"

# Auto-detect OpenWrt SDK paths in /builder
if [ -d "/builder/staging_dir" ]; then
    TOOLCHAIN_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "toolchain-*" | head -1)
    TARGET_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "target-*" | head -1)
    if [ -n "$TOOLCHAIN_DIR" ]; then
        export PATH="$TOOLCHAIN_DIR/bin:$PATH"
    fi
fi

# Auto-detect compiler (OpenWrt SDK or generic musl toolchain)
BUILD_DIR=${BUILD_DIR:-build/target}
export BUILD_DIR

if command -v aarch64-openwrt-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-openwrt-linux-musl-gcc
elif command -v aarch64-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-linux-musl-gcc
else
    echo "Error: No suitable cross compiler found"
    exit 1
fi

echo "Using compiler: $($CC --version | head -n 1)"

TARGET_DIR=${TARGET_DIR:-/opt/target}
export TARGET_DIR

if [ -f CMakeLists.txt ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "Error: CMakeLists.txt found but cmake is not installed"
        exit 1
    fi
    cmake -S . -B build \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_SYSROOT="$TARGET_DIR" \
        -DCMAKE_FIND_ROOT_PATH="$TARGET_DIR"
    cmake --build build
elif [ -f Makefile ]; then
    make clean BUILD_DIR="$BUILD_DIR"
    make BUILD_DIR="$BUILD_DIR" CC="$CC" TARGET_DIR="$TARGET_DIR"
else
    echo "Error: No build system found (Makefile/CMakeLists.txt)"
    exit 1
fi
