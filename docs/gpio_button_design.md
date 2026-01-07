# GPIO 按键设计文档

## 硬件概述

NanoHat OLED 模块有 3 个轻触按键，连接到 Allwinner H5 的 GPIO 引脚：

| 按键 | GPIO 编号 | Allwinner 端口 | 功能 |
|------|-----------|----------------|------|
| K1   | GPIO 0    | PA0            | 上一页 |
| K2   | GPIO 2    | PA2            | 开关/菜单 |
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

#### 方案一：`edge="both"`（当前使用）

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

**策略**：
- 只在**松开**时上报短按事件，避免长按被误判为短按
- 按下后记录时间，松开时根据持续时长判定短/长按
- 若仅捕获到松开中断且当前未处于按下状态，则视为一次快速短按

#### 方案二：`edge="falling"`（已弃用）

仅在按下（下降沿）时触发中断。

**问题**：
- 无法直接检测按键松开
- 需要在应用层延迟确认短按，影响交互体验

### 长按实现

使用双边沿检测时，长按通过以下方式实现：

1. 按下中断发生时记录时间戳
2. 通过 `btn_pressed[]` 数组跟踪按下状态
3. 松开时计算持续时长：
   - 持续 >= 600ms，报告长按
   - 否则报告短按

```c
#define LONG_PRESS_MS 600

// 按下中断时：
btn_pressed[i] = true;
btn_press_time[i] = now;

// 松开时判定短/长按：
if (btn_pressed[i] && !currently_pressed) {
    btn_pressed[i] = false;
    if (now - btn_press_time[i] >= LONG_PRESS_MS) {
        return BTN_K1_LONG_PRESS + i;
    }
    return BTN_K1_PRESS + i;
}
```

### 去抖动策略

硬件按键存在接触抖动（按下/松开时产生多次跳变）。实现依赖：

1. **边沿触发合并**：Linux 内核会合并快速的边沿跳变
2. **状态跟踪**：`btn_pressed[]` 标志防止同一次按下产生重复事件
3. **100ms poll 超时**：事件检查之间的自然去抖窗口

未添加显式软件去抖延迟以保持响应性。

## 按键电平与原理图

通常可通过原理图判断按键电平：若有上拉电阻，空闲为高电平、按下为低电平；若有下拉电阻则相反。但实际硬件还可能受到引脚复用配置、上/下拉方向或板级改版影响，因此**最终以实测为准**，建议在目标板上读取 `/sys/class/gpio/gpioN/value` 校验空闲电平。

## 案例：按键不灵敏排查（2025-01）

**现象**：
- 三个按键偶发不响应，长按失效且不会触发短按动作。

**根因**：
- 代码假设“按下=低电平”，但实测空闲电平为 `0`、按下为 `1`，电平逻辑与假设相反，导致状态机在空闲态误判为“持续按下”，真实按键事件被吞。
- sysfs 的 `value` 读取会清掉边沿事件，若在无事件时读取会增加丢按键概率。

**修复**：
- 启动时探测空闲电平并推断“按下电平”，避免硬编码。
- 仅在 `POLLPRI` 事件发生时读取 `value`，减少清事件的副作用。

**结论**：
原理图是判断电平的首要依据，但现场实测能快速验证是否存在“逻辑电平反向”的实现问题。

## 理想实现路线（后续版本规划）

### 方案一：内核 gpio-keys + 设备树（推荐）
- 由内核处理边沿、中断与去抖，应用层只需读取 `/dev/input/eventX`。
- 可配置 `debounce-interval`、`gpio-key,wakeup` 等参数，稳定且可维护。

### 方案二：libgpiod v2 事件 + 状态机
- 用户态事件驱动，带时间戳与内核级去抖（`gpiod_line_settings_set_debounce_period`）。
- 建议独立输入线程 + 事件队列，避免 UI 阻塞导致丢事件。

### 方案三：sysfs + poll（仅兼容考虑）
- sysfs 已过时且事件不排队，必须确保只在 `POLLPRI` 事件上读取 `value`。
- 需要软件去抖与状态机，否则抖动和丢边沿会显著影响可靠性。

### 状态机模板（简化）
- IDLE →（按下沿）→ PRESSED  
- PRESSED →（松开沿，< 600ms）→ SHORT  
- PRESSED →（松开沿，>= 600ms）→ LONG

### 去抖建议
- 即使有 RC 去抖，仍建议保留 10–30ms 的软件去抖窗口，避免阈值附近抖动导致多次边沿。

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
    BTN_K1_PRESS,        // K1 短按（松开触发）
    BTN_K2_PRESS,        // K2 短按（松开触发）
    BTN_K3_PRESS,        // K3 短按（松开触发）
    BTN_K1_LONG_PRESS,   // K1 长按（松开时按住 >= 600ms）
    BTN_K2_LONG_PRESS,   // K2 长按（松开时按住 >= 600ms）
    BTN_K3_LONG_PRESS    // K3 长按（松开时按住 >= 600ms）
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
            case BTN_K2_PRESS:
                toggle_display();
                break;
            case BTN_K2_LONG_PRESS:
                toggle_service_menu();
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
2. 验证边沿设置：`cat /sys/class/gpio/gpio0/edge` 应显示 "both"
3. 检查权限：GPIO sysfs 需要 root 权限或 gpio 组成员身份

### 短按不触发

确认使用 `edge="both"` 且松开事件被正确捕获（短按在松开时上报）。

### GPIO 导出失败

可能其他进程已导出该 GPIO。实现会优雅处理 `EBUSY` 错误，如 GPIO 已导出则继续执行。

## 文件结构

```
src/
├── gpio_button.h    # 公共 API 和常量定义
├── gpio_button.c    # 实现代码
└── main.c           # 主程序中的使用
```
