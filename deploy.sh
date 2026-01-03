#!/bin/bash
set -e

TARGET_IP="192.168.33.254"
TARGET_USER="root"
BIN_SRC="src/nanohat-oled"
INIT_SRC="src/nanohat-oled.init"

echo "=== Deploying to $TARGET_IP ==="

# 1. Check if binary exists
if [ ! -f "$BIN_SRC" ]; then
    echo "Error: Binary '$BIN_SRC' not found. Please compile first."
    exit 1
fi

# 2. Upload Binary
echo "Uploading binary..."
scp -o BatchMode=yes "$BIN_SRC" "$TARGET_USER@$TARGET_IP:/usr/bin/nanohat-oled"

# 3. Upload Init Script
echo "Uploading init script..."
scp -o BatchMode=yes "$INIT_SRC" "$TARGET_USER@$TARGET_IP:/etc/init.d/nanohat-oled"

# 4. Configure & Enable
echo "Configuring service..."
ssh -o BatchMode=yes "$TARGET_USER@$TARGET_IP" "
    chmod +x /usr/bin/nanohat-oled
    chmod +x /etc/init.d/nanohat-oled
    /etc/init.d/nanohat-oled enable
    /etc/init.d/nanohat-oled restart
    echo 'Service nanohat-oled deployed and restarted.'
"
