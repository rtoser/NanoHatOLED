# ADR0005 新人入门指南

本文面向刚接手项目的同学，介绍 libgpiod 基础、项目架构、测试与 mock、Linux 事件机制以及我们的设计选择。

## 1. 快速定位项目

**目录入口**
- 新架构代码：`src/`
- 旧实现备份：`src_old/`
- 新文档索引：`docs/adr0005/README.md`

**当前进度**
- Phase 1：基础设施已完成（Host 测试通过）
- Phase 2：GPIO HAL 已完成（Host + Target 基础验证通过）

**构建方式**
- 交叉编译基于 Docker 镜像 `openwrt-sdk:sunxi-cortexa53-24.10.5`
- 镜像默认不含 `cmake`，如需 CMake 需扩展镜像或自行安装

## 2. libgpiod 基础知识（v2）

**概念**
- `gpiochip`：GPIO 控制器设备（如 `/dev/gpiochip1`）
- `line offset`：芯片内的引脚编号（如 `0/2/3`）
- **边沿事件**：rising / falling，按键会产生两个边沿

**常用工具**
- `gpiomon`：监听边沿事件  
  例：`gpiomon -c gpiochip1 -e both --idle-timeout 10s 0 2 3`  
  注意：选项在前，line 在后
- `gpioget`：读取电平  
  例：`gpioget -c gpiochip1 0 2 3`

**v2 API 变化要点**
- `gpiod_line_request_*` 取代旧的 `gpiod_line_*`
- 通过 `gpiod_line_request_get_fd()` 获取可 poll 的 fd
- `gpiod_edge_event_buffer` 支持批量读取事件

## 3. 我们的 GPIO HAL 设计

**编译期配置**
- `GPIOCHIP_PATH`：默认 `/dev/gpiochip1`
- `BTN_OFFSETS`：默认 `0,2,3`

**按键判定**
- 自动探测按下电平（兼容高/低电平有效）
- 短按：按下后释放触发
- 长按：松开时按住时长超过 600ms 触发

**去抖策略**
- 优先硬件去抖（驱动支持时）
- 若驱动不支持（`ENOTSUP/EINVAL`），回退软件去抖（30ms）

**事件等待**
- 统一使用 `get_fd()+poll()` 等待事件
- 读到边沿后通过 `read_edge_events()` 批量处理
- 若只收到“按下”边沿会继续等待“释放”边沿以生成短按事件

## 4. 系统架构概览（ADR0005）

**三线程结构**
- 主线程：`poll()` GPIO/timerfd/eventfd → 产生事件
- UI 线程：消费事件、更新状态、渲染界面
- ubus 线程：`uloop_run()` 处理 ubus 调用与对象注册

**队列策略**
- 关键事件不轻易丢弃（按键/退出）
- tick 事件可合并（仅保留最新）
- ubus 任务支持超时与重复意图合并

**ulooop 唤醒**
- UI 入队任务后写 `eventfd`
- ubus 线程通过 `uloop_fd` 被唤醒处理任务

## 5. 测试架构与 Mock

**Host / Target 分层**
- Host：Mock 硬件，快速回归
- Target：真实硬件，验证 GPIO/ubus 交互

**Mock 组件**
- `time_mock`：控制时间流逝
- `gpio_hal_mock`：模拟边沿事件、可 poll 唤醒

**Host 测试入口**
```bash
cd tests
make test-host
```

**Target 测试入口（人工按键）**
```bash
cd tests
make test-target TARGET=192.168.33.254 TARGET_USER=root \
  DOCKER_IMAGE=openwrt-sdk:sunxi-cortexa53-24.10.5 \
  GPIOCHIP_PATH=/dev/gpiochip1 BTN_OFFSETS=0,2,3
```

## 6. Linux 事件机制速览

**可用机制**
- `poll`/`epoll`：等待多个 fd
- `timerfd`：稳定定时
- `eventfd`：线程间唤醒
- `signalfd`：信号转 fd（可选）
- `gpiod` fd：GPIO 边沿事件
- `uloop`：OpenWrt 事件循环（ubus 基于它）

**我们的选择**
- 主线程统一 `poll()`：GPIO + timerfd + eventfd
- ubus 线程使用 `uloop` + `eventfd` 接入任务队列

## 7. 设计取舍与原因

- **libgpiod 替代 sysfs**：sysfs 已废弃且易竞态；libgpiod 提供内核时间戳与事件缓冲
- **三线程**：把 UI 渲染、GPIO 监听、ubus 调用彻底解耦
- **poll 替代 wait_edge_events**：实测某些环境下 wait 接口不稳定，poll 更可控
- **编译期映射**：避免运行时配置复杂化，硬件固定时更稳

## 8. 常见问题

**Q: gpiomon 报 “cannot find line -e”?**  
A: 选项必须在前，line 参数放最后。

**Q: test_gpio_hw 一直 FAIL?**  
A: 确认按键在提示后按下；用 `gpiomon` 验证映射；检查 `GPIOCHIP_PATH/BTN_OFFSETS`。
