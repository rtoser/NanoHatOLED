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
BUILD=${BUILD:-default}
export BUILD_DIR
export BUILD

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

    # Determine build type
    case "$BUILD" in
        debug)   CMAKE_BUILD_TYPE=Debug ;;
        release) CMAKE_BUILD_TYPE=Release ;;
        *)       CMAKE_BUILD_TYPE=RelWithDebInfo ;;
    esac

    # Configure and build
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
        -DTARGET_DIR="$TARGET_DIR"
    cmake --build "$BUILD_DIR" --parallel
elif [ -f Makefile ]; then
    make clean BUILD_DIR="$BUILD_DIR" BUILD="$BUILD"
    make BUILD_DIR="$BUILD_DIR" BUILD="$BUILD" CC="$CC" TARGET_DIR="$TARGET_DIR"
else
    echo "Error: No build system found (Makefile/CMakeLists.txt)"
    exit 1
fi
