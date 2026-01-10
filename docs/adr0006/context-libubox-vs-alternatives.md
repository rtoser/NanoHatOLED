# Context: libubox 技术定位与选型分析

## 1. 概述

在 OpenWrt 生态中，`libubox` 是核心的基础设施库。本文件旨在记录 `libubox`（主要包含 `uloop` 和 `blobmsg`）的技术定位、与常见替代方案的对比，以及其在资源受限环境下的设计动机与收益。此信息将作为项目架构设计决策（尤其是事件循环与 IPC 选型）的重要输入。

## 2. 技术定位

`libubox` 并非重新发明底层的 I/O 多路复用机制，而是**基于 Linux 标准机制（epoll）的极简封装**。它在 OpenWrt 生态中的地位，类似于 GLib 之于 GNOME 或 Qt 之于 KDE，是系统级的基石库。

### 2.1 Event Loop (`uloop`)
*   **机制**：完全基于 Linux 的 `epoll` (Edge Triggered / Level Triggered)。
*   **本质**：它是 `epoll` 的一层极其薄的 Wrapper。
*   **代码量**：核心仅几百行代码。
*   **特性**：仅支持 Linux，移除了所有跨平台兼容层，专注于极致轻量。

### 2.2 Data Serialization (`blobmsg`)
*   **机制**：一种自定义的二进制 TLV (Type-Length-Value) 格式。
*   **定位**：JSON 的二进制同构体（Schemaless）。
*   **特性**：支持与 JSON 的双向转换，但传输时为紧凑的二进制，支持**零拷贝**读取。

## 3. 方案对比

| 特性 | libubox (uloop + blobmsg) | libevent / libuv | GLib (GMainLoop) | Raw epoll + JSON |
| :--- | :--- | :--- | :--- | :--- |
| **底层机制** | `epoll` | `epoll` / `kqueue` / `IOCP` | `poll` / `epoll` | `epoll` |
| **二进制大小** | **极小** (OpenWrt 上约 30-40KB) | 中等 (~200KB - 500KB) | 巨大 (MB 级别) | 极小 (libc) |
| **依赖** | 仅 libc | libc, openssl(可选) | 复杂的依赖树 | libc, json-c |
| **跨平台** | **否** (仅 Linux/OpenWrt) | 是 (Win/Mac/Linux) | 是 | 仅 Linux |
| **功能集** | 仅最基础 I/O, Timer, Process | DNS, Async FS, SSL | 全功能 (ORM, Objects...) | 取决于实现 |
| **IPC 亲和性** | **原生支持 ubus** | 无 | DBus | 需自行实现 |
| **开发效率** | 高 (但在 OpenWrt 外很少用) | 高 | 高 | 低 (样板代码多) |

## 4. 动机与收益 (Why "Re-invent the Wheel"?)

OpenWrt 之所以维护这样一套独立的库，主要基于以下嵌入式环境的硬约束：

### 4.1 极致的体积控制 (Flash Space)
*   **约束**：目标设备 Flash 往往仅 4MB - 16MB。
*   **收益**：避免每个服务静态链接庞大的通用库（如 `libevent`）。`libubox` 作为共享库仅几十 KB，被系统内 100+ 个进程复用，显著节省 Flash 空间。

### 4.2 极致的内存效率 (RAM)
*   **约束**：RAM 往往仅 32MB - 128MB。
*   **收益**：
    *   `blobmsg` 避免了文本 JSON 解析带来的大量 `malloc/free` 开销和内存碎片。
    *   `uloop` 结构体极小，无复杂对象模型，降低每个进程的内存占用。

### 4.3 统一的生态接口
*   **约束**：系统总线 `ubus` 需要高效的数据传输。
*   **收益**：
    *   `ubus` 客户端库强依赖 `blobmsg` 进行数据封包。
    *   `ubus` 强依赖 `uloop` 处理消息回调。
    *   使用 `libubox` 可以获得开箱即用的 `ubus` 集成，无需手动编写胶水代码将 `ubus` fd 挂载到其他事件循环。

### 4.4 依赖管理
*   **收益**：`libubox` 仅依赖 libc，编译极快，无循环依赖，极大简化了构建系统（Buildroot/OpenWrt SDK）的维护成本。
*   **补充**：在 OpenWrt 上 `libubox` 已是基础依赖，使用它不会引入额外系统依赖负担。

## 5. 对本项目的启示

### 5.1 事件循环选型
*   **ADR0006 目标**：从一开始采用 `uloop` 作为唯一事件循环。
*   **评估**：GPIO/Timer 监听可由 `uloop_fd` 与 `uloop_timeout` 覆盖，减少手写 poll 与跨线程通知。
*   **结论**：ADR0006 选择 `uloop`，并将 `ubus` 异步回调直接挂接到同一事件循环。

### 5.2 数据传输选型
*   **现状**：当前项目与系统交互主要通过 `ubus`。
*   **建议**：应充分利用 `blobmsg` 接口构建请求，避免手动拼接 JSON 字符串，以提升性能并减少安全风险。

## 6. ADR0006 决策结论

*   **事件循环**：采用单线程 `uloop`，不再引入自研的事件/任务/结果队列。
*   **线程模型**：取消 UI/ubus 线程拆分，所有事件源在同一 `uloop` 处理。
*   **变化范围**：删除 ADR0005 的 push-tick 链路与跨线程队列，保留 HAL 与驱动可扩展性。
