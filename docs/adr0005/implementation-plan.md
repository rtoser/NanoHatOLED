# ADR 0005 重新实现计划（分阶段）

本文件用于跟踪 ADR 0005 的“重新实现”路径，按 Phase 划分任务、测试与预计改动文件清单。

> 说明：ADR0005 已归档，新实现主线为 ADR0006（参见 `docs/adr0006/`）。

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

## Phase 4 三线程（ubus 线程 + 任务/结果队列 + eventfd）

**状态**：已完成（Host 测试通过，待 Target 验证）

**任务**
- 实现 `task_queue`/`result_queue`（超时与过期丢弃）
- ubus 线程 + `eventfd` 唤醒
- 支持同 service/action 合并（只保留最后一次意图）
- 支持 `mark_abandoned` 机制（UI 线程放弃等待时标记）

**测试**
- `test_task_queue` 覆盖 push/pop、合并、满队列拒绝、超时判断
- `test_result_queue` 覆盖 push/pop、abandoned 标记与替换
- `test_ubus_async` 覆盖延迟隔离、超时丢弃、优雅退出
- Host 运行：`cd tests && make test-host`

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
- `tests/test_result_queue.c`
- `tests/test_ubus_async.c`
- `tests/mocks/ubus_mock.h`
- `tests/Makefile`

**实际产出**
- `src/task_queue.c`
- `src/task_queue.h`
- `src/result_queue.c`
- `src/result_queue.h`
- `src/ubus_thread.c`
- `src/ubus_thread.h`
- `src/hal/ubus_hal_real.c`
- `src/hal/ubus_hal_mock.c`
- `tests/test_task_queue.c`
- `tests/test_result_queue.c`
- `tests/test_ubus_async.c`
- `tests/mocks/ubus_mock.h`
- `tests/Makefile`

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

## Phase 5 三线程组装

**状态**：已完成（Host 测试通过 + Target 编译通过）

**任务**
- 改造 `main.c`：组装主线程 + UI 线程 + ubus 线程
- 更新 `src/Makefile`：添加 `task_queue`、`result_queue`、`ubus_thread`
- ubus 使用 mock 实现，验证三线程协作

**测试**
- Host 编译通过：`cd tests && make test-host`
- Target 编译通过：`cd src && docker run ... sh build_in_docker.sh`

**预计改动文件（核心）**
- `src/main.c`
- `src/Makefile`
- `src/build_in_docker.sh`

**实际产出**
- `src/main.c`（三线程组装：主线程 + UI 线程 + ubus 线程）
- `src/Makefile`（添加 task_queue、result_queue、ubus_thread、ubus_hal_mock）

## Phase 6 真实 ubus 接入

**状态**：已完成（Host 测试通过，待 Target 验证）

**任务**
- 实现编译期服务配置机制 (`service_config.h/c`)
- 完善 `ubus_hal_real.c`：接入 libubus/libubox（rc.list / rc.init）
- 实现 `sys_status` 模块：异步服务状态查询 + 本地系统信息采集
- 更新 `ubus_hal.h`：添加 installed/running 字段

**构建说明**
- 服务列表通过编译期宏配置：`make MONITORED_SERVICES="svc1,svc2"`
- 默认监控服务：`dropbear,uhttpd`
- Target 构建需链接：`-lubus -lubox -lblobmsg_json`

**测试**
- Host 测试：`make test-host`（10/10 通过）
- Target 测试：`make test-target-ubus MONITORED_SERVICES="dropbear,uhttpd"`

**预计改动文件（核心）**
- `src/service_config.h`
- `src/service_config.c`
- `src/hal/ubus_hal.h`
- `src/hal/ubus_hal_real.c`
- `src/hal/ubus_hal_mock.c`
- `src/sys_status.h`
- `src/sys_status.c`
- `src/Makefile`

**预计改动文件（测试）**
- `tests/target/test_ubus_hw.c`
- `tests/Makefile`

**实际产出**
- `src/service_config.h`（编译期服务列表配置）
- `src/service_config.c`（MONITORED_SERVICES 解析）
- `src/hal/ubus_hal.h`（添加 installed/running 字段）
- `src/hal/ubus_hal_real.c`（libubus 接入：query_service, action_service）
- `src/hal/ubus_hal_mock.c`（更新 QUERY 模拟）
- `src/sys_status.h`（异步状态查询接口）
- `src/sys_status.c`（/proc 信息采集 + 异步服务查询）
- `src/Makefile`（添加 MONITORED_SERVICES 宏、libubus 链接）
- `tests/target/test_ubus_hw.c`（Target ubus 硬件测试）
- `tests/Makefile`（添加 test-target-ubus）

## Phase 7 UI 业务与显示

**状态**：已完成（Host 测试通过，待 Target OLED 验证）

**任务**
- [x] 完善 `ui_controller`：多页面支持（K1/K3 切换）
- [x] 系统信息页：CPU、内存、温度、IP、uptime
- [x] 网络页：IP、流量统计
- [x] 服务状态页：显示服务运行状态（支持进入模式）
- [x] 页面控制器架构（page.h, page_controller.h/c, anim.h/c）
- [x] u8g2 测试桩（主机测试）
- [x] 设计 display_hal 抽象（支持多驱动：SSD1306、未来 SSD1302）
- [x] 实现 `display_hal_ssd1306.c`：集成 u8g2 驱动
- [x] 动画帧率与 ui_thread tick 机制对接

**测试**
- Host 测试通过（10/10）
- Target OLED 正常显示（待验证）
- 页面切换动画流畅（待验证）

**UI 设计文档**
- `docs/adr0005/ui-design-spec.md`：详细 UI 规格说明

**Display HAL 设计说明**
- 采用插件式架构，通过 `display_hal_ops_t` 接口抽象
- Host 测试使用 `display_hal_null.c` + `u8g2_stub.c`
- Target 使用 `display_hal_ssd1306.c` + u8g2 库（从源码编译）
- 未来可扩展其他驱动（如 SSD1302）

**构建说明**
- Target 构建需要 u8g2 源码：`U8G2_SRC_DIR=u8g2/csrc`
- Host 测试使用 mock：`tests/mocks/display_mock.c`

**预计改动文件（核心）**
- `src/page.h`
- `src/anim.h`
- `src/anim.c`
- `src/page_controller.h`
- `src/page_controller.c`
- `src/pages/pages.h`
- `src/pages/page_home.c`
- `src/pages/page_network.c`
- `src/pages/page_services.c`
- `src/ui_controller.c`
- `src/ui_controller.h`
- `src/ui_thread.c`
- `src/hal/display_hal.h`
- `src/hal/u8g2_stub.h`
- `src/hal/u8g2_stub.c`
- `src/hal/display_hal_null.c`
- `src/hal/display_hal_ssd1306.c`
- `src/Makefile`

**预计改动文件（测试）**
- `tests/test_ui_controller.c`
- `tests/test_ui_thread_default.c`
- `tests/mocks/sys_status_mock.c`
- `tests/mocks/display_mock.c`
- `tests/mocks/display_mock.h`
- `tests/Makefile`

**实际产出（已完成）**
- `src/page.h`（页面接口定义）
- `src/anim.h`（动画类型、时长常量）
- `src/anim.c`（缓动函数、滑动/抖动计算）
- `src/page_controller.h`（页面控制器接口）
- `src/page_controller.c`（页面切换、动画、屏幕状态管理）
- `src/pages/pages.h`（页面注册列表）
- `src/pages/page_home.c`（首页：hostname、CPU、内存、uptime）
- `src/pages/page_network.c`（网络页：IP、流量）
- `src/pages/page_services.c`（服务页：列表、进入模式选择）
- `src/ui_controller.h`（集成 page_controller）
- `src/ui_controller.c`（事件转发、状态同步）
- `src/ui_thread.c`（动画帧率与 tick 机制对接）
- `src/hal/display_hal.h`（显示驱动抽象接口）
- `src/hal/u8g2_stub.h`（u8g2 测试桩头文件）
- `src/hal/u8g2_stub.c`（u8g2 测试桩实现）
- `src/hal/display_hal_null.c`（null 驱动，用于主机测试）
- `src/hal/display_hal_ssd1306.c`（SSD1306 I2C 驱动）
- `src/sys_status.h`（添加 struct tag）
- `src/Makefile`（Target 构建配置，包含 u8g2 编译）
- `tests/test_ui_controller.c`（更新测试用例）
- `tests/test_ui_thread_default.c`（更新测试用例）
- `tests/mocks/sys_status_mock.c`（系统状态 mock）
- `tests/mocks/display_mock.c`（显示 mock）
- `tests/mocks/display_mock.h`（显示 mock 头文件）
- `tests/Makefile`（更新依赖）

## Phase 8 集成调优

**状态**：待开始

**任务**
- 端到端功能测试
- 性能调优（内存、CPU 占用）
- 稳定性测试（24h 运行）
- 清理旧代码（`src_old` 可归档或删除）

**测试**
- Target 端到端功能验证
- 性能指标采集
- 稳定性报告

**预计改动文件**
- 按需调整各模块
- 清理 `src_old`（可选）
