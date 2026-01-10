#!/bin/sh
set -eu

TARGET_IP=${TARGET_IP:-192.168.33.254}
TARGET_USER=${TARGET_USER:-root}
SSH_OPTS=${SSH_OPTS:-"-o BatchMode=yes -o StrictHostKeyChecking=accept-new"}
BIN=${BIN:-src/build/target/nanohat-oled}
REMOTE_BIN=${REMOTE_BIN:-/tmp/nanohat-oled}
RUN_SECONDS=${RUN_SECONDS:-2}

if [ ! -f "$BIN" ]; then
    echo "Binary not found: $BIN"
    exit 1
fi

echo "== Uploading $BIN to $TARGET_USER@$TARGET_IP:$REMOTE_BIN =="
scp $SSH_OPTS "$BIN" "$TARGET_USER@$TARGET_IP:$REMOTE_BIN"

echo "== Running smoke test on target =="
ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "
    chmod +x $REMOTE_BIN &&
    (killall nanohat-oled 2>/dev/null || true);
    $REMOTE_BIN >/tmp/nanohat-oled.log 2>&1 &
    sleep $RUN_SECONDS;
    killall -TERM nanohat-oled 2>/dev/null || true;
    sleep 1;
    echo '--- SIGTERM log ---';
    tail -n +1 /tmp/nanohat-oled.log;
    $REMOTE_BIN >/tmp/nanohat-oled.log 2>&1 &
    sleep $RUN_SECONDS;
    killall -INT nanohat-oled 2>/dev/null || true;
    sleep 1;
    echo '--- SIGINT log ---';
    tail -n +1 /tmp/nanohat-oled.log
"
