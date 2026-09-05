# Board and SDK provenance

- SDK: ESP-IDF v5.5.3, revision `2c211b236707889e8400c4dc5644dd5c4ee071e0`.
- Vendor: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16 at `294543798f1a44e2f2c4d2976522323f2beee11d`.
- Reference: `02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test` (display command table, reset sequence, orientation, touch configuration).
- Schematic: https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf, dated 2026-03-26. Page 1 J4 and LCD reset connections inspected.

Important correction: `DisplayPort` constructor defaults and J4 schematic use **CS 15 and touch INT 5**. The example's unused `user_config.h` macros reverse these signals. This project follows the actual constructor and schematic, not those macros or the preliminary plan.

The board wrapper is a small adaptation of the vendor integration, not a copy of the full demo. Its only PMIC writes configure ALDO3 at 3.3 V and toggle its enable bit for panel reset. Charger current and all unrelated power configuration remain untouched. Register definitions were cross-checked with the vendor's bundled XPowers AXP2101 implementation (`0x90` bit 2 and `0x94`).

The upstream board repository does not provide a top-level license grant in the inspected revision. The factual wiring and required initialization sequence are attributed here; clarify redistribution rights before packaging this project commercially. Managed dependencies retain their own upstream licenses and notices in the component cache; `dependencies.lock` records exact versions and hashes.
