# GPIO 按键设计文档

## 硬件概述

NanoHat OLED 模块有 3 个轻触按键，连接到 Allwinner H5 的 GPIO 引脚：

| 按键 | GPIO 编号 | Allwinner 端口 | 功能 |
|------|-----------|----------------|------|
| K1   | GPIO 0    | PA0            | 上一页 |
| K2   | GPIO 2    | PA2            | 确认/选择 |
| K3   | GPIO 3    | PA3            | 下一页 |

所有按键均为低电平有效，外部有上拉电阻：
- 空闲状态：GPIO 读取为高电平 (1)
- 按下状态：GPIO 读取为低电平 (0)

## 实现架构

### Linux GPIO Sysfs 接口

实现使用 Linux GPIO sysfs 接口 (`/sys/class/gpio/`)，以保证在不同 Linux 发行版（OpenWrt、FriendlyCore 等）上的兼容性：

```
/sys/class/gpio/
├── export          # 写入 GPIO 编号以导出
├── unexport        # 写入 GPIO 编号以取消导出
└── gpio{N}/
    ├── direction   # "in" 或 "out"
    ├── value       # "0" 或 "1"
    ├── edge        # "none", "rising", "falling", "both"
    └── active_low  # "0" 或 "1"
```

### 中断驱动检测

实现使用边沿触发中断配合 `poll()` 系统调用，而非忙等待轮询 GPIO 值：

```c
struct pollfd pfd[3];
for (int i = 0; i < 3; i++) {
    pfd[i].fd = btn_fds[i];        // /sys/class/gpio/gpioN/value 的文件描述符
    pfd[i].events = POLLPRI | POLLERR;
}
int ret = poll(pfd, 3, timeout_ms);
```

优点：
- 空闲时 CPU 占用几乎为零
- 按键事件即时响应
- 主循环可设置短超时（100ms）用于显示更新

## 设计权衡

### 边沿触发模式选择

#### 方案一：`edge="both"`（已弃用）

在按下（下降沿）和松开（上升沿）时都触发中断。

```
按键时序图：
    HIGH ─────┐         ┌───── HIGH
              │         │
     LOW      └─────────┘
              ↑         ↑
           下降沿     上升沿
           (按下)     (松开)
```

**问题**：当快速按下并松开后 `poll()` 返回时，需要读取当前 GPIO 值来判断是按下还是松开事件。如果读取时按键已松开，会看到高电平，无法区分：
- 这个中断是按下触发的（按键已松开）？
- 还是松开触发的？

这导致快速点击时约 20-40% 的按键丢失。

#### 方案二：`edge="falling"`（当前使用）

仅在按下（下降沿）时触发中断。

**优点**：
- 每个中断 = 一次按键，保证不丢失
- 无需状态比较
- 中断和读取值之间无竞态条件
- 100% 可靠的按键检测

**代价**：
- 无法直接检测按键松开
- 长按检测需要额外轮询当前值

### 长按实现

使用仅下降沿检测时，长按通过以下方式实现：

1. 下降沿中断发生时记录时间戳
2. 通过 `btn_pressed[]` 数组跟踪按下状态
3. 每次 `gpio_button_wait()` 调用时检查按键是否仍被按住：
   - 读取当前 GPIO 值
   - 如果仍为低电平且持续 >= 600ms，报告长按事件
4. 当按键读取为高电平时清除按下状态

```c
#define LONG_PRESS_MS 600

// 下降沿中断时：
btn_pressed[i] = true;
btn_press_time[i] = now;
btn_long_reported[i] = false;
return BTN_K1_PRESS + i;

// 后续调用时检查长按：
if (btn_pressed[i] && !btn_long_reported[i]) {
    if (now - btn_press_time[i] >= LONG_PRESS_MS) {
        btn_long_reported[i] = true;
        return BTN_K1_LONG_PRESS + i;
    }
}
```

### 去抖动策略

硬件按键存在接触抖动（按下/松开时产生多次跳变）。实现依赖：

1. **边沿触发合并**：Linux 内核会合并快速的边沿跳变
2. **状态跟踪**：`btn_pressed[]` 标志防止同一次按下产生重复事件
3. **100ms poll 超时**：事件检查之间的自然去抖窗口

未添加显式软件去抖延迟以保持响应性。

## API 参考

### 初始化 / 清理

```c
int gpio_button_init(void);   // 成功返回 0，失败返回 -1
void gpio_button_cleanup(void);
```

### 事件检测

```c
// 带超时的阻塞等待（推荐用于主循环）
button_event_t gpio_button_wait(int timeout_ms);

// 非阻塞轮询（内部调用 wait(0)）
button_event_t gpio_button_poll(void);

// 直接读取 GPIO（用于调试）
bool gpio_button_read(int gpio);
```

### 事件类型

```c
typedef enum {
    BTN_NONE = 0,
    BTN_K1_PRESS,        // K1 按下
    BTN_K2_PRESS,        // K2 按下
    BTN_K3_PRESS,        // K3 按下
    BTN_K1_LONG_PRESS,   // K1 长按 >= 600ms
    BTN_K2_LONG_PRESS,   // K2 长按 >= 600ms
    BTN_K3_LONG_PRESS    // K3 长按 >= 600ms
} button_event_t;
```

## 使用示例

```c
#include "gpio_button.h"

int main() {
    if (gpio_button_init() < 0) {
        fprintf(stderr, "按键初始化失败\n");
        return 1;
    }

    while (running) {
        // 更新显示等
        update_display();

        // 等待按键或 100ms 超时
        button_event_t event = gpio_button_wait(100);

        switch (event) {
            case BTN_K1_PRESS:
                previous_page();
                break;
            case BTN_K3_PRESS:
                next_page();
                break;
            case BTN_K2_LONG_PRESS:
                toggle_display();
                break;
            default:
                break;
        }
    }

    gpio_button_cleanup();
    return 0;
}
```

## 故障排查

### 按键无响应

1. 检查 GPIO 导出：`ls /sys/class/gpio/gpio{0,2,3}/`
2. 验证边沿设置：`cat /sys/class/gpio/gpio0/edge` 应显示 "falling"
3. 检查权限：GPIO sysfs 需要 root 权限或 gpio 组成员身份

### 按键丢失

如使用 `edge="both"`，请切换到 `edge="falling"`（见上文设计权衡）。

### GPIO 导出失败

可能其他进程已导出该 GPIO。实现会优雅处理 `EBUSY` 错误，如 GPIO 已导出则继续执行。

## 文件结构

```
src/
├── gpio_button.h    # 公共 API 和常量定义
├── gpio_button.c    # 实现代码
└── main.c           # 主程序中的使用
```
