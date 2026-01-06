# ADR 0005: 终极三线程架构

*   **状态**: 提议 (Proposed)
*   **日期**: 2026-01-05
*   **作者**: Libo
*   **合并自**: ADR 0001, ADR 0003, ADR 0004

## 1. 背景 (Context)

当前单线程架构（ADR 0002）存在以下瓶颈：
- 按键响应受制于 `poll` 超时和阻塞 I/O
- UI 渲染节拍随 ubus 调用耗时漂移
- 动画期间按键可能被合并或丢失
- 进程不可被 `ubus list` 发现，缺乏远程控制能力

当前基线：`src/main.c` 为单线程主循环，`src/gpio_button.c` 基于 sysfs。

本 ADR 整合 ADR 0001（双线程事件队列）、ADR 0003（ubus 工作线程）、ADR 0004（ubus 对象注册），形成统一的终极架构。

## 2. 决策 (Decision)

采用**三线程事件驱动架构**：

```
┌─────────────────────────────────────────────────────────────────┐
│                         线程架构                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐    事件队列     ┌──────────────┐              │
│  │   主线程      │ ─────────────▶ │   UI 线程    │              │
│  │ (事件生产者)  │                │ (事件消费者)  │              │
│  └──────┬───────┘                └──────┬───────┘              │
│         │                               │                       │
│         │ poll()                        │ u8g2 渲染             │
│         ▼                               ▼                       │
│  ┌──────────────┐                ┌──────────────┐              │
│  │ GPIO sysfs   │                │ I2C /dev     │              │
│  │ timerfd      │                │ OLED 显示    │              │
│  │ eventfd      │                └──────────────┘              │
│  └──────────────┘                                               │
│                                                                  │
│  ┌──────────────┐    任务队列     ┌──────────────┐              │
│  │   UI 线程    │ ─────────────▶ │  ubus 线程   │              │
│  │              │ ◀───────────── │              │              │
│  └──────────────┘    结果队列     └──────┬───────┘              │
│                                          │                       │
│                                          │ uloop_run()          │
│                                          ▼                       │
│                                   ┌──────────────┐              │
│                                   │ ubus 对象    │              │
│                                   │ nanohat-oled │              │
│                                   └──────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

### 2.1 线程职责

| 线程 | 职责 | 持有资源 |
|------|------|----------|
| **主线程** | 监听 GPIO 中断、定时器；生产事件入队 | GPIO fd, timerfd, eventfd |
| **UI 线程** | 消费事件、状态更新、页面渲染、动画 | u8g2, I2C fd, sys_status |
| **ubus 线程** | 执行阻塞 ubus 调用、运行 uloop、注册 ubus 对象 | ubus_context, uloop |

### 2.2 队列设计

#### 事件队列 (主线程 → UI 线程)

```c
typedef enum {
    EVT_NONE,
    EVT_BUTTON_K1_SHORT, EVT_BUTTON_K1_LONG,
    EVT_BUTTON_K2_SHORT, EVT_BUTTON_K2_LONG,
    EVT_BUTTON_K3_SHORT, EVT_BUTTON_K3_LONG,
    EVT_TICK_1S,         // 1 秒定时
    EVT_TICK_FRAME,      // 帧定时 (动画期间)
    EVT_SHUTDOWN,        // 退出信号
} event_type_t;

typedef struct {
    event_type_t type;
    uint64_t timestamp_ms;  // CLOCK_MONOTONIC
} event_t;
```

- **容量**: 16（按键事件低频，足够）
- **溢出策略**:
  - **关键事件不丢**：`EVT_SHUTDOWN`、按键事件必须保序交付；队列满时可阻塞短时或返回错误并触发降级策略
  - **可合并事件**：`EVT_TICK_1S`、`EVT_TICK_FRAME` 允许合并/去重（仅保留最新）
  - **记录告警**：发生丢弃或合并时记录计数，便于诊断
- **同步**: `pthread_mutex_t` + `pthread_cond_t`

#### 任务队列 (UI 线程 → ubus 线程)

```c
typedef struct {
    char service_name[32];
    service_action_t action;  // ACTION_START, ACTION_STOP, ACTION_QUERY
    uint32_t request_id;
    uint32_t timeout_ms;
    uint64_t enqueue_time_ms;
} ubus_task_t;
```

- **容量**: 32
- **溢出策略**:
  - 默认**拒绝新任务**并返回错误（UI 提示或重试）
  - 允许对同一服务的重复操作做**合并**（只保留最后一次意图）
  - 记录丢弃/合并统计
- **超时策略**:
  - 任务入队时记录 `enqueue_time_ms` 与 `timeout_ms`
  - ubus 线程执行前检查 `now - enqueue_time_ms`，超时则丢弃并回填超时结果
  - UI 线程也可在等待结果时按 `request_id` 超时放弃

#### 结果队列 (ubus 线程 → UI 线程)

```c
typedef struct {
    char service_name[32];
    service_action_t action;
    bool success;
    int error_code;
    uint32_t request_id;
} ubus_result_t;
```

- **容量**: 32
- **溢出策略**:
  - 结果必须可追踪（`request_id` 对应任务存在）
  - 队列满时优先丢弃**过期结果**（已超时或已被 UI 放弃的请求）
  - 记录丢弃统计

### 2.3 ubus 对象

注册 `nanohat-oled` 对象，提供方法：

| 方法 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `status` | 无 | version, uptime, page, screen_on | 查询运行状态 |
| `restart` | 无 | ok: true | 触发优雅重启 |

`restart` 实现：先 `ubus_send_reply()`，再设置 `restart_requested` 原子标志，主线程检测后触发 `EVT_SHUTDOWN`。

### 2.4 线程同步与退出

1. **唤醒**:
   - 主线程入队后 `pthread_cond_signal()` 唤醒 UI 线程
   - UI 线程入队后 `pthread_cond_signal()` 唤醒 ubus 线程
   - ubus 线程结果入队后 `pthread_cond_signal()` 唤醒 UI 线程

2. **退出**:
   - 收到 SIGTERM/SIGINT → 主线程写 eventfd → poll 返回 → 入队 `EVT_SHUTDOWN`
   - 主线程 `pthread_cond_broadcast()` 唤醒所有等待线程
   - ubus 线程收到退出信号后 `uloop_end()` 退出事件循环
   - 各线程检查退出标志后清理资源

### 2.5 ubus 线程与任务队列集成

- ubus 线程仍以 `uloop_run()` 为主循环，但通过 **eventfd + uloop_fd** 接入任务队列：
  1. UI 线程入队任务后 `write(task_eventfd)`；
  2. ubus 线程注册 `uloop_fd` 监听 `task_eventfd`；
  3. 回调中 drain 队列并执行任务，结果入队后通知 UI。
- 这样既保留 ubus 的事件循环模型，又避免“只靠 `pthread_cond_signal()` 无法唤醒 `uloop_run()`”的问题。

## 3. 备选方案 (Alternatives)

| 方案 | 优点 | 缺点 | 决策 |
|------|------|------|------|
| 单线程 + deadline (ADR 0002) | 简单 | 阻塞 I/O 影响响应 | 不采纳 |
| 双线程 (ADR 0001) | 响应好 | ubus 仍阻塞 UI | 不采纳 |
| 单线程 + ubus 线程 (ADR 0002+0003) | 中等复杂 | 按键响应受限 | 不采纳 |
| 全量 uloop 主循环 | 单线程 | 改动范围大 | 不采纳 |
| **三线程 (本方案)** | 完全解耦 | 复杂度最高 | **采纳** |

## 4. 后果 (Consequences)

### 优势 (Pros)

1. **高响应性**: 按键事件即时捕获，不受渲染或 I/O 阻塞
2. **流畅动画**: UI 线程独立调度，帧率稳定
3. **阻塞隔离**: ubus 调用完全异步，不影响主循环
4. **可观测性**: 进程注册 ubus 对象，支持远程状态查询和重启
5. **职责清晰**: 三线程各司其职，边界明确

### 劣势 (Cons)

1. **复杂度高**: 三线程 + 三队列 + 多种同步原语
2. **调试困难**: 并发问题定位难度增加
3. **资源占用**: 多线程栈空间（可配置为 64KB/线程）

## 5. 实施要点 (Implementation Notes)

### 5.1 文件结构

```
src/
├── main.c              # 入口、信号处理、线程创建
├── event_loop.c/h      # 主线程：GPIO/timer 监听
├── ui_thread.c/h       # UI 线程：事件消费、渲染
├── ubus_thread.c/h     # ubus 线程：任务执行、uloop
├── event_queue.c/h     # 事件队列（通用环形缓冲区）
├── task_queue.c/h      # 任务/结果队列
└── ... (existing)
```

### 5.2 关键实现

1. **定时器**: 使用 `timerfd_create(CLOCK_MONOTONIC, ...)` 避免时钟跳变
2. **线程栈**: `pthread_attr_setstacksize()` 设为 64KB 节省内存
3. **队列**: 通用环形缓冲区模板，支持溢出策略、合并策略与统计
4. **ubus 对象**: 在 `ubus_thread_init()` 中注册，退出时注销
5. **原子标志**: `restart_requested` 使用 `atomic_bool` 或 `sig_atomic_t`
6. **GPIO 事件集成**: 主线程使用 `poll()` 监听 `timerfd`、`eventfd` 以及 **gpiod request fd**，并在可读时批量 `read_edge_events()`

### 5.3 线程所有权规则

- **禁止**: 跨线程访问 u8g2、I2C fd、ubus_context
- **允许**: 通过队列传递事件/任务/结果
- **共享**: 退出标志（原子变量）、队列互斥锁

### 5.4 procd 配置

确保 `/etc/init.d/nanohat-oled` 启用 respawn：
```sh
procd_set_param respawn 3600 5 5
```

## 6. GPIO 接口：sysfs vs libgpiod

### 6.1 当前实现 (sysfs + poll) 的劣势

| 问题 | 说明 | 影响 |
|------|------|------|
| **已废弃** | Linux 4.8 起 sysfs GPIO 被标记 deprecated | 未来内核可能移除 |
| **竞态条件** | export 后需 `usleep(100ms)` 等待 sysfs 节点就绪 | 初始化不可靠 |
| **全局命名空间** | GPIO 编号是系统全局的 | 与其他程序冲突风险 |
| **无事件时间戳** | poll 返回时才取时间，非内核级 | 长按检测精度受 poll 超时影响 |
| **性能开销** | 每次读取需 `lseek` + `read` | 微秒级开销（可接受） |
| **ABI 不稳定** | 内核/设备树变更可能改变 GPIO 编号 | 硬编码 GPIO 0/2/3 有风险 |

### 6.2 libgpiod 的优势

| 特性 | 说明 |
|------|------|
| **官方推荐** | Linux 内核唯一推荐的用户空间 GPIO 接口 |
| **内核级时间戳** | `gpiod_line_event_read()` 返回 `struct timespec` |
| **批量操作** | `gpiod_line_request_bulk_*` 原子操作多个 GPIO |
| **命名空间隔离** | 通过 chip name + line offset 标识，无全局冲突 |
| **无需 export** | 直接 `open("/dev/gpiochipX")` |
| **线程安全** | 库内部处理并发 |
| **事件队列** | 内核缓冲事件，减少丢失风险 |

### 6.3 迁移可行性

**依赖检查**：

| 组件 | Docker SDK | 目标系统 (OpenWrt) |
|------|------------|-------------------|
| libgpiod.so | ✅ 已包含 | 需安装 `libgpiod` |
| gpiod.h | ✅ 已包含 | 需安装 `libgpiod-dev` (编译时) |
| /dev/gpiochip* | N/A | 需加载 `kmod-gpio-chardev` |

**代码改动范围**：

```
src/gpio_button.c  - 完全重写（~150 行）
src/gpio_button.h  - 接口不变
src/Makefile       - 添加 -lgpiod
```

**API 映射**：

| sysfs 操作 | libgpiod 等价 |
|-----------|---------------|
| `open("/sys/class/gpio/export")` | `gpiod_chip_open()` |
| `write(fd, "0")` (export) | 无需，直接请求 line |
| `open(".../edge")` + `write("both")` | `gpiod_line_request_both_edges_events()` |
| `poll(fd, POLLPRI)` | `gpiod_line_event_wait()` |
| `read(fd, &val)` | `gpiod_line_event_read()` |
| `open(".../unexport")` | `gpiod_line_release()` + `gpiod_chip_close()` |

### 6.4 迁移决策

**建议**: 在 ADR 0005 实施时同步迁移到 libgpiod

**理由**:
1. 主线程重写 GPIO 监听逻辑时，顺便切换接口成本最低
2. libgpiod 事件时间戳与 `timerfd` 配合更精确
3. 避免未来内核升级导致 sysfs 不可用

**风险**:
1. 目标系统需确保 `libgpiod` 和 `kmod-gpio-chardev` 已安装
2. GPIO chip/line 映射需实测确认（可能是 `gpiochip0` line 0/2/3）
3. 硬件去抖可能不被驱动支持，需要软件去抖兜底

### 6.5 libgpiod v2.x 实现草案

**注意**：目标系统使用 libgpiod v2.1.3，API 与 v1.x 完全不同。

```c
#include <gpiod.h>

static struct gpiod_chip *chip;
static struct gpiod_line_request *request;
static struct gpiod_edge_event_buffer *event_buffer;

static const unsigned int btn_offsets[NUM_BUTTONS] = {0, 2, 3};  // K1, K2, K3

int gpio_button_init(void) {
    // Open chip
    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) return -1;

    // Configure line settings for edge detection
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
    gpiod_line_settings_set_debounce_period_us(settings, 30000);  // 30ms debounce

    // Build line config
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_config, btn_offsets, NUM_BUTTONS, settings);

    // Request config with consumer name
    struct gpiod_request_config *req_config = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_config, "nanohat-oled");

    // Request lines
    request = gpiod_chip_request_lines(chip, req_config, line_config);

    // Cleanup config objects
    gpiod_request_config_free(req_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);

    if (!request) return -1;

    // Allocate event buffer
    event_buffer = gpiod_edge_event_buffer_new(16);
    if (!event_buffer) return -1;

    return 0;
}

button_event_t gpio_button_wait(int timeout_ms) {
    int64_t timeout_ns = (int64_t)timeout_ms * 1000000;

    // Wait for edge events
    int ret = gpiod_line_request_wait_edge_events(request, timeout_ns);
    if (ret <= 0) {
        // timeout or error - check long press
        return check_long_press();
    }

    // Read events
    int num_events = gpiod_line_request_read_edge_events(request, event_buffer, 16);

    for (int i = 0; i < num_events; i++) {
        struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(event_buffer, i);

        unsigned int offset = gpiod_edge_event_get_line_offset(event);
        enum gpiod_edge_event_type type = gpiod_edge_event_get_event_type(event);
        uint64_t timestamp_ns = gpiod_edge_event_get_timestamp_ns(event);

        // type: GPIOD_EDGE_EVENT_RISING_EDGE / GPIOD_EDGE_EVENT_FALLING_EDGE
        // timestamp_ns: kernel monotonic timestamp
        process_edge(offset, type, timestamp_ns);
    }

    return get_pending_event();
}

void gpio_button_cleanup(void) {
    if (event_buffer) gpiod_edge_event_buffer_free(event_buffer);
    if (request) gpiod_line_request_release(request);
    if (chip) gpiod_chip_close(chip);
}
```

**去抖策略**：
- 优先启用硬件去抖；若驱动不支持（如返回 `-1` 且 `errno=ENOTSUP/EINVAL`），回退到软件去抖（基于时间阈值过滤抖动边沿）。

**GPIO 映射可配置（编译期宏）**：
- `GPIOCHIP_PATH`（例如 `/dev/gpiochip0`）
- `BTN_OFFSETS`（例如 `0,2,3`）
- 默认值与当前板卡一致，但建议在不同硬件平台按需覆盖。

**与主线程 poll 集成的实现建议**：
- 若主线程统一 `poll()`，使用 `gpiod_line_request_get_fd()` 获取 fd 加入 poll 集合；
- `POLLIN` 可读时调用 `gpiod_line_request_read_edge_events()` 批量读取；
- `wait_edge_events()` 仅适用于 **独立 GPIO 线程** 的实现。

**v2.x 关键变化**：
- `gpiod_chip_open_by_name()` → `gpiod_chip_open()`
- `gpiod_line_*` → `gpiod_line_request_*` + `gpiod_line_settings_*`
- 内置硬件去抖动：`gpiod_line_settings_set_debounce_period_us()`
- 事件时间戳：纳秒级精度
- 多键检测能力：`read_edge_events()` 可一次返回多个 line 的事件，支持组合键检测（当前未使用）

## 7. 迁移路径

> **详细测试计划**: 参见 [测试架构设计](../adr0005/testing-architecture.md)

每个 Phase 采用 **Host Mock 测试 → Target 集成测试** 的验证策略。

### Phase 1: 基础设施

**交付物**:
- 通用环形队列库 (`ring_queue.c/h`)
- HAL 接口定义 (`gpio_hal.h`, `display_hal.h`, `ubus_hal.h`, `time_hal.h`)

**验收标准**:
- [ ] 队列单元测试 100% 通过 (Host)
- [ ] HAL 接口可编译

### Phase 2: GPIO 迁移

**交付物**:
- libgpiod v2.x 实现 (`gpio_hal_libgpiod.c`)
- GPIO Mock (`gpio_hal_mock.c`)

**验收标准**:
- [ ] 按键逻辑测试通过 (Host Mock)
- [ ] K1/K2/K3 短按、长按检测正常 (Target)
- [ ] 与单线程主循环集成，功能不退化

### Phase 3: 双线程 (主线程 + UI 线程)

**交付物**:
- 事件队列 (`event_queue.c/h`)
- 主线程 (`event_loop.c/h`)
- UI 线程 (`ui_thread.c/h`)

**验收标准**:
- [ ] 事件流转测试通过 (Host)
- [ ] 并发安全测试通过 (Host)
- [ ] 按键响应 < 50ms (Target)
- [ ] 动画期间按键不丢失 (Target)

### Phase 4: 三线程 (+ ubus 线程)

**交付物**:
- 任务/结果队列 (`task_queue.c/h`, `result_queue.c/h`)
- ubus 线程 (`ubus_thread.c/h`)
- ubus 对象注册

**验收标准**:
- [ ] 异步 ubus 测试通过 (Host Mock)
- [ ] `ubus list` 显示 nanohat-oled (Target)
- [ ] `ubus call nanohat-oled restart` 工作正常 (Target)
- [ ] ubus 操作不阻塞 UI (Target)

### Phase 5: 集成与调优

**验收标准**:
- [ ] 全功能测试通过
- [ ] CPU 空闲占用 < 1%
- [ ] 内存占用 < 2MB RSS
- [ ] 24h 稳定性测试通过

## 8. 相关决策

| ADR | 新状态 | 说明 |
|-----|--------|------|
| 0001 | Merged → 0005 | 双线程事件队列 |
| 0002 | Superseded by 0005 | 单线程节拍（当前实现） |
| 0003 | Merged → 0005 | ubus 工作线程 |
| 0004 | Merged → 0005 | ubus 对象注册 |

## 9. 相关文档

| 文档 | 说明 |
|------|------|
| [测试架构设计](../adr0005/testing-architecture.md) | 分阶段测试策略与用例 |
| [Phase 1-2 实现设计](../design/phase1-2-implementation.md) | 队列库与 GPIO HAL 详细设计 |

---

本 ADR 作为 NanoHatOLED 终极架构的实施蓝图，整合了事件驱动、异步 I/O、远程控制等核心需求。
