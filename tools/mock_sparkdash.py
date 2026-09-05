#!/usr/bin/env python3
"""Controlled, synthetic sparkDash server. No access to real DGX nodes.

Use http://HOST:5556/normal as the board URL. Other prefixes: chunked, slow,
stall, interrupted, malformed, oversized, empty, many, reorder, auth, forbidden,
missing, rate, error. Switch --scenario through an ignored control file during
hardware tests without modifying the production dashboard.
"""
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def nodes(count=5, reverse=False):
    result = [{"id": f"node{i}", "name": f"Synthetic node {i}",
               "role": "head" if i == 1 else "worker", "workerLabel": "Synthetic cluster"}
              for i in range(1, count + 1)]
    return list(reversed(result)) if reverse else result


def metrics(node):
    return {**node, "online": True, "metrics": {
        "gpu": {"temperature": 53, "usage": 0, "power": {"draw": 12.5, "limit": 120},
                "vram": {"used": 1024, "total": 2048, "available": 0, "percentage": 50}},
        "cpu": {"usage": 12, "temperature": 48},
        "storage": [{"label": "/", "used": 512, "total": 4096}],
        "network": {"primaryInterface": "eth0", "interfaces": [
            {"name": "eth0", "operstate": "up", "rxSpeed": 1024, "txSpeed": 2048}]},
        "llm": [{"available": True, "modelId": "Synthetic model", "backend": "vllm",
                 "generationTps": 7, "prefillTps": 120}] if node["role"] == "head" else []}}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        parts = self.path.strip("/").split("/")
        scenario = self.server.scenario
        if parts and parts[0] != "api":
            scenario = parts.pop(0)
        if self.server.control and self.server.control.exists():
            scenario = self.server.control.read_text().strip()
        status = {"auth": 401, "forbidden": 403, "missing": 404,
                  "rate": 429, "error": 500}.get(scenario, 200)
        records = nodes(0 if scenario == "empty" else 17 if scenario == "many" else 5,
                        scenario == "reorder")
        payload = {"sparks": records}
        if len(parts) == 4 and parts[:2] == ["api", "sparks"] and parts[3] == "metrics":
            node = next((n for n in records if n["id"] == parts[2]), None)
            if node is None:
                status, payload = 404, {"error": "Unknown node"}
            else:
                payload = metrics(node)
        elif parts != ["api", "sparks"]:
            status, payload = 404, {"error": "Unknown endpoint"}
        data = json.dumps(payload).encode()
        if scenario == "malformed":
            data = b'{"sparks":invalid}'
        if scenario == "oversized":
            data = b'{"sparks":[],"padding":"' + b'x' * 17000 + b'"}'
        try:
            if scenario == "slow":
                time.sleep(6)
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            if scenario == "rate":
                self.send_header("Retry-After", "3")
            if scenario == "chunked":
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for i in range(0, len(data), 97):
                    chunk = data[i:i + 97]
                    self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
            else:
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                if scenario == "stall":
                    self.wfile.write(data[:1])
                    self.wfile.flush()
                    time.sleep(7)
                    self.wfile.write(data[1:])
                elif scenario == "interrupted":
                    self.wfile.write(data[:len(data)//2])
                    self.close_connection = True
                else:
                    self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass


def serve(host="127.0.0.1", port=5556, scenario="normal", control=None):
    server = ThreadingHTTPServer((host, port), Handler)
    server.scenario, server.control = scenario, Path(control) if control else None
    return server


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5556)
    parser.add_argument("--scenario", default="normal")
    parser.add_argument("--control")
    args = parser.parse_args()
    with serve(args.host, args.port, args.scenario, args.control) as server:
        print(f"Synthetic server on {server.server_address}", flush=True)
        server.serve_forever()
