#!/bin/bash
set -e

# Target Makefile path
# In the SDK environment, after feeds install, it is usually linked in package/feeds/packages/libgpiod
# But the original file is in feeds/packages/libs/libgpiod
# We should patch the original file in feeds/ directory to be safe.

TARGET="feeds/packages/libs/libgpiod/Makefile"

if [ ! -f "$TARGET" ]; then
    echo "Error: $TARGET not found. Did you run './scripts/feeds install libgpiod'?"
    exit 1
fi

echo ">>> Patching $TARGET to remove Python dependencies..."

# 1. Comment out the include of python3-package.mk
sed -i 's|^include ../../lang/python/python3-package.mk|# include ../../lang/python/python3-package.mk|' "$TARGET"

# 2. Comment out any line invoking Py3Build (Configure, Compile, Install)
sed -i '/call Py3Build/s/^/#/' "$TARGET"

# 3. Comment out the final build calls for python packages
sed -i '/call Py3Package/s/^/#/' "$TARGET"
sed -i '/call BuildPackage,python3-gpiod/s/^/#/' "$TARGET"

# 4. Verification
if grep -q "^# include .*python3-package.mk" "$TARGET"; then
    echo ">>> Success: Python dependency include commented out."
else
    echo ">>> Warning: Python dependency include NOT commented out?"
fi

echo ">>> libgpiod is now Python-free."
