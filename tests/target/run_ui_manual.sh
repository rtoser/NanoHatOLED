#!/bin/sh
set -eu

APP=${APP:-/tmp/nanohat-oled}
LOG=${LOG:-/tmp/nanohat-oled-manual.log}

if [ ! -x "$APP" ]; then
    echo "App not found or not executable: $APP"
    exit 1
fi

killall nanohat-oled 2>/dev/null || true
"$APP" >"$LOG" 2>&1 &
PID=$!

echo "Step 1: Verify Home page renders (CPU/MEM/RUN). Press Enter to continue."
read -r _
echo "Step 2: Press K3 to go to next page, K1 to go back. Press Enter to continue."
read -r _
echo "Step 3: Short press K2 to sleep screen. Press any key to wake. Press Enter to continue."
read -r _
echo "Step 4: Long press K2 on a non-enterable page; title should shake. Press Enter to finish."
read -r _

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

echo "--- app log ---"
tail -n +1 "$LOG"
