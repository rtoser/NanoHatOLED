#!/bin/bash
# One-time preparation of a freshly flashed OpenWrt board for nanohat-oled:
#   1. install runtime packages (libgpiod, gpiod-tools, i2c-tools, kmod-i2c-core)
#   2. enable I2C0 at 400 kHz in the device tree
#   3. reboot and verify /dev/i2c-0, the OLED at 0x3c and the button gpiochip
#
# Device-tree handling covers both OpenWrt layouts:
#   * 25.12+: the dtb is embedded in a FIT 'uImage'. We extract it, patch it and
#     switch boot.scr to boot with the external 'dtb' file, keeping the stock
#     bootm as a fallback line so a broken dtb still boots the board.
#   * 24.10:  a plain 'dtb' file sits on the boot partition and is patched in place.
#
# Host requirements: dumpimage, mkimage (u-boot-tools), fdtget, fdtput (dtc).
#   macOS: brew install u-boot-tools dtc
set -euo pipefail

TARGET_IP="${TARGET_IP:-192.168.33.254}"
TARGET_USER="${TARGET_USER:-root}"
SSH_OPTS="${SSH_OPTS:-"-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new"}"
I2C_NODE="${I2C_NODE:-/soc/i2c@1c2ac00}"      # I2C0 on H5/H3
I2C_HZ="${I2C_HZ:-400000}"
BOOT_DEV="${BOOT_DEV:-/dev/mmcblk0p1}"
NO_REBOOT=0

usage() {
    cat <<USAGE
Usage: $(basename "$0") [-t target_ip] [-u target_user] [-n]
  -n   do not reboot after patching (verify later by hand)
Environment overrides: TARGET_IP TARGET_USER I2C_NODE I2C_HZ BOOT_DEV
USAGE
    exit 1
}

while getopts "t:u:nh" opt; do
    case "$opt" in
        t) TARGET_IP=$OPTARG ;;
        u) TARGET_USER=$OPTARG ;;
        n) NO_REBOOT=1 ;;
        *) usage ;;
    esac
done

for tool in dumpimage mkimage fdtget fdtput; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Error: '$tool' not found (brew install u-boot-tools dtc)"; exit 1; }
done

remote() { ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "$@"; }
# dropbear has no sftp-server, so files travel over plain ssh pipes.
fetch()  { ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "cat '$1'" > "$2"; }
upload() { ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "cat > '$2'" < "$1"; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/board-setup.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

echo "=== Preparing $TARGET_USER@$TARGET_IP ==="
remote 'cat /etc/openwrt_release | grep -E "DISTRIB_(RELEASE|TARGET)"; uname -r'

# ---------------------------------------------------------------- 1. packages
echo "--- Installing packages ---"
remote 'if command -v apk >/dev/null 2>&1; then
            apk update >/dev/null && apk add libgpiod gpiod-tools i2c-tools kmod-i2c-core
        else
            opkg update >/dev/null && opkg install libgpiod gpiod-tools i2c-tools kmod-i2c-core
        fi'
remote 'ubus list | grep -qx rc || echo "WARNING: ubus object rc missing - install/start rpcd for the Services page"'

# ------------------------------------------------------------ 2. device tree
echo "--- Mounting boot partition $BOOT_DEV ---"
remote "mkdir -p /mnt/boot; grep -qs ' /mnt/boot ' /proc/mounts || mount $BOOT_DEV /mnt/boot; ls -l /mnt/boot"

if remote 'test -f /mnt/boot/dtb && ! test -f /mnt/boot/boot.scr.orig'; then
    LAYOUT=plain          # 24.10 style: separate dtb file, stock boot.scr
else
    LAYOUT=fit            # 25.12 style: dtb inside FIT uImage (or already converted by us)
fi
echo "Boot layout: $LAYOUT"

if [ "$LAYOUT" = plain ]; then
    remote 'test -f /mnt/boot/dtb.orig || cp /mnt/boot/dtb /mnt/boot/dtb.orig'
    fetch /mnt/boot/dtb.orig "$WORK/dtb"
else
    fetch /mnt/boot/uImage "$WORK/uImage"
    dumpimage -l "$WORK/uImage" | grep -q "FIT description" || { echo "Error: uImage is not a FIT image"; exit 1; }
    IDX=$(dumpimage -l "$WORK/uImage" | sed -nE 's/^ *Image ([0-9]+) \(fdt.*/\1/p' | head -1)
    [ -n "$IDX" ] || { echo "Error: no fdt sub-image in FIT"; exit 1; }
    dumpimage -T flat_dt -p "$IDX" -o "$WORK/dtb" "$WORK/uImage" >/dev/null
fi

echo "Model: $(fdtget "$WORK/dtb" / model)"
echo "$I2C_NODE before: status=$(fdtget "$WORK/dtb" "$I2C_NODE" status 2>/dev/null || echo '?')"
fdtput -t s "$WORK/dtb" "$I2C_NODE" status okay
fdtput -t u "$WORK/dtb" "$I2C_NODE" clock-frequency "$I2C_HZ"
echo "$I2C_NODE after:  status=$(fdtget "$WORK/dtb" "$I2C_NODE" status) clock-frequency=$(fdtget "$WORK/dtb" "$I2C_NODE" clock-frequency)"

if [ "$LAYOUT" = fit ]; then
    # Keep the pristine boot.scr; always derive ours from it (idempotent re-runs).
    remote 'test -f /mnt/boot/boot.scr.orig || cp /mnt/boot/boot.scr /mnt/boot/boot.scr.orig'
    fetch /mnt/boot/boot.scr.orig "$WORK/boot.scr.orig"
    tail -c +73 "$WORK/boot.scr.orig" > "$WORK/boot.cmd.orig"     # strip 64-byte legacy header + 8-byte script header

    grep -q '^setenv loadkernel fatload mmc' "$WORK/boot.cmd.orig" &&
    grep -q '^setenv uenvcmd run loadkernel \\&\\& bootm \\\$kernel_comp_addr_r$' "$WORK/boot.cmd.orig" &&
    grep -q '^run uenvcmd$' "$WORK/boot.cmd.orig" ||
        { echo "Error: unexpected boot.scr layout, refusing to rewrite it:"; cat "$WORK/boot.cmd.orig"; exit 1; }

    # Insert the dtb load, boot with the external dtb, and keep the stock boot as fallback.
    awk '
        { print }
        /^setenv loadkernel fatload mmc/ { print "setenv loaddtb fatload mmc \\$mmc_bootdev \\$fdt_addr_r dtb" }
    ' "$WORK/boot.cmd.orig" |
    sed -e 's|^setenv uenvcmd run loadkernel \\&\\& bootm \\\$kernel_comp_addr_r$|setenv uenvcmd run loadkernel \\\&\\\& run loaddtb \\\&\\\& bootm \\$kernel_comp_addr_r - \\$fdt_addr_r\
setenv uenvfallback run loadkernel \\\&\\\& bootm \\$kernel_comp_addr_r|' \
        -e 's|^run uenvcmd$|run uenvcmd\
run uenvfallback|' > "$WORK/boot.cmd"
    echo "--- new boot.cmd ---"; cat "$WORK/boot.cmd"
    mkimage -A arm64 -O linux -T script -C none -n "OpenWrt boot (external dtb, i2c0 on)" \
        -d "$WORK/boot.cmd" "$WORK/boot.scr" >/dev/null
fi

echo "--- Writing boot partition ---"
upload "$WORK/dtb" /mnt/boot/dtb.new
remote 'mv /mnt/boot/dtb.new /mnt/boot/dtb'
if [ "$LAYOUT" = fit ]; then
    upload "$WORK/boot.scr" /mnt/boot/boot.scr.new
    remote 'mv /mnt/boot/boot.scr.new /mnt/boot/boot.scr'
fi
remote 'sync; ls -l /mnt/boot; umount /mnt/boot'

if [ "$NO_REBOOT" = 1 ]; then
    echo "Skipping reboot (-n). Reboot the board, then check: ls -l /dev/i2c-0; i2cdetect -y 0"
    exit 0
fi

# ------------------------------------------------------------------ 3. reboot
echo "--- Rebooting ---"
remote 'reboot' || true
sleep 15
for i in $(seq 1 24); do
    if remote 'true' 2>/dev/null; then break; fi
    sleep 5
    [ "$i" = 24 ] && { echo "Error: board did not come back within 2 minutes"; exit 1; }
done

echo "--- Verifying ---"
remote 'echo "i2c0 status: $(tr -d "\0" < /sys/firmware/devicetree/base/soc/i2c@1c2ac00/status)";
        ls -l /dev/i2c-0 || { echo "ERROR: /dev/i2c-0 missing (booted with the fallback command?)"; exit 1; };
        echo "--- i2cdetect -y 0 (expect 3c) ---"; i2cdetect -y 0;
        echo "--- gpiodetect ---"; gpiodetect'
CHIP=$(remote 'gpiodetect | sed -nE "s/^(gpiochip[0-9]+) \[1c20800\.pinctrl\].*/\1/p"' | tr -d '\r')
if [ -n "$CHIP" ]; then
    echo "Buttons (PA0/PA2/PA3) are on /dev/$CHIP -> build with GPIOCHIP_PATH=/dev/$CHIP"
else
    echo "WARNING: could not identify the 1c20800.pinctrl gpiochip; run gpiodetect on the board"
fi
echo "Board setup complete. Next: ./deploy.sh -t $TARGET_IP"
