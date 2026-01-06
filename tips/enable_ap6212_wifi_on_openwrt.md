# 在 OpenWrt 上启用 AP6212 (BCM43430) WiFi

本文档记录了在 **NanoPi NEO Plus2** (及其他使用 AP6212/BCM43430 芯片的 Allwinner H5 设备) 上启用 WiFi 的简便方法。

## 1. 核心原理
得益于 OpenWrt 主线内核对该开发板 **Device Tree (DTS)** 的完整支持，我们无需手动编译驱动或修改内核，只需从官方源安装对应的内核模块和固件包即可。

## 2. 快速安装命令

确保设备已通过网线连接互联网，SSH 登录后执行：

```bash
# 1. 更新软件包列表
opkg update

# 2. 安装驱动、固件及管理工具
# kmod-brcmfmac: 核心内核驱动
# brcmfmac-firmware-43430-sdio: 芯片专用固件 (.bin) 和 NVRAM 配置 (.txt)
# iw: 无线配置工具
# wpad-basic-wolfssl: 支持 WPA2/WPA3 加密 (家用 WiFi 必备)
opkg install kmod-brcmfmac brcmfmac-firmware-43430-sdio iw wpad-basic-wolfssl
```

## 3. 验证驱动状态

安装完成后，驱动通常会自动加载。你可以通过以下步骤验证：

### 3.1 检查内核日志
```bash
dmesg | grep brcmfmac
```
**成功标志**：看到类似 `Firmware: BCM43430/1 ... version ...` 的日志。

### 3.2 检查固件文件
确认 `/lib/firmware/brcm/` 目录下存在以下关键文件：
*   `brcmfmac43430-sdio.bin` (或指向它的软链接)
*   `brcmfmac43430-sdio.txt` (NVRAM配置，非常重要，缺失会导致无法开启)

### 3.3 测试扫描信号
```bash
# 启用接口
ip link set wlan0 up

# 扫描周围 WiFi
iw wlan0 scan | grep SSID
```

## 4. 启用 WiFi 配置
默认情况下 OpenWrt 的 WiFi 是禁用的。初始化配置：

```bash
# 生成默认配置
wifi config

# 启用无线电并重启网络
uci set wireless.radio0.disabled=0
uci commit wireless
wifi
```

此时你应该能在 LuCI 网页后台看到并配置 WiFi 了。
