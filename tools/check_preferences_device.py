#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Check QA Settings/Back/Save behavior and a preference retained across USB reboot."""

import argparse
import json
import re
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--save", choices=("on", "off"), required=True)
    p.add_argument("--expect-boot", choices=("on", "off"))
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = args.output.resolve()
    root = Path(__file__).resolve().parents[1]
    if not any(out.is_relative_to(root / d) for d in ("logs", ".local")):
        p.error("use an ignored evidence directory")
    if out.exists() or out.with_suffix(".raw").exists():
        p.error("refusing to overwrite evidence")
    ports = [
        x
        for x in list_ports.comports()
        if (x.vid, x.pid, x.serial_number) == (0x303A, 0x1001, "D4:05:92:B9:04:28")
    ]
    if len(ports) != 1:
        p.error("expected exactly one known board")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = port.rts = False
    port.port = ports[0].device
    start = time.monotonic()
    due = start + 5
    sent = complete = verified = saw_boot = False
    statuses, checks, failures = [], [], []
    buffer = b""
    target = int(args.save == "on")
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        port.open()
        with out.with_suffix(".raw").open("xb") as raw:
            while time.monotonic() - start < 90:
                if time.monotonic() >= due:
                    port.write(b"STATUS\n")
                    due = time.monotonic() + 5
                data = port.read(port.in_waiting or 1)
                raw.write(data)
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.decode(errors="replace")
                    fields = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}
                    saw_boot |= "sparkdash: BOOT" in line
                    if any(
                        x in line
                        for x in (
                            "Guru Meditation",
                            "assert failed",
                            "abort() was called",
                        )
                    ):
                        failures.append("Crash signature")
                    if "diagnostics:" in line:
                        statuses.append(fields)
                        if not sent and fields.get("connected") == 1:
                            if args.expect_boot and fields.get("auto_rotate") != int(
                                args.expect_boot == "on"
                            ):
                                failures.append(
                                    "Saved preference did not survive reboot"
                                )
                                break
                            port.write(
                                b"TEST_PREFS_ON\n" if target else b"TEST_PREFS_OFF\n"
                            )
                            sent = True
                        elif complete:
                            verified = fields.get("auto_rotate") == target and (
                                target or fields.get("orientation") == 0
                            )
                    if "qa_preferences:" in line:
                        checks.append(fields)
                        complete = (
                            fields.get("complete") == 1
                            and fields.get("pass") == 1
                            and fields.get("auto_rotate") == target
                        )
                        if not complete:
                            failures.append("QA Settings test failed")
                if verified or failures:
                    break
    finally:
        port.close()
    if not complete or not verified:
        failures.append("Missing successful save and STATUS readback")
    if args.expect_boot and not saw_boot:
        failures.append("No observed reboot; persistence not established")
    result = {
        "statuses": statuses,
        "checks": checks,
        "saw_boot": saw_boot,
        "failures": failures,
        "scope": __doc__,
    }
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": checks, "saw_boot": saw_boot, "failures": failures}))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
