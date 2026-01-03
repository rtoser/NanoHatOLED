# NanoHat OLED 架构设计

## 概述

NanoHat OLED 是一个轻量级的嵌入式显示应用，专为 NanoPi NEO2 Plus 设计。采用单进程事件驱动架构，通过 GPIO 中断响应按键，定时轮询系统状态，在 128x64 OLED 屏幕上显示多页信息。

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        main.c                                │
│                     (主循环/页面渲染)                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ gpio_button  │  │  sys_status  │  │  u8g2_port_linux │   │
│  │  (按键驱动)   │  │  (状态采集)   │  │   (显示驱动)      │   │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘   │
│         │                 │                    │             │
└─────────┼─────────────────┼────────────────────┼─────────────┘
          │                 │                    │
          ▼                 ▼                    ▼
    ┌──────────┐     ┌──────────┐         ┌──────────┐
    │ GPIO     │     │ /proc    │         │ I2C      │
    │ sysfs    │     │ /sys     │         │ /dev/i2c │
    └──────────┘     └──────────┘         └──────────┘
```

## 模块说明

### 1. main.c - 主控制器

**职责**：
- 初始化各子系统（显示、按键、信号处理）
- 运行主事件循环
- 页面切换逻辑
- 渲染四个显示页面

**页面结构**：
| 页面 | 内容 |
|------|------|
| Status | CPU 使用率/温度、内存使用率、运行时间 |
| Network | IP 地址、实时上下行网速 |
| Services | xray/dropbear/dockerd 服务状态 |
| System | 内存详细信息（总量/可用/空闲） |

**主循环逻辑**：
```c
while (running) {
    // 每秒更新系统状态并重绘
    if (now != last_update) {
        sys_status_update(&sys_status);
        draw_current_page();
    }

    // 等待按键中断（100ms 超时）
    button_event_t event = gpio_button_wait(100);
    if (event != BTN_NONE) {
        handle_button(event);
    }
}
```

### 2. gpio_button.c - 按键驱动

**职责**：
- 配置 GPIO 引脚（GPIO 0/2/3）
- 设置边沿触发中断
- 使用 `poll()` 等待中断事件
- 处理按键防抖

**设计要点**：
- 使用 Linux GPIO sysfs 接口
- 边沿触发（falling edge）而非轮询
- poll() 阻塞等待，CPU 占用极低
- 50ms 防抖延时

**按键映射**：
| 按键 | GPIO | 功能 |
|------|------|------|
| K1 | GPIO 0 | 上一页 / 唤醒屏幕 |
| K2 | GPIO 2 | 开关屏幕 |
| K3 | GPIO 3 | 下一页 / 唤醒屏幕 |

### 3. sys_status.c - 系统状态采集

**职责**：
- 读取 CPU 使用率（/proc/stat）
- 读取 CPU 温度（/sys/class/thermal）
- 读取内存信息（/proc/meminfo）
- 读取网络流量（/proc/net/dev）
- 检测服务运行状态（/proc/*/comm）

**性能优化**：
- 直接读取 /proc 文件系统，避免 fork/exec
- 服务状态检测结果缓存 5 秒
- 网速计算基于时间差和字节差

**数据结构**：
```c
typedef struct {
    float cpu_usage;
    float cpu_temp;
    uint64_t mem_total, mem_free, mem_available;
    char ip_addr[16];
    char hostname[32];
    uint32_t uptime;
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_speed, tx_speed;
    service_status_t services[MAX_SERVICES];
    int service_count;
} sys_status_t;
```

### 4. u8g2_port_linux.c - 显示驱动移植层

**职责**：
- 实现 u8g2 库的 Linux I2C 后端
- 封装 I2C 设备操作（open/write/ioctl）
- 提供 GPIO 和延时回调

**硬件参数**：
- I2C 总线：`/dev/i2c-0`
- 设备地址：`0x3C`
- 显示芯片：SSD1306
- 分辨率：128x64

## 数据流

```
系统状态采集                    按键输入
     │                            │
     ▼                            ▼
sys_status_update()      gpio_button_wait()
     │                            │
     └──────────┬─────────────────┘
                │
                ▼
          main loop
                │
                ▼
        draw_current_page()
                │
                ▼
        u8g2_SendBuffer()
                │
                ▼
           I2C 输出
                │
                ▼
          OLED 显示
```

## 设计决策

### 1. 单线程 vs 多线程

**选择**：单线程 + 事件驱动

**理由**：
- 硬件资源有限（单核 ARM）
- 逻辑简单，无需复杂同步
- poll() 提供足够的响应性
- 降低内存占用和调试复杂度

### 2. 轮询 vs 中断

**选择**：GPIO 中断 + 定时刷新

**理由**：
- 按键使用中断，响应迅速且 CPU 友好
- 系统状态使用定时轮询（1 秒间隔）
- 服务状态使用缓存（5 秒间隔）

### 3. 静态链接 vs 动态链接

**选择**：静态链接（musl libc）

**理由**：
- OpenWrt 环境简化部署
- 单一可执行文件，无依赖问题
- musl 体积小，适合嵌入式

### 4. 进程检测方式

**选择**：直接读取 /proc/*/comm

**理由**：
- 避免 fork/exec 开销
- BusyBox pgrep/pidof 行为不一致
- 纯用户态实现，更可控

## 资源占用

| 指标 | 数值 |
|------|------|
| 二进制大小 | ~300KB（strip 后） |
| 内存占用 | < 2MB |
| CPU 占用 | < 1%（空闲时） |
| I2C 带宽 | ~1KB/s（刷新时） |

## 依赖关系

```
main.c
  ├── gpio_button.h
  ├── sys_status.h
  └── u8g2_port_linux.h
        └── u8g2.h (u8g2 库)

sys_status.c
  └── 系统头文件 (stdio, dirent, ifaddrs, ...)

gpio_button.c
  └── poll.h, fcntl.h

u8g2_port_linux.c
  └── linux/i2c-dev.h
```

## 扩展性

### 添加新页面
1. 在 `page_t` 枚举中添加新页面
2. 实现 `draw_page_xxx()` 函数
3. 在 `draw_current_page()` 的 switch 中添加 case

### 添加新服务监控
修改 `sys_status.c` 中的 `services[]` 数组：
```c
static const char *services[] = {"xray", "dropbear", "dockerd", "新服务", NULL};
```

### 更换显示屏
修改 `display_init()` 中的 u8g2 setup 函数，选择对应的显示控制器。
