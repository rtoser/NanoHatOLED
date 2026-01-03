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
- 服务页菜单控制（K2 长按进入/退出，K1/K3 选择，K2 短按启停，超时退出）
- 通过 libubus/procd 执行服务启停
- 非菜单页长按抖动提示

**页面结构**：
| 页面 | 内容 |
|------|------|
| Status | 主机名、CPU 使用率/温度、内存使用率、运行时间 |
| Network | IP 地址、实时上下行网速 |
| Services | xray_core/dropbear/dockerd 服务状态（最多 3 项） |
| System | 内存详细信息（总量/可用/空闲） |

**主循环逻辑**：
```c
while (running) {
    // 每秒更新系统状态并重绘
    if (now != last_update) {
        sys_status_update_basic(&sys_status);
        draw_current_page();
    }

    // 服务页状态按 3-5 秒间隔刷新
    if (current_page == PAGE_SERVICES &&
        now - last_service_refresh >= 5) {
        sys_status_update_services(&sys_status);
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
- 检测按键按下与长按事件

**设计要点**：
- 使用 Linux GPIO sysfs 接口
- 双边沿触发（press/release）
- poll() 阻塞等待，CPU 占用极低
- 长按阈值 600ms（短按在松开时上报）

**按键映射**：
| 按键 | GPIO | 功能 |
|------|------|------|
| K1 | GPIO 0 | 上一页 / 唤醒屏幕 |
| K2 | GPIO 2 | 短按开关屏幕 / 服务页长按进入菜单 |
| K3 | GPIO 3 | 下一页 / 唤醒屏幕 |

### 3. sys_status.c - 系统状态采集

**职责**：
- 读取 CPU 使用率（/proc/stat）
- 读取 CPU 温度（/sys/class/thermal）
- 读取内存信息（/proc/meminfo）
- 读取网络流量（/proc/net/dev）
- 获取主机名与 IP 地址（优先 br-lan/eth0/wlan0）
- 通过 libubus/procd 检测服务运行状态

**性能优化**：
- 直接读取 /proc 文件系统，避免 fork/exec
- 服务状态通过 libubus/procd 获取
- 服务状态仅在启动、进入/退出服务页以及服务页停留时按 3-5 秒间隔刷新
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

### 4. ubus_service.c - 服务控制与状态查询

**职责**：
- 建立并维护 libubus 连接
- 查询 procd 服务状态（`service list`）
- 触发服务启停（`service start/stop`）

**设计要点**：
- 使用同步 `ubus_invoke`
- 断线时按需重连
- 服务页刷新节流以降低调用频率

### 5. u8g2_port_linux.c - 显示驱动移植层

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
sys_status_update_basic()      gpio_button_wait()
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
- 服务状态仅在启动、进入/退出服务页以及服务页停留时按 3-5 秒间隔刷新

### 3. 静态链接 vs 动态链接

**选择**：静态链接（musl libc）

**理由**：
- OpenWrt 环境简化部署
- 单一可执行文件，无依赖问题
- musl 体积小，适合嵌入式

### 4. 服务状态获取方式

**选择**：通过 libubus/procd 查询

**理由**：
- 状态与控制同源，避免服务名/进程名不一致
- 不依赖 /proc 扫描
- 与 OpenWrt 服务管理机制一致

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
  ├── ubus_service.h
  └── u8g2_port_linux.h
        └── u8g2.h (u8g2 库)

sys_status.c
  ├── ubus_service.h
  └── 系统头文件 (stdio, ifaddrs, ...)

ubus_service.c
  ├── libubus.h
  └── libubox/blobmsg.h

gpio_button.c
  └── poll.h, fcntl.h

u8g2_port_linux.c
  └── linux/i2c-dev.h
```

## 扩展性

### 添加新页面
1. 在 `page_t` 枚举中添加新页面
2. 实现 `draw_page_xxx()` 函数
3. 在 `render_page()` 的 switch 中添加 case

### 添加新服务监控
修改 `sys_status.c` 中的 `services[]` 数组：
```c
static const service_entry_t services[] = {
    {"xray_core", "xray_core"},
    {"dropbear", "dropbear"},
    {"dockerd", "dockerd"},
    {"新服务", "服务名"},
    {NULL, NULL}
};
```

### 更换显示屏
修改 `display_init()` 中的 u8g2 setup 函数，选择对应的显示控制器。
