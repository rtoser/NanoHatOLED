# Phase 3 事件循环与 UI 线程设计

* **版本**: 1.0
* **日期**: 2026-01-07
* **状态**: 设计中

本文档用于说明 ADR0005 Phase 3 的详细设计，重点阐述机制选型、取舍理由、事件队列策略，以及 UI 刷新“混合策略”的决策过程。目标是让维护者理解“为什么这样做”，而不仅是“怎么做”。

---

## 1. 背景与目标

**背景**：现有实现将输入检测与 UI 更新耦合在单线程内，容易在渲染或 I/O 阻塞时丢事件，并且难以扩展到异步任务。

**Phase 3 目标**：
- 将“输入事件采集”和“UI 渲染”解耦为双线程。
- 主线程使用统一事件循环（`poll()`）监听 GPIO/timer/eventfd。
- UI 线程专注消费事件并渲染，避免阻塞输入。
- 明确队列策略：关键事件不丢，tick 事件可合并。
- 明确 tick 与 eventfd 的用途与价值，给出合理的刷新策略。

---

## 2. 线程模型与职责

### 2.1 主线程（Event Loop）
**职责**：
- `poll()` 监听多个 fd（GPIO、timerfd、eventfd）。
- 读取并转换为高层事件（按键/系统事件/tick）。
- 将事件入队并唤醒 UI 线程。

**优势**：
- 不依赖 UI 线程是否阻塞，输入仍可被采集。
- `poll()` 统一入口，避免多处定时器和多处阻塞等待。

### 2.2 UI 线程
**职责**：
- 阻塞消费事件队列。
- 更新状态并渲染（I/O、动画、文本更新）。
- 需要触发定时刷新时，向主线程请求调整 tick。

**优势**：
- 渲染与输入完全解耦，避免 UI 卡顿导致输入丢失。

---

## 3. 事件来源与事件模型

### 3.1 事件来源
- **GPIO fd**：按键边沿事件（由 `gpio_hal` 转为 SHORT/LONG）。
- **timerfd**：周期 tick，驱动动画/超时/刷新。
- **eventfd**：跨线程唤醒与控制事件（退出/配置变更）。

### 3.2 事件类型（示例）
```c
typedef enum {
    EVT_NONE = 0,
    EVT_BTN_K1_SHORT,
    EVT_BTN_K2_SHORT,
    EVT_BTN_K3_SHORT,
    EVT_BTN_K1_LONG,
    EVT_BTN_K2_LONG,
    EVT_BTN_K3_LONG,
    EVT_TICK,
    EVT_SHUTDOWN,
    EVT_TICK_CONFIG
} app_event_type_t;
```

---

## 4. eventfd 的机制与选型理由

**机制**：`eventfd` 是 Linux 的内核计数器型 fd。`write()` 累加计数，`read()` 读出并清零，可被 `poll/epoll` 监听。

**为什么用 eventfd**：
- **跨线程唤醒**：主线程在 `poll()` 中阻塞时，其他线程可以“敲门”。
- **控制事件传递**：如“请求调节 tick 周期/关闭 tick”、“退出”之类没有自然 fd 的事件。
- **统一事件循环**：所有唤醒都通过 fd 进入主线程，避免额外锁与条件变量交织。

**典型使用场景**：
1) 请求主线程立即退出。
2) UI 线程请求主线程开启/关闭 tick（调整 timerfd）。
3) 后台线程产生“无 fd 事件”（如状态变更）需立刻处理。

---

## 5. tick 机制的用途与策略

**tick 的用途**：
- 动画时间推进（翻页、闪烁、渐变）。
- UI 超时（自动返回/休眠）。
- 状态机周期任务（如定期刷新指标）。

**结论**：tick 不是“固定帧率必须存在”，而是“当需要时间推进时启用”。

---

## 6. UI 刷新策略：事件驱动 vs 固定帧率

### 6.1 事件驱动
**优点**：空闲时不渲染，CPU/功耗低。  
**缺点**：没有 tick 就无法平滑动画。

### 6.2 固定帧率
**优点**：动画稳定、响应上限可控。  
**缺点**：空闲时也在刷新，浪费资源。

### 6.3 混合策略（推荐）
**策略**：默认事件驱动；当 UI 需要动画或定时刷新时启用 tick；动画结束后关闭 tick。

**理由**：
- 兼顾性能与体验。
- tick 只在“需要时间推进”时存在。
- 适合资源受限的嵌入式设备。

---

## 7. 事件队列策略

**目标**：关键事件不丢，tick 可合并。

**策略（对齐 thread-model 约定）**：
- 关键事件（按键、退出）不轻易丢弃：优先挤出 tick，必要时短暂阻塞后仍满则降级并记录统计。
- tick 事件只保留最新（合并）。

**实现要点（建议）**：
- 基于 `ring_queue`，使用合并函数合并 `EVT_TICK`。
- 当队列满且 incoming 为关键事件时：
  1) 尝试清理一个 tick；
  2) 若无 tick，可阻塞等待短暂空位；
  3) 仍满则记录降级统计（用于诊断）。

---

## 8. 主线程事件循环（建议流程）

```
loop:
    poll(gpio_fd, timerfd, eventfd)

    if gpio_fd:
        read edge events
        push EVT_BTN_* to queue

    if timerfd:
        read timerfd
        push EVT_TICK (可合并)

    if eventfd:
        read eventfd
        handle control message (shutdown/tick config)

    notify UI thread
```

**注意**：读取 timerfd/eventfd 必须 drain（避免重复唤醒）。

---

## 9. UI 线程事件处理（建议流程）

```
loop:
    wait(queue_not_empty)
    while queue has events:
        evt = pop
        handle(evt)
        if needs_animation:
            request tick enable
        if animation_end:
            request tick disable
```

**要点**：
- UI 线程只负责状态变更和渲染。
- 任何跨线程控制，通过 eventfd 请求主线程执行。

**默认 handler 行为（实现参考）**：
- 使用 `ui_controller` 处理事件并渲染。
- 按键事件触发短动画：请求 `event_loop_request_tick(50ms)`，计数 10 次后关闭 tick。
- K2 短按关闭电源时，立即关闭 tick。
- 自动息屏：若持续无按键输入超过 30s，则关闭屏幕；任意按键事件会唤醒并重置计时。
- 空闲检测使用 1s 低频 tick（默认启动），动画结束后回落到 idle tick。
- 自动息屏逻辑位于 `ui_controller`，由 tick 的时间戳驱动。

---

## 10. 失败模式与边界

- **队列满**：保证关键事件不丢，tick 优先合并/丢弃。
- **过度唤醒**：eventfd 写频繁可能导致唤醒风暴，需要在写入前合并请求或去抖。
- **长时间阻塞**：UI 线程阻塞不可影响输入采集（主线程独立）。

---

## 11. 测试策略（Phase 3）

- `test_event_queue`：关键事件保序 + tick 合并。
- `test_thread_safety`：并发 push/pop 不死锁。
- `test_event_loop_stub`：模拟 fd 唤醒链路。
- Target 验证：双线程下按键与 UI 渲染同时进行无丢事件。

---

## 12. 设计取舍总结

- **eventfd**：用作“跨线程唤醒铃铛”，统一进入 `poll()`。
- **tick**：只在需要时间推进时启用，避免固定帧率浪费。
- **混合刷新策略**：兼顾用户体验与资源占用。
- **队列策略**：关键事件优先，tick 可合并/丢弃。

此文档作为 Phase 3 的设计规范，后续实现需与此保持一致，并在变更时更新本文件。
