# ESP32 playground

Local development notes and tools for a **Waveshare ESP32-C6-Touch-AMOLED-2.16**: a USB-connected ESP32-C6 with a 480 × 480 AMOLED touchscreen.

The board was successfully contacted on macOS on **2026-09-05**. The factory image has been backed up, and the board now runs the native [sparkDash companion firmware](firmware/sparkdash/README.md). Phone provisioning and full release validation are in progress.

## Quick start

Requires Python 3 and [uv](https://docs.astral.sh/uv/). Run from this repository:

```sh
uv run scripts/esp32_serial.py list
uv run scripts/esp32_serial.py monitor --seconds 5
```

The helper uses pinned PySerial and selects the known board by USB serial number. Listing devices does not open them. Monitoring receives bytes without sending application commands, but **opening USB serial may reboot this board**, as observed during discovery. The port is closed on exit.

To choose a port explicitly or save raw output:

```sh
uv run scripts/esp32_serial.py monitor --port /dev/cu.usbmodem2101 --seconds 10 --output logs/session.bin
```

Paths can change after reconnecting. `/dev/cu.usbmodem2101` was the observed path, not a permanent identifier.

## Documentation

- [Hardware and observed pin assignments](docs/hardware.md)
- [USB, serial, backup, and development workflow](docs/interaction.md)
- [Initial discovery notes](notes/2026-09-05-discovery.md)
- [Manufacturer and Espressif references](docs/references.md)
- [Agent instructions](AGENTS.md)

## Local Codex skill

The versioned skill is in [skills/esp32-c6-waveshare/SKILL.md](skills/esp32-c6-waveshare/SKILL.md). It is installed locally through a symlink at `~/.codex/skills/esp32-c6-waveshare`. Use `$esp32-c6-waveshare` in a future task; a new task/session may be needed for skill discovery. If this repository moves, update the symlink.

Raw logs, flash backups, local credentials, and build outputs are ignored by Git. Firmware bring-up results are recorded in [the validation notes](notes/2026-09-05-firmware-bringup.md).
