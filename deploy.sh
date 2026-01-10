#!/bin/bash
set -euo pipefail

TARGET_IP="${TARGET_IP:-192.168.33.254}"
TARGET_USER="${TARGET_USER:-root}"
BIN_SRC="${BIN_SRC:-src/build/target/nanohat-oled}"
INIT_SRC="${INIT_SRC:-src/nanohat-oled.init}"
SSH_OPTS="${SSH_OPTS:-"-o BatchMode=yes -o StrictHostKeyChecking=accept-new"}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [-t target_ip] [-u target_user] [-b bin_src] [-i init_src]

Environment variables override defaults (e.g. TARGET_IP, BIN_SRC).
EOF
    exit 1
}

while getopts "t:u:b:i:" opt; do
    case "$opt" in
        t) TARGET_IP=$OPTARG ;;
        u) TARGET_USER=$OPTARG ;;
        b) BIN_SRC=$OPTARG ;;
        i) INIT_SRC=$OPTARG ;;
        *) usage ;;
    esac
done

echo "=== Deploying to $TARGET_USER@$TARGET_IP ==="

if [ ! -f "$BIN_SRC" ]; then
    echo "Error: binary '$BIN_SRC' not found; run make first."
    exit 1
fi
if [ ! -f "$INIT_SRC" ]; then
    echo "Warning: init script '$INIT_SRC' not found; skipping deployment of init."
fi

remote() {
    ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "$@"
}

echo "Stopping service..."
remote "service nanohat-oled stop" || true

echo "Uploading binary to /usr/bin/nanohat-oled..."
scp $SSH_OPTS "$BIN_SRC" "$TARGET_USER@$TARGET_IP:/usr/bin/nanohat-oled"

if [ -f "$INIT_SRC" ]; then
    echo "Uploading init script..."
    scp $SSH_OPTS "$INIT_SRC" "$TARGET_USER@$TARGET_IP:/etc/init.d/nanohat-oled"
fi

echo "Setting permissions..."
remote "chmod +x /usr/bin/nanohat-oled"
if [ -f "$INIT_SRC" ]; then
    remote "chmod +x /etc/init.d/nanohat-oled && /etc/init.d/nanohat-oled enable"
fi

echo "Verifying binary on target..."
remote "file -L /usr/bin/nanohat-oled"

echo "Starting service..."
remote "service nanohat-oled start"

echo "Deployment complete."
