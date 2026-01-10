# NanoHat OLED (ADR0006)

Single-threaded libubox/uloop implementation.

## Status

**Phase 1 完成** - uloop 基础骨架 + 构建基线

## Build

```bash
# Docker 交叉编译（从项目根目录执行）
docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/src" -w /src \
    openwrt-sdk:sunxi-cortexa53-24.10.5 \
    sh build_in_docker.sh

# 输出
build/target/nanohat-oled  # 83KB ARM64 可执行文件
```

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
- Archived ADR0005: `src_adr0005/`
