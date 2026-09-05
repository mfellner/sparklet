# Automatic rotation bring-up

## Recovery snapshot

Before flashing, enumeration matched VID:PID `303a:1001`, serial
`D4:05:92:B9:04:28`, port `/dev/cu.usbmodem2101`. esptool 5.4.0 read the full
16,777,216-byte installed image into ignored
`backups/pre-rotation-2026-09-06.bin`.
SHA-256: `22165dc912c493bf78f4b46ef3266656334888d8ef2db79ae65d6d7a2739ddfa`.
The adjacent `.sha256` records the checksum. The image may contain credentials;
it must remain outside Git. QA flashing used the project's generated arguments.

## Observations

- Upright sensor data: approximately (-26, 951, -138) mg, with no extra PMIC writes.
  Initial saturated values are rejected by the magnitude gate.
- User confirmed all four physical orientations and aligned Settings/Back and
  navigation arrows. Evidence is user observation, not simulated touch.
- `logs/rotation-physical-check.json`: bounded 60-second QA run passed, 11 STATUS
  samples, 9 connected samples, five nodes, no errors. Two physical button samples
  in its raw log each took 6 ms from sampled input to completed SPI transfer;
  including one 33 ms input poll gives a 39 ms bound. Optical scan-out is unmeasured.
- Host ASan/UBSan suites passed: core, portal address, and new rotation tests.
  Rotation coverage includes exact pixel expectations, all 480×480 inverse touch
  coordinates at every angle, stripe rectangle alignment, settling/invalid-motion
  handling, and explicit legacy preference decoding (including every padding byte).

Further acceptance results are recorded below as completed. Raw logs remain ignored.

## QA harness correction

The first automated Settings run (`logs/rotation-prefs-off.json`) failed with a
stack-protection fault in `spark_diag`: the new QA function constructed widgets on
the 5 KiB diagnostics stack. It had not completed the save. The test was moved to
an LVGL asynchronous callback on the existing 20 KiB UI task; the diagnostics task
only schedules the work and waits for acknowledgement. Normal Settings already
runs on the UI task. This failure was in the added test harness, not evidence of a
successful preference check; subsequent runs are recorded separately.

## Settings and persistence

After moving the test to the UI task, both
`logs/rotation-prefs-off-retest.json` and `logs/rotation-prefs-on-retest.json`
passed. The test used actual Settings widgets, Back, and Save callbacks. Rotation
retained the unsaved switch/slider values and did not change the activity timestamp;
Back left the saved preference unchanged; Save acknowledged success and preserved
brightness 60% and dim delay 120 seconds.

The second test observed a new boot with `auto_rotate=0 orientation=0`, then saved
on and observed `auto_rotate=1 orientation=90` with the physical device on its side.
This establishes saved-off reboot persistence and re-enabling rotation. It is
programmatic UI-event/NVS evidence; the separately requested manual slider/scroll
check was not reported by the user.

## Four-angle timing

`logs/rotation-timing-final.json` passed all 80 cached-selection-to-completed-SPI
samples: 20 at each orientation. Maximum times were 162 ms (0°), 166 ms (90°),
171 ms (180°), and 171 ms (270°), below the 250 ms gate. The test restores the
starting angle and does not write preferences. Physical accelerometer triggering
and touch alignment are covered by the earlier user check, not this forced-angle test.

## Setup mode

`logs/rotation-portal-final.json` passed all 20 actual device HTTP/setup checks and
restored live polling. Minimum-ever internal heap was 34,128 bytes, above 32 KiB.
The board remained sideways with auto-rotate enabled while displaying setup.
This validates setup transport and memory with the rotation implementation; it does
not establish a new phone QR-decoding or radio-association result.

## Final normal firmware

The normal image was flashed with generated arguments and has application SHA-256
`66c1e44bda5136b8ea7068f5e78e87c857d094abf5d9b8ce3bbdf392233e1525`.
Its binary was checked for absence of QA preference/rotation/server controls and
raw IMU logging. `logs/rotation-normal-dim-final.json` passed a 150-second run:
29 STATUS samples, 27 connected/live samples, five nodes and stable zero errors.
All samples reported saved-on rotation and an available sensor, at 90° with the
board on its side. The display dimmed to 10% after the saved 120-second delay while
polling continued. This also confirms saved-on persistence through normal flashing
and another observed boot.

- Minimum sampled free internal heap: 66,432 bytes.
- Minimum sampled largest free block: 47,104 bytes.
- Minimum sampled diagnostic stack spare: 3,252 bytes.
- Minimum sampled network stack spare: 2,968 bytes.
- Minimum sampled UI stack spare: 13,740 bytes.
