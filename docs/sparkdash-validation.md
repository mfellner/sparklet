# sparkDash validation status

Updated 2026-09-05. This is a development build, not a completed release acceptance report.

| Gate | Evidence / status |
| --- | --- |
| Exact board wiring | Schematic J4 and active vendor constructor agree: LCD CS15, touch INT5/reset11; ALDO3 reset. |
| Factory backup | Complete 16 MiB read, SHA-256 and identity metadata recorded in bring-up notes. Restore not physically tested. |
| Pinned SDK/dependencies | ESP-IDF 5.5.3 build succeeded; managed dependency lock committed. |
| Shared core tests | ASan/UBSan tests passed for parser, normalization, configuration, formatting, list reconciliation and scheduler. |
| Controlled host HTTP | Five unittest groups passed: normal/chunked, list variations, status errors, interrupted/oversized/malformed/stalled responses. Does not validate IDF transport. |
| Display / touch | User confirmed upright readable screen and Settings/Back touch; supplied photos. Full corner/swipe/dim testing pending. |
| QR setup | Two-step Wi-Fi/URL QR build compiled, flashed and booted. iPhone photos confirm decoding and successful Wi-Fi join. |
| Startup memory | 24-row draw stripe: 53,872 B free internal heap, 38,912 B largest block; network stack high-water mark 5,220 B. |
| Phone provisioning | QR-assisted join succeeded. Portal incorrectly rejected IPv4-mapped IPv6 local addresses; corrected and host regression-tested. Subsequent phone screenshot confirms saved Wi-Fi and successful connection. |
| Live five-node display | Photos show five discovered nodes and live cards at positions 1, 2 and 5, plus Details. Simultaneous API/value comparison for every node remains pending. |
| Recovery / performance | Physical Wi-Fi, DNS/mDNS, IDF HTTP faults and timing measurements pending. |
| 24-hour soak | Explicitly excluded by the user from v1 completion; not performed. |
| Full release / restore gate | Pending. Do not treat a generated build bundle as passed release validation. |

Raw boot logs and backups remain ignored. Startup memory alone does not establish worst-case memory acceptance. The user excluded the 24-hour soak from v1 completion. Bounded controlled-failure, navigation, stack and memory measurements remain required; no long-duration reliability claim is made.
