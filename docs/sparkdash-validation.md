# sparkDash validation status

Updated 2026-09-05. This is a development build, not a completed release acceptance report.

| Gate | Evidence / status |
| --- | --- |
| Exact board wiring | Schematic J4 and active vendor constructor agree: LCD CS15, touch INT5/reset11; ALDO3 reset. |
| Factory backup | Complete 16 MiB read, SHA-256 and identity metadata recorded in bring-up notes. Full factory restore booted successfully, followed by a verified return to the saved sparkDash image/configuration. |
| Pinned SDK/dependencies | ESP-IDF 5.5.3 builds succeeded; managed dependency lock committed. Reproducible-build mode produced identical app/bootloader/partition binaries in two build directories. |
| Shared core tests | ASan/UBSan tests passed for parser, normalization, configuration, formatting, list reconciliation and scheduler. |
| Controlled host HTTP | Five unittest groups passed: normal/chunked, list variations, status errors, interrupted/oversized/malformed/stalled responses. Does not validate IDF transport. |
| Display / touch | User confirmed upright readable screen and Settings/Back touch; supplied photos. A 150-second untouched run verified the default 120-second dim timer, 10% brightness command and continued polling. Physical corner/swipe/first-touch wake and Settings-specific visual confirmation remain pending. |
| QR setup | Two-step Wi-Fi/URL QR build compiled, flashed and booted. iPhone photos confirm decoding and successful Wi-Fi join. |
| Startup / live sampled memory | Startup: 53,872 B free internal heap, 38,912 B largest block. Subsequent 60-second live check: minimum sampled heap 68,968 B, largest block 50,176 B; spare stacks diagnostics 1,800 B, network 2,988 B, UI 13,740 B. Exact-16-KiB response and fault runs also passed; the QA run reported minimum-ever internal heap 52,456 B. Portal-load stack/peak checks remain pending. |
| Phone provisioning | QR-assisted join succeeded. Portal incorrectly rejected IPv4-mapped IPv6 local addresses; corrected and host regression-tested. Subsequent phone screenshot confirms saved Wi-Fi and successful connection. |
| Live five-node display | Captured deployed responses for dgx01/dgx02/dgx03/dgx04/gx10 replayed through the actual ESP32 HTTP client: all 80 numeric/validity fields, roles, online states and node order matched an independent reference. Frozen responses avoid comparing different collector samples; user photos separately show live Overview/Details rendering. |
| Recovery / HTTP transport | Real-device synthetic-server tests passed normal/chunked and exact 16 KiB bodies, 401/403/404/429/500, invalid/oversized/interrupted data, slow headers, stalled and trickling bodies, cache retention, empty/17-node lists, and device-only Wi-Fi reconnect. Recovery observed 2.1–3.4 seconds after restoring healthy responses; Wi-Fi reconnect about 2.5 seconds. Additional prolonged-outage and synthetic-server restart test passed (recovery 30.88 s); repeated failure intervals showed backoff, and a real 60-second list refresh preserved selected node2 while its index changed 1 → 3. Touch/frame latency checks remain pending. |
| 24-hour soak | Explicitly excluded by the user from v1 completion; not performed. |
| Candidate bundle / restore gate | Candidate 3376e53 (`sparkdash-v1-rc1`) passed ZIP integrity, checksums, flash-reference checks and a flash from the extracted bundle. Its subsequent runtime check failed: bootloader output ended at application handoff and no STATUS samples arrived. A later bootloader connection attempt received no serial data. Cause is unresolved; this candidate is not accepted. Full-image recovery previously passed; remaining interaction checks also remain open. |

Raw boot logs and backups remain ignored. Startup memory alone does not establish worst-case memory acceptance. The user excluded the 24-hour soak from v1 completion. Bounded controlled-failure, navigation, stack and memory measurements remain required; no long-duration reliability claim is made.
