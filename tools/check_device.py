#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Bounded live USB check; opening can reboot the board. No credentials are sent."""

import argparse
import json
from pathlib import Path
import re
import time
import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument(
        "--output", type=Path, required=True, help="new ignored result JSON"
    )
    parser.add_argument(
        "--expect-dim",
        action="store_true",
        help="verify inactivity dimming and continued polling; leave device untouched",
    )
    args = parser.parse_args()
    if not 30 <= args.seconds <= 300:
        parser.error("duration must be 30–300 seconds; this is not a soak test")
    root = Path(__file__).resolve().parents[1]
    result_path = args.output.resolve()
    if not any(result_path.is_relative_to(root / d) for d in ("logs", ".local")):
        parser.error("save hardware measurements under ignored logs/ or .local/")
    if result_path.exists() or result_path.with_suffix(".raw").exists():
        parser.error("refusing to overwrite results")
    ports = [
        p
        for p in list_ports.comports()
        if (p.vid, p.pid, p.serial_number) == (0x303A, 0x1001, "D4:05:92:B9:04:28")
    ]
    if len(ports) != 1:
        parser.error("expected exactly one known Waveshare board")
    result_path.parent.mkdir(parents=True, exist_ok=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = connection.rts = False
    connection.port = ports[0].device
    samples = []
    buffer = b""
    failures = []
    started = time.monotonic()
    try:
        connection.open()
        next_status = started + 5
        with result_path.with_suffix(".raw").open("xb") as raw:
            while time.monotonic() - started < args.seconds:
                if time.monotonic() >= next_status:
                    connection.write(b"STATUS\n")
                    next_status = time.monotonic() + 5
                data = connection.read(connection.in_waiting or 1)
                raw.write(data)
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.decode(errors="replace")
                    if any(
                        marker in line
                        for marker in (
                            "Guru Meditation",
                            "assert failed",
                            "abort() was called",
                        )
                    ):
                        failures.append("Crash signature in raw log")
                    if "diagnostics:" in line:
                        fields = {
                            key: int(value)
                            for key, value in re.findall(r"(\w+)=(\d+)", line)
                        }
                        fields["host_elapsed_ms"] = round(
                            (time.monotonic() - started) * 1000
                        )
                        samples.append(fields)
    finally:
        connection.close()
    live = [s for s in samples if s.get("connected") and s.get("nodes") == 5]
    if len(live) < 2 or live[-1]["requests"] <= live[0]["requests"]:
        failures.append("No demonstrated sequence of live five-node HTTP polling")
    if live and live[-1]["errors"] != live[0]["errors"]:
        failures.append("Request errors increased during live observation")
    for key, limit in [
        ("heap", 32768),
        ("largest", 16384),
        ("stack", 1024),
        ("net_stack", 1024),
        ("ui_stack", 1024),
    ]:
        if live and min(s.get(key, 0) for s in live) < limit:
            failures.append(f"Observed {key} below {limit}")
    if args.expect_dim:
        dimmed = [s for s in live if s.get("dimmed") == 1 and s.get("brightness") == 10]
        if len(dimmed) < 2 or dimmed[-1]["requests"] <= dimmed[0]["requests"]:
            failures.append("No demonstrated dimmed state with continued polling")
        if dimmed and dimmed[0]["uptime_ms"] < dimmed[0]["dim_after"] * 1000:
            failures.append("Dimmed earlier than the configured inactivity timeout")
    result = {
        "duration_seconds": args.seconds,
        "samples": samples,
        "failures": failures,
        "scope": "USB counters and sampled idle memory only; not touch timing or maximum response load",
    }
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                "samples": len(samples),
                "live_samples": len(live),
                "failures": failures,
                "result": str(result_path),
            }
        )
    )
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
