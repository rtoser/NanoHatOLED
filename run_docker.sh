#!/bin/bash
set -e

# Cross-compile for aarch64 musl (OpenWrt)
echo "=== Building nanohat-oled ==="

docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/work" \
    -w /work \
    dockcross/linux-arm64-musl \
    bash -c "
        export PATH=/usr/xcc/aarch64-linux-musl-cross/bin:\$PATH
        make clean
        make -j\$(nproc)
        file nanohat-oled
    "

echo "=== Build complete ==="
ls -lh src/nanohat-oled
