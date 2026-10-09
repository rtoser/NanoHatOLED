# NanoHat OLED (ADR0006)

Single-threaded libubox/uloop implementation.

## Status

**Phase 6 完成** - Home/Gateway/Network/Services/Settings 五页，uloop + ubus 异步查询（见 `docs/adr0006/implementation-plan.md`）

## Build

```bash
# Docker 交叉编译（从项目根目录执行）
docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/src" -w /src \
    openwrt-sdk:sunxi-cortexa53-24.10.5 \
    sh build_in_docker.sh

# 输出
build/target/nanohat-oled  # ARM64 (musl) 可执行文件
```

SDK 镜像必须与目标板的 OpenWrt 发行版一致（libubox/libubus SONAME 带日期戳）。
换版本时先 `rm -rf src/build/target`，再用对应镜像（如 `openwrt-sdk:sunxi-cortexa53-25.12.5`）编译。
板级参数通过 `docker run -e GPIOCHIP_PATH=... -e BTN_OFFSETS=... -e I2C_DEV_PATH=... -e MONITORED_SERVICES=...` 覆盖。
NanoPi NEO2 + 25.12.x（内核 6.12）按键在 `/dev/gpiochip0`，需 `-e GPIOCHIP_PATH=/dev/gpiochip0`（默认 gpiochip1）。
完整的烧写/依赖/部署流程见 `docs/adr0006/deploy.md`。

## Structure

```
src/
├── main.c                  # uloop 主循环入口
├── hal/
│   ├── display_hal.h       # 显示 HAL 接口
│   ├── display_hal_null.c  # null 实现（测试）
│   ├── time_hal.h          # 时间 HAL 接口
│   ├── time_hal_real.c     # CLOCK_MONOTONIC 实现
│   ├── u8g2_stub.h         # u8g2 测试桩
│   └── u8g2_stub.c
├── u8g2/                   # u8g2 子模块
├── cmake/                  # CMake 工具链
├── CMakeLists.txt
└── build_in_docker.sh
```

## References

- Architecture: `docs/adr0006/architecture.md`
- Implementation plan: `docs/adr0006/implementation-plan.md`
- Build & deploy: `docs/adr0006/deploy.md`
- Archived ADR0005: `src_adr0005/`
