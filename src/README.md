# NanoHat OLED 驱动程序

基于 u8g2 图形库的 NanoHat OLED 显示驱动，适用于 NanoPi NEO2 Plus。

## 功能特性

- **OLED 显示**：128x64 SSD1306 I2C 显示屏
- **多页面界面**：
  - 状态页：CPU 使用率、温度、内存、运行时间
  - 网络页：IP 地址、实时上下行速度
  - 系统页：内存详情
- **按键控制**：K1/K3 翻页，K2 确认，支持长按检测
- **系统服务**：OpenWrt procd 服务，开机自启

## 文件说明

| 文件 | 说明 |
|------|------|
| `main.c` | 主程序，页面绘制和事件处理 |
| `gpio_button.c/h` | GPIO 按键驱动，中断检测 |
| `sys_status.c/h` | 系统状态采集（CPU/内存/网络） |
| `u8g2_port_linux.c/h` | u8g2 的 Linux I2C 移植层 |
| `u8g2/` | u8g2 图形库（子模块） |
| `Makefile` | 编译配置 |
| `nanohat-oled.init` | OpenWrt procd 服务脚本 |

## 编译

### 交叉编译（macOS/Linux 主机）

```bash
# 使用 Docker 交叉编译
cd ..
bash run_docker.sh
```

输出：`src/nanohat-oled`（静态链接的 aarch64 可执行文件）

### 本地编译（目标设备上）

```bash
make
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
| K1 | PA0 (GPIO 0) | 上一页 |
| K2 | PA2 (GPIO 2) | 确认/长按功能 |
| K3 | PA3 (GPIO 3) | 下一页 |

## 依赖

- Linux 内核 I2C 支持（`/dev/i2c-0`）
- Linux 内核 GPIO sysfs 支持（`/sys/class/gpio/`）

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
