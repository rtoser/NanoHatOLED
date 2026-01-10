# NanoHat OLED（ADR0005 重新实现）

本目录为 ADR0005 版本的代码与文档入口，已归档。新的重构主线请参考 `docs/adr0006/`，新代码位于 `src/`。
旧实现保留在 `src_old/`。

## 状态

- Phase 1-7：已完成

## 目录说明

```
src_adr0005/
├── main.c               # 主入口（事件循环 + UI 线程）
├── CMakeLists.txt       # CMake 构建配置
├── build_in_docker.sh   # Docker 构建脚本
├── cmake/               # CMake 工具链文件
├── hal/                 # 硬件抽象层
│   ├── gpio_hal.h
│   ├── gpio_hal_libgpiod.c
│   ├── display_hal.h
│   ├── display_hal_ssd1306.c
│   ├── ubus_hal.h
│   ├── ubus_hal_real.c
│   ├── time_hal.h
│   └── time_hal_real.c
├── pages/               # UI 页面
│   ├── page_home.c
│   ├── page_network.c
│   └── page_services.c
└── u8g2/                # u8g2 图形库（submodule）
```

## 交叉编译（Docker + OpenWrt SDK）

```bash
cd src_adr0005

# 默认构建（RelWithDebInfo）
docker run --rm --platform linux/amd64 -v "$(pwd):/src" -w /src openwrt-sdk:sunxi-cortexa53-24.10.5 sh build_in_docker.sh

# Release 构建（更小）
BUILD=release docker run --rm --platform linux/amd64 -v "$(pwd):/src" -w /src openwrt-sdk:sunxi-cortexa53-24.10.5 sh build_in_docker.sh

# Debug 构建
BUILD=debug docker run --rm --platform linux/amd64 -v "$(pwd):/src" -w /src openwrt-sdk:sunxi-cortexa53-24.10.5 sh build_in_docker.sh
```

输出文件：`build/target/nanohat-oled`（约 95KB）

## 构建系统

项目使用 CMake 构建，自动：
- 只编译所需的 u8g2 文件（19 个，而非全部 129 个）
- 应用 `-ffunction-sections -fdata-sections` 和 `-Wl,--gc-sections` 优化二进制大小

### 配置选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `GPIOCHIP_PATH` | `/dev/gpiochip1` | GPIO 芯片设备路径 |
| `BTN_OFFSETS` | `0,2,3` | 按钮 GPIO 偏移量 |
| `MONITORED_SERVICES` | `dropbear,uhttpd` | 监控的服务列表 |

## Host 测试

```bash
cd src_adr0005/tests
make test-host
```

## 部署

```bash
# 停止服务、上传、启动
ssh root@<device-ip> "service nanohat-oled stop; sleep 1"
scp build/target/nanohat-oled root@<device-ip>:/usr/bin/
ssh root@<device-ip> "service nanohat-oled start"
```

## 参考文档

- `docs/adr0005/README.md`
- `docs/adr/0005-ultimate-threaded-architecture.md`
- `docs/docker_build.md`
