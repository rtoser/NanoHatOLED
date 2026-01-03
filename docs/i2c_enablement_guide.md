# NanoPi NEO Plus2 I2C 启用与性能优化指南

本文档记录了在 OpenWrt (24.10) 系统上启用 NanoHat OLED 所需的 I2C 接口及其性能优化的完整实战过程，包含理论分析与操作步骤。

## 1. 硬件连接 (Hardware Context)

NanoHat OLED 使用 I2C 接口与开发板通信。在 NanoPi NEO Plus2 (及 NEO2) 上，连接关系如下：

*   **物理接口**: 24-Pin GPIO Header (CON3)
*   **引脚定义**:
    *   **Pin 3**: I2C0_SDA (GPIOA12)
    *   **Pin 5**: I2C0_SCL (GPIOA11)
    *   **Pin 1**: 3.3V VCC
    *   **Pin 6/9/14/20**: GND
*   **I2C 地址**: `0x3C` (OLED 屏幕)

## 2. 检查 I2C 系统状态

在尝试驱动屏幕前，必须确保操作系统内核已正确加载 I2C 驱动并识别到总线。

### 检查步骤

1.  **检查设备节点**:
    ```bash
    ls -l /dev/i2c*
    ```
    *   **成功**: 显示 `/dev/i2c-0` 等设备。
    *   **失败**: 显示 "No such file or directory"。这意味着 I2C 控制器在设备树中被禁用 (Disabled) 或内核模块未加载。

2.  **检查内核模块**:
    ```bash
    lsmod | grep i2c
    ```
    确保 `i2c_core` 和平台总线驱动 (如 `i2c_mv64xxx` 或 `i2c_sun6i`) 已加载。如果没有，安装 `kmod-i2c-core`。

3.  **扫描 I2C 总线 (Ping 屏幕)**:
    使用 `i2c-tools` 包：
    ```bash
    opkg update && opkg install i2c-tools
    i2cdetect -y 0
    ```
    *   你应该在矩阵中看到 **`3c`**，这代表 OLED 屏幕已在线。

## 3. 启用 I2C 与超频 (修改设备树)

在官方 OpenWrt 固件中，I2C0 接口默认通常是 **Disabled** 状态，且频率可能默认为 **100kHz** (导致 OLED 刷新慢)。我们需要修改设备树 (Device Tree) 来启用它并提升速度。

### 方法 A: 反编译并直接修改 DTB (实战推荐)

这是在不支持动态 Overlay 的 Bootloader 环境下最直接的方法。

#### 步骤

1.  **定位启动分区 (关键!)**:
    Linux 设备名可能会变 (如 `mmcblk0` vs `mmcblk2`)。请务必先检查分区表，找到那个约 **20MB** 的小分区：
    ```bash
    cat /proc/partitions
    # 找到类似 mmcblk0p1 (20480 blocks) 的分区
    ```

2.  **挂载分区**:
    ```bash
    mkdir -p /mnt/boot
    mount /dev/mmcblk0p1 /mnt/boot  # 根据上一步结果调整设备名
    ls /mnt/boot/dtb                # 这是一个文件，实际上是 sun50i-h5-nanopi-neo-plus2.dtb
    ```

3.  **安装编译器**:
    ```bash
    opkg install dtc
    # 或
    opkg install device-tree-compiler
    ```

4.  **反编译 (DTB -> DTS)**:
    ```bash
    dtc -I dtb -O dts -o /tmp/system.dts /mnt/boot/dtb
    ```

5.  **修改 DTS (启用 + 超频)**:
    编辑 `/tmp/system.dts`，查找 `i2c@1c2ac00` (I2C0 控制器地址) 节点。
    *   修改状态为 `okay`。
    *   添加/修改 `clock-frequency` 为 `400000` (400kHz) 或 `800000` (800kHz)。

    ```dts
    i2c@1c2ac00 {
        compatible = "allwinner,sun6i-a31-i2c";
        reg = <0x1c2ac00 0x400>;
        // ... 其他属性 ...
        status = "okay";              // <--- 启用接口
        clock-frequency = <400000>;   // <--- 设置为 400kHz (推荐) 或 800000
    };
    ```
    *建议*: 先试 400kHz，如果稳定且需要更流畅动画，再试 800kHz。

6.  **重新编译 (DTS -> DTB)**:
    ```bash
    dtc -I dts -O dtb -o /tmp/new.dtb /tmp/system.dts
    ```
    *注意：编译过程中可能会出现 "cell 0 is not a phandle reference" 的警告，这是反编译丢失符号表导致的，通常无害。*

7.  **替换并重启**:
    ```bash
    cp /mnt/boot/dtb /mnt/boot/dtb.bak  # 备份！
    cp /tmp/new.dtb /mnt/boot/dtb       # 覆盖
    reboot
    ```

### 方法 B: 使用 Device Tree Overlay (理论上更优，但受限)

如果 U-Boot 配置了 Overlay 加载逻辑，这是更安全的方法。

1.  编写片段文件 `enable-i2c0.dts`:
    ```dts
    /dts-v1/;
    /plugin/;
    / {
        fragment@0 {
            target = <&i2c0>;
            __overlay__ {
                status = "okay";
                clock-frequency = <400000>;
            };
        };
    };
    ```
2.  编译为 `.dtbo`: `dtc -I dts -O dtb -o enable-i2c0.dtbo enable-i2c0.dts`
3.  配置 Bootloader 加载该文件。

**为什么实战中没用这个方法？**
经过检查 `boot.scr` (U-Boot 启动脚本)，官方 OpenWrt 固件**没有**包含 `fdt apply` 或加载 `.dtbo` 文件的逻辑。因此，简单的放置 Overlay 文件是无效的。

## 4. 反编译修改的利弊权衡

### 风险 (Cons)
1.  **符号丢失 (Phandle Loss)**: 反编译会丢失 DTS 中的标签 (Labels) 和 `__symbols__` 节点。DTB 被编译为二进制时，所有的 `&label` 引用都被转换成了数字 ID (phandle)。反编译时无法恢复原始标签名，如果后续想基于这个反编译出的 DTB 再做 Overlay，可能会因为缺少符号表而失败。
2.  **维护性差**: 系统升级 (Sysupgrade) 会覆盖 `/boot` 分区，导致修改丢失，需要重新操作。
3.  **不可逆**: 这是一个破坏性操作，如果修改错误可能导致系统无法启动 (因此备份至关重要)。

### 优势 (Pros)
1.  **通用性强**: 不依赖 Bootloader 的高级功能 (如 U-Boot 的 `fdt apply`)，只要是 Linux 系统都能用。
2.  **即时生效**: 对于只需启用一个内置外设 (如 I2C) 的简单需求，这是最快路径，无需重新编译 U-Boot 或内核。

## 5. 软件驱动方案

硬件打通后，推荐使用 **用户态驱动** 方案：
*   **工具**: `i2c-tools` (检测)。
*   **开发库**: C 语言 (`u8g2` 移植), Go (`periph.io`), 或 Python (`smbus`).
*   **优势**: 也就是 User-Space Driver。相比内核 Framebuffer 驱动，用户态驱动无需重新编译内核模块，部署灵活，且在 400kHz/800kHz I2C 频率下，足以驱动 128x64 OLED 实现 60FPS 左右的流畅动画。
