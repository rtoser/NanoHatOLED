# 测试架构设计

> 用例清单与运行方式详见：`../../tests/README.md`

*   **版本**: 1.0
*   **日期**: 2026-01-05
*   **关联**: ADR 0005 (终极三线程架构)

## 1. 概述

本文档定义 NanoHatOLED 项目的测试策略，核心目标是在嵌入式开发场景下实现**快速迭代**和**可靠验证**的平衡。

### 1.1 测试金字塔

```
        ┌─────────────┐
        │  系统测试    │  ← Target 设备，手动/脚本验证
        │  (Target)   │     验证完整功能
        └──────┬──────┘
               │
        ┌──────▼──────┐
        │  集成测试    │  ← Target 设备，自动化测试程序
        │  (Target)   │     验证硬件交互
        └──────┬──────┘
               │
        ┌──────▼──────┐
        │  单元测试    │  ← Host (macOS/Linux)，Mock 硬件
        │   (Host)    │     快速迭代，CI 可运行
        └─────────────┘
```

### 1.2 双环境策略

| 环境 | 编译器 | HAL 实现 | 用途 |
|------|--------|----------|------|
| **Host** (macOS/Linux) | `gcc` (native) | Mock | 单元测试、逻辑验证、CI |
| **Target** (OpenWrt) | `aarch64-openwrt-linux-musl-gcc` | Real | 集成测试、系统测试 |

### 1.3 当前实现状态（摘要）

- Phase 1 与 Phase 2 已完成 Host Mock 测试
- Host 运行：`cd tests && make test-host`（执行 `test_ring_queue` 与 `test_gpio_button`）
- Target 侧已有 `tests/target/test_gpio_hw.c` 与 `tests/target/test_dual_thread.c`，`tests/Makefile` 提供 `test-target`/`test-target-dual`

## 2. 硬件抽象层 (HAL)

### 2.1 设计原则

通过接口抽象隔离硬件依赖，支持编译时切换实现（`wait_event()` 与 `get_fd()` 必须语义一致，便于 poll/uloop 集成）：

```c
// hal/gpio_hal.h
typedef struct {
    int  (*init)(void);
    void (*cleanup)(void);
    int  (*wait_event)(int timeout_ms, gpio_event_t *event);
    int  (*get_fd)(void);  // 用于 poll/epoll 集成
} gpio_hal_ops_t;

extern const gpio_hal_ops_t *gpio_hal;
```

### 2.2 HAL 模块

| 模块 | 接口文件 | 真实实现 | Mock 实现 |
|------|----------|----------|-----------|
| GPIO | `gpio_hal.h` | `gpio_hal_libgpiod.c` | `gpio_hal_mock.c` |
| 显示 | `display_hal.h` | `display_hal_u8g2.c` | `display_hal_mock.c` |
| ubus | `ubus_hal.h` | `ubus_hal_real.c` | `ubus_hal_mock.c` |
| 时间 | `time_hal.h` | `time_hal_real.c` | `time_hal_mock.c` |

### 2.3 编译时切换

```makefile
# Host 测试构建
test-host: CFLAGS += -DHAL_MOCK
test-host: SRCS += $(wildcard hal/*_mock.c)

# Target 生产构建
release: CFLAGS += -DHAL_REAL -DGPIOCHIP_PATH=\"/dev/gpiochip1\" -DBTN_OFFSETS=\"0,2,3\"
release: SRCS += hal/gpio_hal_libgpiod.c hal/ubus_hal_real.c
```

当前测试构建入口：`tests/Makefile`。

## 3. 分阶段测试计划

### 3.1 Phase 1: 基础设施

**目标**: 队列库 + HAL 接口定义

**交付物**:
```
src/
├── ring_queue.c/h          # 通用环形队列
└── hal/
    ├── gpio_hal.h          # GPIO 抽象接口
    ├── display_hal.h       # 显示抽象接口
    ├── ubus_hal.h          # ubus 抽象接口
    └── time_hal.h          # 时间抽象接口

tests/
├── test_ring_queue.c       # 队列单元测试
└── mocks/
    └── time_mock.c         # 时间 Mock 基础实现
```

**Host 单元测试**:

| 测试用例 | 验证点 |
|----------|--------|
| `test_queue_init` | 初始化后队列为空 |
| `test_queue_push_pop` | 基本入队出队 |
| `test_queue_full` | 队列满时正确报告 |
| `test_queue_overflow_policy_configurable` | 溢出策略按配置生效（覆盖/拒绝/合并） |
| `test_queue_empty_pop` | 空队列 pop 返回失败 |
| `test_queue_thread_safety` | 多线程并发 push/pop |

**测试代码示例**:
```c
// tests/test_ring_queue.c
void test_queue_overflow_replaces_oldest(void) {
    ring_queue_t q;
    ring_queue_init(&q, 4, sizeof(int));
    ring_queue_set_overflow_policy(&q, RQ_OVERWRITE_OLDEST);

    int vals[] = {0, 1, 2, 3, 4, 5};
    for (int i = 0; i < 6; i++) {
        ring_queue_push(&q, &vals[i]);
    }

    int out;
    ring_queue_pop(&q, &out);
    TEST_ASSERT_EQUAL(2, out);  // 0,1 被覆盖
}
```

**验收标准**:
- [ ] 所有队列单元测试通过
- [ ] 队列支持任意元素类型
- [ ] 队列溢出策略可配置
- [ ] HAL 接口定义完整，可编译

---

### 3.2 Phase 2: GPIO 迁移 (libgpiod)

**目标**: 使用 libgpiod v2.x 重写 GPIO 按键驱动

**交付物**:
```
src/hal/
├── gpio_hal_libgpiod.c     # libgpiod v2.x 实现
└── gpio_hal_mock.c         # GPIO Mock 实现

tests/
├── test_gpio_button.c      # Host 按键逻辑测试
└── target/
    └── test_gpio_hw.c      # Target 硬件验证
```

**Host 单元测试** (使用 Mock):

| 测试用例 | 验证点 |
|----------|--------|
| `test_short_press_k1` | K1 短按 (<600ms) 检测 |
| `test_short_press_k2` | K2 短按检测 |
| `test_short_press_k3` | K3 短按检测 |
| `test_long_press_k1` | K1 长按 (>=600ms) 检测 |
| `test_long_press_k2` | K2 长按检测 |
| `test_long_press_k3` | K3 长按检测 |
| `test_debounce` | 30ms 内抖动被过滤 |
| `test_debounce_fallback_soft` | 驱动不支持硬件去抖时的软件去抖路径 |
| `test_press_during_press` | 一键按住时另一键按下 |
| `test_timeout_no_event` | 超时返回 BTN_NONE |
| `test_rapid_presses` | 快速连续按键 |
| `test_gpio_fd_wakeup` | `get_fd()` 可被 poll 唤醒并与 `wait_event()` 一致 |
| `test_event_queue` | tick 合并与关键事件兜底 |
| `test_thread_safety` | event_queue 并发 push/pop |
| `test_event_flow` | 事件循环与 UI 线程基础链路（Linux 下） |
| `test_ui_controller` | UI 事件处理/渲染、唤醒时间戳与自动息屏 |
| `test_ui_thread_default` | ui_thread 默认 handler 集成路径 |

**Mock 能力**:
```c
// tests/mocks/gpio_mock.h（由 gpio_hal_mock 提供）
void gpio_mock_inject_edge(int line, edge_type_t type, uint64_t timestamp_ns);
void gpio_mock_set_line_value(int line, int value);
void gpio_mock_clear_events(void);
int  gpio_mock_get_pending_count(void);
int  gpio_mock_get_fd(void);  // 返回可 poll 的 eventfd/pipe
void gpio_mock_set_debounce_supported(bool supported);
```

**测试代码示例**:
```c
// tests/test_gpio_button.c
void test_long_press_detection(void) {
    gpio_mock_clear_events();

    // 模拟按下 K2 (line 2)
    gpio_mock_inject_edge(2, EDGE_FALLING, 0);

    // 时间推进 600ms
    time_mock_advance_ms(600);

    // 模拟松开（长按在松开时判定）
    gpio_mock_inject_edge(2, EDGE_RISING, 600000000ULL);

    // 检查长按事件
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(100, &evt);

    TEST_ASSERT_EQUAL(1, ret);
    TEST_ASSERT_EQUAL(BTN_K2_LONG_PRESS, evt.type);
}
```

**Target 集成测试**:

| 测试用例 | 操作 | 预期 |
|----------|------|------|
| `test_k1_hw` | 手动按 K1 | 检测到 K1 事件 |
| `test_k2_hw` | 手动按 K2 | 检测到 K2 事件 |
| `test_k3_hw` | 手动按 K3 | 检测到 K3 事件 |
| `test_long_press_hw` | 按住 K2 > 1s 后松开 | 检测到长按事件 |
| `test_gpio_fd_pollable` | 获取 fd 并 poll | fd 有效且可 poll |

**Target 测试程序**:
```c
// tests/target/test_gpio_hw.c
int main(void) {
    printf("=== GPIO Hardware Test ===\n");
    gpio_hal->init();

    printf("Press K1 within 5 seconds...\n");
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(5000, &evt);

    if (ret > 0 && evt.line == 0) {
        printf("PASS: K1 detected, type=%d\n", evt.type);
    } else {
        printf("FAIL: ret=%d, line=%d\n", ret, evt.line);
    }

    gpio_hal->cleanup();
    return (ret > 0) ? 0 : 1;
}
```

**验收标准**:
- [ ] 所有 Host Mock 测试通过
- [ ] Target 上 K1/K2/K3 短按、长按均可检测
- [ ] gpioinfo 显示 consumer 为 "nanohat-oled"
- [ ] 与现有单线程主循环集成，功能不退化

---

### 3.3 Phase 3: 双线程 (主线程 + UI 线程)

**目标**: 实现事件队列，分离事件监听与 UI 渲染

**交付物**:
```
src/
├── event_queue.c/h         # 事件队列（基于 ring_queue）
├── event_loop.c/h          # 主线程：GPIO + timerfd
├── ui_thread.c/h           # UI 线程：事件消费 + 渲染
└── hal/
    └── display_hal_mock.c  # 显示 Mock

tests/
├── test_event_queue.c      # 事件队列测试
├── test_event_flow.c       # 事件流转测试
├── test_thread_safety.c    # 并发安全测试
└── target/
    └── test_dual_thread.c  # Target 双线程验证
```

**Host 单元测试**:

| 测试用例 | 验证点 |
|----------|--------|
| `test_event_queue_basic` | 事件入队出队 |
| `test_event_queue_blocking_pop` | 空队列阻塞等待 |
| `test_event_queue_timeout` | 等待超时返回 |
| `test_event_queue_signal` | push 后唤醒等待线程 |
| `test_event_queue_shutdown` | 广播唤醒所有等待者 |
| `test_event_overflow` | 溢出策略按事件类型生效 |
| `test_event_no_drop_critical` | 关键事件不丢且保序 |
| `test_event_coalesce_ticks` | tick 事件可合并去重 |

**Host 并发测试**:

| 测试用例 | 验证点 |
|----------|--------|
| `test_concurrent_push_pop` | 生产者/消费者并发 |
| `test_burst_events` | 突发大量事件 |
| `test_slow_consumer` | 消费者慢于生产者 |
| `test_graceful_shutdown` | 优雅退出无死锁 |
| `test_wakeup_via_eventfd` | eventfd 唤醒主循环 |

**测试代码示例**:
```c
// tests/test_thread_safety.c
void test_concurrent_push_pop(void) {
    event_queue_t q;
    event_queue_init(&q, 16);

    atomic_int produced = 0;
    atomic_int consumed = 0;

    // 生产者线程
    pthread_t producer;
    pthread_create(&producer, NULL, producer_func, &q);

    // 消费者（主线程）
    while (consumed < 1000) {
        event_t e;
        if (event_queue_pop(&q, &e, 100)) {
            consumed++;
        }
    }

    pthread_join(producer, NULL);
    TEST_ASSERT_EQUAL(1000, consumed);
}
```

**事件流转测试**:
```c
// tests/test_event_flow.c
void test_button_to_ui_flow(void) {
    // 初始化双线程环境
    event_queue_t eq;
    event_queue_init(&eq, 16);

    // 模拟主线程推送按键事件
    event_t btn_evt = { .type = EVT_BUTTON_K1_SHORT, .timestamp_ms = 1000 };
    event_queue_push(&eq, &btn_evt);

    // 模拟 UI 线程消费
    event_t recv;
    bool got = event_queue_pop(&eq, &recv, 100);

    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL(EVT_BUTTON_K1_SHORT, recv.type);
}
```

**Display Mock 能力**:
```c
// tests/mocks/display_mock.c
typedef struct {
    int frame_count;
    int last_page;
    char last_text[256];
} display_mock_state_t;

void display_mock_reset(void);
display_mock_state_t *display_mock_get_state(void);
```

**Target 集成测试**:

| 测试用例 | 操作 | 预期 |
|----------|------|------|
| `test_button_response` | 按 K2 | UI 响应，页面切换 |
| `test_animation_fps` | 触发动画 | 帧率稳定 ~30fps |
| `test_button_during_animation` | 动画中按键 | 按键不丢失 |
| `test_shutdown` | 发送 SIGTERM | 优雅退出，无残留线程 |
| `test_shutdown_during_wait` | GPIO 阻塞时退出 | 仍可及时退出 |
| `test_dual_thread` | event_loop + UI 线程 | 验证按键、tick、自动息屏与唤醒流程 |

**验收标准**:
- [ ] 所有 Host 单元测试通过
- [ ] 并发压力测试无死锁
- [ ] Target 上按键响应 < 50ms
- [ ] 动画期间按键不丢失
- [ ] 优雅退出无资源泄漏

---

### 3.4 Phase 4: 三线程 (+ ubus 线程)

**目标**: 添加 ubus 工作线程，实现完全异步服务操作

**交付物**:
```
src/
├── task_queue.c/h          # 任务队列
├── result_queue.c/h        # 结果队列
├── ubus_thread.c/h         # ubus 线程
└── hal/
    └── ubus_hal_mock.c     # ubus Mock

tests/
├── test_task_queue.c       # 任务队列测试
├── test_ubus_async.c       # 异步 ubus 测试
├── test_ubus_object.c      # ubus 对象测试
└── target/
    └── test_ubus_hw.c      # Target ubus 验证
```

**Host 单元测试**:

| 测试用例 | 验证点 |
|----------|--------|
| `test_task_queue_basic` | 任务入队出队 |
| `test_result_queue_basic` | 结果入队出队 |
| `test_ubus_async_start` | 异步启动服务 |
| `test_ubus_async_stop` | 异步停止服务 |
| `test_ubus_latency_isolation` | ubus 延迟不阻塞 UI |
| `test_ubus_failure_handling` | ubus 失败正确报告 |
| `test_ubus_object_status` | status 方法返回正确信息 |
| `test_ubus_object_restart` | restart 方法触发退出标志 |
| `test_uloop_wakeup_task_eventfd` | 任务入队可唤醒 `uloop_run()` |
| `test_uloop_graceful_shutdown` | 退出信号能终止 uloop 并回收资源 |
| `test_task_timeout_drop` | 任务超时被丢弃并返回超时结果 |

**ubus Mock 能力**:
```c
// tests/mocks/ubus_mock.c
void ubus_mock_set_latency_ms(int ms);           // 模拟调用延迟
void ubus_mock_set_next_result(bool success);    // 模拟成功/失败
void ubus_mock_inject_service_state(const char *name, bool running);
int  ubus_mock_get_call_count(void);
```

**测试代码示例**:
```c
// tests/test_ubus_async.c
void test_ubus_latency_not_blocking_ui(void) {
    ubus_mock_set_latency_ms(2000);  // 模拟 2 秒延迟

    uint64_t start = time_mock_now_ms();

    // 发起异步操作
    ubus_task_t task = { .action = ACTION_STOP, .service = "xray" };
    task_queue_push(&task);

    // 验证立即返回
    uint64_t elapsed = time_mock_now_ms() - start;
    TEST_ASSERT_LESS_THAN(10, elapsed);

    // 等待结果
    time_mock_advance_ms(2500);
    ubus_result_t result;
    bool got = result_queue_pop(&result, 100);

    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_TRUE(result.success);
}
```

**Target 集成测试**:

| 测试用例 | 操作 | 预期 |
|----------|------|------|
| `test_service_start` | 通过 UI 启动 xray | 服务启动，状态更新 |
| `test_service_stop` | 通过 UI 停止 xray | 服务停止，状态更新 |
| `test_ubus_list` | `ubus list` | 显示 nanohat-oled |
| `test_ubus_status` | `ubus call nanohat-oled status` | 返回版本、uptime |
| `test_ubus_restart` | `ubus call nanohat-oled restart` | 进程重启 |
| `test_ui_during_ubus` | ubus 操作中按键 | UI 响应正常 |

**验收标准**:
- [ ] 所有 Host 单元测试通过
- [ ] ubus 操作不阻塞 UI 线程
- [ ] `ubus list` 可见 nanohat-oled
- [ ] `ubus call nanohat-oled restart` 工作正常
- [ ] 服务启停后状态正确更新

---

### 3.5 Phase 5: 集成与调优

**目标**: 端到端验证，性能优化，生产就绪

**测试项目**:

| 类别 | 测试项 | 验收标准 |
|------|--------|----------|
| **功能** | 全部页面导航 | 所有页面可达 |
| **功能** | 服务启停 | 状态正确，无延迟感 |
| **功能** | 屏幕开关 | 长按 K2 开关屏幕 |
| **性能** | 按键响应 | < 50ms |
| **性能** | 动画帧率 | >= 25fps 稳定 |
| **性能** | CPU 空闲占用 | < 1% |
| **性能** | 内存占用 | < 2MB RSS |
| **稳定性** | 长时间运行 | 24h 无崩溃 |
| **稳定性** | 压力测试 | 快速连按 100 次无异常 |
| **恢复** | SIGTERM | 优雅退出 |
| **恢复** | SIGKILL + respawn | procd 自动拉起 |
| **恢复** | ubus 断连 | 自动重连或优雅降级 |

**性能 Profiling**:
```bash
# CPU 占用监控
ssh root@192.168.33.254 "top -b -n 10 -d 1 | grep nanohat"

# 内存监控
ssh root@192.168.33.254 "cat /proc/\$(pgrep nanohat-oled)/status | grep -E 'VmRSS|VmSize'"

# 线程状态
ssh root@192.168.33.254 "cat /proc/\$(pgrep nanohat-oled)/task/*/stat"
```

**验收标准**:
- [ ] 所有功能测试通过
- [ ] 性能指标达标
- [ ] 24h 稳定性测试通过
- [ ] 文档更新完成

---

## 4. 测试基础设施

### 4.1 目录结构

```
tests/
├── mocks/
│   ├── gpio_mock.c         # GPIO Mock
│   ├── gpio_mock.h
│   ├── display_mock.c      # 显示 Mock
│   ├── display_mock.h
│   ├── ubus_mock.c         # ubus Mock
│   ├── ubus_mock.h
│   ├── time_mock.c         # 时间 Mock
│   └── time_mock.h
├── test_ring_queue.c       # Phase 1
├── test_gpio_button.c      # Phase 2
├── test_event_queue.c      # Phase 3
├── test_event_flow.c       # Phase 3
├── test_thread_safety.c    # Phase 3
├── test_task_queue.c       # Phase 4
├── test_ubus_async.c       # Phase 4
├── test_ubus_object.c      # Phase 4
├── test_main.c             # 测试入口
├── target/
│   ├── test_gpio_hw.c      # Phase 2 Target
│   ├── test_dual_thread.c  # Phase 3 Target
│   └── test_ubus_hw.c      # Phase 4 Target
└── Makefile
```

### 4.2 构建与运行命令（以 `tests/Makefile` 为准）

**Host 单元测试**：
```bash
cd tests
make test-host
```

**Target 集成测试（GPIO + 双线程）**：
```bash
cd tests
make test-target TARGET=192.168.33.254 TARGET_USER=root \
  DOCKER_IMAGE=openwrt-sdk:sunxi-cortexa53-24.10.5 \
  GPIOCHIP_PATH=/dev/gpiochip1 BTN_OFFSETS=0,2,3
```

说明：
- `test-target` 会使用 Docker 交叉编译并上传到 `/tmp/test_gpio_hw` 后自动执行
- 测试执行期间需要人工按键（10 秒内）
- 建议先停止服务：`ssh root@<ip> "service nanohat-oled stop; sleep 1"`
- `test-target` 依次运行 `test_gpio_hw` 与 `test_dual_thread`，默认覆盖 GPIO 和自动息屏链路
- 若只想执行双线程交互，可以单独调用 `make test-target-dual ...`
- 若目标使用其他 gpiochip，可用 `GPIOCHIP_PATH=/dev/gpiochip0` 覆盖；建议先用 `gpiomon -c <chip> 0 2 3` 确认按键所在的 chip。
- `test-target-dual` 为交互式双线程验证，需按提示完成按键/息屏/唤醒流程

### 4.3 Mock 时间控制

```c
// tests/mocks/time_mock.h
void     time_mock_reset(void);
void     time_mock_set_now_ms(uint64_t ms);
void     time_mock_advance_ms(uint64_t delta);
uint64_t time_mock_now_ms(void);

// 与 time_hal.h 集成
// HAL_MOCK 模式下 time_hal_now_ms() 调用 time_mock_now_ms()
```

### 4.4 CI 集成

```yaml
# .github/workflows/test.yml
name: Test
on: [push, pull_request]

jobs:
  host-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and test
        run: |
          cd tests
          make test-host
```

---

## 4.5 Target 测试流程（GPIO）

1. 停止服务：`ssh root@<ip> "service nanohat-oled stop; sleep 1"`
2. 运行：`cd tests && make test-target ...`
3. 看到提示后按任意按键：
   - PASS 示例：`PASS: event=K1_SHORT line=0 ts=...`
   - FAIL 示例：`FAIL: ret=0`（超时无事件）
4. 若失败，先用 `gpiomon` 确认映射：
   - `gpiomon -c gpiochip1 -e both --idle-timeout 10s 0 2 3`
   - 注意参数顺序：选项在前，line 在后

## 4.6 Target 测试流程（双线程）

`make test-target`  会按顺序运行 `test_gpio_hw` 与 `test_dual_thread`，无需额外命令；若需要重跑或单独调试双线程可用：
1. 停止服务：`ssh root@<ip> "service nanohat-oled stop; sleep 1"`
2. 运行：`cd tests && make test-target-dual ...`
3. 按提示完成 3 个步骤：
   - 10 秒内按任意键（验证事件链路）
   - 保持无按键，等待自动息屏
   - 再按任意键唤醒
4. 看到 `TEST PASSED` 结束；失败会提示具体阶段

## 5. 开发工作流

```
┌─────────────────────────────────────────────────────────────┐
│                      开发循环                                │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   ┌─────────┐     ┌─────────┐     ┌─────────┐              │
│   │ 编写代码 │ ──▶ │Host测试 │ ──▶ │  通过?  │              │
│   └─────────┘     └─────────┘     └────┬────┘              │
│        ▲                               │                    │
│        │              否 ◀─────────────┘                    │
│        │                               │ 是                 │
│        │                               ▼                    │
│        │                        ┌─────────┐                │
│        │                        │交叉编译  │                │
│        │                        └────┬────┘                │
│        │                             │                      │
│        │                             ▼                      │
│        │                        ┌─────────┐                │
│        │                        │部署Target│                │
│        │                        └────┬────┘                │
│        │                             │                      │
│        │              否             ▼                      │
│        └─────────────────────  ┌─────────┐                │
│                                │Target测试│                │
│                                └────┬────┘                │
│                                     │ 是                   │
│                                     ▼                      │
│                                ┌─────────┐                │
│                                │  提交    │                │
│                                └─────────┘                │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**快速命令**:
```bash
# 本地迭代（秒级反馈）
make test-host

# 部署验证
make release && ./deploy.sh

# Target 测试
make test-target
make test-target-dual
```

---

## 6. 参考

- [ADR 0005: 终极三线程架构](../adr/0005-ultimate-threaded-architecture.md)
- [libgpiod v2.x API](https://libgpiod.readthedocs.io/)
- [Unity Test Framework](http://www.throwtheswitch.org/unity)
