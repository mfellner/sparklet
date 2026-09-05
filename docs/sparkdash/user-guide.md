# User guide

[Documentation index](README.md)

## What you need

- The Waveshare ESP32-C6-Touch-AMOLED-2.16 with sparkDash firmware installed.
- USB power; use a data-capable cable when updating or diagnosing firmware.
- A phone with a QR-capable camera and browser.
- A personal 2.4 GHz Wi-Fi network and its password. The station configuration requires WPA2 or stronger personal authentication; enterprise, open and 5-GHz-only networks are outside v1.
- An existing sparkDash server reachable from that network over HTTP.

Default server: `http://dgx01.local:5555`. Credentials are entered on the temporary local setup page, never in source files or command-line arguments.

## First-time setup

1. Power the display. Without a saved connection it creates a temporary WPA2-protected access point named `SparkDash-XXXX`.
2. Scan **1. Scan to join setup Wi-Fi** on the display with your phone camera. Accept the phone's join prompt. The code contains the currently displayed setup SSID and password.
3. Stay on that network even if the phone reports **No Internet Connection**. This is expected: the access point serves configuration locally.
4. Tap **Next: setup page** on the display and scan the second QR code. Alternatively open `http://192.168.4.1` directly in the phone browser.
5. Tap **Scan networks**, select the local network, or type its name manually. Enter its Wi-Fi password and the sparkDash server address.
6. Tap **Save and connect**. Allow up to 30 seconds for association and an IP address.
7. On successful Wi-Fi connection, the portal reports that settings were saved. Setup remains available for approximately eight seconds, then closes and the display loads the dashboard.

The QR codes are generated on the device without an external service. Their purpose is to avoid typing the setup network password and URL. The setup network password changes each session, including after reboot. If the phone remembers an old one, forget that SparkDash network and scan the current code again.

A successful Wi-Fi join saves the candidate connection even if sparkDash is temporarily unavailable. The display then retries the server automatically. A failed Wi-Fi attempt leaves the previous saved connection intact and keeps setup available.

## Server address formats

| Example | Meaning |
| --- | --- |
| `http://dgx01.local:5555` | Default mDNS hostname and port |
| `http://192.168.1.50:5555` | Direct IPv4 address; substitute your actual server address |
| `http://dashboard.local/sparkdash` | HTTP on port 80 with a reverse-proxy base path |

A base path is prepended to both API endpoints. HTTPS, IPv6 address literals, URL credentials, query strings, fragments and spaces are rejected. There is no username/password or bearer-token configuration for the server. An authenticated server returning 401/403 displays an access-denied error.

## Overview and navigation

The top of the card identifies the node and its reported role/status. The subtitle is its configured name. The three bars show GPU allocation, GPU temperature and GPU utilization. Power, available memory, and model/worker information appear below them.

- Swipe left across the card to move to the next node; swipe right for the previous node.
- The `<` and `>` buttons perform the same actions.
- Navigation wraps from the last node to the first and vice versa.
- `1 / 5`, for example, means the first of five discovered nodes.
- **Details** opens the selected node's vertically scrollable values. **Back** returns to the same node.
- Node swipes are disabled inside Details so they do not conflict with vertical scrolling.

Navigation uses cached data immediately. The newly selected node is requested after any current HTTP request finishes. A node first discovered but not yet polled has unknown status; it is not automatically offline. Server list refreshes preserve selection by ID even if its position changes. If that node disappears, selection moves to its former position or the last remaining node.

Head and standalone nodes show the first available LLM backend. Workers show their worker label and optional head relationship, and omit local throughput. A worker is not expected to report the head's local generation rate.

## Reading values correctly

| Item | Interpretation |
| --- | --- |
| GPU alloc. | Reported allocation and total, with a unified-memory fallback |
| Available memory | The server's supplied available value; not total minus allocation |
| Temperature | GPU temperature in °C; Details also includes CPU temperature |
| GPU usage | Reported utilization; zero is a valid value |
| GPU power | Reported draw and limit in watts |
| tok/s / prefill/s | Selected available backend's generation/prefill throughput |
| Root storage | Root mount `/`, with the collector's `nvme0n1p2` fallback |
| Network RX/TX | Primary interface receive/transmit rate, formatted from bytes per second |
| Received Ns ago | Time since the last accepted HTTP response on the ESP32 |

Memory and storage use MiB/GiB with binary conversion and one decimal place. Missing numbers display `--`; valid zero remains visible. A missing model displays **LLM unavailable**. Offline status, missing data, and zero utilization are different conditions.

“Received” is **not collector sample age**. The server currently supplies no per-domain sample timestamps, so a recent HTTP response can contain older collector data. Red/amber bars are visual indicators, not configurable hardware alarm limits; the current implementation changes the bar indicator to red above a raw bar value of 85.

Long labels are shortened on Overview and appear in the scrollable Details content up to their storage limits. Common Unicode dashes and quotes are converted to supported punctuation for display. Other unsupported characters can still show a replacement glyph; stored IDs and API addressing are unchanged.

## Settings and dimming

Settings displays the server, Wi-Fi details, firmware version and a diagnostic summary. It also warns if more than 16 nodes were returned.

- Brightness defaults to **60%**, adjustable from 10% to 100%.
- Inactivity dimming defaults to **120 seconds**, adjustable from 30 to 600 seconds.
- **Save display** persists the chosen values. Slider movements preview brightness but do not write NVS on every movement.
- After inactivity, brightness is commanded to 10%; metric polling continues.
- The first touch after dimming is consumed to wake the screen. A subsequent touch activates controls.
- Setup QR pages remain awake. Normal Overview, Details and Settings use the inactivity policy.

The idle dim command and continued polling were measured. Physical first-touch consumption and Settings-specific visual behavior still require final confirmation on the latest firmware.

## Reconfigure, cancel and forget

**Reconfigure** starts a new temporary setup session. It disconnects the normal station connection while setup is active. Enter the new candidate settings through the portal. The prior saved record is retained until the candidate joins Wi-Fi successfully.

**Cancel** restores the saved connection when one exists. On first-time setup there is no previous connection to restore, so cancellation cannot produce a working dashboard.

**Forget connection** opens an explicit confirmation screen. **Keep settings** cancels the operation. **Forget** removes the saved connection and returns to setup; ordinary temporary Wi-Fi/server outages never erase it. A successful firmware update normally preserves the saved configuration. Full-flash restoration replaces it with the contents of the selected backup.

For connection and display problems, use the [troubleshooting guide](operations.md#troubleshooting).
