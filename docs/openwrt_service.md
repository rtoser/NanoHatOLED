# OpenWrt Procd 服务配置

## 概述

NanoHat OLED 使用 OpenWrt 的 procd 进程管理器作为系统服务运行。procd 是 OpenWrt 的核心进程管理器，提供进程监控、自动重启、优雅停止等功能。

## 服务脚本

服务脚本位于 `/etc/init.d/nanohat-oled`：

```sh
#!/bin/sh /etc/rc.common
# OpenWrt procd init script for nanohat-oled

START=99
STOP=10
USE_PROCD=1

PROG=/usr/bin/nanohat-oled
NAME=nanohat-oled

start_service() {
    procd_open_instance "$NAME"
    procd_set_param command "$PROG"
    procd_set_param respawn ${respawn_threshold:-3600} ${respawn_timeout:-5} ${respawn_retry:-5}
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_set_param pidfile /var/run/${NAME}.pid
    procd_close_instance
}

reload_service() {
    stop
    start
}
```

## 关键参数说明

| 参数 | 说明 |
|------|------|
| `START=99` | 启动优先级，数字越大越晚启动（99 = 最后启动） |
| `STOP=10` | 停止优先级，数字越小越早停止 |
| `USE_PROCD=1` | 启用 procd 模式 |
| `procd_open_instance "$NAME"` | 创建命名实例，便于追踪 |
| `procd_set_param command` | 设置要执行的命令 |
| `procd_set_param respawn` | 崩溃自动重启配置 |
| `procd_set_param stdout/stderr` | 重定向输出到 syslog |
| `procd_set_param pidfile` | PID 文件路径 |

### respawn 参数

```
respawn <threshold> <timeout> <retry>
```

- `threshold` (3600)：在此秒数内的重启计入重试次数
- `timeout` (5)：重启前等待秒数
- `retry` (5)：最大重试次数，超过后停止重启

## 服务管理命令

```bash
# 启动服务
/etc/init.d/nanohat-oled start

# 停止服务
/etc/init.d/nanohat-oled stop

# 重启服务
/etc/init.d/nanohat-oled restart

# 查看状态
/etc/init.d/nanohat-oled status

# 设置开机自启
/etc/init.d/nanohat-oled enable

# 禁用开机自启
/etc/init.d/nanohat-oled disable

# 或使用 service 命令
service nanohat-oled start
service nanohat-oled stop
service nanohat-oled status
```

## 最佳实践

### 1. 不要重写 stop_service()

procd 会自动跟踪通过 `procd_set_param command` 启动的进程，并在 stop 时正确终止。手动使用 `killall` 会绑定进程跟踪机制。

```sh
# 错误做法
stop_service() {
    killall nanohat-oled  # 不需要！
}

# 正确做法：不定义 stop_service()，让 procd 处理
```

### 2. 使用命名实例

```sh
procd_open_instance "$NAME"  # 而非 procd_open_instance
```

便于在多实例场景下区分和管理。

### 3. 设置 pidfile

```sh
procd_set_param pidfile /var/run/${NAME}.pid
```

帮助 procd 更可靠地跟踪进程。

### 4. 重定向输出到 syslog

```sh
procd_set_param stdout 1
procd_set_param stderr 1
```

程序的 stdout/stderr 会被转发到 syslog，可通过 `logread` 查看。

## procd 自动提供的功能

1. **进程监控**：procd 持续监控进程状态
2. **优雅停止**：先发送 SIGTERM，超时后发送 SIGKILL
3. **崩溃重启**：进程异常退出时自动重启（受 respawn 参数控制）
4. **状态查询**：`status` 命令显示进程运行状态
5. **开机自启**：`enable` 创建符号链接到 `/etc/rc.d/`

## 日志查看

```bash
# 查看所有日志
logread

# 过滤 nanohat-oled 日志
logread | grep nanohat-oled

# 实时查看日志
logread -f | grep nanohat-oled
```

## 故障排查

### 服务无法启动

```bash
# 检查可执行文件是否存在
ls -la /usr/bin/nanohat-oled

# 手动运行查看错误
/usr/bin/nanohat-oled

# 查看 procd 日志
logread | grep procd
```

### 服务反复重启

检查 respawn 日志，可能是程序崩溃：

```bash
logread | grep -E "(respawn|nanohat-oled)"
```

### 开机自启不生效

```bash
# 检查是否已启用
ls -la /etc/rc.d/ | grep nanohat-oled

# 重新启用
/etc/init.d/nanohat-oled enable
```

## 安装步骤

```bash
# 1. 复制可执行文件
scp nanohat-oled root@<device>:/usr/bin/
ssh root@<device> "chmod +x /usr/bin/nanohat-oled"

# 2. 复制服务脚本
scp nanohat-oled.init root@<device>:/etc/init.d/nanohat-oled
ssh root@<device> "chmod +x /etc/init.d/nanohat-oled"

# 3. 启用并启动
ssh root@<device> "/etc/init.d/nanohat-oled enable"
ssh root@<device> "/etc/init.d/nanohat-oled start"
```
