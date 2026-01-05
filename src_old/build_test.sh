#!/bin/sh
set -e

if command -v aarch64-openwrt-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-openwrt-linux-musl-gcc
elif command -v aarch64-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-linux-musl-gcc
else
    echo "Error: No suitable cross compiler found"
    exit 1
fi

echo "Compiling test_jitter..."
$CC -Os -Wall src/test_jitter.c -o src/test_jitter
echo "Build complete."
