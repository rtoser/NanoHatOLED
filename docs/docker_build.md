# Docker 交叉编译指南

## 概述

本项目使用 Docker 进行交叉编译，从 macOS（或其他主机系统）编译出适用于 OpenWrt aarch64 目标平台的可执行文件。

## 编译环境

| 组件 | 说明 |
|------|------|
| 主机系统 | macOS (Apple Silicon / Intel) 或 Linux |
| Docker 镜像 | `dockcross/linux-arm64-musl` |
| 目标架构 | aarch64 (ARM64) |
| C 库 | musl libc（静态链接） |
| 目标系统 | OpenWrt 24.10+ |

## 为什么使用 Docker 交叉编译

### 1. 环境一致性
- 编译环境与开发者主机系统解耦
- 避免"在我机器上能编译"的问题
- CI/CD 友好

### 2. 简化工具链管理
- 无需手动安装交叉编译工具链
- dockcross 镜像预装完整工具链
- 一行命令即可编译

### 3. musl libc 优势
- 静态链接后体积小
- 与 OpenWrt 原生兼容
- 无 glibc 版本兼容问题

## 编译步骤

### 快速编译

```bash
# 在项目根目录执行
bash run_docker.sh
```

输出文件：`src/nanohat-oled`

### 手动编译

```bash
# 运行 Docker 容器并编译
docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/work" \
    -w /work \
    dockcross/linux-arm64-musl \
    bash -c "
        export PATH=/usr/xcc/aarch64-linux-musl-cross/bin:\$PATH
        make clean
        make -j\$(nproc)
    "
```

## 编译脚本详解

### run_docker.sh

```bash
#!/bin/bash
set -e

# Cross-compile for aarch64 musl (OpenWrt)
echo "=== Building nanohat-oled ==="

docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/work" \                    # 挂载源码目录
    -w /work \                                  # 设置工作目录
    dockcross/linux-arm64-musl \               # 使用 musl 工具链镜像
    bash -c "
        export PATH=/usr/xcc/aarch64-linux-musl-cross/bin:\$PATH
        make clean
        make -j\$(nproc)                        # 并行编译
        file nanohat-oled                       # 验证输出文件
    "

echo "=== Build complete ==="
ls -lh src/nanohat-oled
```

**关键参数说明**：

| 参数 | 说明 |
|------|------|
| `--rm` | 容器退出后自动删除 |
| `--platform linux/amd64` | 在 Apple Silicon 上使用 x86 模拟（镜像仅支持 amd64） |
| `-v "$(pwd)/src:/work"` | 将 src 目录挂载到容器的 /work |
| `-w /work` | 设置容器工作目录 |

### Makefile

```makefile
CC = aarch64-linux-musl-gcc        # 交叉编译器
CFLAGS = -I./u8g2/csrc -I. -O2 -Wall
LDFLAGS = -static                   # 静态链接

# U8g2 图形库源文件
U8G2_SRC_DIR = u8g2/csrc
U8G2_SOURCES = $(wildcard $(U8G2_SRC_DIR)/*.c)
U8G2_OBJECTS = $(U8G2_SOURCES:.c=.o)

# 应用源文件
APP_SOURCES = main.c u8g2_port_linux.c gpio_button.c sys_status.c
APP_OBJECTS = $(APP_SOURCES:.c=.o)

TARGET = nanohat-oled

all: $(TARGET)

$(TARGET): $(APP_OBJECTS) $(U8G2_OBJECTS)
	$(CC) $(APP_OBJECTS) $(U8G2_OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(APP_OBJECTS) $(U8G2_OBJECTS) $(TARGET)
```

**编译选项说明**：

| 选项 | 说明 |
|------|------|
| `-I./u8g2/csrc` | u8g2 库头文件路径 |
| `-O2` | 优化级别 2 |
| `-Wall` | 开启所有警告 |
| `-static` | 静态链接所有库 |

## dockcross 镜像

### 什么是 dockcross

[dockcross](https://github.com/dockcross/dockcross) 是一组预构建的 Docker 镜像，包含各种目标平台的交叉编译工具链。

### 可用镜像

| 镜像 | 目标平台 | 用途 |
|------|---------|------|
| `linux-arm64-musl` | aarch64 + musl | OpenWrt、Alpine |
| `linux-arm64` | aarch64 + glibc | Debian/Ubuntu ARM64 |
| `linux-armv7` | armv7 + glibc | Raspberry Pi 等 |
| `linux-x64` | x86_64 + glibc | 通用 Linux |

### 镜像内部结构

```
/usr/xcc/aarch64-linux-musl-cross/
├── bin/
│   ├── aarch64-linux-musl-gcc
│   ├── aarch64-linux-musl-g++
│   ├── aarch64-linux-musl-ar
│   ├── aarch64-linux-musl-strip
│   └── ...
├── lib/
└── include/
```

## Apple Silicon 注意事项

### 平台模拟

dockcross 镜像仅提供 `linux/amd64` 版本。在 Apple Silicon Mac 上，Docker Desktop 通过 Rosetta 2 模拟 x86_64 环境。

```bash
docker run --platform linux/amd64 ...
```

### 性能影响

- 编译速度约为原生的 30-50%
- 首次运行需要下载 ~1GB 镜像
- 后续编译使用缓存的镜像

### 替代方案

如需更好性能，可以：
1. 在 Linux x86 机器上编译
2. 使用 GitHub Actions 进行 CI 编译
3. 在目标设备上本地编译（如果支持）

## 验证编译结果

```bash
# 检查文件类型
file src/nanohat-oled

# 期望输出：
# nanohat-oled: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV),
# static-pie linked, with debug_info, not stripped

# 检查文件大小
ls -lh src/nanohat-oled

# 优化体积（可选）
aarch64-linux-musl-strip src/nanohat-oled
```

## 部署到目标设备

```bash
# 停止运行中的服务
ssh root@<device-ip> "killall nanohat-oled 2>/dev/null; rm -f /usr/bin/nanohat-oled"

# 上传新版本
scp src/nanohat-oled root@<device-ip>:/usr/bin/

# 启动服务
ssh root@<device-ip> "/etc/init.d/nanohat-oled start"
```

## 常见问题

### 1. Docker 镜像下载慢

使用国内镜像加速：

```bash
# 配置 Docker daemon.json
{
  "registry-mirrors": ["https://mirror.ccs.tencentyun.com"]
}
```

### 2. 编译报错：找不到头文件

确保 u8g2 子模块已初始化：

```bash
git submodule update --init --recursive
```

### 3. 目标设备报错：Exec format error

确认编译的架构与目标设备匹配：

```bash
# 检查设备架构
ssh root@<device> "uname -m"
# 应输出：aarch64

# 检查编译产物
file src/nanohat-oled
# 应包含：ARM aarch64
```

### 4. 静态链接后文件过大

使用 strip 去除调试信息：

```bash
docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/work" \
    -w /work \
    dockcross/linux-arm64-musl \
    aarch64-linux-musl-strip nanohat-oled
```

strip 前后对比：
- 带调试信息：~15MB
- strip 后：~300KB

## CI/CD 集成示例

### GitHub Actions

```yaml
name: Build

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Build with dockcross
        run: |
          docker run --rm \
            -v ${{ github.workspace }}/src:/work \
            -w /work \
            dockcross/linux-arm64-musl \
            bash -c "make clean && make -j$(nproc)"

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: nanohat-oled
          path: src/nanohat-oled
```

## 参考资料

- [dockcross GitHub](https://github.com/dockcross/dockcross)
- [musl libc](https://musl.libc.org/)
- [OpenWrt 开发文档](https://openwrt.org/docs/guide-developer/start)
