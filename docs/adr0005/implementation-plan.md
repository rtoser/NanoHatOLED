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

**状态**：已完成（Host Mock）

**任务**
- 实现 `gpio_hal_libgpiod`（`get_fd()` + `read_edge_events()` + 软件去抖 fallback）
- 实现 `gpio_hal_mock`（eventfd/pipe 可 poll）
- 支持编译期宏 `GPIOCHIP_PATH` 与 `BTN_OFFSETS`

**测试**
- `test_gpio_button.c` 覆盖短/长按、去抖 fallback、`get_fd()` 与 `wait_event()` 一致性
- Target 侧硬件验证
  - Host 运行：`cd tests && make test-host`
  - Target 运行：`make test-target`（待接入）

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

**任务**
- 实现事件队列的“关键事件不丢 + tick 合并”
- 主线程 `poll()` 监听 gpiod/timerfd/eventfd
- UI 线程按事件驱动渲染

**测试**
- `test_event_queue` 覆盖关键事件保序、tick 合并
- `test_thread_safety` 并发安全
- Target 侧双线程验证

**预计改动文件（核心）**
- `src/event_queue.c`
- `src/event_queue.h`
- `src/event_loop.c`
- `src/event_loop.h`
- `src/ui_thread.c`
- `src/ui_thread.h`
- `src/hal/display_hal_mock.c`

**预计改动文件（测试）**
- `tests/test_event_queue.c`
- `tests/test_event_flow.c`
- `tests/test_thread_safety.c`
- `tests/mocks/display_mock.c`
- `tests/mocks/display_mock.h`
- `tests/target/test_dual_thread.c`

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
