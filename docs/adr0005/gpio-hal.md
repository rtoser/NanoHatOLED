# GPIO HAL 说明（libgpiod + mock）

本文件描述 GPIO HAL 的对外接口、编译期配置与去抖策略。

## 编译期配置

- `GPIOCHIP_PATH`：GPIO 芯片路径（默认 `/dev/gpiochip0`）
- `BTN_OFFSETS`：按键 line 偏移（默认 `0,2,3`）

示例：
```c
#define GPIOCHIP_PATH "/dev/gpiochip1"
#define BTN_OFFSETS 1,4,7
```

## 去抖策略

- 首选硬件去抖：`gpiod_line_settings_set_debounce_period_us()`。
- 若驱动不支持（`ENOTSUP/EINVAL`），自动切换软件去抖（30ms 内丢弃抖动边沿）。

## 事件模型

- 短按：按下后释放触发（`*_SHORT`）
- 长按：按住超过 600ms 触发（`*_LONG`）
- 事件时间戳优先使用内核单调时间（ns）

## `get_fd()` 与 `wait_event()` 约束

- `get_fd()` 返回可被 `poll()` 监听的 fd；
- `wait_event()` 必须与 `get_fd()` 一致，保证 fd 可读时能取到事件；
- 主线程应使用 `poll()` + `read_edge_events()` 批量读取。

## Host Mock 行为

- Linux 下使用 `eventfd`，非 Linux 使用 `pipe` 模拟唤醒；
- Mock 支持软去抖路径，便于覆盖“硬件去抖不可用”场景。
