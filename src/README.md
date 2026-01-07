# NanoHat OLED（ADR0005 重新实现）

本目录为新架构（ADR0005）重写版本的代码与文档入口。旧实现保留在 `src_old/`。

## 状态

- Phase 1：已完成（基础设施 + Host 测试）
- Phase 2：已完成（GPIO HAL + Host Mock 测试）
- Phase 3+：进行中

## 目录说明

```
src/
├── hal/                 # 硬件抽象层
│   ├── gpio_hal.h
│   ├── gpio_hal_libgpiod.c
│   ├── gpio_hal_mock.c
│   ├── display_hal.h
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

## 部署（示例）

```bash
ssh root@<device-ip> "service nanohat-oled stop; sleep 1"
scp nanohat-oled root@<device-ip>:/usr/bin/
ssh root@<device-ip> "service nanohat-oled start"
```

## 参考文档

- `docs/adr0005/README.md`
- `docs/adr/0005-ultimate-threaded-architecture.md`
