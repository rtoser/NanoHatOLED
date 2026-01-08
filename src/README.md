# NanoHat OLED（ADR0005 重新实现）

本目录为新架构（ADR0005）重写版本的代码与文档入口。旧实现保留在 `src_old/`。

## 状态

- Phase 1：已完成（基础设施 + Host 测试）
- Phase 2：已完成（GPIO HAL + Host Mock 测试）
- Phase 3+：进行中

## 目录说明

```
src/
├── main.c              # 主入口（事件循环 + UI 线程）
├── Makefile            # 编译入口（生成 nanohat-oled）
├── hal/                 # 硬件抽象层
│   ├── gpio_hal.h
│   ├── gpio_hal_libgpiod.c
│   ├── gpio_hal_mock.c
│   ├── display_hal.h
│   ├── display_hal_null.c
│   ├── ubus_hal.h
│   ├── time_hal.h
│   └── time_hal_real.c
├── ring_queue.c
└── ring_queue.h
```

## Host 测试

```bash
cd tests
make test-host
```

## 交叉编译（Docker）

使用 OpenWrt SDK 镜像：
```bash
docker run --rm --platform linux/amd64 -v "$(pwd)/src:/src" openwrt-sdk:sunxi-cortexa53-24.10.5 sh /src/build_in_docker.sh
```

> 当前重实现尚未完成，`build_in_docker.sh` 会要求 `Makefile` 或 `CMakeLists.txt` 存在后才可编译。

## 本地（Host）构建

使用 `BUILD_DIR` 参数将产物隔离到单独目录（默认 `build/host`）：

```bash
cd src
BUILD_DIR=build/host make
```

这样可以避免 Host 与 Target 编译产物冲突，测试/调试时可以直接运行 `build/host/bin/nanohat-oled`（如果需要链接实际 HAL，替换 `hal/display_hal_null.c`）。

当前已提供 `Makefile`，默认链接 `display_hal_null.c`（无显示输出），后续接入真实显示 HAL 时可替换链接文件。

可选构建模式（默认 `BUILD=default`，即 `-O2`，不包含 `-g/-DNDEBUG`）：

- `BUILD=debug`：`-O0 -g -DDEBUG`
- `BUILD=release`：`-O2 -DNDEBUG`

示例：
```bash
cd src
BUILD=release BUILD_DIR=build/target make
```

## 部署（示例）

```bash
ssh root@<device-ip> "service nanohat-oled stop; sleep 1"
scp nanohat-oled root@<device-ip>:/usr/bin/
ssh root@<device-ip> "service nanohat-oled start"
```

## 参考文档

- `docs/adr0005/README.md`
- `docs/adr/0005-ultimate-threaded-architecture.md`
