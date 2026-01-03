# Device Tree Overlay (DTO) 与 U-Boot 机制详解

本文档深入解析设备树覆盖 (Device Tree Overlay) 的底层原理，以及 Bootloader (特别是 U-Boot) 如何处理这一机制。这也解释了为什么在某些 OpenWrt 固件中我们需要直接修改 DTB 而非使用 Overlay。

## 1. 核心概念：什么是 Overlay？

设备树 (Device Tree) 是描述硬件结构的静态数据结构。在嵌入式系统中，硬件配置可能会动态变化（例如插入了一个扩展板 HAT）。重新编译整个设备树非常繁琐，**Overlay** 机制应运而生。

可以将 Overlay 理解为给硬件描述文件打“补丁”：

*   **Base DTB (主设备树)**: 系统的地基。描述 CPU、内存、内置控制器（I2C, SPI, UART）等固定硬件。
*   **Overlay DTBO (覆盖文件)**: 增量补丁。描述“我要启用 I2C0”或“我要把 GPIO 5 定义为一个 LED”。
*   **Merge (合并)**: 将 DTBO 的属性覆盖或添加到 Base DTB 上的过程。

## 2. Overlay 的生效时机

Overlay 可以在两个阶段生效：

### 阶段 A: Bootloader 阶段 (U-Boot) —— *最推荐*
在 Linux 内核启动**之前**完成合并。
1.  U-Boot 将 Base DTB 从存储介质加载到内存地址 `A`。
2.  U-Boot 将 Overlay DTBO 加载到内存地址 `B`。
3.  U-Boot 执行合并命令 (`fdt apply`)，将 B 的内容应用到 A 上。
4.  U-Boot 启动 Linux 内核，并告知内核：“设备树在地址 A”。
*   **结果**: Linux 启动时看到的就是已经包含新硬件的“原生”设备树，无需任何额外驱动支持。

### 阶段 B: Kernel 阶段 (Runtime)
在 Linux 启动**之后**，通过 `configfs` 动态加载。
*   用户空间工具将 DTBO 写入 `/sys/kernel/config/device-tree/overlays/`。
*   内核动态解析并触发驱动探测。
*   **适用场景**: 支持热插拔的硬件（如 USB 外设带来的复杂配置）。

## 3. U-Boot 的底层处理逻辑

要让 Overlay 在 Bootloader 阶段生效，U-Boot 必须执行一系列特定的命令。以下是一个标准的 Overlay 加载序列：

```bash
# 1. 加载 Base DTB 到内存 ${fdt_addr_r}
load mmc 0:1 ${fdt_addr_r} /boot/sun50i-h5-nanopi.dtb

# 2. 初始化 FDT 系统
fdt addr ${fdt_addr_r}

# 3. 扩容 FDT 内存空间 (为了容纳新增加的节点，必须预留空间)
fdt resize 8192

# 4. 加载 Overlay 文件到内存 ${fdtoverlay_addr_r}
load mmc 0:1 ${fdtoverlay_addr_r} /boot/overlays/enable-i2c0.dtbo

# 5. 执行合并 (关键步骤！)
fdt apply ${fdtoverlay_addr_r}

# 6. 启动内核
booti ${kernel_addr_r} - ${fdt_addr_r}
```

## 4. 如何判断 U-Boot 是否支持 Overlay？

我们不能想当然地认为放一个 `.dtbo` 文件进去它就会生效。必须确认 U-Boot 是否配置了上述逻辑。

### 方法一：检查启动脚本 (boot.scr)
这是最直接的证据。
*   **操作**: 挂载启动分区，反编译或用 `strings` 查看 `boot.scr`。
*   **判断**:
    *   ✅ **支持**: 脚本中包含循环加载 `*.dtbo` 的逻辑，或者显式调用了 `fdt apply`。
    *   ❌ **不支持**: 脚本仅包含 `load dtb` -> `booti`，中间没有其他对 fdt 的操作。

### 方法二：检查 U-Boot 命令行能力
如果有串口 (UART) 访问权限，可以在启动时打断进入 U-Boot 命令行。
*   输入 `fdt apply`。
*   如果提示 `Unknown command`，说明该 U-Boot 二进制文件在编译时甚至没有开启 `CONFIG_OF_LIBFDT_OVERLAY` 选项，彻底不支持。

## 5. 针对 NanoPi OpenWrt 24.10 的分析

在我们的实战中，分析了官方固件的 `boot.scr`：

```bash
setenv loaddtb fatload mmc $mmc_bootdev $fdt_addr_r dtb
# ... 设置 bootargs ...
setenv uenvcmd run loadkernel && run loaddtb && booti $kernel_addr_r - $fdt_addr_r
run uenvcmd
```

**结论**:
该启动脚本**完全缺失** Overlay 加载逻辑。它只是简单地加载单一的 `dtb` 文件并启动。
这就是为什么我们需要使用“反编译 -> 修改 -> 覆盖”这种破坏性但有效的方法。要在该系统上使用标准 Overlay，必须重写并重新编译 `boot.scr`。
