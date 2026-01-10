# uloop 单线程事件驱动与 UI 设计笔记

本文档记录 ADR0006 实现过程中关于 libubox/uloop 事件机制和 UI 刷新设计的讨论要点。

## 1. uloop 单线程异步原理

### 1.1 核心机制

uloop 本质是 Linux **epoll** 的薄封装，实现的是 **事件多路复用**（不是真正的并发）。

```
┌─────────────────────────────────────────────┐
│                 uloop_run()                  │
│  ┌─────────────────────────────────────┐    │
│  │          epoll_wait()               │◄───┼─── 阻塞等待事件
│  │  (单点阻塞，监控所有 fd + timers)    │    │
│  └─────────────────────────────────────┘    │
│                    │                         │
│         事件发生（fd可读/超时）              │
│                    ▼                         │
│  ┌─────────────────────────────────────┐    │
│  │  分发到对应 callback（同步执行）     │    │
│  │  - uloop_fd.cb()                    │    │
│  │  - uloop_timeout.cb()               │    │
│  │  - uloop_signal.cb()                │    │
│  └─────────────────────────────────────┘    │
│                    │                         │
│         callback 必须快速返回               │
│                    ▼                         │
│              继续 epoll_wait()              │
└─────────────────────────────────────────────┘
```

### 1.2 关键约束

| 规则 | 原因 |
|------|------|
| callback 不能阻塞 | 会卡住整个事件循环 |
| 所有 I/O 必须非阻塞 | 阻塞 read() 会挂起全局 |
| 长任务需分片 | 否则 UI 卡顿、按键无响应 |

### 1.3 对比真正的异步/多线程

```
多线程：        线程A ──────────────────────►
                线程B ──────────────────────►
                （真正并行，需要锁）

uloop单线程：   ─callback1─┬─callback2─┬─callback3─►
                          │           │
                     epoll_wait  epoll_wait
                （串行执行，无需锁，但不能阻塞）
```

### 1.4 同类实现

uloop 采用的模式与以下框架相同：
- Node.js (libuv)
- Python asyncio
- nginx
- Redis

---

## 2. Timeout 精度问题

### 2.1 epoll_wait 的精度限制

```c
// uloop 内部调用
epoll_wait(fd, events, maxevents, timeout_ms);  // 精度：毫秒级
```

**实际精度受限于：**

| 因素 | 影响 |
|------|------|
| 内核调度延迟 | +1~10ms（非实时内核） |
| 其他 callback 执行时间 | 累积延迟 |
| 系统负载 | 高负载时抖动更大 |

**实测数据（典型嵌入式 Linux）：**
```
请求 50ms → 实际 48~65ms（±15ms 抖动）
请求 20ms → 实际 18~35ms（抖动比例更大）
```

### 2.2 应对策略

**方案 A：固定间隔（简单，有累积漂移）**
```c
void ui_tick(struct uloop_timeout *t) {
    render();
    uloop_timeout_set(t, 50);  // 每次固定 50ms
}
```

**方案 B：校正间隔（可选优化，消除漂移）**
```c
static uint64_t next_tick_ms;

void ui_tick(struct uloop_timeout *t) {
    uint64_t now = time_hal->get_ms();
    render();

    next_tick_ms += 50;  // 期望的下一帧时间
    int64_t delay = next_tick_ms - time_hal->get_ms();

    if (delay < 5) delay = 5;  // 最小间隔保护
    uloop_timeout_set(t, (int)delay);
}
```

### 2.3 结论

50ms tick 在嵌入式 Linux 上是可靠的，20ms 会有明显抖动。ADR0006 采用 50ms 作为动画刷新基准。
当前实现先用固定间隔（方案 A）落地，若后续出现明显抖动或漂移，再引入方案 B 的校正策略。

---

## 3. 渲染超时处理

### 3.1 问题场景

```
期望：|--render--|(10ms)  |--render--|(10ms)  ...
      0         50ms      100ms

实际：|----render 卡住----|(80ms)  |--render--|
      0                   80ms     130ms  ← 丢帧
```

### 3.2 处理策略

**策略 A：丢帧追赶（推荐）**

```c
void ui_tick(struct uloop_timeout *t) {
    uint64_t now = time_hal->get_ms();

    // 如果落后超过 2 帧，跳过中间帧
    while (next_tick_ms + UI_TICK_MS < now) {
        next_tick_ms += UI_TICK_MS;
        frames_dropped++;
    }

    render();  // 只渲染最新状态

    next_tick_ms += UI_TICK_MS;
    uloop_timeout_set(t, next_tick_ms - time_hal->get_ms());
}
```

**策略 B：降级刷新率**

```c
void ui_tick(struct uloop_timeout *t) {
    uint64_t start = time_hal->get_ms();
    render();
    uint64_t elapsed = time_hal->get_ms() - start;

    if (elapsed > UI_TICK_MS * 0.8) {
        // 渲染耗时超过 80%，切换到低刷新率
        current_tick_ms = UI_TICK_STATIC_MS;
    }

    uloop_timeout_set(t, current_tick_ms);
}
```

**策略 C：分片渲染（复杂动画时）**

```c
// 将大渲染任务拆分
typedef struct {
    int phase;      // 0=clear, 1=draw_bg, 2=draw_text, 3=flush
    page_t *page;
} render_ctx_t;

void ui_tick(struct uloop_timeout *t) {
    switch (ctx.phase) {
        case 0: u8g2_ClearBuffer(&u8g2); break;
        case 1: draw_background(ctx.page); break;
        case 2: draw_content(ctx.page); break;
        case 3: u8g2_SendBuffer(&u8g2); break;
    }
    ctx.phase = (ctx.phase + 1) % 4;
    uloop_timeout_set(t, UI_TICK_MS / 4);  // 每片 12.5ms
}
```

### 3.3 ADR0006 选择

当前实现采用“固定间隔 + 状态驱动”的简单策略，渲染超时未引入显式追帧逻辑。
若后续遇到卡顿或帧丢失，可优先引入策略 A（丢帧追赶），必要时再考虑 B/C。

---

## 4. 动画状态机设计

### 4.1 状态定义

```c
typedef enum {
    UI_STATE_IDLE,       // 息屏，无 timeout
    UI_STATE_STATIC,     // 静态页，1000ms 刷新
    UI_STATE_ANIMATING,  // 动画中，50ms 刷新
    UI_STATE_TRANSITION, // 页面切换动画
} ui_state_t;
```

### 4.2 状态转换图

```
                    ┌──────────────┐
         按键唤醒    │   IDLE      │  超时息屏
        ┌──────────►│  (无刷新)    │◄──────────┐
        │           └──────────────┘           │
        │                  │                   │
        │              按键唤醒                │
        │                  ▼                   │
        │           ┌──────────────┐           │
        │           │   STATIC    │           │
        │    ┌─────►│  (1000ms)   │◄─────┐    │
        │    │      └──────────────┘      │    │
        │    │             │              │    │
        │  动画结束     切换页面      数据无变化 │
        │    │             ▼              │    │
        │    │      ┌──────────────┐      │    │
        │    └──────│ TRANSITION  │──────┘    │
        │           │   (50ms)    │           │
        │           └──────────────┘           │
        │                  │                   │
        │              进入动画页              │
        │                  ▼                   │
        │           ┌──────────────┐           │
        └───────────│  ANIMATING  │───────────┘
                    │   (50ms)    │
                    └──────────────┘
```

### 4.3 刷新间隔配置

| 状态 | 间隔 | 说明 |
|------|------|------|
| IDLE | 0 (停止) | 息屏省电，仅响应按键唤醒 |
| STATIC | 1000ms | 静态页面，监控数据变化 |
| ANIMATING | 50ms | 动画播放，20fps |
| TRANSITION | 50ms | 页面切换动画 |

### 4.4 实现代码框架

```c
typedef struct {
    ui_state_t state;
    struct uloop_timeout timer;
    uint64_t last_input_ms;      // 息屏计时
    page_t *current_page;
    transition_t transition;     // 切换动画上下文
} ui_controller_t;

static ui_controller_t g_ui;

// 获取当前状态的 tick 间隔
static int get_tick_interval(void) {
    switch (g_ui.state) {
        case UI_STATE_IDLE:       return 0;     // 停止
        case UI_STATE_STATIC:     return 1000;
        case UI_STATE_ANIMATING:
        case UI_STATE_TRANSITION: return 50;
    }
    return 1000;
}

// 状态切换
static void set_state(ui_state_t new_state) {
    if (g_ui.state == new_state) return;

    ui_state_t old = g_ui.state;
    g_ui.state = new_state;

    // 调整 timer
    int interval = get_tick_interval();
    if (interval > 0) {
        uloop_timeout_set(&g_ui.timer, interval);
    } else {
        /* 若环境不支持 cancel，可直接不再调度 */
    }

    printf("UI: %d -> %d (tick=%dms)\n", old, new_state, interval);
}

// UI tick 回调
static void ui_tick_cb(struct uloop_timeout *t) {
    (void)t;

    // 检查息屏超时
    uint64_t now = time_hal_now_ms();
    if (now - g_ui.last_input_ms > SCREEN_OFF_TIMEOUT_MS) {
        set_state(UI_STATE_IDLE);
        display_hal->set_power(false);
        return;
    }

    // 根据状态渲染
    switch (g_ui.state) {
        case UI_STATE_STATIC:
            if (page_needs_redraw(g_ui.current_page)) {
                render_page(g_ui.current_page);
            }
            break;

        case UI_STATE_ANIMATING:
            if (!render_animation_frame(g_ui.current_page)) {
                // 动画结束
                set_state(UI_STATE_STATIC);
            }
            break;

        case UI_STATE_TRANSITION:
            if (!render_transition_frame(&g_ui.transition)) {
                // 切换完成
                g_ui.current_page = g_ui.transition.target;
                set_state(page_has_animation(g_ui.current_page)
                         ? UI_STATE_ANIMATING : UI_STATE_STATIC);
            }
            break;

        default:
            break;
    }

    // 重新调度（如果不是 IDLE）
    int interval = get_tick_interval();
    if (interval > 0) {
        uloop_timeout_set(&g_ui.timer, interval);
    }
}

// 按键事件处理
void ui_handle_button(const gpio_event_t *event) {
    g_ui.last_input_ms = time_hal_now_ms();

    // 从息屏唤醒
    if (g_ui.state == UI_STATE_IDLE) {
        display_hal->set_power(true);
        set_state(UI_STATE_STATIC);
        render_page(g_ui.current_page);
        return;
    }

    // 处理按键导航
    page_t *next = handle_navigation(g_ui.current_page, event);
    if (next != g_ui.current_page) {
        // 启动页面切换动画
        g_ui.transition.source = g_ui.current_page;
        g_ui.transition.target = next;
        g_ui.transition.progress = 0;
        set_state(UI_STATE_TRANSITION);
    }
}
```

### 4.5 关键设计点

| 设计点 | 说明 |
|--------|------|
| 单一 timer | 只用一个 uloop_timeout，根据状态调整间隔 |
| 息屏由 UI 管理 | 不是独立 timer，而是在 tick 中检查 |
| 切换动画独立状态 | TRANSITION 与 ANIMATING 分开，便于管理 |
| 按键重置息屏计时 | 任何输入都更新 `last_input_ms` |

补充：当前实现使用 `page_controller_is_animating()` + 屏幕状态判断来等价替代 UI_STATE 显式枚举，
行为一致但实现更轻量。

---

## 5. 参考资料

- [libubox 源码](https://git.openwrt.org/project/libubox.git)
- [epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- ADR0006 架构文档: `docs/adr0006/architecture.md`
- ADR0006 UI 设计规范: `docs/adr0006/ui-design-spec.md`
