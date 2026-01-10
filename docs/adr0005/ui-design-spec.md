# NanoHat OLED UI 设计规范

本文档定义 ADR0005 Phase 7 的 UI 布局与交互规范。

## 1. 页面架构

### 1.1 插件式页面注册

采用注册式架构，支持轻松添加新页面：

```c
typedef enum {
    PAGE_MODE_VIEW,     /* 浏览模式 */
    PAGE_MODE_ENTER,    /* 进入模式（如服务列表选择） */
} page_mode_t;

typedef struct page {
    const char *name;           /* 页面标识符 */
    bool can_enter;             /* 是否支持进入模式 */

    /* 生命周期 */
    void (*init)(void);
    void (*destroy)(void);

    /* 渲染 - 返回标题字符串，NULL 使用默认 */
    const char *(*get_title)(const sys_status_t *status);
    void (*render)(u8g2_t *u8g2, const sys_status_t *status, page_mode_t mode);

    /* 按键事件 - 返回 true 表示已处理 */
    bool (*on_key)(uint8_t key, bool long_press, page_mode_t mode);

    /* 模式切换回调 */
    void (*on_enter)(void);
    void (*on_exit)(void);
} page_t;
```

### 1.2 页面注册示例

```c
/* 页面定义 */
extern const page_t page_home;
extern const page_t page_network;
extern const page_t page_services;

/* 页面列表 - 添加新页面只需修改此数组 */
static const page_t *pages[] = {
    &page_home,
    &page_network,
    &page_services,
};
#define PAGE_COUNT (sizeof(pages) / sizeof(pages[0]))
```

### 1.3 页面生命周期

```
                    ┌─────────────┐
                    │   init()    │ 程序启动时调用
                    └──────┬──────┘
                           │
                           ▼
        ┌─────────────────────────────────────┐
        │                                     │
        │  ┌───────────┐     ┌───────────┐   │
        │  │ PAGE_VIEW │◄───►│PAGE_ENTER │   │ 运行时循环
        │  └───────────┘     └───────────┘   │
        │        │                 │         │
        │        └────► render() ◄─┘         │
        │                                     │
        └─────────────────────────────────────┘
                           │
                           ▼
                    ┌─────────────┐
                    │  destroy()  │ 程序退出时调用
                    └─────────────┘
```

## 2. 硬件规格

### 2.1 显示屏

| 参数 | 值 |
|------|-----|
| 尺寸 | 0.96 英寸 |
| 分辨率 | 128 × 64 像素 |
| 类型 | 双色 OLED |
| 黄色区域 | Y: 0-15 (16px) |
| 蓝色区域 | Y: 16-63 (48px) |

### 2.2 按键定义

| 按键 | GPIO | 位置 | 基本语义 |
|------|------|------|----------|
| K1 | - | 左 | 向左/向上/递减 |
| K2 | - | 中 | 确认/进入/退出 |
| K3 | - | 右 | 向右/向下/递增 |

## 3. 布局规范

### 3.1 屏幕分区

```
┌─────────────────────────────────────┐ Y=0
│           标题栏 (16px)             │ 黄色区域
│  标题文字                  页码指示  │
├─────────────────────────────────────┤ Y=16
│                                     │
│           内容区 (48px)             │ 蓝色区域
│           最多 3 行内容              │
│                                     │
└─────────────────────────────────────┘ Y=64
```

### 3.2 标题栏规范

| 元素 | 位置 | 字体 | 说明 |
|------|------|------|------|
| 标题文字 | 左对齐 X=2 | 12px 粗体 | 浏览模式左对齐，进入模式居中 |
| 页码指示 | 右对齐 X=126 | 8px | 格式 "N/M"，进入模式隐藏 |
| 可进入标识 | 页码左侧 | 8px | 符号 "⏎"，仅可进入页面显示 |
| 分隔线 | Y=15 | 1px | 贯穿全宽 |

### 3.3 内容区规范

| 参数 | 值 |
|------|-----|
| 可用高度 | 48px (Y: 16-63) |
| 行高 | 16px |
| 最大行数 | 3 行 |
| 左边距 | 2px |
| 右边距 | 2px |
| 内容字体 | 12px 或 10px |

### 3.4 各页面布局

#### 3.4.1 首页 (Home)

**标题**：动态显示主机名（如 "NanoPi-R2S"）

```
┌─────────────────────────────────────┐
│ {hostname}                     1/3 │
├─────────────────────────────────────┤
│ CPU: {cpu}%          Temp: {temp}°C│  行1: CPU + 温度
│ MEM: {used}M / {total}M            │  行2: 内存
│ Up:  {uptime}                      │  行3: 运行时间
└─────────────────────────────────────┘
```

| 字段 | 格式 | 示例 |
|------|------|------|
| cpu | 整数 0-100 | 45 |
| temp | 整数 | 52 |
| used/total | 整数 MB | 256 / 512 |
| uptime | Xd Xh Xm | 3d 12h 5m |

#### 3.4.2 Network 页

**标题**：固定 "Network"

```
┌─────────────────────────────────────┐
│ Network                        2/3 │
├─────────────────────────────────────┤
│ IP: {ip_addr}                      │  行1: IP 地址
│ GW: {gateway}                      │  行2: 网关
│ ↓{rx_speed}         ↑{tx_speed}    │  行3: 实时流量
└─────────────────────────────────────┘
```

| 字段 | 格式 | 示例 |
|------|------|------|
| ip_addr | IPv4 | 192.168.1.100 |
| gateway | IPv4 | 192.168.1.1 |
| rx_speed | X.X KB/s 或 X.X MB/s | 12.5KB/s |
| tx_speed | 同上 | 3.2KB/s |

#### 3.4.3 Services 页 - 浏览模式

**标题**：固定 "Services"

```
┌─────────────────────────────────────┐
│ Services                   3/3   ⏎ │  ⏎ 表示可进入
├─────────────────────────────────────┤
│ {name1}                         {s}│  s: ▶=运行 ■=停止
│ {name2}                         {s}│  图标右对齐
│ {name3}                         {s}│
└─────────────────────────────────────┘
```

#### 3.4.4 Services 页 - 进入模式

```
┌─────────────────────────────────────┐
│            Services                │  标题居中，无页码
├─────────────────────────────────────┤
│ {name1}                         {s}│
│ ████████ {name2} ██████████████ {s}│  选中行反白
│ {name3}                         {s}│
└─────────────────────────────────────┘
```

#### 3.4.5 Services 页 - 滚动指示

当服务数量 > 3 时：

```
┌─────────────────────────────────────┐
│            Services              ▲ │  ▲ 上方有更多
├─────────────────────────────────────┤
│ {name2}                         {s}│
│ ████████ {name3} ██████████████ {s}│
│ {name4}                         {s}│
│                                  ▼ │  ▼ 下方有更多
└─────────────────────────────────────┘
```

## 4. 交互规范

### 4.1 屏幕状态机

```
┌─────────────────────────────────────────────────────┐
│                                                     │
│    SCREEN_OFF ◄──────────────────┐                  │
│        │                         │                  │
│        │ 任意键                   │ 自动超时         │
│        │ (仅唤醒,不触发动作)       │ 或 K2短按(首页)  │
│        ▼                         │                  │
│    SCREEN_ON ────────────────────┘                  │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 4.2 页面模式状态机

```
┌─────────────────────────────────────────────────────┐
│                                                     │
│    PAGE_VIEW ◄───────────────────┐                  │
│        │                         │                  │
│        │ K2长按                   │ K2长按           │
│        │ (can_enter=true)        │                  │
│        ▼                         │                  │
│    PAGE_ENTER ───────────────────┘                  │
│                                                     │
│    K2长按 + can_enter=false → 标题晃动动画          │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 4.3 按键行为矩阵

#### 4.3.1 息屏状态

| 按键 | 动作 |
|------|------|
| K1 | 唤醒屏幕（不触发其他动作） |
| K2 短按 | 唤醒屏幕 |
| K2 长按 | 唤醒屏幕 |
| K3 | 唤醒屏幕 |

#### 4.3.2 浏览模式 (PAGE_VIEW)

| 按键 | Home 页 | Network 页 | Services 页 |
|------|---------|------------|-------------|
| K1 | 上一页 (→slide) | 上一页 | 上一页 |
| K3 | 下一页 (←slide) | 下一页 | 下一页 |
| K2 短按 | 息屏 | 无动作 | 无动作 |
| K2 长按 | 标题晃动 | 标题晃动 | 进入模式 |

注：K1 切换到上一页时，页面从右侧滑入；K3 切换到下一页时，页面从左侧滑入。

#### 4.3.3 进入模式 (PAGE_ENTER) - Services 页

| 按键 | 动作 |
|------|------|
| K1 | 上移选择（循环到底部） |
| K3 | 下移选择（循环到顶部） |
| K2 短按 | 切换选中服务状态 |
| K2 长按 | 退出到浏览模式 |

#### 4.3.4 自动息屏

- **息屏超时**: 30 秒无按键操作后自动关闭屏幕
- **计时重置**: 任意按键按下时，息屏计时器重新开始
- **唤醒行为**: 息屏状态下任意按键唤醒屏幕，该按键仅用于唤醒，不触发其他功能

### 4.4 服务状态机

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   STOPPED (■) ◄──────────────────────┐                      │
│       │                              │                      │
│       │ K2切换                        │ 停止成功             │
│       ▼                              │                      │
│   STARTING (▶闪烁) ─────────────────►│                      │
│       │         成功                  │                      │
│       │                              │                      │
│       │ 失败                         │                      │
│       ▼                              │                      │
│   ERROR (?显示1s) ──► 恢复原状态      │                      │
│                       ▲              │                      │
│   STOPPING (■闪烁) ───┘              │                      │
│       ▲              失败            │                      │
│       │                              │                      │
│   RUNNING (▶) ◄──────────────────────┘                      │
│       │                                                     │
│       │ K2切换                                              │
│       ▼                                                     │
│   STOPPING (■闪烁) ─────────────────► STOPPED               │
│                      成功                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 5. 动画规范

### 5.1 动画列表

| 动画 | 触发条件 | 时长 | 描述 |
|------|----------|------|------|
| 页面滑动 | K1/K3 切换页面 | 200ms | 整页水平滑动 |
| 标题晃动 | K2长按不可进入页面 | 400ms | 标题左右晃动 3 次 |
| 进入模式 | K2长按进入 | 250ms | 标题滑至居中 |
| 退出模式 | K2长按退出 | 250ms | 标题滑回左侧 |
| 服务闪烁 | 服务启停中 | 300ms周期 | 图标闪烁 |
| 错误提示 | 服务操作失败 | 1000ms | 显示 ? 后恢复 |

### 5.2 页面滑动

```
K3 按下 (下一页)：
┌─────────┐      ┌─────────┐
│ Page A  │ ──►  │ Page B  │
└─────────┘      └─────────┘
   当前页 ← 滑出    新页 ← 滑入

方向：A 向左滑出，B 从右侧滑入
时长：200ms
曲线：ease-out
```

```
K1 按下 (上一页)：
┌─────────┐      ┌─────────┐
│ Page B  │ ◄──  │ Page A  │
└─────────┘      └─────────┘
   新页 ← 滑入    当前页 ← 滑出

方向：B 从左侧滑入，A 向右滑出
时长：200ms
曲线：ease-out
```

### 5.3 标题晃动

```
触发：K2 长按不可进入的页面
效果：标题文字水平晃动

时间轴 (400ms)：
  0ms   - 原位
  65ms  - 右移 8px
  130ms - 左移 8px
  195ms - 右移 6px
  260ms - 左移 6px
  325ms - 右移 3px
  400ms - 原位

振幅递减，模拟物理阻尼
```

### 5.4 进入/退出模式

```
进入模式 (250ms)：
  标题从左对齐 (X=2) 滑动到居中
  页码指示器淡出
  曲线：ease-in-out

退出模式 (250ms)：
  标题从居中滑动到左对齐 (X=2)
  页码指示器淡入
  曲线：ease-in-out
```

### 5.5 服务状态闪烁

```
启动中 (STARTING)：
  ▶ 图标以 300ms 周期闪烁
  300ms 显示 → 300ms 隐藏 → 循环

停止中 (STOPPING)：
  ■ 图标以 300ms 周期闪烁
  300ms 显示 → 300ms 隐藏 → 循环

错误 (ERROR)：
  显示 ? 图标 1000ms
  然后恢复到操作前的状态图标
```

## 6. 数据结构

### 6.1 页面控制器状态

```c
typedef enum {
    SCREEN_OFF,
    SCREEN_ON,
} screen_state_t;

typedef enum {
    ANIM_NONE,
    ANIM_SLIDE_LEFT,      /* 页面向左滑出 */
    ANIM_SLIDE_RIGHT,     /* 页面向右滑出 */
    ANIM_TITLE_SHAKE,     /* 标题晃动 */
    ANIM_ENTER_MODE,      /* 进入模式转场 */
    ANIM_EXIT_MODE,       /* 退出模式转场 */
} anim_type_t;

typedef struct {
    /* 屏幕状态 */
    screen_state_t screen_state;
    uint64_t last_activity_ms;      /* 用于自动息屏 */

    /* 页面状态 */
    int current_page;               /* 当前页面索引 */
    page_mode_t page_mode;          /* 浏览/进入模式 */

    /* 动画状态 */
    anim_type_t anim_type;
    uint64_t anim_start_ms;
    int anim_from_page;             /* 滑动动画：起始页 */
    int anim_to_page;               /* 滑动动画：目标页 */
} page_controller_t;
```

### 6.2 服务 UI 状态

```c
typedef enum {
    SVC_UI_STOPPED,     /* ■ 静止 */
    SVC_UI_RUNNING,     /* ▶ 静止 */
    SVC_UI_STARTING,    /* ▶ 闪烁 */
    SVC_UI_STOPPING,    /* ■ 闪烁 */
    SVC_UI_ERROR,       /* ? 显示后恢复 */
} svc_ui_state_t;

typedef struct {
    char name[SERVICE_NAME_MAX_LEN];
    svc_ui_state_t ui_state;
    svc_ui_state_t prev_state;      /* ERROR 恢复用 */
    uint64_t state_change_ms;       /* 状态变化时间 */
    uint32_t pending_request_id;    /* 等待中的请求 */
} service_ui_item_t;

typedef struct {
    service_ui_item_t items[MAX_SERVICES];
    int count;
    int selected_index;             /* 进入模式下的选中项 */
    int scroll_offset;              /* 滚动偏移 (首个可见项索引) */
} services_page_state_t;
```

### 6.3 动画辅助函数

```c
/* 缓动函数 */
float ease_out(float t);           /* t: 0.0-1.0 */
float ease_in_out(float t);

/* 动画进度计算 */
float anim_progress(uint64_t start_ms, uint64_t duration_ms);

/* 标题晃动偏移计算 */
int calc_shake_offset(float progress);  /* 返回 X 偏移像素 */
```

## 7. 扩展指南

### 7.1 添加新页面

1. 创建页面源文件 `src/pages/page_xxx.c`

2. 实现 `page_t` 接口：

```c
static const char *xxx_get_title(const sys_status_t *status) {
    return "PageTitle";
}

static void xxx_render(u8g2_t *u8g2, const sys_status_t *status, page_mode_t mode) {
    /* 渲染内容区，最多 3 行 */
}

static bool xxx_on_key(uint8_t key, bool long_press, page_mode_t mode) {
    /* 处理按键，返回 true 表示已处理 */
    return false;
}

const page_t page_xxx = {
    .name = "xxx",
    .can_enter = false,
    .get_title = xxx_get_title,
    .render = xxx_render,
    .on_key = xxx_on_key,
};
```

3. 在 `page_controller.c` 中注册：

```c
static const page_t *pages[] = {
    &page_home,
    &page_network,
    &page_services,
    &page_xxx,        /* 新增 */
};
```

### 7.2 添加可进入页面

设置 `can_enter = true` 并实现模式切换回调：

```c
const page_t page_xxx = {
    .name = "xxx",
    .can_enter = true,
    .on_enter = xxx_on_enter,
    .on_exit = xxx_on_exit,
    /* ... */
};
```

## 8. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-01-09 | 初始版本 |
