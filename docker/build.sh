#!/bin/bash
# Build SDK image from scratch (debian-based)
set -e
# This script is designed to run from anywhere; docker/ is the build context.
cd "$(dirname "$0")"

# --- Configuration ---
SDK_TAG="${1:-rockchip-armv8-24.10.5}"
IMAGE_NAME="openwrt-sdk:${SDK_TAG}"

VERSION=${SDK_TAG##*-}
TARGET_TAG=${SDK_TAG%-*}
TARGET=${TARGET_TAG//-///}
BASE_URL="https://mirrors.aliyun.com/openwrt/releases"
TARGET_URL="${BASE_URL}/${VERSION}/targets/${TARGET}/"
DL_DIR="dl"

# --- Dynamic SDK Discovery ---
echo ">>> Discovering SDK filename for manual build..."
HTML=$(curl -fsSL "${TARGET_URL}") || { echo "!!! ERROR: 无法访问 ${TARGET_URL}"; exit 1; }
SDK_FILE=$(echo "$HTML" | grep -o 'href="openwrt-sdk-[^"]*\.tar\.zst"' | sed -e 's/href="//' -e 's/"//' | head -1)
if [ -z "$SDK_FILE" ]; then
    echo "!!! ERROR: Could not find SDK tarball." && exit 1
fi
# 校验文件名格式，防止命令注入
if [[ ! "$SDK_FILE" =~ ^openwrt-sdk-[a-zA-Z0-9._-]+\.tar\.zst$ ]]; then
    echo "!!! ERROR: 无效的 SDK 文件名格式: ${SDK_FILE}" && exit 1
fi
SDK_DIR=$(basename "$SDK_FILE" .tar.zst)
SDK_URL="${TARGET_URL}${SDK_FILE}"
CHECKSUM_URL="${TARGET_URL}sha256sums"
echo "    Found SDK: ${SDK_FILE}"

# --- Main Logic ---
mkdir -p "${DL_DIR}"
MAX_RETRIES=3

# 下载 sha256sums 文件
echo ">>> Downloading sha256sums..."
curl -fsSL -o "${DL_DIR}/sha256sums" "${CHECKSUM_URL}" || { echo "!!! ERROR: 无法下载 sha256sums"; exit 1; }

# 获取期望的校验值
EXPECTED_HASH=$(grep -F "*${SDK_FILE}" "${DL_DIR}/sha256sums" | awk '{print $1}')
if [ -z "$EXPECTED_HASH" ]; then
    echo "!!! ERROR: 在 sha256sums 中找不到 ${SDK_FILE} 的校验值" && exit 1
fi

# 下载并校验 SDK（支持自动重试）
for ((i=1; i<=MAX_RETRIES; i++)); do
    # 下载 SDK（支持断点续传）
    if [ ! -f "${DL_DIR}/${SDK_FILE}" ]; then
        echo ">>> Downloading SDK..."
        wget -c -O "${DL_DIR}/${SDK_FILE}" "${SDK_URL}"
    fi

    # 校验 SDK 完整性
    echo ">>> Verifying SDK checksum..."
    if command -v sha256sum &>/dev/null; then
        ACTUAL_HASH=$(sha256sum "${DL_DIR}/${SDK_FILE}" | awk '{print $1}')
    else
        ACTUAL_HASH=$(shasum -a 256 "${DL_DIR}/${SDK_FILE}" | awk '{print $1}')
    fi

    if [ "$EXPECTED_HASH" = "$ACTUAL_HASH" ]; then
        echo "    Checksum OK"
        break
    fi

    echo "!!! WARNING: Checksum 校验失败! (尝试 $i/$MAX_RETRIES)"
    echo "    期望值: ${EXPECTED_HASH}"
    echo "    实际值: ${ACTUAL_HASH}"
    rm -f "${DL_DIR}/${SDK_FILE}"

    if [ "$i" -eq "$MAX_RETRIES" ]; then
        echo "!!! ERROR: 已达到最大重试次数，下载失败"
        exit 1
    fi
    echo ">>> 正在重新下载..."
done

echo "=== Building SDK Image (Manual Method): ${IMAGE_NAME} ==="
# Build context is docker/
docker build \
    --platform linux/amd64 \
    --build-arg SDK_FILE="${SDK_FILE}" \
    --build-arg SDK_DIR="${SDK_DIR}" \
    --build-arg SDK_IMAGE_TAG="${SDK_TAG}" \
    -t "${IMAGE_NAME}" \
    -f Dockerfile \
    .

echo "Build complete: ${IMAGE_NAME}"
