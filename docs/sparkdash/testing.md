# Testing and acceptance

[Documentation index](README.md)

## Evidence policy

A passing build proves compilation/linking. Host tests exercise shared code, not ESP-IDF transport or physical touch. USB navigation exercises the command/cache path, not the touchscreen. A brightness command establishes the requested brightness, not a calibrated optical measurement. Each test result must state its scope.

The [validation report](../sparkdash-validation.md) records current results. The earlier post-flash USB stall recovered after physical reconnection. The final v1 results, including that incident, are recorded in the report. The 24-hour soak is explicitly excluded from the goal and must not be silently reintroduced as a gate. Use the procedures below when changing the corresponding implementation; do not infer long-duration reliability from bounded checks.

## Host tests

Activate the pinned SDK, then run from the repository root:

```sh
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build
ctest --test-dir tests/host/build --output-on-failure
python3 tests/host/test_http_fixtures.py
```

CMake uses the SDK's cJSON source and the actual firmware core. Address and undefined-behavior sanitizers are enabled by default. If a host cannot support them, document that limitation rather than reporting an unsanitized run as equivalent.

| Suite | Coverage |
| --- | --- |
| `core_tests` | Roles/worker compatibility, optional numbers and valid zero, unified memory, root/interface/backend choice, URL/form and versioned-record validation, UTF-8 bounds, duplicate/oversized IDs, body/depth/arena limits, matching IDs, list reconciliation, formatting and scheduling |
| `portal_address_tests` | Actual setup-address guard, IPv4 and mapped IPv6, other/truncated addresses and a host dual-stack socket |
| `test_http_fixtures.py` | Local HTTP server response modes exercised with host transport/shared parser |

Host fixtures are synthetic. Keep real API captures in ignored files, not committed tests. For changed metric rules, adjust independently expected values; avoid assertions that merely repeat the implementation.

## Bounded live-device baseline

Read [interaction instructions](../interaction.md), discover the known board, close other monitors, and ensure the normal firmware is installed. Opening the USB connection can reset the device.

```sh
uv run scripts/esp32_serial.py list
uv run tools/check_device.py --seconds 60 --output logs/live-check-new.json
```

The helper selects USB identity `303a:1001` / `D4:05:92:B9:04:28`, captures raw output alongside JSON, sends STATUS every five seconds and closes the port. It refuses existing evidence paths and paths outside ignored `logs/` or `.local/`. Its healthy fixture expects the current five-node installation, at least two live samples, increasing request count and stable errors.

Acceptance thresholds checked in observed live samples:

| Measurement | Minimum |
| --- | ---: |
| Free internal heap | 32 KiB |
| Largest free internal block | 16 KiB |
| Spare diagnostics/network/UI task stack | 1 KiB each |

The separate portal validator additionally measured the setup HTTP task peak stack. The application's periodic health line adds minimum-ever internal heap, which helps identify transient pressure but is not a complete per-phase allocation trace.

For inactivity, leave the physical device untouched:

```sh
uv run tools/check_device.py --seconds 150 --expect-dim --output logs/dim-check-new.json
```

The check requires the configured timeout to have elapsed, at least two samples at dimmed/10% state, and continued polling. A 150-second run is suitable for the default 120-second timeout. The helper supports 30–300 seconds; a longer configured timeout needs a separately documented bounded observation method or a deliberately changed test preference. Do not claim this simulates first-touch wake.

## Controlled HTTP transport on the ESP32

Build/flash the isolated QA configuration described in [development](development.md). Use this Mac's LAN IPv4 address reachable from the display. The runner starts its own synthetic server on port 5556; do not start a second process on that port.

```sh
uv run tools/check_http_device.py --host MAC_LAN_IP --output logs/http-device-new.json
```

The firmware receives a volatile test URL over USB; its saved NVS URL/credentials are unchanged. The runner requires explicit source readback before comparing data and in cleanup when returning to the saved source. JSON, raw USB evidence and any captured production snapshot remain ignored. Always inspect failures and cleanup status; reinstall normal firmware after the run.

| Scenario | Expected behavior |
| --- | --- |
| Normal / chunked / exact 16 KiB | Accepted, normalized fields match fixture |
| 401 / 403 | Access denied; prior data retained |
| 404 | Error plus required discovery refresh |
| 429 | Bounded Retry-After; recovery after rate limiting ends |
| 500 / interrupted connection | Error without partial cache replacement |
| Malformed / oversized | Rejected; prior valid data retained |
| Slow headers / stalled body / trickle | Operation/overall deadline ends request; healthy service recovers |
| Empty / 17-node list | Empty state or first 16 plus overflow warning |
| Device Wi-Fi reconnect | Only the display disconnects; saved configuration survives |

The five-second overall deadline is measured inside firmware. Host-observed time includes scheduling, scenario switching and USB sampling; report both scope and tolerance rather than treating host elapsed time as an exact network deadline.

To extend the test to repeated failures, a stopped/restarted synthetic service, and a normal 60-second list refresh that reorders an existing selection:

```sh
uv run tools/check_http_device.py --host MAC_LAN_IP --cases chunked --extended --output logs/http-extended-new.json
```

This tests only the controlled server/display. Never restart production DGX services or the production router to manufacture an outage.

For manual fixture work, the standalone mock server is available separately:

```sh
python3 tools/mock_sparkdash.py --host 0.0.0.0 --port 5556 --scenario normal
```

Binding all interfaces makes it reachable on the LAN. Use it on the intended controlled network, and stop it when finished. A scenario can also be selected by a base-path prefix such as `/chunked` or `/oversized`. Inspect the tool's `--help` before using optional control-file behavior.

## Live API comparison

With QA firmware installed:

```sh
uv run tools/check_http_device.py --host MAC_LAN_IP --live-source http://dgx01.local:5555 --output logs/api-comparison-new.json
```

The runner performs read-only GETs for discovery and five metrics responses, freezes them, and replays those exact samples through the actual ESP32 HTTP/parser/cache path. It compares 16 scalar/validity fields per node, role, reported online state and ordering against independent Python normalization. Eighty scalar comparisons passed previously. Freezing avoids false mismatches from changing live collector samples.

This establishes normalization and transport for the captured shapes. Physical rendering is separately supported by user photos and must be rechecked after UI changes. It does not prove collector correctness or sample freshness.

## Hands-on acceptance checklist

Record the application revision, date, observer, action and actual outcome for each item. Mark unperformed checks pending rather than inferring success from nearby results.

| Area | Procedure and required outcome |
| --- | --- |
| Display | Check orientation, colors, safe margins and readable punctuation, including the server's em dash; no missing rectangle for supported substitutions |
| Touch | Tap center and controls near corners; touch coordinates match visible targets |
| Navigation | Swipe both directions and use both arrows across all five nodes; wrap at ends; no button gesture also changes nodes |
| Details | Scroll full name, GPU/CPU/storage/network/backend values; Back preserves selection; vertical scroll never changes node |
| Roles / missing data | Worker omits local throughput; valid zero differs from missing; offline remains distinguishable |
| Settings | Brightness preview and Save work; saved preference survives reboot; Settings also dims |
| Wake | Wait for dimming; first touch restores brightness without activating a button or changing node; second touch works normally |
| Fresh setup | Phone joins current QR, opens URL QR, scans/manual entry, saves credentials and reaches live data |
| Wrong credentials | On a controlled network, failed association leaves setup available and previous saved record intact |
| Reconfigure / cancel | Start candidate setup, cancel before success, reconnect with previous saved configuration |
| Unavailable server | Candidate joins Wi-Fi while test server is unavailable; saved connection enters retry and recovers when service starts |
| Persistence | Reboot and normal update retain Wi-Fi, server and display preferences |
| Resolution | `.local` and direct IPv4 both work on the actual display |
| Setup resources | Measure portal handler stack/peak heap under scan, status and maximum supported submission; same memory margins hold |

## Performance and final completion

Normal touch-to-visible feedback target is 150 ms; cached node changes target 250 ms. Healthy foreground polling is approximately every two seconds. Network requests/timeouts must not freeze interaction; recovery must occur within 45 seconds of restored connectivity.

Measure physical feedback with a timestamped/high-frame-rate recording or suitable device instrumentation that captures the input and actual visible update. A USB command timestamp or LVGL render-submission event alone is not touch-to-photon evidence. Do not claim these thresholds passed from the 100 ms UI timer or 33 ms refresh configuration alone.

For a new release, repeat the affected acceptance gates:

1. Verify boot, saved connection and live polling on the final image; investigate any reset or USB/runtime failure.
2. Complete the physical, provisioning, timing and portal-resource checks affected by the changes.
3. Re-run checks affected by any resulting code change; do not repeat unrelated passing suites without a reason.
4. Verify the final bundle's checksums, source/SDK/lock metadata, generated flash references and normal-build configuration.
5. Update the validation report with exact evidence, remaining limitations and the explicit soak exclusion.

Full factory restore followed by return to a saved custom image has already been physically exercised. See [recovery](../recovery.md); repeat only if a changed recovery-relevant artifact or failure warrants it.

## Portal and display timing validators

The isolated QA build also accepts `TEST_PORTAL` and `TEST_NAV`; normal release binaries exclude them. No test controls are served over HTTP on the normal LAN.

```sh
uv run tools/check_portal_device.py --output logs/portal-check-new.json
uv run tools/check_ui_device.py --seconds 150 --output logs/ui-timing-new.json
```

The portal validator calls the actual device HTTP server through its own setup address, exercises asynchronous scan and invalid/maximum submissions, waits for a deliberately nonexistent SSID to fail, verifies saved NVS bytes are unchanged, cancels and verifies live reconnection. It borrows the idle metrics response buffer while a test lease suspends polling; cancellation cannot cause simultaneous buffer use. The test adds a client within the firmware, so its measured pressure is conservative relative to a phone client. It does not simulate phone radio authentication or claim to do so. Raw data and credentials are not emitted by the test protocol.

The UI validator automatically exercises 20 next/previous selections and requires each to reach completed SPI transfer within 250 ms. During the remaining window, physically tap the navigation or Settings/Back buttons. It requires actual button samples; `--touch-only` captures these without repeating navigation. The touch gate adds one configured 33 ms input polling period to sampled-input-to-panel-transfer duration and compares with 150 ms. A queue-drain operation completes pending SPI writes without changing any panel register. Optical scan-out is not instrumented; user confirmation of visible operation remains separate. Missing physical samples are a failed/incomplete capture, never a pass.

## Automatic rotation

`rotation_tests` runs with the host suites above. It covers four-angle RGB565
transforms, inverse touch coordinates, even panel rectangles, settled versus
ambiguous acceleration, sample interruptions, and v1/v2 preference records.

With the isolated QA firmware installed, run:

```sh
uv run tools/check_rotation_device.py --output logs/rotation-timing-new.json
```

This USB-only test forces each angle and measures 20 cached navigation renders per
angle, requiring all 80 completed transfers within 250 ms. It retains the saved
auto-rotate preference and restores the starting angle afterward. It does not
simulate accelerometer motion or physical touch. Opening USB can reboot the device.
The earlier navigation test now waits for the *next* accepted selection sequence,
so a previous render cannot accidentally satisfy the current sample.

Physically check both sides and upside down, Settings/Back and arrows, horizontal
swipes, Details scrolling, brightness/dim sliders, setup QR codes, and first-touch
wake. Check Back discards an unsaved switch, Save applies it, disabling restores
upright, and both setting values persist across reboot. Hold a finger down while
turning: rotation must wait until release. Leave the device flat/diagonal to check
it holds its previous orientation. Reinstall normal firmware afterward; the
`TEST_ROTATION` command and raw accelerometer logs must not ship in that image.

The QA Settings test uses the actual on-device widgets and Save callback. It checks
unsaved controls survive rotation, Back leaves the committed preference unchanged,
and Save publishes a successful acknowledgement without changing brightness or dim
delay. It intentionally changes the saved toggle. Check both values across reboot:

```sh
uv run tools/check_preferences_device.py --save off --output logs/preferences-off-new.json
uv run tools/check_preferences_device.py --expect-boot off --save on --output logs/preferences-on-new.json
```

The second run requires an observed boot and saved-off readback before saving on.
After flashing normal firmware, verify STATUS reports `auto_rotate=1`. This is
firmware/UI-event and NVS validation, not a substitute for physical touch testing.
`TEST_PREFS_ON` and `TEST_PREFS_OFF` exist only in QA builds.
