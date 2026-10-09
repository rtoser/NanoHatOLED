# ADR0006 构建与部署指南（OpenWrt / NanoPi NEO2）

> 本文档描述 `src/`（ADR0006 单线程 uloop 实现）的交叉编译与物理设备部署流程。
> 旧的 ADR0005 流程见 `docs/docker_build.md`。

## 0. 先决条件：目标板必须运行 OpenWrt

`src/build/target/nanohat-oled` 只能在 **OpenWrt aarch64 (musl)** 上运行：

| 依赖 | 来源 | 说明 |
|------|------|------|
| `/lib/ld-musl-aarch64.so.1` | OpenWrt 基础系统 | glibc 系统（Armbian/Debian）无法加载 |
| `libubox.so.<date>` | 基础镜像自带 | uloop 事件循环 |
| `libubus.so.<date>` + `ubusd` + `rpcd` | 基础镜像自带 / `rpcd` 需确认 | Services 页通过 ubus `rc` 对象查询/启停服务 |
| `libgpiod.so.3`（libgpiod 2.x） | `packages` feed | 按键 |
| `/dev/i2c-0` | 设备树启用 + `kmod-i2c-core` | SSD1306 OLED |
| `/etc/init.d/nanohat-oled` | 本仓库 `src/nanohat-oled.init` | procd 服务脚本 |

Armbian（systemd + glibc，无 libubox/ubus/procd）**不能直接运行**本二进制；
在 Armbian 上只能跑仓库根目录下的旧版 Python/BakeBit 演示（`install.sh`）。

## 1. 版本绑定：OpenWrt 发行版 ↔ SDK 镜像

`libubox` / `libubus` 的 SONAME 带日期戳，**跨发行版不兼容**。二进制必须用与目标板
同一发行版的 SDK 编译，否则启动时报 `Error loading shared library libubox.so.xxx`，
procd 会不断 respawn。

| OpenWrt | 内核 | 包管理 | libubox SONAME | libubus SONAME | libgpiod | 本地 SDK 镜像 |
|---------|------|--------|----------------|----------------|----------|---------------|
| 24.10.5 | 6.6.119 | opkg | `libubox.so.20240329` | `libubus.so.20250102` | 2.1.3 (`.so.3`) | `openwrt-sdk:sunxi-cortexa53-24.10.5` |
| 25.12.5 | 6.12.94 | apk | `libubox.so.20260213` | `libubus.so.20251202` | 2.1.3 (`.so.3`) | `openwrt-sdk:sunxi-cortexa53-25.12.5` |

换发行版（例如 24.10.x → 25.12.x）的完整动作：**重建 SDK 镜像 → 删除旧 build 目录 → 重新编译 → 重新部署**。
同一系列内的小版本（24.10.5 → 24.10.8）通常 SONAME 不变，但部署前请在板上核对：

```bash
ls /usr/lib/libubox.so.* /usr/lib/libubus.so.* /usr/lib/libgpiod.so.*
```

## 2. 构建 SDK Docker 镜像（每个 OpenWrt 发行版一次）

`docker/build-official.sh` 基于 Docker Hub 的 `openwrt/sdk:<target>-<version>` 镜像，
预编译 `ubus` 与 `libgpiod` 并把 `staging_dir` 固化进镜像：

```bash
cd docker
./build-official.sh sunxi-cortexa53-25.12.5     # 产出 openwrt-sdk:sunxi-cortexa53-25.12.5
# 24.10 系列同理：./build-official.sh sunxi-cortexa53-24.10.5
```

- 需要 `--platform linux/amd64`（脚本已内置）；Apple Silicon 上通过 Rosetta 模拟，耗时约 20–40 分钟。
- 可用的 tag 见 <https://hub.docker.com/r/openwrt/sdk/tags>（`sunxi-cortexa53-25.12.5`、`sunxi-cortexa53-24.10.8` 等）。
- 官方 `openwrt/sdk` 镜像不一定带系统 cmake（25.12.5 的就没有）。SDK 在编译 ubus 时会自行构建
  host cmake/ninja 到 `staging_dir/host/bin`，`build_in_docker.sh` 会优先使用它，无需再改镜像。
- 构建完成后验证：

```bash
docker run --rm --platform linux/amd64 openwrt-sdk:sunxi-cortexa53-25.12.5 \
    sh -c 'ls /builder/staging_dir/target-*/usr/lib | grep -E "ubox|ubus|gpiod"'
```

## 3. 交叉编译应用

```bash
# 项目根目录执行；换 SDK 版本前先清理旧的 CMake 缓存
rm -rf src/build/target

docker run --rm --platform linux/amd64 \
    -v "$(pwd)/src:/src" -w /src \
    openwrt-sdk:sunxi-cortexa53-25.12.5 \
    sh build_in_docker.sh
# 输出：src/build/target/nanohat-oled
```

构建模式：`-e BUILD=release`（`-O2 -DNDEBUG`）、`-e BUILD=debug`（`-O0 -g`），默认 RelWithDebInfo。

> 25.12.5 的 libubox 头文件 `blobmsg.h:387` 会触发两条 `-Wsign-compare` 警告，来自上游，可忽略。

### 板级参数（编译期常量）

通过 `docker run -e` 传入，未设置时使用 CMake 默认值：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `GPIOCHIP_PATH` | `/dev/gpiochip1` | 承载 K1/K2/K3 的 gpiochip |
| `BTN_OFFSETS` | `0,2,3` | K1/K2/K3 在该 chip 上的 line offset（PA0/PA2/PA3） |
| `I2C_DEV_PATH` | `/dev/i2c-0` | OLED 所在 I2C 总线 |
| `MONITORED_SERVICES` | `xray_core,uhttpd,dropbear,collectd` | Services 页显示的服务（最多 8 个） |

```bash
docker run --rm --platform linux/amd64 -v "$(pwd)/src:/src" -w /src \
    -e GPIOCHIP_PATH=/dev/gpiochip0 -e MONITORED_SERVICES=dropbear,uhttpd \
    openwrt-sdk:sunxi-cortexa53-25.12.5 sh build_in_docker.sh
```

> 这些值写入 `src/build/target/CMakeCache.txt`；改回默认值时需 `rm -rf src/build/target`。

## 4. 目标板一次性准备

### 4.1 烧写 OpenWrt

镜像：<https://downloads.openwrt.org/releases/25.12.5/targets/sunxi/cortexa53/>

- `openwrt-25.12.5-sunxi-cortexa53-friendlyarm_nanopi-neo2-ext4-sdcard.img.gz`（推荐，方便改 `/boot` 中的 dtb）
- `openwrt-25.12.5-sunxi-cortexa53-friendlyarm_nanopi-neo2-squashfs-sdcard.img.gz`

macOS 烧写 TF 卡：

```bash
diskutil list                       # 找到 TF 卡，例如 /dev/disk4
diskutil unmountDisk /dev/disk4
gzip -dc openwrt-25.12.5-sunxi-cortexa53-friendlyarm_nanopi-neo2-ext4-sdcard.img.gz \
    | sudo dd of=/dev/rdisk4 bs=4m
sudo diskutil eject /dev/disk4
```

### 4.2 首次联网与 SSH

OpenWrt 默认把 NEO2 唯一的网口配置为 `lan`，**静态 192.168.1.1，不会向路由器申请 DHCP**。
通过串口（UART0，115200）或把电脑直连到板子（电脑设 192.168.1.x）后执行：

```bash
# 1) 网口改为 DHCP 客户端（默认 lan 是 br-lan 桥接 eth0）
uci set network.lan.proto='dhcp'
uci -q delete network.lan.ipaddr
uci -q delete network.lan.netmask
uci -q delete network.lan.ip6assign
uci commit network

# 2) 关闭板子自己的 DHCP 服务器和 IPv6 RA，避免和局域网里的路由器打架
uci set dhcp.lan.ignore='1'
uci set dhcp.lan.ra='disabled'
uci set dhcp.lan.dhcpv6='disabled'
uci commit dhcp

# 3) 应用（若是 SSH 连着 192.168.1.1 操作，这一步会断开，属正常）
service network restart && service dnsmasq restart && service odhcpd restart

passwd                               # 设置 root 密码
ip -4 addr show br-lan               # 查看拿到的新 IP（或看路由器的 DHCP 租约表）
```

> `dhcp.lan.ignore=1` 只关掉 DHCPv4 分发，dnsmasq 仍为板子自己提供 DNS 转发，`apk add` 不受影响。
> 如果想彻底不跑 DHCP/DNS 服务：`service dnsmasq disable; service dnsmasq stop; service odhcpd disable; service odhcpd stop`。

`deploy.sh` 使用 `BatchMode=yes`，需要免密登录。dropbear 的公钥文件是 `/etc/dropbear/authorized_keys`：

```bash
cat ~/.ssh/id_ed25519.pub | ssh root@<ip> \
    'mkdir -p /etc/dropbear && cat >> /etc/dropbear/authorized_keys && chmod 600 /etc/dropbear/authorized_keys'
```

### 4.3 一键初始化：`board-setup.sh`

后面三步（装包、启用 I2C0、确认按键 gpiochip）已经脚本化，宿主机需要
`brew install u-boot-tools dtc`（提供 `dumpimage`/`mkimage`/`fdtget`/`fdtput`）：

```bash
./board-setup.sh -t <ip>          # 装包 → 打补丁 → 重启 → 验证 i2cdetect / gpiodetect
```

脚本做的事：

1. `apk add libgpiod gpiod-tools i2c-tools kmod-i2c-core`（24.10 自动改用 opkg）。
2. 启用 I2C0（`/soc/i2c@1c2ac00`，`status=okay`，`clock-frequency=400000`）：
   - **25.12+**：dtb 已经打进 FIT 格式的 `uImage`，不再有单独的 dtb 文件。脚本用 `dumpimage`
     抽出 dtb，`fdtput` 打补丁后写成 `/mnt/boot/dtb`，并把 `boot.scr` 改成
     `bootm $kernel_comp_addr_r - $fdt_addr_r`（外部 dtb），原始命令保留为下一行回退：
     dtb 加载或校验失败时仍按原样启动。原文件备份为 `boot.scr.orig`，重复执行安全。
   - **24.10**：直接补丁 `/mnt/boot/dtb`，备份为 `dtb.orig`。
3. 重启并检查 `/dev/i2c-0`、`i2cdetect -y 0` 出现 `3c`、`gpiodetect` 列出 `1c20800.pinctrl`。

> sysupgrade / 重刷会覆盖 boot 分区，之后需要再跑一次 `board-setup.sh`。
> 文件通过 `ssh ... 'cat > file'` 传输：OpenWrt 的 dropbear 没有 sftp-server，
> 新版 `scp` 默认走 SFTP 会直接失败（`deploy.sh` 同样已改为 ssh 管道）。

`rpcd` 提供 ubus 的 `rc` 对象，Services 页依赖它，默认镜像已自带：

```bash
ubus list | grep -x rc      # 应有输出
ubus call rc list           # 应列出所有 init 脚本
```

### 4.4 按键所在的 gpiochip

H5 的按键在 PA0/PA2/PA3，属于 `1c20800.pinctrl`（PIO）。内核 6.12（25.12.x）在 NEO2 上只注册
这一个控制器，编号为 **gpiochip0**；旧内核可能把 R_PIO 排在前面而让 PIO 变成 gpiochip1
（CMake 默认值）。以 `board-setup.sh` 末尾打印的结果为准，不一致时重编：

```bash
docker run --rm --platform linux/amd64 -v "$(pwd)/src:/src" -w /src \
    -e GPIOCHIP_PATH=/dev/gpiochip0 openwrt-sdk:sunxi-cortexa53-25.12.5 sh build_in_docker.sh
```

## 5. 部署与升级

```bash
./deploy.sh -t <ip>                              # 默认 root 用户
# 或 TARGET_IP=<ip> ./deploy.sh
```

脚本依次：停止服务 → 上传 `/usr/bin/nanohat-oled` 与 `/etc/init.d/nanohat-oled` →
`enable` → 启动 → 读取 `logread` 检查是否有动态链接错误。日常升级只需重新编译后再跑一次。

验证：

```bash
ssh root@<ip> 'service nanohat-oled status; logread | grep nanohat-oled | tail'
ssh root@<ip> 'gpioinfo | grep consumer=nanohat-oled'    # 三条按键线已被占用，硬件去抖 30ms
```

2026-09-07 在 NanoPi NEO2 + OpenWrt 25.12.1 上按本文流程验证通过（25.12.5 SDK 编译，SONAME 相同）。

板上单元测试（用与目标板一致的 SDK 镜像交叉编译，通过 ssh 上传执行）：

```bash
DOCKER_IMAGE=openwrt-sdk:sunxi-cortexa53-25.12.5 TARGET_IP=<ip> ./tests/target/run_unit_ssh.sh
```

## 6. 常见故障

| 现象 | 原因 | 处理 |
|------|------|------|
| `Error loading shared library libubox.so.2024xxxx` | 二进制与板上 OpenWrt 版本不匹配 | 用对应版本 SDK 重编（§1） |
| `Failed to open I2C device` / init 脚本拒绝启动 | `/dev/i2c-0` 不存在 | §4.4 |
| 屏幕正常但按键无响应 | gpiochip 编号不对 | §4.5 |
| Services 页全部 `?` | `rpcd` 未运行 | `service rpcd start && service rpcd enable` |
| `deploy.sh` 报 Permission denied | 未配置免密公钥 | §4.2 |
| `scp` 报 `sftp-server: not found` | dropbear 无 SFTP | 用 `scp -O` 或 ssh 管道；脚本已处理 |
| 重刷/sysupgrade 后 `/dev/i2c-0` 消失 | boot 分区被覆盖 | 再跑一次 `./board-setup.sh` |
