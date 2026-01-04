#!/bin/bash
# Build OpenWrt SDK Docker image
set -e

cd "$(dirname "$0")"

# Default version, can be overridden by parameter
VERSION="${1:-24.10.5}"
IMAGE_NAME="openwrt-sdk-sunxi"
SDK_FILE="openwrt-sdk-${VERSION}-sunxi-cortexa53_gcc-13.3.0_musl.Linux-x86_64.tar.zst"
SDK_URL="https://mirrors.tuna.tsinghua.edu.cn/openwrt/releases/${VERSION}/targets/sunxi/cortexa53/${SDK_FILE}"

echo "=== Checking SDK availability ==="
echo "URL: ${SDK_URL}"

# Check if URL exists
HTTP_CODE=$(curl -sL -o /dev/null -w "%{http_code}" --head "${SDK_URL}")
if [ "${HTTP_CODE}" != "200" ]; then
    echo "ERROR: SDK not found (HTTP ${HTTP_CODE})"
    echo "Please check available versions at:"
    echo "  https://mirrors.tuna.tsinghua.edu.cn/openwrt/releases/"
    exit 1
fi

echo "SDK found, starting build..."
echo ""

echo "=== Building OpenWrt SDK Docker image ==="
echo "Version: ${VERSION}"
echo "Image:   ${IMAGE_NAME}"
echo ""

BUILDINFO_BASE_URL="https://mirrors.tuna.tsinghua.edu.cn/openwrt/releases/${VERSION}/targets/sunxi/cortexa53"
OPENWRT_VERSION="v${VERSION}"

docker build \
    --build-arg SDK_URL="${SDK_URL}" \
    --build-arg SDK_DIR="openwrt-sdk-${VERSION}-sunxi-cortexa53_gcc-13.3.0_musl.Linux-x86_64" \
    --build-arg BUILDINFO_BASE_URL="${BUILDINFO_BASE_URL}" \
    --build-arg OPENWRT_VERSION="${OPENWRT_VERSION}" \
    -t "${IMAGE_NAME}" \
    .

echo ""
echo "=== Build complete ==="
echo "Image: ${IMAGE_NAME}"
echo ""
echo "Usage:"
echo "  # Build project"
echo "  docker run --rm -v \"\$(pwd)/src:/src\" ${IMAGE_NAME} sh /src/build_in_docker.sh"
echo ""
echo "  # Interactive shell"
echo "  docker run -it --rm -v \"\$(pwd)/src:/src\" ${IMAGE_NAME}"
