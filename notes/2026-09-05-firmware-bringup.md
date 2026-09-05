# sparkDash firmware bring-up

## Backup and identity

On 2026-09-05, esptool 5.4.0 confirmed the attached ESP32-C6 rev v0.2 (USB serial D4:05:92:B9:04:28) has 16 MB flash, manufacturer 0x20, device 0x4018.

A full 16,777,216-byte backup was saved outside Git to `backups/factory-2026-09-05-1040.bin`, with metadata in the adjacent JSON file. SHA-256: `c1ede195afd99779507c8fe27d224ebb73fe63bbf8668e8aeb160d7f3a8157ea`.

## Schematic correction

Page 1 J4 agrees with the vendor DisplayPort constructor: GPIO15 connects the display CS position; GPIO5 connects touch INT; GPIO11 is touch reset. Clock/data are 0/1/2/3/4. LCD_RESET connects through R16 to ALDO3. The configuration macros in the same vendor example reverse CS and INT; they are not used by its constructor call. Implementation follows the schematic and constructor. This supersedes the preliminary feasibility note's source-derived pin table.

## Build foundation

ESP-IDF v5.5.3 is installed outside Git at `/Users/max/esp/esp-idf-v5.5.3`. Dependencies are locked by the component manager. Board-independent core tests run under ASan and UBSan. Network transfer rates from sparkDash's SystemCollector are bytes/second; firmware formats these as B/s, KiB/s or MiB/s.

Hardware and release validation results will be appended as they are observed. A successful compilation is not physical display/touch validation.
