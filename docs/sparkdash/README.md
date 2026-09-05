# sparkDash for ESP32 documentation

A native, read-only sparkDash companion for the **Waveshare ESP32-C6-Touch-AMOLED-2.16**, with a 480 × 480 AMOLED touchscreen. The ESP32 joins local Wi-Fi and polls an existing sparkDash server; it does not collect GPU metrics itself.

## Current status

Firmware, QR provisioning, node views, host tests, device test tools, backup/recovery tooling and release packaging are implemented. Earlier physical runs demonstrated phone setup and live five-node operation. **V1 acceptance is incomplete:** candidate `3376e53` flashed successfully, but its subsequent USB check stopped receiving data after application handoff. The board needs a physical power-cycle and further diagnosis. Do not interpret a successful build or package checksum as a successful runtime test.

The [validation report](../sparkdash-validation.md) is the current authority for tested facts and remaining gates. The [bring-up log](../../notes/2026-09-05-firmware-bringup.md) preserves the investigation history, including failed tests and corrections. The user explicitly excluded the 24-hour soak test; no 24-hour reliability claim is made.

## Reading guide

| Audience / task | Guide |
| --- | --- |
| Configure the display and use its controls | [User guide](user-guide.md) |
| Understand tasks, data contracts, memory and persistence | [Architecture and API](architecture.md) |
| Install the SDK, build, change and debug firmware | [Development guide](development.md) |
| Run host, transport, live-data and physical checks | [Testing and acceptance](testing.md) |
| Create, verify, flash and troubleshoot a release | [Release and operations](operations.md) |
| Identify the board and its wiring | [Hardware reference](../hardware.md), [source provenance](../../firmware/sparkdash/PROVENANCE.md) |
| Back up or restore the entire flash | [Recovery procedure](../recovery.md) |
| Inspect the original research and references | [Feasibility](../sparkdash-feasibility.md), [references](../references.md) |
| Use USB safely | [Interaction instructions](../interaction.md) |

## Scope

V1 provides one node card at a time, horizontal navigation, scrollable Details, Settings, temporary phone setup, persistent connection/display preferences, automatic reconnect, brightness and inactivity dimming. It discovers up to 16 nodes in server order. The current installation has five nodes: dgx01, dgx02, dgx03, dgx04 and gx10.

V1 excludes server modifications, remote shutdown/wake/update actions, WebSockets, MQTT, Home Assistant, automatic rotation, charts/history, OTA, audio, IMU, SD storage and battery management. USB is the primary power source. The firmware does not configure charging current or enable flash encryption/eFuse changes.

## Documentation conventions

Commands assume the repository root unless a guide explicitly changes directories. `PORT` and `MAC_LAN_IP` are placeholders requiring substitution. The observed macOS USB port is not a permanent identity. Discover the board before each hardware session.

Synthetic examples contain no real credentials. Private flash images, raw logs, captured API responses and generated bundles belong in ignored directories. Documentation describes implemented behavior separately from physical verification; consult the acceptance report before distributing a candidate as a finished release.
