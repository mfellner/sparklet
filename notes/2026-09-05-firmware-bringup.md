# sparkDash firmware bring-up

## Backup and identity

On 2026-09-05, esptool 5.4.0 confirmed the attached ESP32-C6 rev v0.2 (USB serial D4:05:92:B9:04:28) has 16 MB flash, manufacturer 0x20, device 0x4018.

A full 16,777,216-byte backup was saved outside Git to `backups/factory-2026-09-05-1040.bin`, with metadata in the adjacent JSON file. SHA-256: `c1ede195afd99779507c8fe27d224ebb73fe63bbf8668e8aeb160d7f3a8157ea`.

## Schematic correction

Page 1 J4 agrees with the vendor DisplayPort constructor: GPIO15 connects the display CS position; GPIO5 connects touch INT; GPIO11 is touch reset. Clock/data are 0/1/2/3/4. LCD_RESET connects through R16 to ALDO3. The configuration macros in the same vendor example reverse CS and INT; they are not used by its constructor call. Implementation follows the schematic and constructor. This supersedes the preliminary feasibility note's source-derived pin table.

## Build foundation

ESP-IDF v5.5.3 is installed outside Git at `/Users/max/esp/esp-idf-v5.5.3`. Dependencies are locked by the component manager. Board-independent core tests run under ASan and UBSan. Network transfer rates from sparkDash's SystemCollector are bytes/second; firmware formats these as B/s, KiB/s or MiB/s.

Hardware and release validation results will be appended as they are observed. A successful compilation is not physical display/touch validation.

## First physical bring-up

The first application boot exposed a SPI concurrency assertion: initial brightness was sent from the main task after LVGL started drawing. Moving initial brightness before the adapter starts resolved it. The next flash booted successfully, initialized the 480 × 480 touch controller, and started the protected setup AP at 192.168.4.1. Initial internal heap was 35,604 bytes with a 20,480-byte largest block; the Settings photo subsequently showed 44 KiB free. These are setup measurements, not a network-load acceptance result.

The user confirmed readable upright text and working Settings/Back touch. Photos confirm the setup and Settings layout. The phone could not join the setup Wi-Fi, so provisioning and live-node validation remain pending. A two-step QR setup flow was added to remove manual password entry; its physical scanning result must be recorded separately. Wi-Fi association, DHCP, and disconnect-reason logs contain no credentials.

The QR/USB-diagnostics build booted without a crash, but startup internal heap was 30,672 bytes and largest block 15,872 bytes, below the 32/16 KiB gates. Applied the planned fallback to a single 480 × 24 RGB565 stripe (23,040 bytes); both SPI transfer limit and adapter buffer height changed together. QR uses a 256 × 256 one-bit canvas inside the existing 64 KiB LVGL heap, with a white quiet zone. This does not add a full-screen framebuffer.

The 24-row QR build was flashed using generated IDF arguments and booted successfully. Startup internal heap: 53,872 bytes; largest block: 38,912 bytes; network stack high-water mark: 5,220 bytes. These pass the startup memory gates. No crash or allocation error appeared during bounded boot capture. Phone QR decoding, association, and portal completion are awaiting the user's physical check; memory under live HTTP and portal load remains unverified.

## Phone QR verification and portal correction

The user's next photos confirm that the iPhone camera recognizes the Wi-Fi QR payload and the phone joins SparkDash-0428. The expected “No Internet Connection” label is visible. Safari reaches 192.168.4.1 but receives the application's “Use the setup Wi-Fi” rejection, so the portal flow was not yet successful.

Source inspection found the cause: ESP-IDF's HTTP server creates an IPv6 listener when IPv6 is enabled, and lwIP converts IPv4 local socket addresses into IPv4-mapped IPv6 addresses. The original guard supplied a sockaddr_in-sized buffer and compared its IPv4 field regardless of family. The fix uses sockaddr_storage and accepts either plain IPv4 or IPv4-mapped IPv6 only when the actual local destination is the setup AP address. It does not trust the Host header or remove AP-only restrictions.

ASan/UBSan host regression tests pass for both valid forms, other local addresses, native IPv6, truncated address structures, and a real dual-stack socket connection. The phone must reload the portal after the updated firmware is installed to verify the complete flow.

The portal correction was flashed successfully and completed its bounded boot check: setup AP and DHCP started; free internal heap 53,856 bytes and largest block 38,912 bytes; no crash in capture. The current setup session is left running for the phone retry.

## Successful setup and live UI

The user's next photos show the phone portal reporting saved Wi-Fi and connected status, followed by live Overview cards for dgx01, dgx02, and gx10, with a total of five discovered nodes. Head/worker presentation and navigation positions 1/5, 2/5 and 5/5 are visible. The Details view displays GPU, CPU and storage values. This verifies successful phone provisioning and live data rendering; it does not yet verify every value against a simultaneous API capture or complete the soak gate.

A rectangular missing-glyph marker appeared where the server's node name contains an em dash. The bundled Montserrat fonts include ASCII and the degree symbol but not that dash. Display-only punctuation normalization now renders common Unicode dashes and curly quotes using supported ASCII, leaving stored IDs/names and API addressing unchanged. UTF-8 boundary and punctuation regression tests pass under ASan/UBSan. Metric bar backgrounds now use an explicit opaque gray track so numeric zero remains visibly a valid empty bar.

The punctuation/bar-track update built and flashed successfully. Boot logs confirm the saved connection survived flashing: the device associated with the configured access point and acquired an IPv4 address without reopening setup. The actual glyph replacement and bar contrast await a fresh physical look; compilation and host tests alone do not establish visual quality.

## Bounded v1 acceptance checks (24-hour soak excluded)

The user explicitly removed the 24-hour soak from v1 completion. A 60-second USB observation of the existing application produced nine live five-node samples. Request count rose from 3 to 43 with zero errors. Minimum sampled live heap was 70,068 bytes and largest block 51,200 bytes. The diagnostics task's stack fell to 792 bytes unused, failing the 1 KiB gate. Increased that task from 4 to 5 KiB and added UI/network task high-water marks to STATUS; these changes require a fresh device check.

The code audit also corrected reconfiguration/Forget navigation back into setup, added GPU temperature to Details, put the full name inside the scrollable body, omitted throughput fields for workers, and stopped reporting receipt age before the first successful response. Added a bounded device-check helper and a clean-source release packager with generated offsets, pinned SDK verification, manifest and checksums. Physical UI observations and release acceptance remain separate from successful builds.

The 5 KiB diagnostics build passed a fresh 60-second USB check with nine live samples: requests rose 3 → 43, errors stayed zero, minimum sampled heap 68,968 bytes, largest free block 50,176 bytes. Stack high-water marks were diagnostics 1,800 bytes, network 2,988 bytes and UI 13,740 bytes. These pass the sampled live gates; they do not measure peak allocation pressure during an oversized response or setup portal request. The candidate release at ignored `releases/sparkdash-2078c8a-candidate` was generated from clean commit 2078c8a. Every SHA-256 checksum and generated offset/file reference verified successfully.

## Real ESP32 HTTP failure tests

A separate validation build enables USB-only volatile URL/reconnect commands. The saved NVS record is unchanged, the production server receives no mutation, and normal builds compile these commands out. Tests use the actual device's HTTP client against a synthetic server on this Mac. The release packager rejects test-enabled configurations.

`http-device-qa-1.json` records passing numeric comparison for all 15 normalized scalar fields (including zero available memory), chunked delivery, 401/403/404/429/500, malformed/oversized/interrupted responses, slow headers, stalled bodies, cached-value preservation, empty and 17-node lists, and device-only Wi-Fi reconnect. The initial reorder case checked count only; the later deadline run additionally checked the first reversed ID. Healthy-response recovery was observed in 2.1–3.4 seconds; Retry-After 3 seconds produced about 3.4 seconds recovery. Device-only reconnect completed in about 2.5 seconds.

`http-device-qa-2.json` additionally confirms acceptance of an exact 16 KiB body. Its original trickle fixture finished in 4.5 seconds, so expecting a deadline error was a test defect, not a firmware failure. After changing chunks from 97 to 31 bytes at 750 ms intervals, `http-device-deadline.json` passed: error detected 5.57 seconds after selecting the scenario (includes scheduling/polling latency), followed by recovery in 2.16 seconds. All three runs restored the saved DGX URL. The QA health log's minimum-ever internal heap was 52,456 bytes; largest-block and stack measurements remained above thresholds in observed samples. These are bounded failure tests, not a 24-hour stability claim.

Remaining distinctions: numeric comparisons above use synthetic fixtures; per-node deployed API comparison is still open. Server-list reorder testing here begins from a cleared test cache, so preservation of selection during a live reorder still relies on host tests. Physical gestures, dim/wake and touch-to-visible timing require separate evidence.

After HTTP validation, the normal build was reinstalled with test commands disabled. The test-control string is present in the QA binary and absent from the normal binary, consistent with the compile guard. A fresh 60-second normal-firmware run (`normal-after-http-qa.json`) passed: five nodes, requests 3 → 43, zero errors, all sampled heap/largest-block and UI/network/diagnostics stack thresholds met. Saved Wi-Fi and server settings survived both validation flashes and the return to the normal image.

## Prolonged outage, live reorder, and deployed API comparison

`http-device-extended.json` passed five consecutive failures with the synthetic listener stopped, followed by restart. Observed inter-failure intervals were 1.86, 2.17, 2.16 and 4.32 seconds (USB observation adds polling granularity). Recovery after restarting the synthetic service took 30.88 seconds, within the 45-second gate. Cached data remained intact. Selecting node2 then reversing the list through the regular 60-second refresh preserved its ID while changing its index from 1 to 3.

The first live-data comparison attempted to read before the volatile server switch completed and saw a cached production sample; the synthetic server had received zero requests. Added explicit source readback to validation diagnostics and required it before comparisons and when restoring the saved server. Also added allocation percentage, the sixteenth normalized scalar. `live-api-device-confirmed.json` then passed all 80 scalar/validity checks, roles, online states and order across dgx01, dgx02, dgx03, dgx04 and gx10. Responses were captured with six read-only API GETs, frozen in an ignored snapshot, and replayed through the actual ESP32 transport/parser/cache. This avoids time-varying values being compared across different collector samples. The saved URL was explicitly confirmed restored.

A recovery audit found that an invalid NVS blob could leave unterminated URL bytes in RAM even after validation failed. Startup now resets only that invalid RAM copy to defaults before displaying it, leaving the on-flash record untouched for explicit recovery. Forget now checks erase/commit results rather than claiming success when NVS writes fail. Unterminated-record and unsupported-version host tests pass under ASan/UBSan. Actual flash-write failure injection was not performed.

The normal image was reinstalled after this round, including the invalid-record guard. `normal-after-api-check.json` passed a fresh 60-second observation: continued five-node polling, no request errors, and all sampled memory/stack gates met. Host sanitizer and HTTP fixture suites also passed. Physical corruption/write-failure injection remains untested; saved configuration was not modified for these checks.

## Full-image recovery and inactivity check

On 2026-09-05, flash was re-probed as 16 MB. A fresh full image was captured to ignored `backups/sparkdash-pre-restore-2026-09-05.bin`, with adjacent byte-count/SHA-256/device/tool metadata. Its application region matched the generated normal build byte-for-byte. The original factory backup's size and SHA-256 were re-verified before writing it at address zero with esptool 5.4.0 and all flash-header settings set to `keep`. The full write passed data verification. Bounded boot capture identified `01_Fac` version 1, initialized LCD and 480 × 480 touch, and returned from app_main.

The fresh sparkDash snapshot checksum was then re-verified and the full image restored with the same keep settings. That write also passed data verification. `post-full-restore.json` passed live five-node polling without reconfiguration, errors, or memory/stack gate failures. This exercises both factory restoration and the return to the saved custom configuration. No eFuse/security overrides were used. See `docs/recovery.md` for the tested procedure.

The UI audit found that Settings bypassed inactivity dimming. Dimming now applies there too, retaining an unsaved brightness slider preview when waking. Added read-only brightness/dim state to USB diagnostics. The normal build's 150-second untouched run (`idle-dimming-check.json`) passed the configured 120-second timer, 10% dim state, continued polling while dimmed, zero request errors, and sampled memory/stack gates. This verifies the inactivity policy and successful panel brightness command; physical wake-touch consumption and Settings-specific visual behavior still need hands-on confirmation.
