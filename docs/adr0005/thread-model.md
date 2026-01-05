# 线程模型细化

本文件用于说明 ADR0005 的三线程协作方式，重点是线程职责、唤醒机制与队列策略。

## 线程职责

- 主线程：`poll()` 监听 GPIO/timerfd/eventfd，生成事件。
- UI 线程：消费事件、更新状态、渲染页面。
- ubus 线程：`uloop_run()` 执行 ubus 调用，处理远程控制。

## 事件流转

1. 主线程将按键事件与 tick 事件入队并唤醒 UI 线程。
2. UI 线程消费事件并更新页面；需要调用 ubus 时将任务入队。
3. ubus 线程通过 `task_eventfd` 被 `uloop` 唤醒，执行任务并返回结果。
4. UI 线程被结果队列唤醒后更新状态。

## uloop 与任务队列集成

- ubus 线程注册 `task_eventfd` 到 `uloop_fd`。
- UI 线程入队任务后 `write(task_eventfd)` 触发回调。
- 回调中 drain 队列并执行任务，结果入队后 `pthread_cond_signal()` 唤醒 UI。

## 队列策略

- 关键事件（按键、退出）不轻易丢弃：优先挤出 tick，短阻塞后仍满则降级并记录统计。
- tick 事件可合并：同类型仅保留最新一个。
- 任务队列支持合并重复意图、超时丢弃；结果队列丢弃过期结果。

## 退出流程

- SIGTERM/SIGINT 触发主线程写 `eventfd`。
- 主线程入队 `EVT_SHUTDOWN` 并广播唤醒。
- ubus 线程 `uloop_end()`，所有线程清理资源并退出。
