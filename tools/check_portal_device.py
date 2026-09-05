#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Run the QA firmware's bounded local portal test; opening USB may reset the board."""

import argparse
import json
from pathlib import Path
import re
import time
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
    out.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = port.rts = False
    port.port = ports[0].device
    checks, failures = [], []
    minimum_heap = None
    buffer = b""
    began = complete = restored_live = False
    start = time.monotonic()
    next_status = start + 5
    try:
        port.open()
        with out.with_suffix(".raw").open("xb") as raw:
            while time.monotonic() - start < 160:
                if time.monotonic() >= next_status:
                    port.write(b"STATUS\n")
                    next_status = time.monotonic() + 5
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
                        "diagnostics:" in line
                        and "nodes=5 " in line
                        and "connected=1 " in line
                    ):
                        if not began:
                            port.write(b"TEST_PORTAL\n")
                            began = True
                        elif complete:
                            restored_live = True
                    if "health:" in line:
                        match = re.search(r"minimum=(\d+)", line)
                        if match:
                            value = int(match[1])
                            minimum_heap = (
                                min(minimum_heap, value)
                                if minimum_heap is not None
                                else value
                            )
                    if "qa_portal:" in line:
                        fields = dict(re.findall(r"(\w+)=([\w-]+)", line))
                        if "check" in fields:
                            checks.append(fields)
                            if fields.get("pass") != "1":
                                failures.append(fields["check"])
                            for name, minimum in [
                                ("heap", 32768),
                                ("largest", 16384),
                                ("diag_stack", 1024),
                            ]:
                                if int(fields.get(name, 0)) < minimum:
                                    failures.append(
                                        f"{fields['check']}: {name} below target"
                                    )
                            if (
                                fields["check"]
                                not in ("saved_baseline", "volatile_unavailable_server")
                                and int(fields.get("portal_stack", 0)) < 1024
                            ):
                                failures.append(
                                    f"{fields['check']}: portal stack below target"
                                )
                        if fields.get("complete") == "1":
                            complete = fields.get("pass") == "1"
                if restored_live:
                    break
    finally:
        port.close()
    if not complete or not restored_live:
        failures.append("No completed portal test with restored five-node operation")
    if minimum_heap is None or minimum_heap < 32768:
        failures.append("Minimum-ever internal heap below target or unavailable")
    result = {
        "minimum_heap": minimum_heap,
        "checks": checks,
        "failures": failures,
        "restored_live": restored_live,
        "scope": "Actual setup HTTP server via device loopback, scan, bounded submissions, failed candidate and saved-record/cancel recovery; not phone radio association.",
    }
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                "checks": len(checks),
                "failures": failures,
                "restored_live": restored_live,
            }
        )
    )
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
