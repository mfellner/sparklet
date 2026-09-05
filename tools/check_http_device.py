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
import math
from pathlib import Path
import re
import threading
import time
from urllib.parse import unquote, quote
from urllib.request import urlopen
import serial
from serial.tools import list_ports
from mock_sparkdash import serve


def expected_metrics(response):
    """Independent API-to-scalar reference used to compare a captured server response."""
    m = response.get("metrics") or {}
    gpu, cpu = m.get("gpu") or {}, m.get("cpu") or {}
    vram, unified = gpu.get("vram") or {}, m.get("unifiedMemory") or {}
    power = gpu.get("power") or {}

    def number(obj, key):
        value = obj.get(key)
        return (
            value
            if type(value) in (int, float) and math.isfinite(value) and value >= 0
            else None
        )

    def memory(key):
        first = number(vram, key)
        return first if first is not None else number(unified, key)

    disks = m.get("storage") or []
    disk = next(
        (d for d in disks if d.get("label") == "/"),
        next((d for d in disks if d.get("device") == "nvme0n1p2"), {}),
    )
    network = m.get("network") or {}
    interfaces = network.get("interfaces") or []
    interface = next(
        (
            i
            for i in interfaces
            if network.get("primaryInterface")
            and i.get("name") == network["primaryInterface"]
        ),
        next(
            (
                i
                for i in interfaces
                if not i.get("disabled") and i.get("operstate") == "up"
            ),
            {},
        ),
    )
    worker = response.get("role") != "head" and (
        response.get("role") == "worker" or response.get("workerNode") is True
    )
    llm = (
        {}
        if worker
        else next((l for l in (m.get("llm") or []) if l.get("available") is True), {})
    )
    return {
        "percent": memory("percentage"),
        "used": memory("used"),
        "total": memory("total"),
        "available": memory("available"),
        "temperature": number(gpu, "temperature"),
        "usage": number(gpu, "usage"),
        "power": number(power, "draw"),
        "power_limit": number(power, "limit"),
        "cpu_usage": number(cpu, "usage"),
        "cpu_temp": number(cpu, "temperature"),
        "disk_used": number(disk, "used"),
        "disk_total": number(disk, "total"),
        "rx": number(interface, "rxSpeed"),
        "tx": number(interface, "txSpeed"),
        "generation": number(llm, "generationTps"),
        "prefill": number(llm, "prefillTps"),
    }


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
    parser.add_argument(
        "--extended",
        action="store_true",
        help="prolonged outage, restart and live reorder",
    )
    parser.add_argument(
        "--live-source",
        help="capture and compare read-only API responses from this base URL",
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
    live_snapshot = None
    if args.live_source:

        def capture(path):
            with urlopen(args.live_source.rstrip("/") + path, timeout=5) as response:
                data = response.read(16385)
                if len(data) > 16384:
                    raise ValueError("Live response exceeds supported size")
                return json.loads(data)

        live_snapshot = {"/api/sparks": capture("/api/sparks")}
        for node in live_snapshot["/api/sparks"]["sparks"]:
            path = "/api/sparks/" + quote(node["id"], safe="") + "/metrics"
            live_snapshot[path] = capture(path)
        out.with_suffix(".snapshot.json").write_text(
            json.dumps(live_snapshot, indent=2) + "\n"
        )
        server.snapshot = live_snapshot
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
                    if "qa_source:" in line:
                        current["source"] = unquote(
                            line.split("qa_source:", 1)[1].strip()
                        )
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

        saved_source = None
        try:
            usb.open()
            initial = wait(
                lambda s: s.get("connected") == 1 and "values" in s and s.get("source"),
                45,
            )
            saved_source = initial["source"]
            send(f"TEST_URL http://{args.host}:5556")
            wait(lambda s: s.get("source") == f"http://{args.host}:5556", 10)
            if live_snapshot is not None:
                records = live_snapshot["/api/sparks"]["sparks"]
                assert len(records) == 5, "Expected all five deployed nodes"
                for index, node in enumerate(records):
                    current_node = wait(
                        lambda s: s.get("node") == node["id"] and s.get("received") == 1
                    )
                    response = live_snapshot[
                        "/api/sparks/" + quote(node["id"], safe="") + "/metrics"
                    ]
                    expected = expected_metrics(response)
                    for key, value in expected.items():
                        actual = current_node["values"][key]
                        assert (actual is None and value is None) or (
                            actual is not None
                            and value is not None
                            and math.isclose(actual, value, rel_tol=1e-9, abs_tol=1e-5)
                        ), (node["id"], key, value, actual)
                    expected_role = (
                        0
                        if response.get("role") == "head"
                        else 1
                        if response.get("role") == "worker"
                        or response.get("workerNode") is True
                        else 2
                    )
                    assert current_node["role"] == expected_role
                    assert current_node["online"] == (2 if response["online"] else 1)
                    assert current_node["selected"] == index and current_node[
                        "nodes"
                    ] == len(records)
                    report["cases"].append(
                        {
                            "case": "captured deployed API comparison",
                            "node": node["id"],
                            "passed": True,
                            "fields": expected,
                        }
                    )
                    print("PASS deployed API " + node["id"], flush=True)
                    send("NEXT")
                report["source"] = args.live_source
                report["scope"] = (
                    "Frozen deployed API responses replayed through the actual ESP32 HTTP client; 16 numeric/validity fields plus role, online status, and order for each node. Freezing avoids comparing different collector samples."
                )
            else:
                baseline = wait(
                    lambda s: s.get("node") == "node1" and s.get("received") == 1
                )
                expected = {
                    "percent": 50,
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
                    {
                        "case": "normal numeric fields including valid zero",
                        "passed": True,
                    }
                )
                print("PASS normal metrics", flush=True)
                for scenario in args.cases:
                    before = wait(
                        lambda s: s.get("received") == 1
                        and s.get("status") == "Connected"
                    )
                    start = time.monotonic()
                    control.write_text(scenario)
                    if scenario in ("chunked", "limit"):
                        result = wait(
                            lambda s: s.get("received_ms", 0) > before["received_ms"]
                        )
                        assert result["values"] == expected
                    else:
                        result = wait(
                            lambda s: s.get("errors", 0) > before["errors"], 15
                        )
                        assert (
                            result.get("nodes") == 5 and result["values"] == expected
                        ), result
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
                if args.extended:
                    before = wait(
                        lambda s: s.get("status") == "Connected"
                        and s.get("received") == 1
                    )
                    # Stop only our synthetic listener; keep the production server/router untouched.
                    observations = server.observations
                    server.shutdown()
                    server.server_close()
                    failures = []
                    for increment in range(1, 6):
                        sample = wait(
                            lambda s: s.get("errors", 0)
                            >= before["errors"] + increment,
                            20,
                        )
                        assert (
                            sample.get("nodes") == 5 and sample["values"] == expected
                        ), sample
                        failures.append(time.monotonic())
                    intervals = [
                        round(b - a, 3) for a, b in zip(failures, failures[1:])
                    ]
                    assert intervals[-1] >= 3.5, intervals
                    server = serve("0.0.0.0", 5556, control=control)
                    server.observations = observations
                    threading.Thread(target=server.serve_forever, daemon=True).start()
                    restarted_at = time.monotonic()
                    wait(
                        lambda s: s.get("status") == "Connected"
                        and s.get("received_ms", 0) > before["received_ms"],
                        45,
                    )
                    report["cases"].append(
                        {
                            "case": "prolonged outage and server restart",
                            "passed": True,
                            "failure_intervals": intervals,
                            "recovery_seconds": time.monotonic() - restarted_at,
                        }
                    )
                    print("PASS prolonged outage and server restart", flush=True)
                    # Select node2, then let the existing 60-second list poll apply a reverse order.
                    send("NEXT")
                    selected = wait(
                        lambda s: s.get("node") == "node2" and s.get("selected") == 1
                    )
                    control.write_text("reorder")
                    reversed_state = wait(
                        lambda s: s.get("node") == "node2" and s.get("selected") == 3,
                        75,
                    )
                    report["cases"].append(
                        {
                            "case": "live reorder preserves selected ID",
                            "passed": True,
                            "before_index": 1,
                            "after_index": 3,
                        }
                    )
                    print("PASS live reorder preserves selected ID", flush=True)
                for scenario, count in [("empty", 0), ("many", 16), ("reorder", 5)]:
                    control.write_text(scenario)
                    send(f"TEST_URL http://{args.host}:5556")
                    result = wait(
                        lambda s: s.get("nodes") == count
                        and s.get("status") == "Connected"
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
                    lambda s: s.get("connected") == 1
                    and s.get("status") == "Connected",
                    45,
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
                        lambda s: saved_source is not None
                        and s.get("source") == saved_source
                        and s.get("node") == "dgx01"
                        and s.get("received") == 1,
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
