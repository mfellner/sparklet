# Release and operations

[Documentation index](README.md)

## Release prerequisites

Use a clean committed checkout, the pinned activated ESP-IDF SDK, the locked components, target ESP32-C6 and 16 MiB flash configuration. Normal firmware must have validation-only commands disabled and reproducible build enabled. Read the [validation report](../sparkdash-validation.md) before deciding whether an artifact is a development candidate or an accepted release.

The earlier `sparkdash-v1-rc1` candidate passed flash verification but initially stopped responding on USB. Physical reconnection restored normal boot and live data, followed by further successful validation flashes and checks. Its cause was not isolated; the incident is retained in the validation history. Version 1.0.0 is the v1 application version and a standalone single-app image. Version 1.1.0 is a platform slot image: it requires a device already migrated to the [esp32-playground platform](https://github.com/mfellner/esp32-playground) layout. Always use the final bundle's own manifest, checksums and current acceptance report.

## Package a candidate

From the repository root, after reviewing and committing changes:

```sh
. /Users/max/esp/esp-idf-v5.5.3/export.sh
python3 tools/package_release.py releases/sparklet-candidate-new
```

Use a new name. The tool refuses an existing destination/ZIP, dirty checkout, wrong SDK revision, wrong target/flash size, disabled reproducible mode, enabled validation commands, or a `build/sparklet-flash_args` that does not write `sparkdash.bin` at `0x220000`. It builds first and also rejects changes to tracked dependencies/source caused by the build.

| Bundle artifact | Purpose |
| --- | --- |
| `sparkdash.bin` | Application image for the `sparklet` slot |
| `sparklet-flash_args` | Build-generated flash settings and the single `0x220000 sparkdash.bin` write |
| `partition_table/partition-table.bin` | Generated platform partition table, for comparison with the device's table |
| `project_description.json` | Build metadata; lets `device.py install` treat the bundle directory as a build directory |
| `partitions.csv`, `sdkconfig.defaults.platform` | Platform layout and contract, identical to the platform copies |
| `sdkconfig`, `sdkconfig.defaults` | Exact generated configuration and baseline defaults |
| `dependencies.lock`, `PROVENANCE.md` | Dependency/SDK/vendor identity |
| `manifest.json` | Source revision, app version, compiler, SDK and creation time |
| `README.md`, `FLASH.txt` | Standalone bundle workflow |
| `SOURCE_README.md` | Source-tree development entry point; relative paths require the repository |
| `esp32_serial.py` | USB enumeration and bounded monitor helper |
| `validation.md` | Evidence at packaging time |
| `SHA256SUMS` | Checksums of bundled files |
| Adjacent `.zip` and `.zip.sha256` | Distribution archive and its external checksum |

The bundle contains no bootloader, launcher or full flash arguments: it can only update the Sparklet slot of a migrated device. The bundle README links the platform's [interaction](https://github.com/mfellner/esp32-playground/blob/main/docs/interaction.md) and [recovery](https://github.com/mfellner/esp32-playground/blob/main/docs/recovery.md) instructions instead of copying them.

The release contains no NVS image, factory backup, Wi-Fi password, captured API responses or raw logs. Recovery documentation refers to separately protected backups; the bundle does not contain those private assets.

## Verify and flash an extracted bundle

From the directory containing the ZIP:

```sh
shasum -a 256 -c sparklet-candidate-new.zip.sha256
unzip -t sparklet-candidate-new.zip
```

Extract to a new location. From inside the extracted bundle:

```sh
shasum -a 256 -c SHA256SUMS
uv run esp32_serial.py list
```

Select VID/PID `303a:1001`, USB serial `D4:05:92:B9:04:28`. Close every serial monitor. With a checkout of the platform repository at `PLATFORM`, install from inside the extracted bundle:

```sh
uv run PLATFORM/tools/device.py install sparklet .
```

`device.py` reads `project_description.json`, `sparklet-flash_args` and `partition_table/partition-table.bin` from the bundle, refuses a device whose partition table differs (not yet migrated), and writes only `sparkdash.bin` into the `sparklet` slot.

Manual fallback: first check that the device already uses the platform layout, for example with `uv run PLATFORM/tools/device.py status` (eight partitions, `sparklet` at `0x220000`). Then, with the pinned SDK activated and the discovered port:

```sh
. /Users/max/esp/esp-idf-v5.5.3/export.sh
python -m esptool --chip esp32c6 --port PORT --before default_reset --after hard_reset write_flash @sparklet-flash_args
```

Use the activated SDK's esptool command syntax for application flashing. The independently pinned esptool 5.4.0 recovery commands use their documented hyphenated syntax. Do not mix recipes or substitute guessed application offsets. Require successful data verification, then use bounded boot/live capture and check the physical screen. Successful flashing does not prove successful boot.

Both paths write only the Sparklet slot; NVS, the launcher, the bootloader and the boot selection are preserved. A device still on the single-app layout (factory image or Sparklet 1.0.0) must be migrated with the platform repository first. On an unmigrated single-app device, `0x220000` lies inside the old 6 MiB factory application, so writing `sparklet-flash_args` there would corrupt it. Full-flash restore is different: it replaces all flash, including NVS. Never run an erase operation as routine update preparation.

Downgrading to the standalone v1.0.0 bundle writes its own bootloader, partition table and 6 MiB factory app over the platform layout, including the launcher. That direction was not exercised after the migration.

## USB diagnostics

The normal sparkDash application accepts newline-terminated commands:

| Command | Effect |
| --- | --- |
| `STATUS` | Prints connection, request, memory, stack, dimming and orientation counters |
| `NEXT` | Selects the next cached node and requests a scheduling refresh |
| `PREV` | Selects the previous cached node and requests a scheduling refresh |

These are not shell commands, and the factory application has no verified compatible protocol. No normal USB command edits credentials or invokes a DGX control action. Validation builds additionally implement transport, portal, navigation, rotation, preference and `TEST_OPEN_LAUNCHER` test commands; see [testing](testing.md).

STATUS fields include uptime in milliseconds, discovered count and zero-based selection, station connectivity, completed requests/errors, current free internal heap/largest block, unused stack bytes for diagnostics/network/UI, commanded brightness, dim flag and timeout seconds. `auto_rotate` reports the saved toggle, `orientation` is 0/90/180/270 degrees clockwise from the original upright display, and `imu_ok` indicates rotation availability. Counters reset on reboot. `connected` indicates the Wi-Fi link, not independent proof of a successful current server response. Look for growing requests, stable errors, accepted data and the reported status together.

The periodic `health` line includes minimum-ever internal heap. Log output is observational; it is not proof of physical display updates. The bounded check tools write JSON and raw logs under ignored paths and close the port on completion.

## Troubleshooting

| Symptom | Interpretation and next step |
| --- | --- |
| Phone cannot join SparkDash | Ensure it uses the current setup QR/password; forget the old saved network. Confirm the display is still in setup and close serial monitors that may reset it. |
| “No Internet Connection” on setup Wi-Fi | Expected. Stay joined and open the local HTTP URL. |
| Browser says “Use the setup Wi-Fi” | The HTTP interface guard rejected the actual destination. Check the phone is joined to the setup AP and uses `http://192.168.4.1`. The prior mapped-IPv6 bug is fixed in source; record installed revision if it recurs. |
| Portal reports bad credentials | Verify the personal 2.4 GHz network and 8–63-character password. Retry in the portal; the previous saved record should remain intact. |
| Scan list empty | Use manual SSID entry. Open/enterprise entries are filtered; a scan failure does not invalidate manual configuration. |
| Wi-Fi works but server fails | Check URL, HTTP port and network reachability. Try the server's actual IPv4 through reconfiguration to isolate mDNS issues. Do not restart production services merely to test. |
| “Server access denied” | The endpoint returned 401/403. V1 has no server authentication input. Review server access policy; changing the ESP32 Wi-Fi password does not fix this. |
| “Server rate limited” | 429 caused a bounded Retry-After wait. Let it retry and inspect server policy if repeated. |
| Invalid/incompatible/oversized response | Saved cache remains; inspect a read-only API capture against the documented shape and limits. Do not increase limits without measuring memory. |
| No nodes | A valid empty configuration list was received. Check discovery on sparkDash; it is separate from metrics collection. |
| Offline node / `--` / zero | These are distinct: server-reported offline, absent metric, and a valid numeric zero. |
| Old values with recent receipt | Server samples may predate response receipt; v1 lacks collector timestamps. |
| Rectangle where an em dash appeared | Display punctuation normalization fixes common dashes/quotes. Check firmware revision; other unsupported glyphs remain possible. |
| Dim screen | Touch once or press PWR to wake, then touch again to activate a control. Check saved brightness and timeout in Settings. |
| Launcher appears instead of Sparklet | Expected after Apps, KEY or a 1 s BOOT hold, and after a power cycle that follows leaving through the launcher. Tap the Sparklet tile or press KEY. A launcher log with `reason=crash` means Sparklet failed to start three times; capture a bounded boot log. |
| “Storage unavailable; reset settings from the launcher” | NVS could not be initialized. Sparklet no longer erases the shared partition itself; use launcher Device → Reset settings, which erases every app's saved settings. |
| `idf.py flash` stops with a platform message | Intentional. Use `idf.py -p PORT sparklet-flash` or `device.py install sparklet`. |
| USB port missing | Check a data-capable cable, power, connection and enumeration. Do not select a different ESP32 just because its USB descriptor matches. |
| Port busy | Close the existing monitor/tool before flashing or starting another reader. |
| Port listed but no data | Use one bounded STATUS/boot check. Opening may reset the board. If bootloader and diagnostics both fail, stop repeated software resets and physically power-cycle, then re-enumerate. |
| Bootloader log stops at app handoff | It does not prove app success or failure by itself. Inspect the screen and diagnostics. This occurred once during rc1 development and recovered after physical reconnection; do not infer application health from bootloader output alone. |

If the unresponsive-USB condition recurs, the next step is to physically power the board off/on and leave its USB data connection attached, then report whether the screen is live, blank or frozen. If the unit has a battery, unplugging USB alone may not power it down; use its power control. If download mode is required after that, follow the exact board's documented BOOT procedure and re-enumerate. Do not use protection overrides.

## Backup, rollback and incident records

The original factory backup is a verified 16,777,216-byte image with adjacent device/time/tool/SHA-256 metadata in the platform checkout's ignored `backups/` (moved there from this repository on 2026-10-06). A separate full custom snapshot preserves the then-current configuration. Their actual recovery procedure was tested; follow the platform's [recovery procedure](https://github.com/mfellner/esp32-playground/blob/main/docs/recovery.md), including fresh backup, identity, size and checksum checks, before any full-image replacement.

A rollback to an earlier Sparklet slot bundle normally preserves current NVS; a full-image rollback restores the NVS contained in that snapshot. Keep versioned configuration compatibility in mind when changing struct layouts. Never overwrite the only factory backup, automatically erase all NVS on a parse error, burn eFuses, or bypass unreadable flash restrictions.

For a meaningful failure, record source/binary revision, SDK and lock identity, discovered USB identity, exact bounded procedure, timestamps, reset reason, memory/stack measurements and actual outcome. Keep raw evidence private; commit a reviewed summary. If a test fixture was wrong, preserve that distinction from a firmware defect. Use a new evidence filename so an earlier result cannot be silently replaced.
