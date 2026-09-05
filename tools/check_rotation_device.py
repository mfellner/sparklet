#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Measure QA cached navigation at all four angles; does not simulate physical motion/touch."""

import argparse
import json
import re
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    root = Path(__file__).resolve().parents[1]
    if not any(out.is_relative_to(root / d) for d in ("logs", ".local")):
        parser.error("use an ignored evidence directory")
    if out.exists() or out.with_suffix(".raw").exists():
        parser.error("refusing to overwrite evidence")
    ports = [
        p
        for p in list_ports.comports()
        if (p.vid, p.pid, p.serial_number) == (0x303A, 0x1001, "D4:05:92:B9:04:28")
    ]
    if len(ports) != 1:
        parser.error("expected exactly one known board")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = port.rts = False
    port.port = ports[0].device
    start = time.monotonic()
    due = start + 5
    sent = complete = False
    angle = None
    samples, groups, statuses, failures = [], [], [], []
    buffer = b""
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        port.open()
        with out.with_suffix(".raw").open("xb") as raw:
            while time.monotonic() - start < 160:
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
                    if any(
                        x in line
                        for x in (
                            "Guru Meditation",
                            "assert failed",
                            "abort() was called",
                            "Draw bitmap failed",
                        )
                    ):
                        failures.append("Crash or display-transfer failure")
                    if "diagnostics:" in line:
                        statuses.append(fields)
                        if (
                            not sent
                            and fields.get("nodes") == 5
                            and fields.get("connected") == 1
                        ):
                            port.write(b"TEST_ROTATION\n")
                            sent = True
                    if "qa_rotation:" in line:
                        if "angle" in fields:
                            angle = fields["angle"]
                            if fields.get("applied") != 1:
                                failures.append(f"Could not apply {angle} degrees")
                        complete = complete or fields.get("complete") == 1
                    if "qa_navigation:" in line:
                        fields["angle"] = angle
                        if "sample" in fields:
                            samples.append(fields)
                        if fields.get("complete") == 1:
                            groups.append(fields)
                if complete and len(statuses) >= 2:
                    break
    finally:
        port.close()
    if not complete or {g.get("angle") for g in groups} != {0, 90, 180, 270}:
        failures.append("Missing four-angle completion")
    for angle in (0, 90, 180, 270):
        angle_samples = [s for s in samples if s.get("angle") == angle]
        if len(angle_samples) != 20 or {s.get("sample") for s in angle_samples} != set(
            range(20)
        ):
            failures.append(f"Missing unique samples for {angle} degrees")
    if len(samples) != 80 or any(
        x.get("pass") != 1 or x.get("panel_ms", 1000) > 250 for x in samples
    ):
        failures.append("80 navigation samples did not all pass 250 ms")
    if any(g.get("pass") != 1 for g in groups):
        failures.append("Navigation group failed")
    for s in statuses:
        for key, minimum in {
            "heap": 32768,
            "largest": 16384,
            "stack": 1024,
            "net_stack": 1024,
            "ui_stack": 1024,
        }.items():
            if s.get(key, 0) < minimum:
                failures.append(f"{key} below {minimum}")
    result = {
        "navigation": samples,
        "groups": groups,
        "statuses": statuses,
        "failures": failures,
        "scope": __doc__,
    }
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"samples": len(samples), "groups": groups, "failures": failures}))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
