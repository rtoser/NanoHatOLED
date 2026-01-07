# Phase 1-2 实现设计文档

*   **版本**: 1.0
*   **日期**: 2026-01-06
*   **状态**: 已实现

本文档描述 NanoHatOLED 重构项目 Phase 1（基础设施）和 Phase 2（GPIO 迁移）的设计与实现。

---

## 1. 模块总览

```
src/
├── ring_queue.c/h              # 通用环形队列
└── hal/
    ├── gpio_hal.h              # GPIO 抽象接口
    ├── gpio_hal_libgpiod.c     # libgpiod v2.x 实现
    ├── gpio_hal_mock.c         # Mock 实现（Host 测试）
    ├── display_hal.h           # 显示抽象接口（Phase 3 实现）
    ├── ubus_hal.h              # ubus 抽象接口（Phase 4 实现）
    ├── time_hal.h              # 时间抽象接口
    └── time_hal_real.c         # 真实时间实现

tests/
├── test_ring_queue.c           # 队列单元测试
├── test_gpio_button.c          # GPIO 逻辑测试
├── mocks/
│   ├── time_mock.c/h           # 时间 Mock
│   └── gpio_mock.h             # GPIO Mock 接口
└── target/
    └── test_gpio_hw.c          # Target 硬件验证
```

---

## 2. 环形队列 (ring_queue)

### 2.1 设计目标

- 通用：支持任意元素类型
- 线程安全：内置 mutex
- 策略可配：溢出时可覆盖/拒绝/合并
- 可观测：统计计数便于诊断

### 2.2 数据结构

```c
typedef enum {
    RQ_OVERWRITE_OLDEST = 0,  // 覆盖最旧元素
    RQ_REJECT_NEW = 1,        // 拒绝新元素
    RQ_COALESCE = 2           // 尝试合并
} ring_queue_overflow_policy_t;

typedef enum {
    RQ_RESULT_OK = 0,         // 成功入队
    RQ_RESULT_MERGED = 1,     // 已合并到现有元素
    RQ_RESULT_DROPPED = 2,    // 被拒绝
    RQ_RESULT_ERR = -1        // 参数错误
} ring_queue_result_t;

typedef struct {
    uint8_t *buffer;          // 元素存储
    size_t item_size;         // 单个元素大小
    size_t capacity;          // 最大容量
    size_t head;              // 出队位置
    size_t tail;              // 入队位置
    size_t count;             // 当前元素数
    ring_queue_overflow_policy_t policy;
    ring_queue_merge_fn merge_fn;
    void *merge_user;
    ring_queue_stats_t stats;
    pthread_mutex_t lock;
} ring_queue_t;
```

### 2.3 API

```c
// 初始化/销毁
int  ring_queue_init(ring_queue_t *q, size_t capacity, size_t item_size);
void ring_queue_destroy(ring_queue_t *q);

// 配置
void ring_queue_set_overflow_policy(ring_queue_t *q, ring_queue_overflow_policy_t policy);
void ring_queue_set_merge_fn(ring_queue_t *q, ring_queue_merge_fn fn, void *user);

// 操作
ring_queue_result_t ring_queue_push(ring_queue_t *q, const void *item);
bool ring_queue_pop(ring_queue_t *q, void *out);

// 查询
size_t ring_queue_count(ring_queue_t *q);
size_t ring_queue_capacity(ring_queue_t *q);
ring_queue_stats_t ring_queue_get_stats(ring_queue_t *q);
```

### 2.4 溢出策略详解

#### RQ_OVERWRITE_OLDEST（默认）

队列满时覆盖最旧元素，适用于：
- 帧节拍事件（只关心最新）
- 传感器采样（新数据更重要）

```c
ring_queue_set_overflow_policy(&q, RQ_OVERWRITE_OLDEST);
// 队列满时：新元素入队，head 前移，统计 overwrites++
```

#### RQ_REJECT_NEW

队列满时拒绝新元素，适用于：
- 关键事件（不能丢失任何一个）
- 需要背压的场景

```c
ring_queue_set_overflow_policy(&q, RQ_REJECT_NEW);
// 队列满时：返回 RQ_RESULT_DROPPED，统计 drops++
```

#### RQ_COALESCE

尝试合并到现有元素，适用于：
- 同类事件去重（如多次 tick 合并）
- 同服务操作合并（如快速 start/stop）

```c
// 合并函数：返回 true 表示已合并
bool merge_ticks(void *existing, const void *incoming, void *user) {
    event_t *a = existing;
    const event_t *b = incoming;
    if (a->type == EVT_TICK && b->type == EVT_TICK) {
        a->timestamp = b->timestamp;  // 保留最新时间
        return true;
    }
    return false;
}

ring_queue_set_overflow_policy(&q, RQ_COALESCE);
ring_queue_set_merge_fn(&q, merge_ticks, NULL);
```

### 2.5 使用示例

```c
// 事件队列
typedef struct {
    int type;
    uint64_t timestamp;
} event_t;

ring_queue_t event_queue;
ring_queue_init(&event_queue, 16, sizeof(event_t));

// 生产者
event_t evt = { .type = EVT_BUTTON_K1, .timestamp = now() };
ring_queue_result_t r = ring_queue_push(&event_queue, &evt);
if (r == RQ_RESULT_DROPPED) {
    log_warn("Event queue full, event dropped");
}

// 消费者
event_t out;
if (ring_queue_pop(&event_queue, &out)) {
    handle_event(&out);
}

// 诊断
ring_queue_stats_t stats = ring_queue_get_stats(&event_queue);
printf("pushes=%lu, pops=%lu, drops=%lu\n",
       stats.pushes, stats.pops, stats.drops);
```

---

## 3. 硬件抽象层 (HAL)

### 3.1 设计原则

1. **接口稳定**：上层代码只依赖 `*_hal.h`
2. **编译时切换**：通过链接不同 `.c` 文件选择实现
3. **可测试**：Mock 实现支持事件注入和状态验证
4. **poll 友好**：提供 `get_fd()` 用于统一事件循环

### 3.2 时间 HAL

最简单的 HAL，仅封装时钟获取：

```c
// hal/time_hal.h
uint64_t time_hal_now_ms(void);  // 毫秒级时间戳
uint64_t time_hal_now_ns(void);  // 纳秒级时间戳
```

**真实实现** (`time_hal_real.c`)：
```c
uint64_t time_hal_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
```

**Mock 实现** (`time_mock.c`)：
- 支持 `time_mock_set_now_ms()` 设置当前时间
- 支持 `time_mock_advance_ms()` 推进时间
- 用于测试超时、长按等时间相关逻辑

---

## 4. GPIO HAL

### 4.1 接口设计

```c
// hal/gpio_hal.h
typedef enum {
    GPIO_EVT_NONE = 0,
    GPIO_EVT_BTN_K1_SHORT, GPIO_EVT_BTN_K2_SHORT, GPIO_EVT_BTN_K3_SHORT,
    GPIO_EVT_BTN_K1_LONG,  GPIO_EVT_BTN_K2_LONG,  GPIO_EVT_BTN_K3_LONG
} gpio_event_type_t;

typedef struct {
    gpio_event_type_t type;
    uint8_t line;              // 0=K1, 1=K2, 2=K3
    uint64_t timestamp_ns;     // 内核时间戳
} gpio_event_t;

typedef struct {
    int  (*init)(void);
    void (*cleanup)(void);
    int  (*wait_event)(int timeout_ms, gpio_event_t *event);
    int  (*get_fd)(void);      // 用于 poll 集成
} gpio_hal_ops_t;

extern const gpio_hal_ops_t *gpio_hal;
```

**关键设计决策**：

| 决策 | 理由 |
|------|------|
| 返回高层事件（SHORT/LONG） | 上层无需处理边沿、去抖、长按逻辑 |
| 提供 `get_fd()` | 支持与 timerfd/eventfd 统一 poll |
| 内核时间戳 | 比用户空间取时更精确 |

### 4.2 libgpiod 实现

#### 配置宏

```c
#ifndef GPIOCHIP_PATH
#define GPIOCHIP_PATH "/dev/gpiochip1"  // NanoPi 默认
#endif

#ifndef BTN_OFFSETS
#define BTN_OFFSETS 0, 2, 3             // K1, K2, K3 的 line 偏移
#endif
```

#### 初始化流程

```
gpiod_chip_open()
    │
    ▼
request_lines(with_debounce=true)
    │
    ├─ 成功 → 使用硬件去抖
    │
    └─ ENOTSUP/EINVAL → request_lines(with_debounce=false)
                            │
                            └─ 使用软件去抖
    │
    ▼
gpiod_edge_event_buffer_new(16)
    │
    ▼
detect_pressed_level()  // 自动检测按下电平
```

#### 事件等待流程

```
wait_event(timeout_ms)
    │
    ▼
┌─ 检查 pending 队列 ──────────────────┐
│  有事件 → 返回                        │
└──────────────────────────────────────┘
    │
    ▼
┌─ 计算 effective_timeout ─────────────┐
│  仅考虑用户 timeout                   │
└──────────────────────────────────────┘
    │
    ▼
┌─ poll(gpiod_fd, effective_timeout) ──┐
│  可读 → 读取边沿事件                  │
│  超时 → 返回无事件                    │
└──────────────────────────────────────┘
    │
    ▼
处理边沿事件 → 在松开时判定短/长按 → 生成高层事件
```

#### 核心状态机

```
           press (falling edge)
    IDLE ─────────────────────────► PRESSED
      ▲                                 │
      │                                 │
      │    release (< 600ms)            │ release (>= 600ms)
      ├──────────────── SHORT ◄─────────┤
      │                                 │
      └──────────────── LONG ◄──────────┘
```

### 4.3 Mock 实现

#### 设计目标

- 在 macOS/Linux Host 上运行
- 支持事件注入（测试用）
- 提供可 poll 的 fd（eventfd/pipe）
- 独立实现与 libgpiod 相同的状态机逻辑（按键检测、长按判定）

#### 跨平台 fd

```c
#if defined(__linux__)
    #include <sys/eventfd.h>
    #define USE_EVENTFD 1
#else
    #define USE_EVENTFD 0  // macOS 用 pipe
#endif
```

#### Mock API

```c
// 注入边沿事件
void gpio_mock_inject_edge(int line, edge_type_t type, uint64_t timestamp_ns);

// 设置 line 电平（用于初始状态）
void gpio_mock_set_line_value(int line, int value);

// 清空事件队列
void gpio_mock_clear_events(void);

// 查询 pending 事件数
int gpio_mock_get_pending_count(void);

// 获取可 poll 的 fd
int gpio_mock_get_fd(void);

// 设置是否支持硬件去抖（测试软件去抖路径）
void gpio_mock_set_debounce_supported(bool supported);
```

#### 测试示例

```c
void test_long_press_detection(void) {
    gpio_mock_clear_events();
    gpio_hal->init();

    // 注入按下事件
    gpio_mock_inject_edge(1, EDGE_FALLING, 0);

    // 推进时间 600ms
    time_mock_advance_ms(600);

    // 注入松开事件（长按在松开时判定）
    gpio_mock_inject_edge(1, EDGE_RISING, 600000000ULL);

    // 等待事件
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(100, &evt);

    assert(ret == 1);
    assert(evt.type == GPIO_EVT_BTN_K2_LONG);

    gpio_hal->cleanup();
}
```

---

## 5. ubus HAL（接口预定义）

Phase 4 实现，但接口已定义：

```c
typedef struct {
    char service_name[32];
    ubus_action_t action;      // START, STOP, QUERY
    uint32_t request_id;
    uint32_t timeout_ms;       // 请求超时
    uint64_t enqueue_time_ms;  // 入队时间（用于过期检测）
} ubus_task_t;

typedef struct {
    char service_name[32];
    ubus_action_t action;
    bool success;
    int error_code;
    uint32_t request_id;       // 与 task 对应
} ubus_result_t;
```

**设计要点**：
- `request_id` 用于追踪请求-响应对
- `timeout_ms` 用于检测超时任务
- `enqueue_time_ms` 用于丢弃过期请求

---

## 6. 构建与测试

### 6.1 Host 测试构建

```makefile
# 编译队列测试
gcc -I./src \
    tests/test_ring_queue.c \
    src/ring_queue.c \
    -lpthread -o test_ring_queue

# 编译 GPIO 测试（使用 Mock）
gcc -DHAL_MOCK -I./src \
    tests/test_gpio_button.c \
    src/hal/gpio_hal_mock.c \
    src/hal/time_hal_real.c \
    tests/mocks/time_mock.c \
    -lpthread -o test_gpio
```

### 6.2 Target 测试构建

```makefile
# 交叉编译 GPIO 硬件测试
aarch64-openwrt-linux-musl-gcc \
    -I./src \
    tests/target/test_gpio_hw.c \
    src/hal/gpio_hal_libgpiod.c \
    src/hal/time_hal_real.c \
    -lgpiod -o test_gpio_hw

# 部署并运行
scp test_gpio_hw root@192.168.33.254:/tmp/
ssh root@192.168.33.254 /tmp/test_gpio_hw
```

### 6.3 测试用例覆盖

#### ring_queue

| 测试 | 验证点 |
|------|--------|
| `test_queue_init_empty` | 初始化后为空 |
| `test_queue_push_pop` | 基本入队出队 |
| `test_queue_overwrite_policy` | 溢出覆盖最旧 |
| `test_queue_reject_policy` | 溢出拒绝新 |
| `test_queue_coalesce_policy` | 合并策略 |
| `test_queue_thread_safety` | 多线程并发 |

#### gpio_hal

| 测试 | 验证点 |
|------|--------|
| `test_short_press_k*` | 短按检测 |
| `test_long_press_k*` | 长按检测 |
| `test_debounce` | 去抖过滤 |
| `test_timeout_no_event` | 超时返回 |
| `test_gpio_fd_wakeup` | fd 可被 poll |

---

## 7. 设计决策记录

### 7.1 为什么用 libgpiod 而不是 sysfs？

| 方面 | sysfs | libgpiod |
|------|-------|----------|
| 状态 | Linux 4.8 废弃 | 官方推荐 |
| 初始化 | export 后需 sleep | 直接打开 |
| 时间戳 | 用户空间取时 | 内核级纳秒 |
| 去抖 | 软件实现 | 可用硬件去抖 |
| 多键 | 分别 poll | 批量 read_edge_events |

### 7.2 为什么 HAL 返回高层事件而非原始边沿？

1. **职责分离**：状态机逻辑封装在 HAL 内
2. **简化上层**：主循环只需处理 SHORT/LONG
3. **可测试**：Mock 可直接注入边沿，验证状态机

### 7.3 为什么 Mock 需要真实的 fd？

```c
// 主循环需要统一 poll
struct pollfd fds[] = {
    { .fd = gpio_hal->get_fd(), .events = POLLIN },
    { .fd = timerfd, .events = POLLIN },
    { .fd = shutdown_eventfd, .events = POLLIN }
};
poll(fds, 3, -1);
```

如果 Mock 的 `get_fd()` 返回 -1，主循环逻辑无法测试。

### 7.4 为什么硬件去抖有 fallback？

```c
g_request = request_lines(true);  // 尝试硬件去抖
if (!g_request && (errno == ENOTSUP || errno == EINVAL)) {
    g_request = request_lines(false);
    g_use_soft_debounce = true;   // 回退到软件去抖
}
```

部分 GPIO 控制器（如某些 SBC）不支持硬件去抖，需要软件兜底。

---

## 8. 常见问题

### Q: 如何切换 GPIO 实现？

链接不同的 `.c` 文件：

```makefile
# 真实实现
SRCS += src/hal/gpio_hal_libgpiod.c

# Mock 实现
SRCS += src/hal/gpio_hal_mock.c
```

两者都定义 `const gpio_hal_ops_t *gpio_hal`，链接时二选一。

### Q: 如何修改 GPIO 映射？

编译时定义宏：

```makefile
CFLAGS += -DGPIOCHIP_PATH='"/dev/gpiochip0"'
CFLAGS += -DBTN_OFFSETS='1, 2, 3'
```

### Q: 如何添加新的按键？

1. 修改 `BTN_OFFSETS` 添加新 line
2. 修改 `NUM_BUTTONS`
3. 在 `gpio_event_type_t` 添加新事件类型
4. 更新 `to_button_event()` 映射

### Q: 队列统计如何清零？

当前不支持清零，如需要可添加：

```c
void ring_queue_reset_stats(ring_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    memset(&q->stats, 0, sizeof(q->stats));
    pthread_mutex_unlock(&q->lock);
}
```

---

## 9. 下一步 (Phase 3)

Phase 3 将基于本阶段成果实现：

1. **event_queue**：基于 ring_queue，添加 pthread_cond 阻塞等待
2. **event_loop**：主线程，poll 监听 GPIO fd + timerfd + eventfd
3. **ui_thread**：UI 线程，消费事件队列，调用 display_hal 渲染

---

## 附录：文件清单

| 文件 | 行数 | 说明 |
|------|------|------|
| `src/ring_queue.c` | 173 | 环形队列实现 |
| `src/ring_queue.h` | 59 | 环形队列接口 |
| `src/hal/gpio_hal.h` | 31 | GPIO 抽象接口 |
| `src/hal/gpio_hal_libgpiod.c` | 368 | libgpiod v2.x 实现 |
| `src/hal/gpio_hal_mock.c` | 389 | Mock 实现 |
| `src/hal/time_hal.h` | 9 | 时间抽象接口 |
| `src/hal/time_hal_real.c` | 15 | 真实时间实现 |
| `src/hal/display_hal.h` | 21 | 显示抽象接口 |
| `src/hal/ubus_hal.h` | 39 | ubus 抽象接口 |
| `tests/test_ring_queue.c` | 208 | 队列测试 |
| **总计** | **~1300** | |
