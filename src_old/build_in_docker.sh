#!/bin/sh
set -e

# Auto-detect OpenWrt SDK paths in /builder
if [ -d "/builder/staging_dir" ]; then
    TOOLCHAIN_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "toolchain-*" | head -1)
    TARGET_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "target-*" | head -1)
    if [ -n "$TOOLCHAIN_DIR" ]; then
        export PATH="$TOOLCHAIN_DIR/bin:$PATH"
    fi
fi

# Auto-detect compiler (OpenWrt SDK or generic musl toolchain)
if command -v aarch64-openwrt-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-openwrt-linux-musl-gcc
elif command -v aarch64-linux-musl-gcc >/dev/null 2>&1; then
    CC=aarch64-linux-musl-gcc
else
    echo "Error: No suitable cross compiler found"
    exit 1
fi

echo "Using compiler: $($CC --version | head -n 1)"

# Source files
SOURCES="main.c u8g2_port_linux.c gpio_button.c sys_status.c ubus_service.c u8g2/csrc/*.c"
OUTPUT="nanohat-oled"

TARGET_DIR=${TARGET_DIR:-/opt/target}
CFLAGS="-I${TARGET_DIR}/usr/include"
LDFLAGS="-L${TARGET_DIR}/usr/lib -lubus -lubox -lblobmsg_json -ljson-c"

# Compile with dead code elimination (dynamic linking)
$CC \
    -I. \
    -I./u8g2/csrc \
    $CFLAGS \
    -Os \
    -Wall \
    -fdata-sections \
    -ffunction-sections \
    $SOURCES \
    $LDFLAGS \
    -Wl,--gc-sections \
    -o $OUTPUT

# Strip debug symbols for release
STRIP=$(echo $CC | sed 's/gcc$/strip/')
$STRIP $OUTPUT

echo "Compilation successful: $OUTPUT"
file $OUTPUT
ls -lh $OUTPUT
