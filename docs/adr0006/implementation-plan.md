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

**状态**：已完成

**任务**
- 建立新的 `src/` 架构骨架（`main.c`、基础 HAL 头）
- 接入 `libubox/uloop` 主循环与信号处理
- 接入 SIGTERM/SIGINT 并通过 uloop 有序退出
- 保留 HAL 分层（display/gpio/ubus/time），先提供 `*_null` 或 mock 实现
- 构建系统沿用 CMake，保留 u8g2 子模块引入方式
- 仅编译必要的 u8g2 文件，并启用 `-ffunction-sections -fdata-sections` 与 `-Wl,--gc-sections`
- 建立新的测试目录 `tests/`（ADR0006）

**说明**
- macOS 宿主机无法原生运行 `libubox/uloop`（依赖 Linux epoll/signalfd/timerfd）；需在 Linux 环境/Docker 中运行，或仅交叉编译到目标设备执行。

**实际产出**
- `src/main.c` - uloop 主循环 + sigaction 信号处理
- `src/hal/display_hal.h` + `display_hal_null.c`
- `src/hal/time_hal.h` + `time_hal_real.c`
- `src/hal/u8g2_stub.h` + `u8g2_stub.c`
- `src/CMakeLists.txt` + `cmake/toolchain-openwrt-aarch64.cmake`
- `src/u8g2/`（submodule）
- `src/build_in_docker.sh`
- `tests/CMakeLists.txt`
- `tests/test_uloop_smoke.c`
- `tests/test_timer_basic.c`

**验证**
- Docker 交叉编译成功，输出 83KB ARM64 可执行文件

**测试**
- `test_uloop_smoke`：主循环启动/退出
- `test_timer_basic`：`uloop_timeout` 基础精度验证（需主机安装 libubox-dev）

**预计改动文件（核心）**
- `src/main.c`
- `src/hal/*.h`
- `src/CMakeLists.txt`
- `src/u8g2/`（submodule）

**预计改动文件（测试）**
- `tests/CMakeLists.txt`
- `tests/test_uloop_smoke.c`
- `tests/test_timer_basic.c`

## Phase 2 GPIO 事件接入（uloop_fd）

**状态**：✅ 已完成

**任务**
- [x] 迁移/重构 `gpio_hal_libgpiod` 以适配 uloop 回调
- [x] 提供 `gpio_hal_mock`（pipe/eventfd 驱动）用于主机测试
- [x] 按键去抖策略确认（软去抖保留，硬件去抖优先）
- [x] 更新 main.c 集成 GPIO 到 uloop_fd
- [x] 更新 CMakeLists.txt（BUILD_MODE 开关、libgpiod 链接）
- [x] Code review 修复

**设计决策**
- GPIO HAL 接口改为非阻塞：`read_event()` 返回 1/0/-1（有事件/无事件/错误）
- `get_fd()` 返回可 poll 的 fd，供 uloop_fd 监控
- 长按在阈值触发时立即生成，短按在释放时生成（避免“抬起才响应”的延迟）
- `get_timer_fd()`（可选）用于长按阈值触发，uloop 同时监听
- libgpiod v2 API，支持硬件去抖（fallback 软件去抖 30ms）

**实际产出**
- `src/hal/gpio_hal.h` - 非阻塞接口定义
- `src/hal/gpio_hal_libgpiod.c` - libgpiod v2 实现
- `src/hal/gpio_hal_mock.c` - mock 实现 + 测试注入 API
- `src/main.c` - 集成 uloop_fd 回调
- `src/CMakeLists.txt` - BUILD_MODE + GPIO_DEBUG + libgpiod
- `tests/test_gpio_event_uloop.c` - GPIO uloop 集成测试

**验证**
- Docker 交叉编译成功，输出 99KB ARM64 可执行文件

**Code Review 修复记录**
1. `gpio_hal_mock.c`: 修复 eventfd 路径下 double-close 问题（保存旧值后再置 -1）
2. `test_gpio_event_uloop.c`: 添加 `#include <stdbool.h>` 修复 C11 编译
3. `gpio_hal_libgpiod.c`: `EAGAIN/EWOULDBLOCK` 返回 0（无事件）而非 -1（错误）

**测试**
- `test_gpio_event_uloop`（mock 事件驱动）
  - long press threshold 即时触发
  - long press 后释放不再触发短按
  - release-before-threshold 仅触发短按
  - debounce 过滤 30ms 内抖动
- `test_gpio_hw`（Target 验证，待硬件测试）
 - 目标板自动化：`tests/target/run_unit_ssh.sh`（Docker 交叉编译 + SSH 执行）

**改动文件**
- `src/hal/gpio_hal.h`
- `src/hal/gpio_hal_libgpiod.c`
- `src/hal/gpio_hal_mock.c`
- `tests/test_gpio_event_uloop.c`

## Phase 3 UI 刷新与页面渲染（uloop_timeout）

**状态**：✅ 已完成

**任务**
- [x] UI 刷新节奏改为 `uloop_timeout` 驱动
- [x] 迁移页面控制器与动画模块（`page_controller`/`anim`/`pages`）
- [x] 保留 display HAL（SSD1306 + 未来扩展）
- [x] 新增 Gateway 页面（位于 Home 与 Network 之间）
- [x] 抽取 `sys_status_format_speed_bps()` 共享工具函数

**设计决策**
- UI 刷新策略：50ms（动画）/ 1000ms（静态）/ 0（息屏）
- 动画状态机：IDLE → STATIC → ANIMATING → TRANSITION
- main.c 集成 `ui_controller_tick()` + `ui_controller_render()` + `schedule_ui_timer()`
- 页面顺序：Home → Gateway → Network → Services

**实际产出**
- `src/ui_controller.c` / `src/ui_controller.h` - UI 控制器（状态机 + 定时器策略）
- `src/page_controller.c` / `src/page_controller.h` - 页面控制器
- `src/anim.c` / `src/anim.h` - 动画模块
- `src/page.h` - 页面接口定义
- `src/pages/pages.h` - 页面注册
- `src/pages/page_home.c` - Home 页
- `src/pages/page_gateway.c` - Gateway 页（新增）
- `src/pages/page_network.c` - Network 页
- `src/pages/page_services.c` - Services 页
- `src/sys_status.c` / `src/sys_status.h` - 系统状态（含 format 工具函数）
- `src/service_config.c` / `src/service_config.h` - 服务配置
- `src/fonts.c` / `src/fonts.h` - 字体定义
- `src/hal/display_hal_ssd1306.c` - SSD1306 显示驱动
- `tests/test_ui_controller.c` - UI 控制器测试
- `tests/test_ui_refresh_policy.c` - 刷新策略测试

**验证**
- Docker 交叉编译成功
- 主机单元测试通过（test_ui_controller, test_ui_refresh_policy）

**测试**
- `test_ui_controller`（页面逻辑）
- `test_ui_refresh_policy`（动画/静态/息屏刷新策略）

## Phase 4 ubus 异步接入（单线程）

**状态**：🚧 进行中

**任务**
- [x] 设计 ubus HAL 异步接口（精简版）
- [x] 实现 `ubus_hal_mock.c`（uloop_timeout 模拟延迟）
- [x] 实现 `ubus_hal_real.c`（ubus_invoke_async + ubus_add_uloop）
- [x] 集成 `sys_status.c` 异步查询（callback + request_id）
- [x] 更新 `page_services.c` UI（pending="...", timeout="--"）
- [x] 创建 `test_ubus_async_uloop.c` 单元测试
- [x] 请求超时保护（uloop_timeout 3秒）
- [x] 懒重连机制（rpcd 重启自动恢复）
- [x] Docker 构建验证
- [ ] Target 硬件验证

**设计决策**
- HAL 接口精简：`init()` + `cleanup()` + `query_service_async()`
- 请求超时：`uloop_timeout` 实现（libubus async API 无内置超时）
- 懒重连：错误时重置 `g_rc_id`，下次请求自动重新 lookup
- 每个 service 保存 `request_id` 防止旧响应覆盖新状态
- 回调状态码复用 libubus UBUS_STATUS_* 定义

**超时与重连机制**
```
请求发起 → 启动 uloop_timeout(3s)
    ↓
正常完成 → complete_cb → 取消 timeout → 更新状态
    or
超时触发 → timeout_cb → abort 请求 → 回调 TIMEOUT
    or
rpcd 重启 → complete_cb 收到 NOT_FOUND → 重置 rc_id → 下次自动重连
```

**实际产出**
- `src/hal/ubus_hal.h` - 异步接口定义
- `src/hal/ubus_hal_mock.c` - mock 实现（超时保护 + HANG 模式）
- `src/hal/ubus_hal_real.c` - real 实现（超时保护 + 懒重连）
- `src/sys_status.c` - 集成 `sys_status_query_services()` + callback
- `src/sys_status.h` - 新增查询 API
- `src/pages/page_services.c` - UI 状态显示更新
- `tests/test_ubus_async_uloop.c` - 异步查询单元测试
- `tests/CMakeLists.txt` - 新增测试配置 + ui_draw.c
- `tests/target/run_unit_ssh.sh` - 添加 test_ubus_async_uloop 到目标测试列表
- `src/CMakeLists.txt` - 添加 ubus HAL + libubus 链接

**Mock 测试 API**
```c
void ubus_mock_set_response(const char *service, int status,
                            bool installed, bool running, int delay_ms);
void ubus_mock_set_timeout(int timeout_ms);  /* 配置超时阈值 */
#define MOCK_DELAY_HANG (-1)  /* 永不响应，触发超时 */
```

**测试**
- `test_ubus_async_uloop`（mock 异步查询、HANG 模式超时触发、并发）
- Target 验证：`tests/target/run_unit_ssh.sh`

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
