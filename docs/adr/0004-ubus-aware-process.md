# ADR 0004: 进程注册为 ubus 对象并支持远程重启

*   **状态**: 已合并 (Merged into ADR 0005)
*   **日期**: 2026-01-05
*   **作者**: Libo

## 1. 背景 (Context)

当前 `nanohat-oled` 作为 procd 服务运行，但进程本身没有注册 ubus 对象：
- `ubus list` 中看不到该进程。
- 无法通过 `ubus call` 直接触发重启，只能依赖 `service`/`/etc/init.d`。

需求：让进程出现在 `ubus list` 中，并提供 ubus 方法以便远程重启。

## 2. 决策 (Decision)

在进程内注册**自有 ubus 对象**，提供最小可用接口：

1. **对象名称**
   - `nanohat-oled`（固定名字，便于脚本调用）。

2. **方法**
   - `restart`：返回确认后触发进程自退出，由 procd 负责拉起。
   - `status`（可选，但推荐）：返回版本号、运行时长、当前页面/屏幕开关状态等轻量信息。

3. **事件循环**
   - 采用独立 ubus 线程，线程内部 `uloop_run()`，避免侵入主循环。
   - 主线程通过原子/互斥标志检测 `restart` 请求并优雅退出。

4. **重启方式**
   - **首选**：进程自退出 → procd `respawn` 拉起。
   - 不在 ubus 回调中直接 `exit()`，避免回复尚未发出即终止。

## 3. 备选方案 (Alternatives)

1. **仅依赖 rc/service ubus 对象**
   - `ubus call rc init {"name":"nanohat-oled","action":"restart"}` 可行，
   - 但无法让进程自身出现在 `ubus list`，不满足可感知需求。

2. **主线程整合 uloop**
   - 将主循环完全迁移到 uloop 定时器模型，
   - 可减少线程，但改动范围更大。

3. **在 ubus 回调中直接重启**
   - 简单但风险高，RPC 可能未完成即断开。

## 4. 后果 (Consequences)

### 优势 (Pros)
1. 进程可被 `ubus list` 发现，支持统一的系统级可观测性。
2. 通过 `ubus call nanohat-oled restart` 实现标准化重启入口。
3. ubus 线程隔离，主循环改动小，风险可控。

### 劣势 (Cons)
1. 引入额外线程与同步逻辑。
2. 需要维护 ubus 生命周期（连接、注册、清理）。
3. 若 procd 未启用 respawn，重启会变成“退出不再拉起”。

## 5. 实施要点 (Implementation Notes)

- ubus 线程内调用：
  - `uloop_init()` → `ubus_connect()` → `ubus_add_uloop()` → `ubus_add_object()`。
- `restart` 方法先 `ubus_send_reply()`，再设置 `restart_requested` 标志。
- 主线程检测 `restart_requested` 后走正常退出路径（如 `running = 0` / 触发 SIGTERM）。
- 退出时依次执行 `ubus_remove_object()`、`ubus_free()`、`uloop_done()`。
- `nanohat-oled.init` 需保证 `procd_set_param respawn` 已启用。

---
本 ADR 用于定义“进程可感知 + ubus 远程重启”的最小实现方案。若后续迁移至统一 uloop 主循环，应更新本决策的线程模型部分。
