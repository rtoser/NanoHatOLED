# ADR0006 重新实现计划（分阶段）

本计划聚焦单线程 `libubox/uloop` 架构，删掉自研事件/任务队列，保留 HAL 与驱动可扩展性。

## Phase 0 归档与新主线建立

**状态**：已完成

**任务**
- 归档 ADR0005：`src/` → `src_adr0005/`，`tests/` → `src_adr0005/tests/`
- 创建 ADR0006 文档入口与新 `src/` 根目录

**实际产出**
- `src_adr0005/`
- `docs/adr0006/README.md`
- `docs/adr0006/architecture.md`

## Phase 1 uloop 基础骨架 + 构建基线

**状态**：待开始

**任务**
- 建立新的 `src/` 架构骨架（`main.c`、基础 HAL 头）
- 接入 `libubox/uloop` 主循环与信号处理
- 接入 SIGTERM/SIGINT 并通过 uloop 有序退出
- 保留 HAL 分层（display/gpio/ubus/time），先提供 `*_null` 或 mock 实现
- 构建系统沿用 CMake，保留 u8g2 子模块引入方式
- 仅编译必要的 u8g2 文件，并启用 `-ffunction-sections -fdata-sections` 与 `-Wl,--gc-sections`
- 建立新的测试目录 `tests/`（ADR0006）

**测试**
- `test_uloop_smoke`：主循环启动/退出
- `test_timer_basic`：`uloop_timeout` 基础精度验证

**预计改动文件（核心）**
- `src/main.c`
- `src/hal/*.h`
- `src/CMakeLists.txt`
- `src/u8g2/`（submodule）

**预计改动文件（测试）**
- `tests/Makefile`
- `tests/test_uloop_smoke.c`
- `tests/test_timer_basic.c`

## Phase 2 GPIO 事件接入（uloop_fd）

**状态**：待开始

**任务**
- 迁移/重构 `gpio_hal_libgpiod` 以适配 uloop 回调
- 提供 `gpio_hal_mock`（pipe/eventfd 驱动）用于主机测试
- 按键去抖策略确认（软去抖保留）

**测试**
- `test_gpio_event_uloop`（mock 事件驱动）
- `test_gpio_hw`（Target 验证）

**预计改动文件（核心）**
- `src/hal/gpio_hal.h`
- `src/hal/gpio_hal_libgpiod.c`
- `src/hal/gpio_hal_mock.c`

**预计改动文件（测试）**
- `tests/test_gpio_event_uloop.c`
- `tests/target/test_gpio_hw.c`

## Phase 3 UI 刷新与页面渲染（uloop_timeout）

**状态**：待开始

**任务**
- UI 刷新节奏改为 `uloop_timeout` 驱动
- 迁移页面控制器与动画模块（`page_controller`/`anim`/`pages`）
- 保留 display HAL（SSD1306 + 未来扩展）

**测试**
- `test_ui_controller`（页面逻辑）
- `test_ui_refresh_policy`（动画/静态/息屏刷新策略）

**预计改动文件（核心）**
- `src/ui_controller.c`
- `src/page_controller.c`
- `src/anim.c`
- `src/pages/*.c`
- `src/hal/display_hal*.c`

**预计改动文件（测试）**
- `tests/test_ui_controller.c`
- `tests/test_ui_refresh_policy.c`

## Phase 4 ubus 异步接入（单线程）

**状态**：待开始

**任务**
- 接入 `ubus_invoke_async` + 回调
- 服务配置与 `sys_status` 迁移
- uloop 内统一异步生命周期管理

**测试**
- `test_ubus_async_uloop`（mock）
- `test_ubus_hw`（Target 验证）

**预计改动文件（核心）**
- `src/hal/ubus_hal.h`
- `src/hal/ubus_hal_real.c`
- `src/hal/ubus_hal_mock.c`
- `src/sys_status.c`
- `src/service_config.c`

**预计改动文件（测试）**
- `tests/test_ubus_async_uloop.c`
- `tests/target/test_ubus_hw.c`

## Phase 5 集成调优

**状态**：待开始

**任务**
- 端到端功能验证
- 性能/稳定性测试
- 清理 ADR0005 专用代码与文档指向

## 风险与缓解

- GPIO HAL 适配 uloop：可能需要调整 libgpiod 的事件读取方式，提前用 mock + Target 验证双线覆盖。
- ubus_invoke_async 超时：增加超时回收逻辑与 UI 降级提示，确保回调与状态机可恢复。

**测试**
- `test_e2e_basic`（功能回归）
- 24h 稳定性记录
