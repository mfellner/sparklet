"""Check synthetic HTTP wire scenarios and feed responses into the actual parser.
Does not claim to test ESP-IDF HTTP transport or physical-device behavior.
"""
import http.client
import importlib.util
import pathlib
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mock", ROOT / "tools/mock_sparkdash.py")
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)


class HttpFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = mock.serve(port=0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, path, timeout=2):
        c = http.client.HTTPConnection(*self.server.server_address, timeout=timeout)
        self.addCleanup(c.close)
        c.request("GET", path)
        return c.getresponse()

    def parse(self, payload, accepted=True, node=None):
        with tempfile.NamedTemporaryFile() as f:
            f.write(payload)
            f.flush()
            args = [str(ROOT / "tests/host/build/parse_fixture"), f.name]
            if node:
                args.append(node)
            p = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(p.returncode, 0 if accepted else 1, p.stdout + p.stderr)

    def test_content_length_and_chunked(self):
        for scenario in ("normal", "chunked", "empty", "many", "reorder"):
            with self.subTest(scenario=scenario):
                self.parse(self.request(f"/{scenario}/api/sparks").read())
        self.parse(self.request("/chunked/api/sparks/node1/metrics").read(), node="node1")

    def test_invalid_prescribed_inputs(self):
        for scenario in ("malformed", "oversized"):
            self.parse(self.request(f"/{scenario}/api/sparks").read(), accepted=False)

    def test_http_errors(self):
        for scenario, code in (("auth", 401), ("forbidden", 403), ("missing", 404),
                               ("rate", 429), ("error", 500)):
            r = self.request(f"/{scenario}/api/sparks")
            self.assertEqual(r.status, code)
            if scenario == "rate":
                self.assertEqual(r.getheader("Retry-After"), "3")
            r.read()

    def test_interruption(self):
        with self.assertRaises(http.client.IncompleteRead):
            self.request("/interrupted/api/sparks").read()

    def test_timeouts(self):
        with self.assertRaises(TimeoutError):
            self.request("/slow/api/sparks", timeout=.2)
        with self.assertRaises(TimeoutError):
            self.request("/stall/api/sparks", timeout=.2).read()


if __name__ == "__main__":
    unittest.main()
