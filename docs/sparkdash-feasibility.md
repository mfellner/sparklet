# sparkDash on the Waveshare ESP32-C6

Research date: 2026-09-05. Recommendation: build a native, read-only LVGL dashboard that consumes the existing sparkDash HTTP API over local Wi-Fi. No additional server is required for the first version. This is a proposed implementation, not tested firmware.

## Evidence and feasibility

- Exact board: Waveshare ESP32-C6-Touch-AMOLED-2.16. Its active display is **480 × 480, square**, regardless of the enclosure's rectangular outline. Vendor specifications: 160 MHz C6, 512 KB HP SRAM, 16 KB LP SRAM, 16 MB flash, 2.4 GHz Wi-Fi. The vendor LVGL example explicitly selects a configuration without PSRAM.
- Existing factory evidence in `notes/2026-09-05-discovery.md` establishes a working display/touch initialization path and ESP-IDF 5.5.3. No device was opened, reset, backed up, or flashed during this research.
- Read-only GET requests to `http://dgx01.local:5555` succeeded from this Mac. `/api/sparks` returned five configured nodes in a 2,808-byte response. Individual `/api/sparks/{id}/metrics` responses measured 3,064, 2,362, 3,051, 2,395, and 2,314 bytes for dgx01, dgx02, dgx03, dgx04, and gx10 respectively. All reported online. These are measurements at one point in time, not maximum payload sizes.
- Live data contains head/worker roles, GPU utilization/temperature/power, GPU allocation and available unified memory, storage, worker labels, and head-node LLM throughput/model names. It supports the proposed cards without scraping HTML.
- Raw API captures and research clones are outside Git under `/tmp/esp-spark-research`. Only reviewed summaries are recorded here. The deployed server's exact source revision was not established; the tested endpoint shape agrees with the inspected upstream source.

Pinned source revisions inspected:

- sparkDash: `cc44d3527e7ddd339f513bb205dce4f37072beff`.
- Waveshare: `294543798f1a44e2f2c4d2976522323f2beee11d`.

## Recommended architecture

DGX nodes → existing sparkDash collectors/server → HTTP JSON over Wi-Fi → ESP32 data model → LVGL card.

Keep SSH, GPU probes, model API credentials, and multi-node collection on the existing server. Firmware owns Wi-Fi, bounded JSON parsing, local presentation, touch navigation, configuration, and reconnect behavior.

### Direct HTTP first

1. Load Wi-Fi and server address from NVS; join the LAN and resolve the host.
2. GET `/api/sparks` at startup, after reconnect, and about every 60 seconds. This is a configuration list, **not** live metric snapshots. Retain only IDs, names, order, and presentation metadata.
3. GET `/api/sparks/{id}/metrics` for the visible node about every two seconds. This returns the server's cached monitor snapshot; it does not trigger a fresh SSH collection per device poll.
4. Poll other nodes sequentially at a slower cadence, roughly one background node every two seconds, and retain compact parsed values. Prioritize a newly selected node immediately. With five nodes, this is roughly 2.7 KB/s of JSON if both foreground and one background request occur every two seconds, excluding HTTP overhead. Only one request should be in flight.
5. Preserve selection by ID across reorder/add/remove events; fall back to the nearest surviving node when it disappears. Cached cards make swipes immediate. Label cached status honestly; a cluster online count reflects the last observations, not a simultaneous cluster snapshot.

Use `esp_http_client` and an IDF JSON component with explicit bounds. A proposed starting cap is 16 KiB per response, 16 nodes, and bounded UTF-8 strings (e.g. 96 bytes for node names, 160 for model/worker labels); validate these against fixtures and actual free heap. Reject oversize or malformed responses with an understandable status. Unknown fields are ignored, unknown enum values have fallbacks, absent metrics remain unavailable rather than becoming zero. Do not retain JSON trees: copy selected values into fixed structs and free the parse arena after each request.

Network work must run outside the LVGL task. Pass snapshots through a queue or protected double buffer, then update widgets under the LVGL lock. Use monotonic time for last-response age, finite connection/read timeouts, exponential reconnect backoff, and fresh DNS lookup after repeated failures.

### WebSocket and optional compact API

Upstream also implements `/ws` with `{type:"snapshot", sparks:[...], refreshInterval:...}` in registry order. It is a valid later alternative when simultaneous cluster updates are desirable. It requires bounded frame reassembly and larger peak parsing capacity. Upstream suppresses byte-identical snapshots, so silence alone must not indicate disconnection; use heartbeat/liveness checks.

Add a compact endpoint only if measurements justify it or a stable public contract is desired. Proposed `/api/embedded/v1/snapshot` would return a schema version, node order, bounded labels, role, online state, relevant metrics, and per-domain sample ages. This endpoint does **not** exist today. It can project existing monitor snapshots without starting additional collectors. A standalone adapter service is an option when upstream cannot be changed, but introduces another deployment and cannot invent missing collector timestamps.

An actual limitation matters: upstream tracks `_lastUpdate[domain]` internally but does not expose it in `snapshot()`. A successful HTTP response proves server connectivity, not freshness of every GPU/LLM value. Version one can show “received 2s ago” plus the reported online flag. A stronger “sample stale” guarantee requires exposing domain timestamps/errors from sparkDash. Do not mislabel HTTP receipt time as sensor sample time.

## Screen and interactions

Show one card per node, with horizontal swipes snapping to the next/previous card and a persistent node position indicator. Prefer a recycled current/previous/next layout or a bounded LVGL tileview to allocating unlimited widget trees. A short slide is optional after frame-time measurements; immediate page switching remains acceptable.

Suggested 480 × 480 layout, with a margin for rounded display corners:

- Top: short node ID, online/offline text, Head/Worker badge; a second line for the cluster/name.
- Middle: three prominent bars for GPU allocation, temperature, and utilization.
- Lower: power draw/limit and available memory, then model or worker label.
- Bottom: generation and prefill tokens/second for a head; worker relationship/label for a worker; navigation and connection age.
- Tap a clearly labeled Details control for storage, CPU, network, full model name, and diagnostic connection state. Keep less important data on this second page rather than shrinking all text.

Use the screenshot's dark surface, amber highlights, white values, and restrained red warnings. Target roughly 24–32 px labels and 36–48 px primary values, then judge legibility on the actual 2.16-inch panel. Include arrow/button navigation as a fallback while touch mapping is validated. A user-selectable automatic rotation interval (e.g. 10 seconds) can be added later and paused after touch.

GPU allocation and available memory are distinct. Read `metrics.gpu.vram` with `unifiedMemory` fallback as upstream does; **do not calculate available as total minus GPU allocation** on shared-memory Spark systems. The API calls memory/storage units MB but uses binary conversion; divide by 1024 to match the web display, or label them MiB/GiB consistently in the native UI. Select the root storage entry by label `/`, with upstream's `nvme0n1p2` fallback, rather than summing partitions. For heads, the overview selects the first available LLM backend; reproduce that rule initially. Workers show `workerLabel` and omit local LLM throughput, preventing double-counting cluster generation rates.

Distinguish Wi-Fi disconnected, server unreachable, node offline, waiting for metrics, and missing LLM data. Preserve last values with their receipt age during a network interruption. Dimming/sleep with touch wake is useful for a desk AMOLED; sustained brightness, swipe performance, and battery life require real measurements.

## Firmware foundation and hardware details

Start with `02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test` in the pinned Waveshare source, plus `05_WIFI_STA` and `06_WIFI_AP` as references. The current manifest requests LVGL `^9.5.0`, `espressif/esp_lvgl_adapter ^0.3.0`, `espressif/esp_lcd_sh8601 ^1.0.0`, and `waveshare/esp_lcd_touch_cst9217 ^1.0.4`. These are ranges, not a reproducible lock; pin resolved components after the first successful build. The manifest's broad IDF minimum and the defaults file's older header are not proof of compatibility with arbitrary IDF versions. Use 5.5.3 as the initial SDK baseline.

The board-specific demo explains the factory naming discrepancy: it deliberately uses SH8601/CST9217 component names with its own panel command sequence and touch setup. Preserve that integration for the vendor-specified CO5300/CST9220 rather than substituting another board's drivers.

Source-derived wiring (not independently electrically verified): I2C SCL/SDA 7/8; QSPI clock 0, data 1/2/3/4, CS 5; touch reset 11 and interrupt 15. Panel reset uses AXP2101 ALDO3 sequencing; brightness uses panel command 0x51. QSPI is configured at 40 MHz, 16-bit RGB565; touch sets swap XY and mirror Y. The display flush region is aligned to even coordinate boundaries. Preserve the vendor PMIC initialization and power sequencing, and cross-check the linked schematic before driver changes. The schematic has not been independently inspected in this research.

RAM, not flash, is the principal constraint. One full RGB565 framebuffer is 480 × 480 × 2 = 460,800 bytes (450 KiB); double buffering would be 900 KiB. Instead, start with one 480 × 48 stripe, 46,080 bytes (45 KiB), or two only if heap measurements allow. LVGL supports partial rendering. The vendor example selects a no-PSRAM SPI profile with a 100-row buffer and a 20 KiB UI stack; inspect actual adapter allocation and adjust explicitly. Keep SPI maximum transfer size consistent with chunking (vendor display code uses 50 rows). Avoid full-screen canvas caches and bitmap-heavy themes; disable bundled LVGL demo assets/features and the brightness-test task. Build-time SRAM, DMA-capable heap, largest free block, stack high-water marks, and runtime low-water heap all need measurement with Wi-Fi connected.

Suggested project structure: `firmware/sparkdash/` containing `main/` orchestration and components for `board`, `network_config`, `sparkdash_client`, `metrics_model`, and `ui`. Keep fixtures in a host-test directory with synthetic/anonymized data, and secrets in ignored local files or provisioned NVS. No framework/toolchain was installed for this research.

## Wi-Fi setup

The C6 requires a reachable **2.4 GHz** Wi-Fi network. It does not need the DGX to be wireless; Ethernet and Wi-Fi can share a routed LAN. Verify that guest/client isolation and VLAN rules permit the board to reach port 5555.

For development, provision credentials and server URL over a deliberate USB configuration interface or an ignored local NVS input. For normal use, offer a password-protected temporary setup AP and small phone-accessible page to select SSID, enter password, and set the sparkDash URL. Save only when submitted; stop setup AP/server after connecting. Show the setup AP password and local setup address on screen. Provide a deliberate settings action to reconfigure; do not erase credentials automatically after transient outages. A custom browser setup page requires implementation; Espressif's provisioning manager offers transports/protocols but is not automatically a generic browser portal.

Support `dgx01.local` using Espressif mDNS resolution, with a configurable numeric IP or ordinary LAN DNS fallback. This Mac resolving `.local` does not prove the ESP32 will resolve it automatically or across VLAN boundaries. A DHCP reservation is a useful fallback. Initial transport can match the existing local HTTP deployment; HTTPS, if added, should validate its certificate. No SSH or model API keys belong on the display. Configuration values and raw logs stay out of Git.

## Relevant alternatives from awesome-esp

| Project | Fit |
| --- | --- |
| LVGL | Best match: native embedded widgets, partial rendering, touch navigation; used by exact-board vendor examples. |
| ESPHome | Plausible alternative with LVGL and current CO5300/QSPI and CST9220 support. Exact-board PMIC sequencing and RAM behavior still need work; not established as a drop-in configuration. Prefer if Home Assistant/MQTT integration becomes the main objective. |
| openHASP / HomePoint | Useful touchscreen/MQTT architecture references. No exact-board ready-to-flash support was established; selecting them would add platform/driver and often broker integration work. |
| ESP-DASH | Generates a web dashboard served by the MCU for browser clients. It does not provide the native AMOLED dashboard needed here. |
| Arduino | Exact-board examples exist for Arduino 3.3.3; viable, but ESP-IDF gives direct control of the vendor adapter, task scheduling, memory and networking. |

The awesome list is a discovery index, not a compatibility matrix. No need to introduce MQTT, Home Assistant, or a Linux browser renderer solely for these five cards.

## Implementation and acceptance stages

1. **Reproducible hardware baseline.** Install/pin IDF; enumerate the known board; full readable flash backup and SHA-256 outside Git before replacement; build a minimal vendor-derived display/touch app. Use generated flash arguments. Acceptance: correct colors/orientation, touch across corners, stable PMIC behavior and recorded free heap.
2. **One real node.** Implement Wi-Fi and bounded HTTP parsing; show dgx01 metrics in a native card. Acceptance: values match the browser's definitions, UI remains responsive during timeouts, and unavailable data is not displayed as zero.
3. **All five nodes.** Dynamic discovery/order, swipe navigation, cache and background polling, worker/head behavior, Details screen. Acceptance: missing node, reordering, long labels and all-offline states work without stale-pointer or allocation issues.
4. **Setup and resilience.** Persistent credentials/server URL, setup AP, mDNS/IP fallback, reconnect, dim/wake. Acceptance: router/server restart recovery, bad password recovery, malformed/oversized/chunked JSON handling, and a 24-hour device soak without growing heap loss or watchdog resets.
5. **Optional polish.** Auto-rotation, tiny bounded history sparklines, OTA partitions and rollback after testing, or compact server endpoint with domain freshness. Flash capacity makes OTA plausible; select the partition scheme for the new build rather than copying factory offsets.

Planning estimate, not a guarantee: 1–3 focused development days for a first useful one-node prototype if vendor components build cleanly; about a week for five-node navigation and setup; additional time for soak testing and polish. The first milestone is the uncertainty gate. This research verifies API feasibility and a credible vendor driver path, not real-device Wi-Fi/display concurrency or battery runtime.

## Sources

- [awesome-esp](https://github.com/agucova/awesome-esp)
- [sparkDash pinned server API](https://github.com/MiaAI-Lab/sparkDash/blob/cc44d3527e7ddd339f513bb205dce4f37072beff/server/index.js)
- [sparkDash snapshot and collection](https://github.com/MiaAI-Lab/sparkDash/blob/cc44d3527e7ddd339f513bb205dce4f37072beff/server/sparks/SparkMonitor.js)
- [sparkDash overview rendering](https://github.com/MiaAI-Lab/sparkDash/blob/cc44d3527e7ddd339f513bb205dce4f37072beff/src/components/OverviewPage/OverviewPage.tsx)
- [Waveshare specifications](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16)
- [Waveshare pinned LVGL 9 example](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/294543798f1a44e2f2c4d2976522323f2beee11d/02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test)
- [Waveshare schematic, implementation cross-check pending](https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf)
- [LVGL partial display rendering](https://lvgl.io/docs/open/9.2/porting/display)
- [LVGL 9.5 tileview](https://lvgl.io/docs/open/9.5/widgets/tileview)
- [ESP-IDF 5.5.3 HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c6/api-reference/protocols/esp_http_client.html)
- [Espressif mDNS](https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/en/index.html)
- [Espressif C6 provisioning workshop](https://developer.espressif.com/workshops/esp-idf-with-esp32-c6/assignment-5/)
- [ESPHome QSPI displays](https://esphome.io/components/display/qspi_dbi/), [CST9220 touch](https://esphome.io/components/touchscreen/cst9220/)
- [openHASP](https://www.openhasp.com/), [installer board selection](https://install.openhasp.com/)
- [ESP-DASH](https://github.com/ayushsharma82/ESP-DASH)
