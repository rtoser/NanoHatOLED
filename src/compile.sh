#!/bin/sh
set -e

echo "=== Environment Info ==="
# OpenWrt SDK usually sets CC, but let's be explicit if needed
# Finding the cross compiler
export PATH=$PATH:$(ls -d /opt/sdk/staging_dir/toolchain-*/bin 2>/dev/null)
TARGET_CC=$(ls /opt/sdk/staging_dir/toolchain-*/bin/aarch64-openwrt-linux-musl-gcc 2>/dev/null | head -n 1)

if [ -z "$TARGET_CC" ]; then
    echo "Compiler not found! Searching..."
    find /opt/sdk -name "*gcc"
    exit 1
fi

echo "Using compiler: $TARGET_CC"

echo "=== Compiling ==="
STAGING_DIR_ROOT=$(ls -d /opt/sdk/staging_dir/target-*/usr/include 2>/dev/null | head -n 1)
TOOLCHAIN_INC=$(ls -d /opt/sdk/staging_dir/toolchain-*/include 2>/dev/null | head -n 1)

$TARGET_CC \
    -I. \
    -I./u8g2/csrc \
    -I$STAGING_DIR_ROOT \
    -I$TOOLCHAIN_INC \
    -O2 \
    -static \
    main.c \
    sys_status.c \
    gpio_button.c \
    u8g2_port_linux.c \
    u8g2/csrc/*.c \
    -o nanohat-oled

echo "=== Success ==="
file nanohat-oled
