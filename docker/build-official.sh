#!/bin/bash
# build.sh (to be run from inside 'docker/' directory)
set -e
cd "$(dirname "$0")"

SDK_TAG="${1:-rockchip-armv8-24.10.5}"
IMAGE_NAME="openwrt-sdk:${SDK_TAG}"

echo "=== Building SDK Image: ${IMAGE_NAME} ==="

# Build context is the current directory (docker/)
docker build \
    --platform linux/amd64 \
    --build-arg SDK_IMAGE_TAG="${SDK_TAG}" \
    -t "${IMAGE_NAME}" \
    -f Dockerfile \
    .

echo "Build complete: ${IMAGE_NAME}"