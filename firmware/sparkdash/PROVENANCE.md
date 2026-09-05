# Board and SDK provenance

- SDK: ESP-IDF v5.5.3, revision `2c211b236707889e8400c4dc5644dd5c4ee071e0`.
- Vendor: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16 at `294543798f1a44e2f2c4d2976522323f2beee11d`.
- Reference: `02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test` (display command table, reset sequence, orientation, touch configuration).
- Schematic: https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf, dated 2026-03-26. Page 1 J4 and LCD reset connections inspected.

Important correction: `DisplayPort` constructor defaults and J4 schematic use **CS 15 and touch INT 5**. The example's unused `user_config.h` macros reverse these signals. This project follows the actual constructor and schematic, not those macros or the preliminary plan.

The board wrapper is a small adaptation of the vendor integration, not a copy of the full demo. Its only PMIC writes configure ALDO3 at 3.3 V and toggle its enable bit for panel reset. Charger current and all unrelated power configuration remain untouched. Register definitions were cross-checked with the vendor's bundled XPowers AXP2101 implementation (`0x90` bit 2 and `0x94`).

The upstream board repository does not provide a top-level license grant in the inspected revision. The factual wiring and required initialization sequence are attributed here; Managed dependencies retain their own upstream licenses and notices in the component cache; `dependencies.lock` records exact versions and hashes.

## QMI8658 accelerometer and rotation

The register facts and address are cross-checked against the same pinned exact-board
vendor revision, `02_Example/Arduino-v3.3.3/02_I2C_QMI8658/{qmi8658.cpp,qmi8658.h,02_I2C_QMI8658.ino}`.
The board uses I2C address `0x6b` on SDA8/SCL7; WHO_AM_I must be `0x05`.
CTRL1 `0x60` enables address increment; CTRL2 `0x07` selects ±2 g and
62.5 Hz; CTRL7 `0x01` enables only the accelerometer. Samples are signed
little-endian values at `0x35`, scaled by 16384 LSB/g. Sensor transactions have
10 ms timeouts. No additional PMIC rail, charging, interrupt GPIO, or gyro setup is
performed. Mounting calibration and observed readings belong in the hardware notes.

The managed SH8601 driver's axis-swap operation is unsupported. A board-owned panel
wrapper rotates RGB565 partial updates and their rectangles in software, preserving
the existing controller command table. LVGL and its adapter remain at rotation zero.
One 480×12 render buffer and one equally sized DMA scratch buffer replace the
480×24 render buffer. Scratch reuse and angle changes wait for pending SPI work;
the original IO completion callback still releases LVGL's draw buffer.
