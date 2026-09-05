# sparkDash companion

Read-only native dashboard for the Waveshare ESP32-C6-Touch-AMOLED-2.16. Firmware is under physical validation; it is not yet a fully validated release. The latest candidate has an unresolved post-flash USB response failure; see the [current validation report](../../docs/sparkdash-validation.md).

The [complete documentation](../../docs/sparkdash/README.md) covers [daily use](../../docs/sparkdash/user-guide.md), [architecture/API](../../docs/sparkdash/architecture.md), [development](../../docs/sparkdash/development.md), [testing](../../docs/sparkdash/testing.md), and [release/troubleshooting](../../docs/sparkdash/operations.md). This README is a quick command reference.

## Set up

1. Scan **1. Join setup Wi-Fi** with your phone camera and accept joining `SparkDash-XXXX`.
2. Stay connected if the phone reports no Internet. Tap **Next: setup page** on the ESP32.
3. Scan the second code, or open `http://192.168.4.1` in the phone browser.
4. Select your personal 2.4 GHz Wi-Fi, enter its password in the portal, and save. Default server: `http://dgx01.local:5555`.

Both codes are generated locally. No credentials go to an external QR service. Manual setup details remain visible. The setup password changes between sessions/reboots; if joining fails, forget the phone's saved SparkDash network and scan the current code again. QR scanning does not itself repair a radio/association failure.

Swipe left/right or tap arrows to select a node. Details scroll vertically. Settings controls brightness, dimming, and connection reconfiguration. The first touch after dimming wakes the screen without activating a control. Connection setup disables dimming so the QR remains readable.

The server is unchanged. This client uses only `/api/sparks` and `/api/sparks/{id}/metrics`, over HTTP. HTTPS, enterprise/open Wi-Fi, remote actions, history and OTA are not supported. “Received” measures response receipt age, not collector sample age.

## Build

Install ESP-IDF **v5.5.3** outside this repository using Espressif's install scripts, including its `esp32c6` toolchain. The local verified installation is `/Users/max/esp/esp-idf-v5.5.3`.

```sh
. /Users/max/esp/esp-idf-v5.5.3/export.sh
cd firmware/sparkdash
idf.py build
```

The build enables `CONFIG_APP_REPRODUCIBLE_BUILD` to remove time/date/path variability; application, bootloader and partition binaries were byte-identical across separate build directories. Keep `dependencies.lock` committed; dependency changes require separate review. `sdkconfig.defaults` supplies clean-build settings; generated `sdkconfig`, `managed_components` and `build` are ignored. Source provenance and the corrected schematic wiring are in `PROVENANCE.md` and `../../notes/2026-09-05-firmware-bringup.md`.

Rendering uses one 480 × 24 RGB565 buffer, one software draw unit, partial updates, and a bounded 64 KiB LVGL heap. The smaller stripe is the planned memory fallback after the initial 48-row build missed the memory gates with diagnostics enabled.

## Flash and recover

Read `../../docs/interaction.md` before hardware operations. Enumerate from the repository root:

```sh
uv run scripts/esp32_serial.py list
```

Select USB serial `D4:05:92:B9:04:28`, VID/PID `303a:1001`. Close monitors first. A verified full factory backup already exists in ignored `backups/`, with checksum/metadata recorded in the bring-up notes. Do not overwrite it.

After activating the SDK, flash from this firmware directory using the freshly discovered port:

```sh
idf.py -p /dev/cu.usbmodem2101 flash
```

This uses the project's generated partition/flash arguments and preserves NVS. Capture boot logs with the bounded root helper; opening USB can reset the device. No OTA partitions or filesystem are present. Forgetting credentials requires on-device confirmation.

Factory restoration is a full-flash write of the verified original backup at address zero, using the pinned esptool 5.4.0 and the discovered device. Verify its SHA-256 and exact size before any restore. A full factory restore and a return to the saved sparkDash snapshot were physically verified on 2026-09-05; see `../../docs/recovery.md`. A restore overwrites current settings. Never force protection overrides or change eFuses.

## Test

From the repository root, with ESP-IDF activated:

```sh
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build
ctest --test-dir tests/host/build --output-on-failure
python3 tests/host/test_http_fixtures.py
```

Shared core tests use ASan/UBSan by default. The HTTP fixture tests exercise a real local HTTP server and the shared parser, not the ESP-IDF HTTP transport. To expose synthetic test cases to the board on a controlled LAN:

```sh
python3 tools/mock_sparkdash.py --host 0.0.0.0 --port 5556
```

Use the host's LAN IPv4 address and a scenario prefix such as `/chunked`, `/stall`, `/oversized`, or `/rate` as the configured server base path. Do not disrupt production DGX services or the router for failure testing.

USB diagnostics accept newline-terminated `STATUS`, `NEXT`, and `PREV`. They expose counters and memory, not credentials, and are not a shell. Each physical, transport, live-data and performance check has its own evidence requirements; see the validation report for completed and pending gates. The user explicitly excluded the 24-hour soak; this release makes no 24-hour stability claim. See the bring-up notes for tested facts.

## Create a distributable bundle

From a clean, committed checkout with ESP-IDF activated:

```sh
python3 tools/package_release.py releases/sparkdash-v1-candidate
```

The tool builds first, verifies the pinned SDK/target, copies generated flash arguments and binaries, includes the dependency lock and exact build configuration, and emits revision/compiler metadata plus SHA-256 checksums. It also creates a ZIP with an external checksum. The standalone bundle README/FLASH.txt and bundled USB helper work from the extracted directory; SOURCE_README.md retains repository-relative development instructions. It refuses an existing destination or a dirty checkout. Factory backups, credentials, NVS data and raw logs are excluded. Follow `FLASH.txt` inside the bundle; its flash arguments are relative to that directory. Packaging does not itself pass hardware acceptance gates.

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

Flash that project's generated arguments to the discovered board, then run from the root with this Mac's LAN IPv4:

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
