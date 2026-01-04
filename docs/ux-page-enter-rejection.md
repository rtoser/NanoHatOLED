# 页面进入与拒绝动画设计

本文档描述 NanoHat OLED 长按 K2 进入下级页面时的动画交互设计。

## 背景

当前设计中，长按 K2 在支持菜单的页面（如 Services）会进入菜单模式，在不支持的页面（如 Network）会触发拒绝反馈。

**原有问题**：
1. 全屏 shake 动画看起来像设备受到电磁干扰，用户困惑且不舒适
2. 可进入的页面缺乏明确的视觉提示（affordance）

## 设计目标

1. **语义清晰**：进入/拒绝的动画要有明确的视觉含义
2. **局部反馈**：只动 Title 区域，其余内容保持稳定
3. **风格统一**：进入和拒绝使用同一套位移动画体系
4. **性能友好**：开销小，只需重绘 header 区域
5. **可发现性**：可进入的页面要有明确的视觉提示

## 进入指示器

在支持菜单的页面（如 Services）右下角显示小箭头，提示用户可以长按 K2 进入：

```
[Services          1/4]
---------------------------
 xray_core: ON
 dropbear: ON
 uhttpd: ON
                      ▸   <- 右下角进入指示
```

**设计细节**：
- 符号：`>` (ASCII) 或 `▸` (U+25B8，需字体支持)
- 位置：屏幕右下角 (x=122, y=62)
- 字体：小号字体 (5x7)
- 仅在 `page_supports_menu()` 为 true 且非菜单模式时显示

**实现**：

```c
static void draw_entry_indicator(void) {
    if (page_supports_menu(current_page) && !menu_active) {
        u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(&u8g2, 122, 62, ">");
    }
}
```

## 交互设计

### 场景 1：成功进入菜单

**触发条件**：在 Services 页面长按 K2

**动画效果**：Title "Services" 从左对齐平滑滑动到居中

```
初始状态:  [Services          1/4]
          -------------------------

动画过程:  [  Services        1/4]  <- Title 右移
          [    Services      1/4]
          [     Services         ]  <- 居中，隐藏页码

最终状态:  [     Services         ]  <- 菜单模式，Title 居中
          -------------------------
```

**动画参数**：
- 帧数：6 帧
- 时长：~100ms（16ms/帧）
- 曲线：ease-out（先快后慢）

### 场景 2：拒绝进入

**触发条件**：在 Network/Status/System 页面长按 K2

**动画效果**：Title 向右尝试移动，被弹回原位（衰减振荡）

```
初始状态:  [Network           2/4]

动画过程:
  帧 0:    [  Network         2/4]  <- 右移 +12px（尝试进入）
  帧 1:    [Network           2/4]  <- 弹回 -6px（过冲）
  帧 2:    [ Network          2/4]  <- 再右 +3px
  帧 3:    [Network           2/4]  <- 收敛 -1px
  帧 4:    [Network           2/4]  <- 回位 0px

最终状态:  [Network           2/4]  <- 回到左对齐
```

**动画参数**：
- 帧数：5 帧
- 时长：~80ms
- 曲线：衰减弹簧（damped spring）
- 偏移序列：`[+12, -6, +3, -1, 0]`

## 视觉语义

| 动作 | 动画 | 含义 |
|------|------|------|
| 成功进入 | Title 滑向中心 | "进入了下级状态" |
| 拒绝进入 | Title 右冲后弹回 | "尝试进入但被拒绝" |

**为什么右移表示"进入"**：
- 右 = 前进/深入（符合 LTR 阅读习惯）
- 弹回 = 碰壁/不可通行
- 最终回原位 = 状态未改变

## 实现方案

### 关于 u8g2 动画能力

u8g2 是底层绘图库，**没有内置动画支持**。它只提供绘图原语：
- 画点、线、矩形、圆
- 画文字、位图
- 缓冲区管理

所有动画效果都需要手动实现：每帧在不同位置重绘。这也意味着衰减弹簧动画实现非常简单——只需要一个偏移量查表。

### 数据结构

```c
typedef enum {
    TITLE_ANIM_NONE = 0,
    TITLE_ANIM_ENTER,   // 进入动画（左→居中）
    TITLE_ANIM_REJECT   // 拒绝动画（衰减弹簧）
} title_anim_type_t;

static struct {
    title_anim_type_t type;
    int frame;
    int max_frames;
} title_anim;
```

### 拒绝动画偏移表

```c
static const int reject_offsets[] = {12, -6, 3, -1, 0};
#define REJECT_FRAMES (sizeof(reject_offsets) / sizeof(reject_offsets[0]))
```

### 进入动画计算

```c
// ease-out: 先快后慢
static int calc_enter_offset(int frame, int max_frames, int target_x) {
    float t = (float)frame / max_frames;
    float ease = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out quadratic
    return (int)(ease * target_x);
}
```

### draw_header 修改

```c
static void draw_header(const char *title, int centered, int show_indicator) {
    int base_x = 0;

    if (centered) {
        int w = u8g2_GetStrWidth(&u8g2, title);
        base_x = (128 - w) / 2;
    }

    // 应用动画偏移
    int anim_offset = get_title_anim_offset();
    base_x += anim_offset;

    draw_str(base_x, 11, title);
    // ...
}
```

### 触发逻辑

```c
case BTN_K2_LONG_PRESS:
    if (page_supports_menu(current_page)) {
        start_title_anim(TITLE_ANIM_ENTER);
        menu_enter();
    } else {
        start_title_anim(TITLE_ANIM_REJECT);
    }
    break;
```

## 与现有架构的集成

### 动画状态检查

```c
static inline int is_animating(void) {
    return shake_ticks > 0 || title_anim.type != TITLE_ANIM_NONE;
}
```

### 帧推进

在主循环渲染时推进动画帧：

```c
if (title_anim.type != TITLE_ANIM_NONE) {
    title_anim.frame++;
    if (title_anim.frame >= title_anim.max_frames) {
        title_anim.type = TITLE_ANIM_NONE;
    }
    mark_dirty();
}
```

## 未来扩展

1. **退出动画**：菜单退出时 Title 从居中滑回左对齐
2. **页面切换**：可复用 Title 动画系统做页面标题切换效果
3. **自定义缓动**：支持不同的 easing 函数

## 参考

- iOS 密码错误 shake 动画
- macOS 登录框拒绝动画
- Material Design motion principles
