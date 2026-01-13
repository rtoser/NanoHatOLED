# ADR0006 Architecture

> **当前主线架构文档** - 本文档描述 ADR0006 单线程 uloop 架构的完整设计。

## Goal

Adopt a single-threaded event loop based on libubox/uloop to reduce complexity,
remove custom queues, and align with OpenWrt conventions.

## 架构图

```
┌────────────────────────────────────────────────────────────────────────┐
│                              main.c                                     │
│                       (uloop 事件循环入口)                               │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌────────────────────────────────────────────────────────────────┐    │
│  │                      ui_controller.c                            │    │
│  │          (UI 总控：状态管理、按键分发、渲染调度)                   │    │
│  └───────────┬─────────────────────┬──────────────────────────────┘    │
│              │                     │                                    │
│  ┌───────────▼───────────┐  ┌──────▼──────────────────────────────┐    │
│  │   page_controller.c   │  │          sys_status.c                │    │
│  │ (页面状态机/动画/切换) │  │   (系统状态采集 + 异步服务查询)       │    │
│  └───────────┬───────────┘  └──────────────────┬──────────────────┘    │
│              │                                  │                       │
│  ┌───────────▼───────────┐                     │                       │
│  │     pages/*.c         │                     │                       │
│  │  (各页面渲染/交互)     │                     │                       │
│  └───────────────────────┘                     │                       │
│                                                 │                       │
├─────────────────────────────────────────────────┼───────────────────────┤
│                           HAL 抽象层            │                       │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────▼────────┐              │
│  │ display_hal  │  │   gpio_hal   │  │     ubus_hal     │              │
│  │ (显示驱动)    │  │  (按键输入)   │  │ (ubus 异步查询)   │              │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘              │
└─────────┼─────────────────┼───────────────────┼─────────────────────────┘
          │                 │                   │
          ▼                 ▼                   ▼
    ┌──────────┐     ┌───────────┐      ┌─────────────┐
    │ I2C/SPI  │     │ libgpiod  │      │   rpcd/rc   │
    │ SSD1306  │     │  (GPIO)   │      │   (ubus)    │
    └──────────┘     └───────────┘      └─────────────┘
```

## src/ 目录结构

```
src/
├── main.c                    # 入口：uloop 事件循环、信号处理
├── ui_controller.c/.h        # UI 总控：整合 page_ctrl + sys_status
├── page_controller.c/.h      # 页面状态机：切换、动画、Enter 模式
├── page.h                    # 页面接口定义（插件式架构）
├── sys_status.c/.h           # 系统状态：/proc 读取 + 异步服务查询
├── service_config.c/.h       # 服务配置：编译期 MONITORED_SERVICES 解析
├── anim.c/.h                 # 动画工具：缓动函数、滑动/抖动计算
├── ui_draw.c/.h              # 绘制辅助：带符号坐标的 u8g2 封装
├── fonts.c/.h                # 字体定义
├── u8g2_api.h                # u8g2 类型前向声明
│
├── hal/                      # 硬件抽象层
│   ├── display_hal.h         # 显示 HAL 接口
│   ├── display_hal_ssd1306.c # SSD1306 I2C 实现
│   ├── display_hal_null.c    # 空实现（测试用）
│   ├── gpio_hal.h            # GPIO HAL 接口
│   ├── gpio_hal_libgpiod.c   # libgpiod 实现（生产）
│   ├── gpio_hal_mock.c       # Mock 实现（测试用）
│   ├── ubus_hal.h            # ubus HAL 接口
│   ├── ubus_hal_real.c       # libubus 异步实现（生产）
│   ├── ubus_hal_mock.c       # Mock 实现（测试用）
│   ├── time_hal.h/.c         # 时间获取
│   └── u8g2_stub.c/.h        # u8g2 类型 stub（无真实硬件时）
│
└── pages/                    # 页面实现
    ├── pages.h               # 页面注册表
    ├── page_home.c           # 首页：主机名、CPU、内存、运行时间
    ├── page_gateway.c        # 网关页：网关 IP、上下行流量
    ├── page_network.c        # 网络页：本机 IP、实时网速
    ├── page_services.c/.h    # 服务页：服务列表、启停控制
    └── page_settings.c/.h    # 设置页：亮度调节、自动息屏
```

## 模块职责

### 核心模块

| 文件 | 职责 |
|------|------|
| `main.c` | uloop 事件循环入口；注册 GPIO fd、定时器、信号处理；调度 UI 刷新 |
| `ui_controller.c` | UI 总控；整合 page_controller 和 sys_status；处理按键→服务控制请求 |
| `page_controller.c` | 页面状态机；管理 VIEW/ENTER 模式切换；驱动翻页动画；自动息屏计时 |
| `sys_status.c` | 同步读取 /proc 获取 CPU/内存/网络；通过 ubus_hal 发起异步服务查询 |
| `service_config.c` | 解析编译期 `MONITORED_SERVICES` 宏为服务列表 |
| `anim.c` | 缓动函数（ease_out_quad）、滑动偏移、抖动计算 |
| `ui_draw.c` | 封装 u8g2 绘制，支持负坐标（动画滑出屏幕） |

### 页面模块 (pages/)

| 文件 | 显示内容 | Enter 模式 |
|------|----------|-----------|
| `page_home.c` | 主机名、CPU%、温度、内存%、Uptime | ✗ |
| `page_gateway.c` | 网关 IP、总上传/下载流量 | ✗ |
| `page_network.c` | 本机 IP、实时 ↑↓ 速率 | ✗ |
| `page_services.c` | 服务列表（▶/■ 状态）、启停控制对话框 | ✓ |
| `page_settings.c` | 亮度 1-10、自动息屏开关 | ✓ |

### HAL 层 (hal/)

| 接口 | 生产实现 | Mock 实现 | 说明 |
|------|----------|-----------|------|
| `display_hal.h` | `display_hal_ssd1306.c` | `display_hal_null.c` | u8g2 + I2C 显示 |
| `gpio_hal.h` | `gpio_hal_libgpiod.c` | `gpio_hal_mock.c` | 按键事件（uloop fd 集成） |
| `ubus_hal.h` | `ubus_hal_real.c` | `ubus_hal_mock.c` | 异步 ubus 服务查询/控制 |
| `time_hal.h` | `time_hal_real.c` | - | CLOCK_MONOTONIC 时间 |

## 页面插件架构

```c
typedef struct page {
    const char *name;
    bool can_enter;             // 支持 K2 长按进入交互模式
    void (*init)(void);
    void (*render)(u8g2_t*, const sys_status_t*, page_mode_t, uint64_t, int);
    bool (*on_key)(uint8_t key, bool long_press, page_mode_t mode);
    void (*on_enter)(void);
    void (*on_exit)(void);
} page_t;
```

添加新页面：实现 `page_t` 接口，在 `pages/pages.h` 中注册即可。

## 动画系统

| 动画类型 | 时长 | 用途 |
|----------|------|------|
| `ANIM_SLIDE_LEFT/RIGHT` | 400ms | 翻页过渡 |
| `ANIM_TITLE_SHAKE` | 400ms | 不可进入页面的反馈 |
| `ANIM_ENTER/EXIT_MODE` | 250ms | 进入/退出交互模式 |
| `ANIM_BLINK` | 300ms | 服务状态切换中闪烁 |

## Core Loop

```
uloop_init();

// GPIO events
uloop_fd_add(&gpio_fd, handle_button);

// UI refresh timer (dynamic interval)
uloop_timeout_set(&ui_timer, next_refresh_ms);

// ubus async integration
ubus_add_uloop(ubus_ctx);

uloop_run();
```

## Event Sources

- GPIO: edge events from libgpiod or alternative HAL implementation.
- Timers: `uloop_timeout` drives UI refresh and animation cadence.
- ubus: async invoke + callback integrated into the same loop.

## Rendering Strategy

UI refresh runs on timer:

- Animating: 50 ms refresh
- Screen on, static: 1000 ms refresh
- Screen off: timer disabled (sleep until input)

This removes the ADR0005 push-tick chain and keeps timing local to UI state.

## HAL Retained

Keep the hardware abstraction layer to support:

- Alternative GPIO backends (libgpiod or others)
- Multiple display drivers (SSD1306, future variants)
- ubus implementation split for target vs host tests

### Display HAL Extended

```c
typedef struct {
    int (*init)(void);
    void (*cleanup)(void);
    u8g2_t *(*get_u8g2)(void);
    void (*set_power)(bool on);
    void (*send_buffer)(void);
    void (*clear_buffer)(void);
    void (*set_contrast)(uint8_t level);  /* 1-10 brightness */
} display_hal_ops_t;
```

`set_contrast()` maps 1-10 to hardware contrast range (0-255 for SSD1306).

## Differences vs ADR0005

- No event_queue/task_queue/result_queue
- No UI thread or ubus thread
- Single-threaded uloop with async ubus
- Simpler tick model via `uloop_timeout`

## Service Control Architecture

### Design Principle

Pages produce **intent**, not state mutations. State is owned by `sys_status`.

### Control Flow

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  page_services  │────►│  ui_controller   │────►│   sys_status    │
│                 │     │                  │     │                 │
│ - on_key()      │     │ - handle_button()│     │ - control_      │
│ - sets pending  │     │ - take_request() │     │   service()     │
│   control intent│     │ - delegates to   │     │ - ubus_hal      │
└─────────────────┘     │   sys_status     │     │   async invoke  │
        ▲               └──────────────────┘     └────────┬────────┘
        │                                                  │
        │              ┌──────────────────┐                │
        └──────────────│  control callback │◄──────────────┘
                       │  notify_result()  │
                       └──────────────────┘
```

### Key Points

- Page **never** writes `status->services[]` directly
- Control success updates `running` in `sys_status` callback (optimistic update)
- UI state (STARTING/STOPPING blink) auto-clears when actual state matches expectation
- Control failure triggers `SVC_UI_ERROR` via `notify_result()`
- Force query refresh after control to ensure eventual consistency

## Error Handling Strategy

- uloop 回调返回错误时记录日志并降级（例如保留上次渲染/状态，不阻塞主循环）。
- ubus 异步调用失败时回填失败状态，并在 UI 层显示错误态后自动恢复至上一次稳定状态。

## 相关文档

| 文档 | 说明 |
|------|------|
| [ui-design-spec.md](ui-design-spec.md) | UI 设计规范：页面布局、交互规范、动画、数据结构 |
| [implementation-plan.md](implementation-plan.md) | 实现计划：分阶段开发任务 |
| [../architecture.md](../architecture.md) | 项目架构入口（含旧架构归档） |
