#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Measure QA navigation and physical button input through completed SPI transfers."""

import argparse
import json
from pathlib import Path
import re
import time
import serial
from serial.tools import list_ports


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--seconds", type=int, default=120)
    p.add_argument(
        "--touch-only",
        action="store_true",
        help="capture physical touches without repeating navigation",
    )
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if not 30 <= args.seconds <= 180 or not any(
        out.is_relative_to(root / d) for d in ("logs", ".local")
    ):
        p.error("use 30–180 seconds and an ignored evidence directory")
    if out.exists() or out.with_suffix(".raw").exists():
        p.error("refusing overwrite")
    ports = [
        x
        for x in list_ports.comports()
        if (x.vid, x.pid, x.serial_number) == (0x303A, 0x1001, "D4:05:92:B9:04:28")
    ]
    if len(ports) != 1:
        p.error("expected known board")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = port.rts = False
    port.port = ports[0].device
    start = time.monotonic()
    due = start + 5
    nav = []
    touch = []
    complete = args.touch_only
    sent = args.touch_only
    failures = []
    buffer = b""
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        port.open()
        with out.with_suffix(".raw").open("xb") as raw:
            while time.monotonic() - start < args.seconds:
                if time.monotonic() >= due:
                    port.write(b"STATUS\n")
                    due = time.monotonic() + 5
                data = port.read(port.in_waiting or 1)
                raw.write(data)
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.decode(errors="replace")
                    if any(
                        x in line
                        for x in (
                            "Guru Meditation",
                            "assert failed",
                            "abort() was called",
                        )
                    ):
                        failures.append("Crash signature")
                    if (
                        not sent
                        and "diagnostics:" in line
                        and "nodes=5 " in line
                        and "connected=1 " in line
                    ):
                        port.write(b"TEST_NAV\n")
                        sent = True
                    if "qa_navigation:" in line:
                        fields = {
                            k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)
                        }
                        if "sample" in fields:
                            nav.append(fields)
                        if fields.get("complete") == 1:
                            complete = fields.get("pass") == 1
                    if "qa_touch:" in line:
                        m = re.search(r"input_to_panel_ms=(\d+)", line)
                        if m:
                            touch.append(int(m[1]))
                if complete and len(touch) >= 4:
                    break
    finally:
        port.close()
    if not args.touch_only and (
        not complete or len(nav) != 20 or any(x.get("pass") != 1 for x in nav)
    ):
        failures.append("Twenty cached-navigation samples did not pass 250 ms")
    if not touch:
        failures.append("No physical button samples received")
    elif max(touch) + 33 > 150:
        failures.append(
            "Input-to-panel latency plus one 33 ms input poll period exceeds 150 ms"
        )
    result = {
        "navigation": nav,
        "physical_button_ms": touch,
        "input_poll_period_ms": 33,
        "failures": failures,
        "scope": "Input sampling / accepted cached selection through SPI transfer completion. Adds one configured input poll period for touch bound; panel optical scan-out is not measured.",
    }
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                "navigation_samples": len(nav),
                "touch_samples": len(touch),
                "max_touch_ms": max(touch, default=None),
                "failures": failures,
            }
        )
    )
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
