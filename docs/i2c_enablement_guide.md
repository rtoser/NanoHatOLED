# NanoPi NEO Plus2 I2C 启用与调试指南

本文档记录了在 OpenWrt (24.10) 系统上启用 NanoHat OLED 所需的 I2C 接口的完整过程。

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
    确保 `i2c_core` 和平台总线驱动 (如 `i2c_mv64xxx` 或 `i2c_sun6i`) 已加载。

3.  **扫描 I2C 总线 (Ping 屏幕)**:
    使用 `i2c-tools` 包：
    ```bash
    opkg update && opkg install i2c-tools
    i2cdetect -y 0
    ```
    *   你应该在矩阵中看到 **`3c`**，这代表 OLED 屏幕已在线。

## 3. 启用 I2C (修改设备树)

在官方 OpenWrt 固件中，I2C0 接口默认通常是 **Disabled** 状态。我们需要修改设备树 (Device Tree) 来启用它。

### 方法 A: 反编译并直接修改 DTB (实战验证有效)

这是在不支持动态 Overlay 的 Bootloader 环境下最直接的方法。

#### 步骤

1.  **定位 DTB 文件**:
    通常位于启动分区。在 NanoPi NEO Plus2 上：
    ```bash
    mount /dev/mmcblk2p1 /mnt/boot  # 挂载启动分区 (设备名可能因内核版本而异，如 mmcblk0p1)
    ls /mnt/boot/dtb                # 这是一个文件，实际上是 sun50i-h5-nanopi-neo-plus2.dtb
    ```

2.  **安装编译器**:
    ```bash
    opkg install dtc
    # 或
    opkg install device-tree-compiler
    ```

3.  **反编译 (DTB -> DTS)**:
    ```bash
    dtc -I dtb -O dts -o /tmp/system.dts /mnt/boot/dtb
    ```

4.  **修改 DTS**:
    编辑 `/tmp/system.dts`，查找 `i2c@1c2ac00` (I2C0 控制器地址) 节点。
    将 `status = "disabled";` 修改为 `status = "okay";`。

    ```dts
    i2c@1c2ac00 {
        compatible = "allwinner,sun6i-a31-i2c";
        reg = <0x1c2ac00 0x400>;
        // ... 其他属性 ...
        status = "okay";  // <--- 修改这里
    };
    ```

5.  **重新编译 (DTS -> DTB)**:
    ```bash
    dtc -I dts -O dtb -o /tmp/new.dtb /tmp/system.dts
    ```
    *注意：编译过程中可能会出现 "cell 0 is not a phandle reference" 的警告，这是反编译丢失符号表导致的，通常无害。*

6.  **替换并重启**:
    ```bash
    cp /mnt/boot/dtb /mnt/boot/dtb.bak  # 备份！
    cp /tmp/new.dtb /mnt/boot/dtb       # 覆盖
    reboot
    ```

### 方法 B: 使用 Device Tree Overlay (推荐，但需 U-Boot 支持)

如果 U-Boot 配置了 Overlay 加载逻辑 (如 Armbian 的 `armbianEnv.txt` 或 OpenWrt 的 `boot.scr` 中包含 `fdt apply`)，这是更安全的方法。

1.  编写片段文件 `enable-i2c0.dts`:
    ```dts
    /dts-v1/;
    /plugin/;
    / {
        fragment@0 {
            target = <&i2c0>;
            __overlay__ {
                status = "okay";
            };
        };
    };
    ```
2.  编译为 `.dtbo`: `dtc -I dts -O dtb -o enable-i2c0.dtbo enable-i2c0.dts`
3.  配置 Bootloader 加载该文件。

*注：在 OpenWrt 24.10 官方固件的默认 `boot.scr` 中，并未发现自动加载 Overlay 的逻辑，因此方法 A 是通过验证的可行方案。*

## 4. 反编译修改的利弊权衡

### 风险 (Cons)
1.  **符号丢失 (Phandle Loss)**: 反编译会丢失 DTS 中的标签 (Labels) 和 `__symbols__` 节点。这可能导致后续无法在该 DTB 上再叠加其他 Overlay。
2.  **维护性差**: 系统升级 (Sysupgrade) 会覆盖 `/boot` 分区，导致修改丢失，需要重新操作。
3.  **不可逆**: 这是一个破坏性操作，如果修改错误可能导致系统无法启动 (因此备份至关重要)。

### 优势 (Pros)
1.  **通用性强**: 不依赖 Bootloader 的高级功能，只要是 Linux 系统都能用。
2.  **即时生效**: 对于只需启用一个内置外设 (如 I2C) 的简单需求，这是最快路径。

## 5. 软件驱动方案

硬件打通后，推荐使用 **用户态驱动** 方案：
*   **工具**: `i2c-tools` (检测)。
*   **开发库**: C 语言 (`u8g2` 移植), Go (`periph.io`), 或 Python (`smbus`).
*   **优势**: 也就是 User-Space Driver。相比内核 Framebuffer 驱动，用户态驱动无需重新编译内核模块，部署灵活，且足够驱动 OLED 这种低速设备。
