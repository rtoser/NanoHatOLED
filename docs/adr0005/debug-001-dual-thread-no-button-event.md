# Debug-001: test_dual_thread 检测不到按键事件

## 现象

- `test_gpio_hw.c` 在目标机上可以正常检测到按键事件
- `test_dual_thread.c` 在目标机上始终检测不到按键事件

## 根本原因

`gpio_hal_libgpiod_wait_event()` 的超时逻辑在 `timeout=0` 时存在问题。

### 问题代码 (gpio_hal_libgpiod.c:304-322)

```c
uint64_t start_ms = time_hal_now_ms();

for (;;) {
    // 1. 先检查 pending queue
    if (pending_pop(event)) {
        return 1;
    }

    // 2. 检查超时
    uint64_t now_ms = time_hal_now_ms();
    if (timeout_ms >= 0) {
        uint64_t elapsed_total = now_ms - start_ms;
        if (elapsed_total >= (uint64_t)timeout_ms) {  // 当 timeout=0: 0 >= 0 → TRUE
            return 0;  // ← 直接返回，不执行下面的 poll/read！
        }
    }

    // 3. poll 等待边沿事件（永远执行不到）
    int wait_ret = wait_for_edge_event(fd, effective_timeout);
    ...
    // 4. 读取并处理边沿事件（永远执行不到）
    int processed = read_and_process_edges(event);
}
```

**当 `timeout_ms=0` 且 pending queue 为空时**：

- `elapsed_total = now_ms - start_ms = 0`
- 条件 `elapsed_total >= 0` → **TRUE**
- 函数直接返回 0，**完全跳过了 `wait_for_edge_event()` 和 `read_and_process_edges()`**

## 调用链对比

| 测试 | 调用方式 | timeout | 结果 |
|------|---------|---------|------|
| test_gpio_hw | `gpio_hal->wait_event(10000, &evt)` | 10000ms | 执行 poll + read |
| test_dual_thread | `event_loop_handle_gpio()` → `gpio_hal->wait_event(0, ...)` | 0ms | **直接超时返回** |

## 问题流程

`test_dual_thread.c` 的事件处理流程：

1. `event_loop_run()` 的 poll 检测到 `gpio_fd` 可读（POLLIN）
2. 调用 `event_loop_handle_gpio(loop)` (event_loop.c:192)
3. `event_loop_handle_gpio()` 调用 `gpio_hal->wait_event(0, &gpio_evt)` (event_loop.c:99)
4. `wait_event(0, ...)` 检查 pending queue（为空）
5. 检查超时：`0 >= 0` → **TRUE** → 返回 0
6. **边沿事件从未被读取！** 它仍在内核缓冲区中
7. 下次 poll 仍会返回 POLLIN，但同样不会被处理
8. 死循环：poll 一直返回可读，但事件永远不被处理

## 验证方法

运行 `test_dual_thread` 并观察日志输出：

**会看到（问题表现）**：
```
[loop] poll ret=1
[loop] gpio revents=0x1
[loop] handle_gpio
[gpio] wait_event loop start fd=N timeout=0
[loop] handle_gpio done ret=0    ← 返回 0，没有处理事件
```

**不会看到（正常应有）**：
```
[gpio] wait_for_edge_event poll fd=...   ← 这行不会出现
[gpio] read_and_process_edges num=...    ← 这行不会出现
```

## 调试开关（复现/排查）

- 交叉编译并执行 Target 测试时开启日志：
  - `make test-target-dual DEBUG=1`
  - 需要更详细轮询日志：`make test-target-dual DEBUG=1 VERBOSE=1`
- `BUILD=debug/release` 只影响优化与符号（`-O0/-O2`、`-g`、`-DNDEBUG`），不决定日志开关。

## 设计冲突

`event_loop` 架构的设计意图：

- 外层 poll 负责检测哪个 fd 有事件
- 内层 `wait_event(0, ...)` 期望是"非阻塞读取事件"

但 `wait_event()` 的实现将 `timeout=0` 解释为"立即超时"，而不是"非阻塞但仍尝试读取"。

## 修复方案

采用**方案 A**：修改 `wait_event()` 的超时逻辑，在 `timeout=0` 时特殊处理，直接尝试读取事件。

### 修复后代码 (gpio_hal_libgpiod.c:318-362)

```c
for (;;) {
    pthread_mutex_lock(&g_lock);
    if (pending_pop(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    pthread_mutex_unlock(&g_lock);

    // 修复：timeout=0 时直接尝试读取，不走超时逻辑
    if (timeout_ms == 0) {
        int processed = read_and_process_edges(event);
        if (processed != 0) {
            return processed > 0 ? 1 : -1;
        }
        return 0;
    }

    uint64_t now_ms = time_hal_now_ms();
    int effective_timeout = timeout_ms;
    if (timeout_ms >= 0) {
        uint64_t elapsed_total = now_ms - start_ms;
        if (elapsed_total >= (uint64_t)timeout_ms) {
            return 0;
        }
        int remaining = (int)((uint64_t)timeout_ms - elapsed_total);
        effective_timeout = remaining;
    }

    // 使用 fd + poll 等待事件，避免平台上 wait_edge_events 异常返回。
    int wait_ret = wait_for_edge_event(fd, effective_timeout);
    if (wait_ret <= 0) {
        if (wait_ret < 0) {
            return wait_ret;
        }
        return 0;
    }

    int processed = read_and_process_edges(event);
    if (processed < 0) {
        return -1;
    }
    if (processed > 0) {
        GPIO_LOG("[gpio] pending event delivered count=%zu\n", g_pending.count);
        return 1;
    }
}
```

### 修复要点

在检查 pending queue 之后、进入超时逻辑之前，增加 `timeout_ms == 0` 的特殊分支：

```c
if (timeout_ms == 0) {
    int processed = read_and_process_edges(event);
    if (processed != 0) {
        return processed > 0 ? 1 : -1;
    }
    return 0;
}
```

这样 `timeout=0` 的语义变为"非阻塞读取"：直接尝试从内核缓冲区读取边沿事件，无需 poll 等待。

## 相关文件

- `src/hal/gpio_hal_libgpiod.c:296-343` - `wait_event` 实现
- `src/event_loop.c:95-113` - `event_loop_handle_gpio` 实现
- `tests/target/test_dual_thread.c` - 问题测试用例
- `tests/target/test_gpio_hw.c` - 正常工作的对照用例
