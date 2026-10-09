#!/bin/bash
set -euo pipefail

TARGET_IP="${TARGET_IP:-192.168.33.254}"
TARGET_USER="${TARGET_USER:-root}"
BIN_SRC="${BIN_SRC:-src/build/target/nanohat-oled}"
INIT_SRC="${INIT_SRC:-src/nanohat-oled.init}"
SSH_OPTS="${SSH_OPTS:-"-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new"}"

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

# Upload over a plain ssh pipe: OpenWrt's dropbear has no sftp-server, and
# modern scp defaults to SFTP, so plain scp fails against a stock image.
upload() {
    local src="$1" dst="$2"
    ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "cat > '$dst'" < "$src"
}

echo "Stopping service..."
remote "service nanohat-oled stop" || true

echo "Uploading binary to /usr/bin/nanohat-oled..."
upload "$BIN_SRC" /usr/bin/nanohat-oled

if [ -f "$INIT_SRC" ]; then
    echo "Uploading init script..."
    upload "$INIT_SRC" /etc/init.d/nanohat-oled
fi

echo "Setting permissions..."
remote "chmod +x /usr/bin/nanohat-oled"
if [ -f "$INIT_SRC" ]; then
    remote "chmod +x /etc/init.d/nanohat-oled && /etc/init.d/nanohat-oled enable"
fi

echo "Verifying binary on target..."
# 'file' is not part of the default OpenWrt image; fall back to ls.
remote "ls -l /usr/bin/nanohat-oled; command -v file >/dev/null 2>&1 && file -L /usr/bin/nanohat-oled || true"

echo "Starting service..."
remote "service nanohat-oled start"

# A binary built against a different OpenWrt release fails at dynamic-link time
# (libubox/libubus SONAMEs are date-stamped), and procd would respawn it forever.
echo "Checking startup log..."
sleep 2
STARTUP_LOG=$(remote "logread 2>/dev/null | grep -F nanohat-oled | tail -n 5" || true)
printf '%s\n' "$STARTUP_LOG"
if printf '%s' "$STARTUP_LOG" | grep -qiE "error loading shared|not found|No such file"; then
    echo "Error: nanohat-oled failed to start on target (see log above)." >&2
    echo "       Rebuild with the SDK image that matches the target's OpenWrt release." >&2
    exit 1
fi

echo "Deployment complete."
