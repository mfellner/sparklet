#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial==3.5"]
# ///
"""Exercise the real ESP32 HTTP client against a temporary synthetic LAN server.
Requires a SPARKDASH_TEST_COMMANDS build. Always restores the saved URL in finally.
"""

import argparse
import json
from pathlib import Path
import re
import threading
import time
from urllib.parse import unquote
import serial
from serial.tools import list_ports
from mock_sparkdash import serve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="this Mac LAN IPv4 address")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--cases",
        nargs="+",
        default=[
            "chunked",
            "limit",
            "auth",
            "forbidden",
            "missing",
            "rate",
            "error",
            "malformed",
            "oversized",
            "interrupted",
            "slow",
            "stall",
            "drip",
        ],
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if (
        not any(out.is_relative_to(root / d) for d in ("logs", ".local"))
        or out.exists()
    ):
        parser.error("choose a new file in ignored logs/ or .local/")
    out.parent.mkdir(parents=True, exist_ok=True)
    control = out.with_suffix(".scenario")
    control.write_text("normal")
    server = serve("0.0.0.0", 5556, control=control)
    server.observations = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    ports = [
        p
        for p in list_ports.comports()
        if (p.vid, p.pid, p.serial_number) == (0x303A, 0x1001, "D4:05:92:B9:04:28")
    ]
    if len(ports) != 1:
        raise RuntimeError("Expected exactly one known board")
    usb = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    usb.dtr = usb.rts = False
    usb.port = ports[0].device
    report = {
        "cases": [],
        "failures": [],
        "scope": "Real IDF HTTP transport and USB cache state; no touch simulation",
    }
    buffer = b""
    current = {}
    with out.with_suffix(".raw").open("xb") as raw:

        def send(command):
            usb.write(command.encode() + b"\n")

        def state(timeout=3):
            nonlocal buffer, current
            send("STATUS")
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                data = usb.read(usb.in_waiting or 1)
                raw.write(data)
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.decode(errors="replace")
                    if any(
                        s in line
                        for s in (
                            "Guru Meditation",
                            "assert failed",
                            "abort() was called",
                        )
                    ):
                        raise RuntimeError("Firmware crash; see raw log")
                    if "diagnostics:" in line:
                        current = {
                            k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)
                        }
                    if " qa:" in line:
                        for k, v in re.findall(r"(\w+)=([^\s]*)", line):
                            current[k] = int(v) if v.isdigit() else unquote(v)
                        current["values"] = {}
                    if "qa_value:" in line and "values" in current:
                        m = re.search(r"qa_value: (\w+)=([^ ]+) valid=(\d)", line)
                        if m:
                            key, value, valid = m.groups()
                            current["values"][key] = (
                                float(value) if valid == "1" else None
                            )
                            if key == "prefill":
                                return current.copy()
            return current.copy()

        def wait(predicate, timeout=45):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                s = state()
                if predicate(s):
                    return s
                time.sleep(0.3)
            raise RuntimeError("State timeout: " + json.dumps(current))

        try:
            usb.open()
            wait(lambda s: s.get("connected") == 1 and "values" in s, 45)
            send(f"TEST_URL http://{args.host}:5556")
            baseline = wait(
                lambda s: s.get("node") == "node1" and s.get("received") == 1
            )
            expected = {
                "used": 1024,
                "total": 2048,
                "available": 0,
                "temperature": 53,
                "usage": 0,
                "power": 12.5,
                "power_limit": 120,
                "cpu_usage": 12,
                "cpu_temp": 48,
                "disk_used": 512,
                "disk_total": 4096,
                "rx": 1024,
                "tx": 2048,
                "generation": 7,
                "prefill": 120,
            }
            assert baseline["values"] == expected, baseline
            report["cases"].append(
                {"case": "normal numeric fields including valid zero", "passed": True}
            )
            print("PASS normal metrics", flush=True)
            for scenario in args.cases:
                before = wait(
                    lambda s: s.get("received") == 1 and s.get("status") == "Connected"
                )
                start = time.monotonic()
                control.write_text(scenario)
                if scenario in ("chunked", "limit"):
                    result = wait(
                        lambda s: s.get("received_ms", 0) > before["received_ms"]
                    )
                    assert result["values"] == expected
                else:
                    result = wait(lambda s: s.get("errors", 0) > before["errors"], 15)
                    assert result.get("nodes") == 5 and result["values"] == expected, (
                        result
                    )
                observed_failure_at = time.monotonic()
                elapsed = observed_failure_at - start
                request_started = next(
                    (
                        t
                        for t, name, _ in server.observations
                        if t >= start and name == scenario
                    ),
                    start,
                )
                control.write_text("normal")
                recovered = wait(
                    lambda s: s.get("status") == "Connected"
                    and s.get("received_ms", 0) > result["received_ms"],
                    45,
                )
                report["cases"].append(
                    {
                        "case": scenario,
                        "passed": True,
                        "error_or_refresh_seconds": round(elapsed, 3),
                        "from_first_request_seconds": round(
                            observed_failure_at - request_started, 3
                        ),
                        "recovery_seconds": round(
                            time.monotonic() - start - elapsed, 3
                        ),
                        "heap": recovered.get("heap"),
                        "largest": recovered.get("largest"),
                    }
                )
                print("PASS " + scenario, flush=True)
            for scenario, count in [("empty", 0), ("many", 16), ("reorder", 5)]:
                control.write_text(scenario)
                send(f"TEST_URL http://{args.host}:5556")
                result = wait(
                    lambda s: s.get("nodes") == count and s.get("status") == "Connected"
                )
                if scenario == "reorder":
                    assert result.get("node") == "node5", result
                report["cases"].append(
                    {"case": scenario, "passed": True, "count": result["nodes"]}
                )
                print("PASS " + scenario, flush=True)
            control.write_text("normal")
            send("TEST_RECONNECT")
            started = time.monotonic()
            wait(lambda s: s.get("connected") == 0, 10)
            wait(
                lambda s: s.get("connected") == 1 and s.get("status") == "Connected", 45
            )
            report["cases"].append(
                {
                    "case": "device Wi-Fi reconnect",
                    "passed": True,
                    "seconds": time.monotonic() - started,
                }
            )
        except Exception as error:
            report["failures"].append(str(error))
            print("FAIL " + str(error), flush=True)
        finally:
            if usb.is_open:
                send("TEST_RESET")
                try:
                    restored = wait(
                        lambda s: s.get("node") == "dgx01" and s.get("received") == 1,
                        45,
                    )
                    report["saved_server_restored"] = True
                except Exception as error:
                    report["saved_server_restored"] = False
                    report["failures"].append("Restore: " + str(error))
                usb.close()
            server.shutdown()
            server.server_close()
            report["requests"] = [
                {"time": t, "scenario": s, "path": p} for t, s, p in server.observations
            ]
            out.write_text(json.dumps(report, indent=2) + "\n")
    print("Result: " + str(out), flush=True)
    return bool(report["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
