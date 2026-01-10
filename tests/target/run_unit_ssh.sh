#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
TARGET_IP=${TARGET_IP:-192.168.33.254}
TARGET_USER=${TARGET_USER:-root}
SSH_OPTS=${SSH_OPTS:-"-o BatchMode=yes -o StrictHostKeyChecking=accept-new"}
DOCKER_IMAGE=${DOCKER_IMAGE:-openwrt-sdk:sunxi-cortexa53-24.10.5}
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/tests/build/target"}
REMOTE_DIR=${REMOTE_DIR:-/tmp/nanohat-tests}
SKIP_BUILD=${SKIP_BUILD:-0}

TESTS="test_uloop_smoke test_timer_basic test_gpio_event_uloop test_ui_controller test_ui_refresh_policy test_ubus_async_uloop"

if [ "$SKIP_BUILD" != "1" ]; then
    echo "== Building target unit tests (Docker) =="
    docker run --rm --platform linux/amd64 \
        -v "$ROOT_DIR:/repo" -w /repo/tests \
        "$DOCKER_IMAGE" \
        sh -c "cmake -S . -B build/target -DCMAKE_TOOLCHAIN_FILE=/repo/src/cmake/toolchain-openwrt-aarch64.cmake && cmake --build build/target --parallel"
fi

for t in $TESTS; do
    if [ ! -f "$BUILD_DIR/$t" ]; then
        echo "Missing test binary: $BUILD_DIR/$t"
        exit 1
    fi
done

echo "== Uploading tests to $TARGET_USER@$TARGET_IP:$REMOTE_DIR =="
ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "mkdir -p $REMOTE_DIR"
scp $SSH_OPTS "$BUILD_DIR"/test_* "$TARGET_USER@$TARGET_IP:$REMOTE_DIR/"
ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "chmod +x $REMOTE_DIR/test_*"

echo "== Running tests on target =="
ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "
    set -e
    for t in $TESTS; do
        echo \"=== \$t ===\"
        $REMOTE_DIR/\$t
    done
"
