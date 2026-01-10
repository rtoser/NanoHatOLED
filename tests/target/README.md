# Target Tests

## Smoke (from host via SSH)

```
./tests/target/run_smoke_ssh.sh
```

Environment overrides:

- `TARGET_IP` (default: 192.168.33.254)
- `TARGET_USER` (default: root)
- `BIN` (default: src/build/target/nanohat-oled)
- `REMOTE_BIN` (default: /tmp/nanohat-oled)
- `RUN_SECONDS` (default: 2)

## Manual UI (run on target)

```
./tests/target/run_ui_manual.sh
```

The script starts the app, then prompts for key presses and visual checks.

## Unit Tests (build + run on target via SSH)

```
./tests/target/run_unit_ssh.sh
```

Environment overrides:

- `TARGET_IP` (default: 192.168.33.254)
- `TARGET_USER` (default: root)
- `REMOTE_DIR` (default: /tmp/nanohat-tests)
- `DOCKER_IMAGE` (default: openwrt-sdk:sunxi-cortexa53-24.10.5)
- `BUILD_DIR` (default: tests/build/target)
- `SKIP_BUILD` (set to 1 to skip build and only upload/run)
