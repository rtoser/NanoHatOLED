# OpenWrt SDK Docker 构建环境

本项目提供两套方案来构建一个包含预编译依赖 (`libubus`, `libgpiod`) 的 OpenWrt SDK Docker 镜像。

最终生成的本地镜像名称统一为 `openwrt-sdk:<TAG>` 格式（如 `openwrt-sdk:rockchip-armv8-24.10.5`），可供下游编译脚本无缝使用。

---

## 构建方案

### 方案一：基于官方 SDK 镜像 (推荐)
此方案基于 OpenWrt 官方 Docker Hub 镜像。
*   **优点**: 更接近官方环境，构建过程相对简单。
*   **缺点**: 基础镜像较大。

**使用方法** (在 `docker/` 目录下运行):
```bash
./build-official.sh
```

### 方案二：从零构建 (基于 Debian)
此方案基于轻量的 `debian:bookworm-slim` 镜像，手动下载并解压 SDK。
*   **优点**: 最终镜像体积显著减小 (约 1.4GB vs 2.9GB)。
*   **缺点**: 构建步骤更复杂，依赖外部镜像站。

**使用方法** (在 `docker/` 目录下运行):
```bash
./build.sh
```

---

## 核心构建逻辑 (通用)

两套方案都采用了以下核心技术以确保镜像的纯净、高效和稳定：

1.  **多阶段构建 (Multi-stage Builds)**:
    *   **Builder 阶段**: 在一个临时的构建环境中，完成下载 feeds、编译 `libubus` 和 `libgpiod` 等所有“脏活累活”。此阶段会产生大量构建垃圾（如 `build_dir`, `dl`）。
    *   **Final 阶段**: 以一个干净的基础镜像开始，**仅拷贝** Builder 阶段生成的、已填充好库和头文件的 `staging_dir` 目录。所有临时的构建垃圾都被丢弃。

2.  **优势**:
    *   **体积小**: 最终镜像只比官方镜像大了几十兆（新增的库文件），而不是近 1GB 的构建垃圾。
    *   **速度快**: `compile_in_docker.sh` 无需再编译依赖，可以直接链接，大大加快了项目本身的编译速度。

3.  **依赖净化**:
    *   通过 `patch_libgpiod_makefile.sh` 脚本，自动化地移除了 `libgpiod` 对 Python 的不必要依赖，确保了环境的最小化。

4.  **稳定性**:
    *   对于 `ubus` 及其依赖 `lua` 的编译，强制使用 `-j1` (单线程) 模式，以避免在 Docker/QEMU 环境下因 `jobserver` 问题导致的随机编译失败。

---

## 文件说明

| 文件 | 方案 | 说明 |
| :--- | :--- | :--- |
| `Dockerfile` | **方案二** | 从 Debian 基础镜像开始，采用多阶段构建。|
| `build.sh` | **方案二** | 驱动 `Dockerfile` 的构建脚本。|
| `Dockerfile.official` | **方案一** | 基于 OpenWrt 官方镜像，采用多阶段构建。 |
| `build-official.sh` | **方案一** | 驱动 `Dockerfile.official` 的构建脚本。|
| `patch_libgpiod_makefile.sh`| **通用** | 在构建时自动运行的补丁脚本。 |

---

## 下一步：编译项目

无论你选择哪种方案构建了 SDK 镜像，下一步都是一样的。在项目根目录运行：

```bash
./compile_in_docker.sh rockchip-armv8-24.10.5
```
