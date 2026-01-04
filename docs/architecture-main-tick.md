# 主循环节拍设计（Main Tick）

## 背景与目标
本项目使用单线程事件驱动模型，主循环需要同时处理按键、状态刷新与 OLED 绘制。节拍设计的目标是：
- 按键响应快且稳定
- UI 绘制节拍可控，动画流畅
- 任务调度无明显漂移
- 空闲时尽量省电/省 CPU

## 关键原则
1. **只使用单调时钟**：使用 `CLOCK_MONOTONIC` 计算时间，避免系统时间跳变影响节拍。
2. **多 deadline 统一调度**：为不同任务维护独立的下次触发时间（frame/status/service/menu）。
3. **poll 超时取最小**：`timeout = min(deadline_i - now)`，既能等待事件又能按时执行任务。
4. **允许丢帧防漂移**：绘制太慢时允许跳过帧，保持整体节奏。
5. **按需绘制**：只有 `dirty` 或动画进行中才渲染，避免空转。
6. **动画时间驱动**：根据 `elapsed_ms` 计算位移/进度，掉帧也不会变慢。
7. **按键路径要轻**：`handle_button()` 只更新状态，不做 I/O。
8. **避免阻塞 I/O**：必要时降低频率或拆分更新。

## 推荐的节拍框架
维护多个 deadline，并在循环中统一调度：

- `next_frame_ms`：下一帧渲染时间
- `next_status_ms`：系统状态刷新
- `next_service_ms`：服务列表刷新
- `menu_deadline_ms`：菜单超时

循环中：
1. 计算最小 timeout 并调用 `gpio_button_wait(timeout)`。
2. 处理按键事件（只改状态+`dirty`）。
3. 执行到期任务（状态/服务/菜单）。
4. 若 `dirty` 或动画中，且到达帧时间则渲染。
5. 用“推进式”更新 `next_frame_ms`，防止漂移。

## 伪代码示例
```c
uint64_t next_frame = now + FRAME_INTERVAL_MS;
uint64_t next_status = now + STATUS_UPDATE_MS;
uint64_t next_service = now + SERVICE_REFRESH_MS;

while (running) {
    uint64_t now = mono_ms();
    int64_t timeout = min_deadline(next_frame, next_status, next_service, menu_deadline) - now;
    if (timeout < 0) timeout = 0;

    button_event_t ev = gpio_button_wait((int)timeout);
    if (ev != BTN_NONE) {
        handle_button(ev);  // 只更新状态 + dirty
    }

    now = mono_ms();

    if (now >= next_status) {
        sys_status_update_basic(&sys_status);
        dirty = 1;
        next_status += STATUS_UPDATE_MS;
    }

    if (current_page == PAGE_SERVICES && now >= next_service) {
        sys_status_update_services(&sys_status);
        dirty = 1;
        next_service += SERVICE_REFRESH_MS;
    }

    if (menu_active && now >= menu_deadline) {
        menu_exit();
        dirty = 1;
    }

    if (dirty || animating) {
        if (now >= next_frame) {
            draw_current_page();
            dirty = 0;
            next_frame += frame_interval();  // 动画时更短
            while (next_frame <= now) {
                next_frame += frame_interval();  // 追赶，允许丢帧
            }
        }
    }
}
```

## 与本项目的结合建议
- `handle_button()` 只做状态变更和 `mark_dirty()`，避免阻塞。
- 动画（翻页/抖动）应尽量改为“逐帧推进”而非一次性阻塞。
- 关屏时清空动画状态，避免空转。
- 空闲时把 `next_frame` 设得更远，让 `poll` 由其它 deadline 唤醒。

## 与当前 src/main.c 的对应关系/落地清单
- 时间源：`get_time_ms()` 已使用 `CLOCK_MONOTONIC`，满足单调时钟要求。
- Deadline 变量：`next_frame_ms` / `next_status_ms` / `next_service_ms` 已引入，菜单超时用 `menu_last_input_ms + MENU_TIMEOUT_MS` 计算。
- 超时计算：主循环中 `timeout = min(deadline - now)` 的逻辑已实现，并做了 `0..1000ms` 的裁剪。
- 按需渲染：`dirty` 与 `is_animating()` 作为绘制门控已存在，`mark_dirty()` 用于事件触发重绘。
- 防漂移：绘制后使用 `next_frame_ms += interval` 并通过 `while (next_frame_ms <= now)` 追赶，避免累积漂移。
- 仍需关注的技术债：
  - 翻页动画仍为阻塞式（`transition_to_page()`），会暂停主循环约 160ms。
  - `handle_button()` 内仍有同步 I/O（如 `ubus_service_action()`），可能造成短暂卡顿。
- 落地自检清单：
  - 按键路径是否仅更新状态并 `mark_dirty()`（不触发绘制）。
  - 关屏时是否清理动画状态（避免空转）。
  - 进入/退出服务页时是否正确刷新服务列表与刷新频率。

## 时序图（单线程）
```
时间轴 →
┌───────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│ gpio_button   │   │ handle_button│   │ timed tasks │   │ render frame │
│ wait(timeout) │──▶│ update state │──▶│ status/menu │──▶│ u8g2_SendBuf  │
└───────────────┘   └──────────────┘   └──────────────┘   └──────────────┘
        ▲                    │                 │                 │
        │                    │                 │                 │
  (deadline 到达)────────────┴─────────────────┴─────────────────┘
```

## 常见陷阱
- 用 `now + interval` 直接重置下一帧，会把绘制耗时计入周期并产生漂移。
- `poll` 超时并不是固定睡眠，事件会立即唤醒，不会增加按键延迟。
- sysfs GPIO 边沿事件可能合并，绘制时长过大仍可能影响极短按键。
