# sparkDash companion

Read-only native dashboard for the Waveshare ESP32-C6-Touch-AMOLED-2.16. Version 1.1.0 runs as the `sparklet` app (OTA slot `ota_0` at `0x220000`) of the [esp32-playground platform](https://github.com/mfellner/esp32-playground), next to its launcher; 1.0.0 was the standalone v1 image. Bounded device and host validation are recorded in the [current validation report](../../docs/sparkdash-validation.md).

The [complete documentation](../../docs/sparkdash/README.md) covers [daily use](../../docs/sparkdash/user-guide.md), [architecture/API](../../docs/sparkdash/architecture.md), [development](../../docs/sparkdash/development.md), [testing](../../docs/sparkdash/testing.md), and [release/troubleshooting](../../docs/sparkdash/operations.md). This README is a quick command reference.

## Set up

1. Scan **1. Join setup Wi-Fi** with your phone camera and accept joining `SparkDash-XXXX`.
2. Stay connected if the phone reports no Internet. Tap **Next: setup page** on the ESP32.
3. Scan the second code, or open `http://192.168.4.1` in the phone browser.
4. Select your personal 2.4 GHz Wi-Fi, enter its password in the portal, and save. Default server: `http://dgx01.local:5555`.

Both codes are generated locally. No credentials go to an external QR service. Manual setup details remain visible. The setup password changes between sessions/reboots; if joining fails, forget the phone's saved SparkDash network and scan the current code again. QR scanning does not itself repair a radio/association failure.

Swipe left/right or tap arrows to select a node. Details scroll vertically. Settings controls brightness, dimming, automatic rotation, and connection reconfiguration; **Apps** opens the launcher. KEY (short press) and BOOT (1 s hold) also open the launcher, and a PWR short press dims or wakes the screen. Auto-rotate defaults on; Save display persists the switch, and disabling it restores upright. The first touch after dimming wakes the screen without activating a control. Connection setup disables dimming so the QR remains readable.

The server is unchanged. This client uses only `/api/sparks` and `/api/sparks/{id}/metrics`, over HTTP. HTTPS, enterprise/open Wi-Fi, remote actions, history and over-the-air updates are not supported. “Received” measures response receipt age, not collector sample age.

## Build

Install ESP-IDF **v5.5.3** outside this repository using Espressif's install scripts, including its `esp32c6` toolchain. The local verified installation is `/Users/max/esp/esp-idf-v5.5.3`.

```sh
. /Users/max/esp/esp-idf-v5.5.3/export.sh
cd firmware/sparkdash
idf.py build
```

`CMakeLists.txt` applies the platform contract `sdkconfig.defaults.platform` before `sdkconfig.defaults` and calls `platform_app_slot(sparklet)` from the platform's `mfellner/app_switch` component. Configuration fails unless `partitions.csv` and the platform settings match the platform's `components/app_switch/layout/` exactly; copy those files unchanged. Board support comes from the platform's `mfellner/board` component. For local component work `main/idf_component.yml` may use `override_path`; commits must use the pinned git version.

The build enables `CONFIG_APP_REPRODUCIBLE_BUILD` to remove time/date/path variability; application, bootloader and partition binaries were byte-identical across separate build directories. Keep `dependencies.lock` committed; dependency changes require separate review. `sdkconfig.defaults` supplies clean-build settings; generated `sdkconfig`, `managed_components` and `build` are ignored. Source provenance and the corrected schematic wiring are in `PROVENANCE.md` and `../../notes/2026-09-05-firmware-bringup.md`; board-generic notes are in the [platform repository](https://github.com/mfellner/esp32-playground).

Rendering uses a 480 × 12 RGB565 draw buffer and an equally sized DMA rotation buffer (23,040 bytes total), one software draw unit, partial updates, and a bounded 64 KiB LVGL heap. This preserves the former 24-row buffer budget without a full-screen framebuffer.

## Flash and recover

Read the platform's [interaction instructions](https://github.com/mfellner/esp32-playground/blob/main/docs/interaction.md) before hardware operations. Enumerate from the repository root:

```sh
uv run scripts/esp32_serial.py list
```

Select USB serial `D4:05:92:B9:04:28`, VID/PID `303a:1001`. Close monitors first. A verified full factory backup already exists in ignored `backups/`, with checksum/metadata recorded in the bring-up notes. Do not overwrite it.

The device must already use the platform layout. A device still on the single-app layout (factory image or Sparklet 1.0.0) needs the one-time migration from the [platform repository](https://github.com/mfellner/esp32-playground#build-and-install) first. After activating the SDK, flash from this firmware directory using the freshly discovered port:

```sh
idf.py -p /dev/cu.usbmodem2101 sparklet-flash
```

This writes only `build/sparklet-flash_args` (`0x220000 sparkdash.bin`) into the `sparklet` slot; NVS, the launcher, the bootloader and the boot selection are preserved. Alternatively, from the repository root with a platform checkout at `PLATFORM`, `uv run PLATFORM/tools/device.py install sparklet firmware/sparkdash/build` also checks the device's partition table first. Never use `idf.py flash` or `app-flash`: they would overwrite the launcher and reset the boot selection, so this project makes them fail with a guard message. Capture boot logs with the bounded root helper; opening USB can reset the device. Forgetting credentials requires on-device confirmation and erases only Sparklet's `connection` key, because NVS is shared with the launcher and other apps.

Factory restoration is a full-flash write of the verified original backup at address zero, using the pinned esptool 5.4.0 and the discovered device. It also removes the launcher and platform layout. Verify its SHA-256 and exact size before any restore. A full factory restore and a return to the saved sparkDash snapshot were physically verified on 2026-09-05; see the platform's [recovery procedure](https://github.com/mfellner/esp32-playground/blob/main/docs/recovery.md). A restore overwrites current settings. Never force protection overrides or change eFuses.

## Test

From the repository root, with ESP-IDF activated:

```sh
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build
ctest --test-dir tests/host/build --output-on-failure
python3 tests/host/test_http_fixtures.py
```

Shared core tests (`core_tests`, `portal_address_tests`, `preferences_tests`) use ASan/UBSan by default; orientation tests moved to the platform repository with the board component. The HTTP fixture tests exercise a real local HTTP server and the shared parser, not the ESP-IDF HTTP transport. To expose synthetic test cases to the board on a controlled LAN:

```sh
python3 tools/mock_sparkdash.py --host 0.0.0.0 --port 5556
```

Use the host's LAN IPv4 address and a scenario prefix such as `/chunked`, `/stall`, `/oversized`, or `/rate` as the configured server base path. Do not disrupt production DGX services or the router for failure testing.

USB diagnostics accept newline-terminated `STATUS`, `NEXT`, and `PREV`. QA builds add test commands, including `TEST_OPEN_LAUNCHER`. They expose counters and memory, not credentials, and are not a shell. Each physical, transport, live-data and performance check has its own evidence requirements; see the validation report for completed and pending gates. The user explicitly excluded the 24-hour soak; this release makes no 24-hour stability claim. See the bring-up notes for tested facts.

## Create a distributable bundle

From a clean, committed checkout with ESP-IDF activated:

```sh
python3 tools/package_release.py releases/sparklet-candidate
```

The tool builds first, verifies the pinned SDK/target and that `build/sparklet-flash_args` writes `sparkdash.bin` at `0x220000`, copies the application, its slot flash arguments, the generated partition table and `project_description.json`, includes the dependency lock and exact build configuration, and emits revision/compiler metadata plus SHA-256 checksums. It also creates a ZIP with an external checksum. The standalone bundle README/FLASH.txt and bundled USB helper work from the extracted directory; SOURCE_README.md retains repository-relative development instructions. It refuses an existing destination or a dirty checkout. Factory backups, credentials, NVS data and raw logs are excluded. Follow `FLASH.txt` inside the bundle: `device.py install sparklet` accepts the extracted directory as a build directory, and its flash arguments are relative to that directory. The bundle updates only the Sparklet slot of a migrated device. Packaging does not itself pass hardware acceptance gates.

For a bounded live USB measurement (opening may reboot the device):

```sh
uv run tools/check_device.py --seconds 60 --output logs/live-check.json
```

This verifies continued five-node polling, request-error stability, sampled internal heap/largest block, and UI/network/diagnostics task stack margins. It does not simulate touch or prove worst-case response memory. The JSON and adjacent raw log stay ignored.

## Controlled ESP32 HTTP validation

`tools/check_http_device.py` runs the real device against a synthetic server on this Mac. It requires a separate validation build with `CONFIG_SPARKDASH_TEST_COMMANDS=y` in an ignored `sdkconfig.qa`; the normal default is disabled. Activate the pinned SDK, copy the normal `sdkconfig` to `sdkconfig.qa`, enable that single test option, then use a separate build directory:

```sh
idf.py -C firmware/sparkdash -B firmware/sparkdash/build-qa -D SDKCONFIG="$PWD/firmware/sparkdash/sdkconfig.qa" build
```

Flash it to the discovered board's Sparklet slot:

```sh
idf.py -C firmware/sparkdash -B firmware/sparkdash/build-qa -D SDKCONFIG="$PWD/firmware/sparkdash/sdkconfig.qa" -p PORT sparklet-flash
```

Then run from the root with this Mac's LAN IPv4:

```sh
uv run tools/check_http_device.py --host MAC_LAN_IP --output logs/http-device.json
```

Test commands exist only in that build: `TEST_URL http://...` selects a volatile test server, `TEST_RESET` restores the saved URL, and `TEST_RECONNECT` disconnects/reconnects only this display's Wi-Fi. No command changes saved credentials or sends control requests to a DGX. The runner hosts a synthetic HTTP server on port 5556, checks normalized values and cached-state preservation, then restores the saved URL in its cleanup path. It records ignored JSON/raw evidence. Reinstall the normal build afterward. The release packager rejects a configuration with test commands enabled.

These diagnostics test network-worker/cache behavior; they do not claim to simulate physical touch or prove touch-to-photon latency.

Add `--extended` to the HTTP runner for a prolonged synthetic-server outage/restart and a live list reorder without clearing the cache. Use `--cases chunked --extended` to avoid repeating unrelated fault cases.

To compare the deployed API with the ESP32's normalized cache, use a validation build and:

```sh
uv run tools/check_http_device.py --host MAC_LAN_IP --live-source http://dgx01.local:5555 --output logs/live-api-comparison.json
```

This performs read-only GETs, freezes the five node responses in an ignored snapshot, and replays those exact values through the ESP32. It checks all 16 numeric/validity fields per node, roles, online states and ordering against an independent Python reference. Explicit source readback prevents comparisons against the old cache before switching servers. The saved server is restored in cleanup and the normal firmware must be reinstalled after validation.

To verify default inactivity dimming, leave the device untouched and run:

```sh
uv run tools/check_device.py --seconds 150 --expect-dim --output logs/idle-check.json
```

STATUS reports commanded brightness, dim state and configured timeout. The check verifies dimming after that timeout and continued polling at 10%; it does not simulate the physical wake touch. Use a longer bounded run if you have configured a longer timeout (the helper supports up to 300 seconds).
