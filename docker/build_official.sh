#!/bin/bash
# Build the official OpenWrt SDK Docker image with vim and pre-compiled libs
set -e

cd "$(dirname "$0")/.."

IMAGE_NAME="openwrt-sdk-official"

echo "=== Building ${IMAGE_NAME} ==="
docker build --platform linux/amd64 -f docker/Dockerfile.openwrt -t ${IMAGE_NAME} .

echo ""
echo "Build complete."
echo "Usage:"
echo "  # Run interactive shell (vim available)"
echo "  docker run -it --rm -v \$(pwd):/src ${IMAGE_NAME}"
echo ""
echo "  # Compile project"
echo "  docker run --rm -v \$(pwd):/src ${IMAGE_NAME} ./src/build_in_docker.sh"
