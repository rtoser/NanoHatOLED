# 测试说明

本目录为 ADR0005 归档测试用例，新重构主线请参考 `docs/adr0006/` 与 `src/`。

本目录包含轻量级 C 测试用例，使用项目自带的 Makefile 构建，**不依赖外部测试框架**。每个测试程序成功返回 0，并输出 `ALL TESTS PASSED`。

> 更完整的测试策略与阶段性目标见：`../docs/adr0005/testing-architecture.md`

## 运行方式

**Host（基于 Mock）**：
```
cd src_adr0005/tests
make test-host
```

**Target（交叉编译 + 设备运行）**：
```
cd src_adr0005/tests
make test-target
```

如果目标板上的按键连接到 `/dev/gpiochip0`（可通过 `gpiomon -l` 查询），可以覆盖参数：
```
make test-target GPIOCHIP_PATH=/dev/gpiochip0 BTN_OFFSETS=0,2,3
```

推荐在执行前先用 `gpiomon -c <gpiochip> 0 2 3` 验证按键输出，确保 `GPIOCHIP_PATH` 匹配。

`make test-target`  将依次执行 `test_gpio_hw` 与交互式 `test_dual_thread`，不用额外手工调用 `make test-target-dual`。

可覆盖 Target 参数：
```
make test-target TARGET=192.168.33.254 GPIOCHIP_PATH=/dev/gpiochip1 BTN_OFFSETS=0,2,3
```

开启调试日志（默认关闭）：
```
make test-target-dual DEBUG=1
```

需要更详细日志时：
```
make test-target-dual DEBUG=1 VERBOSE=1
```

测试构建模式（默认 `BUILD=default`，即 `-O2`，不包含 `-g/-DNDEBUG`）：
```
make test-host BUILD=debug
make test-host BUILD=release
```
## 用例清单与保障范围

- `test_ring_queue`
  - ring_queue 初始化 / 入队 / 出队
  - 覆盖 / 拒绝 / 合并策略
  - 合并函数行为

- `test_gpio_button`
  - K1/K2/K3 短按
  - 长按（松开时判定）
  - 去抖 fallback（软去抖路径）
  - 超时返回
  - `get_fd()` poll 唤醒一致性
  - cleanup 后 reinit

- `test_event_queue`
  - tick 合并
  - 队列满时关键事件替换 tick
  - wait 超时与 close 唤醒

- `test_thread_safety`
  - event_queue 并发 push/pop 压测

- `test_ui_controller`
  - UI 事件处理与显示渲染基本行为（基于 display_mock）

- `test_ui_thread_default`
  - ui_thread 默认 handler 使用 ui_controller 的集成路径

- `test_event_flow`（仅 Linux）
  - event_loop + ui_thread 基础链路
  - tick 启用与 shutdown 投递

- `test_gpio_hw`（仅 Target）
  - 基于 libgpiod 的硬件按键验证
  - 设备侧人工按键确认

- `test_dual_thread`（仅 Target）
  - 双线程事件循环联调（event_loop + ui_thread + gpio_hal）
  - 按键输入、自动息屏、唤醒流程验证
  - 事件循环退出与资源清理
  - 通过测试事件队列（tick/主线程）与 UI 线程默认 handler 的协作，确认 `event_loop_request_tick`、自动息屏状态和 `ui_controller` 渲染一致

## 注意事项

- `test_event_flow` 依赖 eventfd/timerfd，非 Linux 环境会自动跳过。
- Target 测试前请停止 `nanohat-oled` 服务，避免 GPIO 被占用。
- `test_dual_thread` 为交互式用例，请按提示完成按键、等待息屏与唤醒步骤。
