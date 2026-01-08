# ADR 0005 重新实现计划（分阶段）

本文件用于跟踪 ADR 0005 的“重新实现”路径，按 Phase 划分任务、测试与预计改动文件清单。

## Phase 1 基础设施

**状态**：已完成

**任务**
- 实现可配置溢出策略的 `ring_queue`（覆盖/拒绝/合并）
- 补齐 HAL 头文件与 `time_hal`，保证 Host 可编译

**测试**
- `test_ring_queue.c` 覆盖覆盖/拒绝/合并策略
- `time_mock` 驱动可控时间
  - Host 运行：`cd tests && make test-host`

**预计改动文件（核心）**
- `src/ring_queue.c`
- `src/ring_queue.h`
- `src/hal/gpio_hal.h`
- `src/hal/display_hal.h`
- `src/hal/ubus_hal.h`
- `src/hal/time_hal.h`

**预计改动文件（测试）**
- `tests/test_ring_queue.c`
- `tests/mocks/time_mock.c`
- `tests/mocks/time_mock.h`
- `tests/Makefile`

**实际产出**
- `src/ring_queue.c`
- `src/ring_queue.h`
- `src/hal/gpio_hal.h`
- `src/hal/display_hal.h`
- `src/hal/ubus_hal.h`
- `src/hal/time_hal.h`
- `tests/test_ring_queue.c`
- `tests/mocks/time_mock.c`
- `tests/mocks/time_mock.h`
- `tests/Makefile`

## Phase 2 GPIO 迁移（libgpiod + 去抖 fallback + 编译期宏）

**状态**：已完成（Host Mock + Target 验证）

**任务**
- 实现 `gpio_hal_libgpiod`（`get_fd()` + `read_edge_events()` + 软件去抖 fallback）
- 实现 `gpio_hal_mock`（eventfd/pipe 可 poll）
- 支持编译期宏 `GPIOCHIP_PATH` 与 `BTN_OFFSETS`

**测试**
- `test_gpio_button.c` 覆盖短/长按、去抖 fallback、`get_fd()` 与 `wait_event()` 一致性
- Target 侧硬件验证
  - Host 运行：`cd tests && make test-host`
  - Target 运行：`make test-target`（待接入）
  - 验证结果：`gpiochip1` + `0/2/3` 可捕获按键事件

**构建说明**
- `gpio_hal_libgpiod.c` 与 `gpio_hal_mock.c` 只能二选一链接
- Host 测试默认使用 Mock；Target/实际运行需链接 libgpiod 实现
- 构建模式：`BUILD=default`（`-O2`）、`BUILD=debug`（`-O0 -g -DDEBUG`）、`BUILD=release`（`-O2 -DNDEBUG`）

**预计改动文件（核心）**
- `src/hal/gpio_hal_libgpiod.c`
- `src/hal/gpio_hal_mock.c`

**预计改动文件（测试）**
- `tests/test_gpio_button.c`
- `tests/mocks/gpio_mock.h`
- `tests/target/test_gpio_hw.c`

**实际产出**
- `src/hal/gpio_hal_libgpiod.c`
- `src/hal/gpio_hal_mock.c`
- `tests/test_gpio_button.c`
- `tests/mocks/gpio_mock.h`
- `tests/target/test_gpio_hw.c`
- `tests/Makefile`

## Phase 3 双线程（事件队列 + 主线程 + UI 线程）

**状态**：进行中（双线程链路与主入口已接入，待业务 UI/显示实现）

**任务**
- 实现事件队列的“关键事件不丢 + tick 合并”
- 主线程 `poll()` 监听 gpiod/timerfd/eventfd
- UI 线程按事件驱动渲染

**测试**
- `test_event_queue` 覆盖关键事件保序、tick 合并
- `test_thread_safety` 并发安全
- Target 侧双线程验证

**预计改动文件（核心）**
- `src/main.c`
- `src/event_queue.c`
- `src/event_queue.h`
- `src/event_loop.c`
- `src/event_loop.h`
- `src/ui_thread.c`
- `src/ui_thread.h`
- `src/ui_controller.c`
- `src/ui_controller.h`
- `src/hal/display_hal_null.c`
- `src/Makefile`

**预计改动文件（测试）**
- `tests/test_event_queue.c`
- `tests/test_event_flow.c`
- `tests/test_thread_safety.c`
- `tests/test_ui_controller.c`
- `tests/test_ui_thread_default.c`
- `tests/mocks/display_mock.c`
- `tests/mocks/display_mock.h`
- `tests/target/test_dual_thread.c`

**实际产出（已完成）**
- `src/main.c`
- `src/event_queue.c`
- `src/event_queue.h`
- `src/event_loop.c`
- `src/event_loop.h`
- `src/ui_thread.c`
- `src/ui_thread.h`
- `src/ui_controller.c`
- `src/ui_controller.h`
- `src/hal/display_hal_null.c`
- `src/Makefile`
- `tests/test_event_queue.c`
- `tests/test_event_flow.c`
- `tests/test_thread_safety.c`
- `tests/test_ui_controller.c`
- `tests/test_ui_thread_default.c`
- `tests/mocks/display_mock.c`
- `tests/mocks/display_mock.h`
- `tests/target/test_dual_thread.c`
- `tests/Makefile`

## Phase 4 三线程（ubus 线程 + 任务/结果队列 + uloop/eventfd）

**任务**
- 实现 `task_queue`/`result_queue`（超时与过期丢弃）
- ubus 线程 `uloop_run()` + `task_eventfd` 唤醒
- 注册 ubus 对象

**测试**
- `test_ubus_async` 覆盖延迟隔离、超时丢弃
- `test_uloop_wakeup` 验证 eventfd 唤醒与优雅退出

**预计改动文件（核心）**
- `src/task_queue.c`
- `src/task_queue.h`
- `src/result_queue.c`
- `src/result_queue.h`
- `src/ubus_thread.c`
- `src/ubus_thread.h`
- `src/hal/ubus_hal_real.c`
- `src/hal/ubus_hal_mock.c`

**预计改动文件（测试）**
- `tests/test_task_queue.c`
- `tests/test_ubus_async.c`
- `tests/test_ubus_object.c`
- `tests/mocks/ubus_mock.c`
- `tests/mocks/ubus_mock.h`
- `tests/target/test_ubus_hw.c`

## 开发日志（Dev Log）

### 2026-01-07：交叉编译失败排查记录

**现象**：
- `main.c` 编译时报 `struct sigaction` 未知、`sigaction/sigemptyset` 隐式声明
- `time_hal_real.c` / `event_loop.c` / `event_queue.c` 报 `CLOCK_MONOTONIC/CLOCK_REALTIME` 未声明
- 链接报 `event_loop.o: file format not recognized`

**原因**：
1) OpenWrt SDK 的 musl 头文件默认不暴露部分 POSIX API，需要在包含头文件前定义功能宏。  
2) 复用本地编译生成的 `.o`（宿主机格式）进行交叉链接，导致目标架构不匹配。

**修复**：
- 在相关源文件顶部添加 `#define _POSIX_C_SOURCE 200809L`（`event_loop.c` 额外加 `_GNU_SOURCE`），再包含头文件。  
- `build_in_docker.sh` 中在 `make` 前执行 `make clean`，避免混用宿主机对象文件。

## Phase 5 集成与替换（新 main + 全流程联调）

**任务**
- 重写 `src/main.c` 组装三线程
- 将旧的 GPIO/ubus 路径切换到 HAL/线程实现
- 按需清理旧路径

**测试**
- Target 端到端功能 + 性能指标 + 稳定性（24h）

**预计改动文件（核心）**
- `src/main.c`
- `src/sys_status.c`（如需改为异步结果更新）
- `src/Makefile`
- `src/compile.sh`
- `src/build_in_docker.sh`

**预计改动文件（测试）**
- `tests/target/*`
- 必要的脚本/基准采集工具
