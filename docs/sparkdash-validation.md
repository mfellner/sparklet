# sparkDash validation status

Updated 2026-09-06. The v1 table below records the original release; automatic-rotation results follow it. Sparklet 1.0.0 completed the bounded v1 acceptance checks below. The 24-hour soak was explicitly excluded and was not performed.

| Gate | Evidence / status |
| --- | --- |
| Exact board wiring | Schematic J4 and active vendor constructor agree: LCD CS15, touch INT5/reset11; ALDO3 reset. |
| Factory backup | Complete 16 MiB read, SHA-256 and identity metadata recorded in bring-up notes. Full factory restore booted successfully, followed by a verified return to the saved sparkDash image/configuration. |
| Pinned SDK/dependencies | ESP-IDF 5.5.3 builds succeeded; managed dependency lock committed. Reproducible-build mode produced identical app/bootloader/partition binaries in two build directories. |
| Shared core tests | ASan/UBSan tests passed for parser, normalization, configuration, formatting, list reconciliation and scheduler. |
| Controlled host HTTP | Five unittest groups passed: normal/chunked, list variations, status errors, interrupted/oversized/malformed/stalled responses. Does not validate IDF transport. |
| Display / touch | User confirmed upright readable screen and Settings/Back touch; supplied photos. A 150-second untouched run verified the default 120-second dim timer, 10% brightness command and continued polling. User subsequently confirmed five-node arrows/swipes, Details/Back, corrected dash and first-touch-only wake. Exact optical latency/calibration is not claimed. |
| QR setup | Two-step Wi-Fi/URL QR build compiled, flashed and booted. iPhone photos confirm decoding and successful Wi-Fi join. |
| Startup / live sampled memory | Startup: 53,872 B free internal heap, 38,912 B largest block. Subsequent 60-second live check: minimum sampled heap 68,968 B, largest block 50,176 B; spare stacks diagnostics 1,800 B, network 2,988 B, UI 13,740 B. Exact-16-KiB response and fault runs also passed; the QA run reported minimum-ever internal heap 52,456 B. The bounded portal test passed: minimum-ever heap 33,856 B, minimum sampled largest block 23,552 B, portal stack 3,420 B and diagnostics stack 1,392 B. All 20 setup checks passed, including failed association, wrong-password retention and saving valid Wi-Fi without waiting for a metrics response; live polling resumed afterward. |
| Phone provisioning | QR-assisted join succeeded. Portal incorrectly rejected IPv4-mapped IPv6 local addresses; corrected and host regression-tested. Subsequent phone screenshot confirms saved Wi-Fi and successful connection. |
| Live five-node display | Captured deployed responses for dgx01/dgx02/dgx03/dgx04/gx10 replayed through the actual ESP32 HTTP client: all 80 numeric/validity fields, roles, online states and node order matched an independent reference. Frozen responses avoid comparing different collector samples; user photos separately show live Overview/Details rendering. |
| Recovery / HTTP transport | Real-device synthetic-server tests passed normal/chunked and exact 16 KiB bodies, 401/403/404/429/500, invalid/oversized/interrupted data, slow headers, stalled and trickling bodies, cache retention, empty/17-node lists, and device-only Wi-Fi reconnect. Recovery observed 2.1–3.4 seconds after restoring healthy responses; Wi-Fi reconnect about 2.5 seconds. Additional prolonged-outage and synthetic-server restart test passed (recovery 30.88 s); repeated failure intervals showed backoff, and a real 60-second list refresh preserved selected node2 while its index changed 1 → 3. Twenty cached-selection-to-completed-panel-transfer samples passed with maximum 157 ms. Four real user taps measured 5, 19, 4 and 4 ms from sampled input to completed SPI transfer. Adding one 33 ms input-poll period gives a 52 ms upper measured bound, below the 150 ms target. Optical scan-out was not measured. |
| 24-hour soak | Explicitly excluded by the user from v1 completion; not performed. |
| Candidate bundle / restore gate | Candidate 3376e53 (`sparkdash-v1-rc1`) passed ZIP integrity, checksums, flash-reference checks and a flash from the extracted bundle. Its subsequent runtime check failed: bootloader output ended at application handoff and no STATUS samples arrived. A later bootloader connection attempt received no serial data. Physical reconnection subsequently restored successful boot and a 60-second five-node/zero-error run; the cause of the USB incident was not isolated. The subsequent normal 1.0.0 image passed a 60-second five-node run with zero errors. Its application SHA-256 is `b5be4bf71b18a93b83c358aea4081431caffa76296a8899e6ff7731d876f17ee`; an independent build directory produced identical binaries. QA controls are excluded from the normal release. Full-image recovery and final physical timing passed. |

Raw boot logs and backups remain ignored. Startup memory alone does not establish worst-case memory acceptance. The user excluded the 24-hour soak from v1 completion. Bounded controlled-failure, navigation, stack and memory measurements passed; no long-duration reliability claim is made. The earlier USB incident recovered after physical reconnection, but its cause was not isolated. This remains a documented limitation rather than an assertion of trouble-free long-term operation.


## Automatic rotation — source update, 2026-09-06

This update adds default-on, four-angle automatic rotation with a saved Settings
switch. It is a source update, not a replacement of the published v1.0.0 bundle.

| Gate | Evidence / status |
| --- | --- |
| Backup and sensor | Full 16,777,216-byte pre-update image checksummed outside Git. QMI8658 identity verified before configuration; upright readings approximately (-26, 951, -138) mg. Exact-board address/register provenance and backup hash are in the bring-up notes. |
| Host and builds | ASan/UBSan core, portal-address and rotation suites passed. Rotation tests cover explicit v1/v2 preference decoding, settling/jitter/invalid samples, known RGB565 pixel expectations, all 480×480 inverse touch points at all angles, and aligned stripe rectangles. Normal and QA builds pass under ESP-IDF 5.5.3; new Python validators pass Ruff. |
| Physical orientation / touch | User confirmed readable output and aligned Settings/Back and navigation arrows on both sides and upside down. Two captured button samples took 6 ms each to completed SPI transfer, or 39 ms including one input-poll interval. Optical scan-out is unmeasured. |
| Settings / persistence | Actual QA UI callbacks verified unsaved controls and activity timestamp survive rotation, Back discards the switch, and Save commits either value without changing brightness/dim delay. Saved-off survived an observed reboot with orientation 0; saving on restored automatic rotation to 90 with the board sideways. |
| Navigation at all angles | 80/80 cached-selection-to-completed-transfer samples passed the 250 ms limit. Maxima: 162 ms at 0°, 166 ms at 90°, 171 ms at 180° and 270°. Tests explicitly await the next accepted selection sequence. |
| Live and setup memory | Initial 60-second QA run passed: five nodes, stable zero errors, minimum sampled heap 63,168 B, largest block 44,032 B; minimum spare diagnostic/network/UI stacks 2,364 / 2,944 / 12,780 B. All 20 portal checks passed and live polling resumed; minimum-ever heap 34,128 B, minimum sampled largest block 24,064 B, spare portal/diagnostic stacks 3,400 / 1,308 B. |

The first new QA Settings harness overflowed the diagnostics stack while creating
widgets. Moving its UI work onto the existing LVGL task resolved it; both settings
retests passed. That failed run remains recorded separately, not counted as a pass.

Manual checks not separately reported for this update: sideways sliders/Details
scrolling, phone QR decoding, holding a touch through rotation, and first-touch
wake after rotation. Sensor-disconnect fault injection was not performed. These
limits are distinct from the successful four-orientation/button check and the
programmatic tests above. The 24-hour soak remains explicitly excluded.


Final normal-image check: generated-argument flash verified successfully, and the
150-second run passed (29 samples, 27 live samples, five nodes, zero errors).
Saved-on rotation survived flashing/reboot and remained at 90° with the device
sideways. At the saved 120-second timeout it dimmed to 10% while polling continued.
QA controls/raw sensor logging are absent from the normal binary. Application
SHA-256: `66c1e44bda5136b8ea7068f5e78e87c857d094abf5d9b8ce3bbdf392233e1525`.
Minimum sampled normal heap/largest block: 66,432 / 47,104 bytes; diagnostic/network/UI stack spare: 3,252 / 2,968 / 13,740 bytes.
