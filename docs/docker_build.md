# Docker 交叉编译指南

> 说明：本文档描述 ADR0005 构建流程，代码已归档至 `src_adr0005/`。ADR0006 将另行维护新的构建说明。

## 概述

本项目使用 Docker + OpenWrt SDK 进行交叉编译，从 macOS（或其他主机系统）编译出适用于 OpenWrt aarch64 目标平台的可执行文件。

## 编译环境

| 组件 | 说明 |
|------|------|
| 主机系统 | macOS (Apple Silicon / Intel) 或 Linux |
| Docker 镜像 | `openwrt-sdk:sunxi-cortexa53-24.10.5` |
| 构建系统 | CMake 3.13+ |
| 目标架构 | aarch64 (ARM64) |
| C 库 | musl libc（动态链接） |
| 目标系统 | OpenWrt 24.10+ |

## 快速开始

```bash
cd src_adr0005

# 构建（默认 RelWithDebInfo）
docker run --rm --platform linux/amd64 -v "$(pwd):/src" -w /src openwrt-sdk:sunxi-cortexa53-24.10.5 sh build_in_docker.sh
```

输出文件：`build/target/nanohat-oled`（约 95KB）

## 构建模式

| 模式 | 命令 | 说明 |
|------|------|------|
| 默认 | `sh build_in_docker.sh` | `-O2`，保留调试信息 |
| Release | `BUILD=release sh build_in_docker.sh` | `-O2 -DNDEBUG`，最小体积 |
| Debug | `BUILD=debug sh build_in_docker.sh` | `-O0 -g -DDEBUG` |

## 构建脚本详解

### build_in_docker.sh

```bash
#!/bin/sh
set -e

# 自动检测 OpenWrt SDK 路径
TOOLCHAIN_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "toolchain-*")
TARGET_DIR=$(find /builder/staging_dir -maxdepth 1 -type d -name "target-*")
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

# 检测编译器
CC=aarch64-openwrt-linux-musl-gcc

# CMake 构建
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DTARGET_DIR="$TARGET_DIR"
cmake --build "$BUILD_DIR" --parallel
```

### CMakeLists.txt 关键配置

```cmake
# 只编译需要的 u8g2 文件（19 个，而非全部 129 个）
set(U8G2_CORE
    u8g2_buffer.c u8g2_box.c u8g2_cleardisplay.c
    u8g2_d_memory.c u8g2_d_setup.c u8g2_font.c u8g2_fonts.c
    u8g2_hvline.c u8g2_intersection.c u8g2_ll_hvline.c u8g2_setup.c
)
set(U8X8_CORE
    u8x8_8x8.c u8x8_byte.c u8x8_cad.c u8x8_display.c
    u8x8_gpio.c u8x8_setup.c u8x8_string.c
)
set(U8X8_DRIVER u8x8_d_ssd1306_128x64_noname.c)

# 链接器优化 - 移除未使用代码
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)
```

## OpenWrt SDK 镜像

### 镜像构建

参考 `docker/` 目录下的 Dockerfile 构建 OpenWrt SDK 镜像：

```bash
cd docker
docker build -t openwrt-sdk:sunxi-cortexa53-24.10.5 .
```

### 镜像内部结构

```
/builder/
├── staging_dir/
│   ├── toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/
│   │   └── bin/
│   │       ├── aarch64-openwrt-linux-musl-gcc
│   │       ├── aarch64-openwrt-linux-musl-g++
│   │       └── ...
│   └── target-aarch64_cortex-a53_musl/
│       ├── usr/include/   # 目标系统头文件
│       └── usr/lib/       # 目标系统库文件
```

## CMake 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `GPIOCHIP_PATH` | `/dev/gpiochip1` | GPIO 芯片设备路径 |
| `BTN_OFFSETS` | `0,2,3` | 按钮 GPIO 偏移量 |
| `MONITORED_SERVICES` | `dropbear,xray_core,collectd,uhttpd` | 监控的服务列表 |
| `TARGET_DIR` | `/opt/target` | 目标 sysroot 路径 |

自定义示例：

```bash
cmake -S . -B build \
    -DGPIOCHIP_PATH="/dev/gpiochip0" \
    -DBTN_OFFSETS="1,2,3" \
    -DMONITORED_SERVICES="sshd,nginx"
```

## Apple Silicon 注意事项

OpenWrt SDK 镜像为 `linux/amd64`，在 Apple Silicon Mac 上需要显式指定平台：

```bash
# 必须添加 --platform linux/amd64 参数
docker run --rm --platform linux/amd64 -v "$(pwd):/src" -w /src openwrt-sdk:sunxi-cortexa53-24.10.5 sh build_in_docker.sh
```

性能影响：编译速度约为原生的 30-50%（通过 Rosetta 2 模拟）。

## 验证编译结果

```bash
# 检查文件类型
file build/target/nanohat-oled
# ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV),
# dynamically linked, interpreter /lib/ld-musl-aarch64.so.1

# 检查文件大小
ls -lh build/target/nanohat-oled
# -rwxr-xr-x  95K  nanohat-oled
```

## 部署到目标设备

```bash
# 停止服务
ssh root@<device-ip> "service nanohat-oled stop; sleep 1"

# 上传新版本
scp build/target/nanohat-oled root@<device-ip>:/usr/bin/

# 启动服务
ssh root@<device-ip> "service nanohat-oled start"
```

## 常见问题

### 1. CMake 找不到编译器

确保在 Docker 容器内运行，或手动设置 PATH：

```bash
export PATH=/builder/staging_dir/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/bin:$PATH
```

### 2. 链接错误：找不到 libgpiod/libubus

确保 `TARGET_DIR` 指向正确的 sysroot：

```bash
cmake -DTARGET_DIR=/builder/staging_dir/target-aarch64_cortex-a53_musl ...
```

### 3. u8g2 子模块未初始化

```bash
git submodule update --init --recursive
```

### 4. 二进制文件过大

确保使用 Release 模式构建：

```bash
BUILD=release sh build_in_docker.sh
```

## 参考资料

- [OpenWrt 开发文档](https://openwrt.org/docs/guide-developer/start)
- [CMake 文档](https://cmake.org/documentation/)
- [u8g2 图形库](https://github.com/olikraus/u8g2)
