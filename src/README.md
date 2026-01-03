# NanoHat OLED 驱动程序

基于 u8g2 图形库的 NanoHat OLED 显示驱动，适用于 NanoPi NEO2 Plus。

## 功能特性

- **OLED 显示**：128x64 SSD1306 I2C 显示屏
- **多页面界面**：
  - 状态页：主机名、CPU 使用率/温度、内存使用率、运行时间
  - 网络页：IP 地址、实时上下行速度
  - 服务页：xray_core/dropbear/dockerd 运行状态（最多 3 项）
  - 系统页：内存详情（总量/可用/空闲）
- **按键控制**：K1/K3 翻页（关屏时唤醒，菜单内用于选择）；K2 短按开关屏幕；K2 长按在服务页进入菜单，菜单内 K2 短按启停、长按退出，超时自动退出（非菜单页长按抖动提示）
- **服务控制**：通过 libubus/procd 启停服务
- **系统服务**：OpenWrt procd 服务，开机自启

## 文件说明

| 文件 | 说明 |
|------|------|
| `main.c` | 主程序，页面绘制和事件处理 |
| `gpio_button.c/h` | GPIO 按键驱动，中断检测 |
| `sys_status.c/h` | 系统状态采集（CPU/内存/网络） |
| `ubus_service.c/h` | libubus 服务控制与状态查询 |
| `u8g2_port_linux.c/h` | u8g2 的 Linux I2C 移植层 |
| `u8g2/` | u8g2 图形库（子模块） |
| `Makefile` | 编译配置 |
| `build_in_docker.sh` | Docker 容器内编译脚本 |
| `nanohat-oled.init` | OpenWrt procd 服务脚本 |
| `../docker/` | OpenWrt SDK Docker 构建环境 |

## 编译

### 交叉编译（推荐）

使用 OpenWrt SDK Docker 容器交叉编译：

```bash
# 1. 首次使用需构建 Docker 镜像（约 5-10 分钟）
cd docker
./build.sh 24.10.5

# 2. 编译项目
cd ..
docker run --rm -v "$(pwd)/src:/src" openwrt-sdk-sunxi-24.10.5 sh build_in_docker.sh
```

输出：`src/nanohat-oled`（静态链接的 aarch64 可执行文件，约 15MB）

详见 [docker/README.md](../docker/README.md)

### 本地编译（目标设备上）

```bash
make CC=gcc
```

## 部署

```bash
# 上传可执行文件
scp nanohat-oled root@<device-ip>:/usr/bin/

# 安装服务脚本
scp nanohat-oled.init root@<device-ip>:/etc/init.d/nanohat-oled
ssh root@<device-ip> "chmod +x /etc/init.d/nanohat-oled"

# 启用并启动服务
ssh root@<device-ip> "/etc/init.d/nanohat-oled enable"
ssh root@<device-ip> "/etc/init.d/nanohat-oled start"
```

## 命令行参数

```
nanohat-oled [选项]

选项：
  -d, --daemon    以守护进程模式运行
  -v, --version   显示版本号
  -h, --help      显示帮助信息
```

## 硬件连接

### OLED 显示屏
- I2C 总线：`/dev/i2c-0`
- I2C 地址：`0x3C`
- 分辨率：128x64

### 按键
| 按键 | GPIO | 功能 |
|------|------|------|
| K1 | PA0 (GPIO 0) | 上一页 / 唤醒屏幕 |
| K2 | PA2 (GPIO 2) | 短按开关屏幕 / 长按进入服务菜单（服务页） |
| K3 | PA3 (GPIO 3) | 下一页 / 唤醒屏幕 |

说明：按键电平以硬件为准，程序启动时会自动探测按下电平；若出现按键异常，可在目标板上读取 `/sys/class/gpio/gpioN/value` 校验空闲电平。

## 依赖

- Linux 内核 I2C 支持（`/dev/i2c-0`）
- Linux 内核 GPIO sysfs 支持（`/sys/class/gpio/`）
- libubus/libubox/libblobmsg_json/libjson-c（服务控制与状态）

## 日志

程序日志输出到 syslog：

```bash
# 查看日志
logread | grep nanohat-oled
```

## 技术文档

- [GPIO 按键设计](../docs/gpio_button_design.md)
- [OpenWrt 服务配置](../docs/openwrt_service.md)
- [I2C 启用指南](../docs/i2c_enablement_guide.md)
