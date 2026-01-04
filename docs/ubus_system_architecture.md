# OpenWrt Ubus 系统架构与开发指南

本文档系统整理了关于 OpenWrt 核心组件 `ubus` (OpenWrt Micro Bus) 的架构原理、应用场景及开发实践，旨在帮助开发者理解 OpenWrt 的进程间通信机制及前后端开发模式。

## 1. 什么是 Ubus？

`ubus` 是 OpenWrt 专门为嵌入式环境设计的**轻量级进程间通信（IPC）总线**。

*   **核心角色**：它类似于标准 Linux 中的 `D-Bus`，但在设计上极其精简，专为低内存、低 CPU 资源的路由器设备优化。
*   **主要功能**：
    *   **RPC (远程过程调用)**：允许一个进程调用另一个进程的函数（如 Web 界面调用 `network` 服务获取 IP）。
    *   **事件广播**：允许服务向总线发送通知（如“有新设备连接 WiFi”），其他订阅者可实时收到。
    *   **JSON 友好**：底层数据结构 (`blobmsg`) 设计上可以与 JSON 无损互转，天然适配 Web 开发。

---

## 2. 核心架构与组件

### 2.1 架构图解
```mermaid
graph TD
    Web_Browser[Web 前端 (JS)] -->|HTTP/JSON-RPC| uhttpd[uhttpd (Web Server)]
    uhttpd -->|uhttpd-mod-ubus| ubusd[ubusd (总线守护进程)]
    
    CLI[命令行 ubus call] -->|libubus| ubusd
    
    subgraph System_Services [系统服务]
        network[netifd (网络配置)] <-->|注册| ubusd
        system[procd (系统进程)] <-->|注册| ubusd
        wireless[hostapd (无线)] <-->|注册| ubusd
        dns[dnsmasq (DNS/DHCP)] <-->|注册| ubusd
        log[logd (日志)] <-->|注册| ubusd
    end
    
    Hotplug[按键/热插拔脚本] -->|ubus send| ubusd
```

### 2.2 为什么有的服务在 ubus list 里？

运行 `ubus list` 可以看到当前注册在总线上的对象。

*   **存在的服务 (如 `dnsmasq`, `network`)**：
    *   这些服务**主动链接**了 `libubus` 并注册了对象。
    *   它们支持**运行时动态交互**。例如 `dnsmasq` 允许外部通过 ubus 查询当前的 DHCP 租约列表，或者动态更新 DNS 记录，而无需重启进程。
*   **不存在的服务 (如 `uhttpd`, `dropbear`)**：
    *   这些通常是“静态”守护进程。它们启动后默默工作，不提供供外部调用的 API 接口。

---

## 3. 开发实战场景

### 3.1 场景一：GPIO 按键事件监听
**目标**：按下路由器上的物理按钮，通知应用程序执行操作（如点亮屏幕、切换显示）。

**实现路径**：不需要编写底层驱动，利用 OpenWrt 现有的 Hotplug 机制。

1.  **DTS 配置**：确保内核加载 `gpio-keys` 驱动，并定义了 `label`。
    ```dts
    gpio-keys {
        button@0 {
            label = "k1";  /* 关键标识符 */
            linux,code = <BTN_1>;
            gpios = <&gpio0 RK_PA0 GPIO_ACTIVE_LOW>;
        };
    };
    ```
2.  **Hotplug 脚本**：创建 `/etc/rc.button/k1` (文件名需匹配 DTS label 的小写)。
    ```bash
    #!/bin/sh
    # $ACTION 是 "pressed" 或 "released"
    if [ "$ACTION" = "pressed" ]; then
        # 发送自定义 ubus 事件广播
        ubus send custom.button.event "{ \"button\": \"$BUTTON\", \"action\": \"pressed\" }"
    fi
    ```
3.  **应用层监听**：
    *   **调试**：`ubus listen custom.button.event`
    *   **代码**：C/C++ 程序调用 `ubus_register_event_handler` 订阅该事件。

### 3.2 场景二：开发前后端分离的 Web 管理应用
**目标**：编写一个网页显示路由器的 WAN 口 IP，**无需编写任何后端代码 (PHP/Python)**。

**原理**：
*   OpenWrt 的 `uhttpd` 加载了 `uhttpd-mod-ubus` 模块。
*   该模块将 HTTP POST 请求（JSON 格式）自动转换为 ubus 调用。

**前端实现流程 (JavaScript)**：

1.  **登录鉴权** (获取 Session ID):
    ```javascript
    // POST /ubus
    {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "call",
        "params": [
            "00000000000000000000000000000000",
            "session", "login",
            { "username": "root", "password": "password" }
        ]
    }
    // 返回结果中包含 ubus_rpc_session
    ```

2.  **调用系统 API**:
    ```javascript
    // POST /ubus
    {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "call",
        "params": [
            "<SESSION_ID>",  // 填入登录获取的 ID
            "network.interface.wan", // 对象
            "status",                // 方法
            {}
        ]
    }
    ```

3.  **安全控制 (ACL)**：
    *   通过配置 `/usr/share/rpcd/acl.d/` 下的 JSON 文件，可以精确控制特定用户只能调用特定的 ubus 方法，安全性极高。

---

## 4. Ubus vs 标准 Linux D-Bus

为什么标准 Linux (Ubuntu/Debian) 不用 ubus？

| 特性 | OpenWrt Ubus | Linux D-Bus |
| :--- | :--- | :--- |
| **设计初衷** | 嵌入式、极简、Web 友好 | 桌面级、功能全、复杂交互 |
| **通信协议** | JSON-friendly (blobmsg) | 二进制序列化 (Binary) |
| **Web 集成** | **原生支持** (无需中间件) | 困难 (需 Python/Node 网关转译) |
| **系统依赖** | 独立，轻量依赖 | 深度绑定 Systemd |
| **权限控制** | 简单 ACL | 复杂 Polkit 策略 |
| **适用场景** | 路由器、IoT 网关 (内存<512MB) | 桌面 PC、服务器 (内存>1GB) |

**总结**：`ubus` 是 OpenWrt 生态中最被低估的“大杀器”。它不仅解决了进程通信，还顺手解决了 Web API 的问题，让 OpenWrt 设备成为天生的 REST API 服务器。
