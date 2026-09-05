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
