# OpenWrt SDK Docker 构建环境

用于交叉编译 NanoHat OLED 驱动的 Docker 容器，基于 OpenWrt 官方 SDK。

## 目标平台

- **设备**: NanoPi NEO2 Plus
- **架构**: aarch64 (ARM Cortex-A53)
- **系统**: OpenWrt 24.10.x (Linux 6.6, musl libc)

## 快速开始

### 1. 构建 Docker 镜像

```bash
cd docker
./build.sh 24.10.5
```

这将创建镜像 `openwrt-sdk-sunxi-24.10.5`（约 1.9GB）。

### 2. 编译项目

```bash
# 在项目根目录
docker run --rm -v "$(pwd)/src:/src" openwrt-sdk-sunxi-24.10.5 sh build_in_docker.sh
```

输出：`src/nanohat-oled`（静态链接的 aarch64 可执行文件）

### 3. 交互式开发

```bash
docker run -it --rm -v "$(pwd)/src:/src" openwrt-sdk-sunxi-24.10.5
# 容器内
sh build_in_docker.sh
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `Dockerfile` | Docker 镜像定义 |
| `build.sh` | 镜像构建脚本（接收版本号参数） |
| `test_ubus.c` | libubus 链接验证程序 |
| `Makefile.test` | 测试程序编译规则 |

## 镜像构建过程

1. **下载 SDK**: 从清华镜像站下载 OpenWrt SDK
2. **配置 feeds**: 使用 GitHub 镜像加速（git.openwrt.org → github.com/openwrt）
3. **浅克隆**: base feed 使用 `--depth 1` 加速下载
4. **编译依赖**: json-c, libubox, libubus

## 环境变量

容器内预配置：

```bash
# 交叉编译器路径
PATH=/opt/sdk/staging_dir/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/bin:$PATH

# OpenWrt staging 目录
STAGING_DIR=/opt/sdk/staging_dir

# 目标平台 sysroot
/opt/target → staging_dir/target-aarch64_cortex-a53_musl
```

## 可用的交叉编译工具

```bash
aarch64-openwrt-linux-musl-gcc    # C 编译器
aarch64-openwrt-linux-musl-g++    # C++ 编译器
aarch64-openwrt-linux-musl-ar     # 静态库工具
aarch64-openwrt-linux-musl-strip  # 符号剥离
```

## 使用不同版本

```bash
# OpenWrt 24.10.5
./build.sh 24.10.5

# OpenWrt 23.05.5 (如果需要)
./build.sh 23.05.5
```

脚本会自动检查 SDK 是否存在于镜像站。

## libubus 验证

验证 libubus 链接是否正常工作：

```bash
# 在容器内编译测试程序
docker run --rm -v "$(pwd)/docker:/src" openwrt-sdk-sunxi-24.10.5 make -f Makefile.test

# 部署到设备测试
scp docker/test_ubus root@<device-ip>:/tmp/
ssh root@<device-ip> "/tmp/test_ubus"
```

## 注意事项

- LD_PRELOAD 的 `runas.so` 警告可以忽略（SDK 内部权限隔离机制）
- 首次构建约需 5-10 分钟（取决于网络速度）
- 镜像大小约 1.9GB，包含完整的 OpenWrt SDK 和工具链
- 主程序编译依赖 libubus/libubox/libblobmsg_json/libjson-c，镜像内已预置
