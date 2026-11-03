"""Actual Interpreter CLI syntax/wire test; HTTP echo is not Editor/backend evidence."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import unittest

CLI = Path(sys.argv.pop(1)).resolve()
DIGEST = base64.b64encode(bytes(range(32))).decode("ascii")


class ReceiptCliTests(unittest.TestCase):
    def invoke(self, *arguments, url="http://127.0.0.1:0"):
        env = dict(os.environ, ARP_EDITOR_REMOTE_TOKEN="01234567890123456789012345678901")
        return subprocess.run([str(CLI), "--url", url, *arguments], env=env,
                              capture_output=True, text=True, encoding="utf-8", timeout=15)

    def test_help_and_local_arity(self):
        self.assertIn("source.receipt.query|release", self.invoke("--help").stdout)
        for name in ("source.receipt.query", "source.receipt.release"):
            valid = ["workspace", "1", "2", DIGEST, DIGEST]
            for args in ([*valid[:n]] for n in range(5)):
                self.assertEqual(self.invoke(name, *args).returncode, 2)
            self.assertEqual(self.invoke(name, *valid, "extra").returncode, 2)
            self.assertEqual(self.invoke(name, "workspace", '1,"mount":42', "2", DIGEST, DIGEST).returncode, 2)
        self.assertEqual(self.invoke("source.receipt.cancel").returncode, 2)
        self.assertEqual(self.invoke("source.receipt.cancel", "1", "extra").returncode, 2)

    def test_opaque_receipt_bytes_and_large_id_are_preserved(self):
        requests = []

        class Echo(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                requests.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"{}")

        with HTTPServer(("127.0.0.1", 0), Echo) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                url = f"http://127.0.0.1:{server.server_port}"
                for name in ("source.receipt.query", "source.receipt.release"):
                    result = self.invoke(name, "workspace", "18446744073709551615", "9007199254740993",
                                         DIGEST, DIGEST, url=url)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(requests[-1], dict(schemaVersion=1, command=name, source="workspace",
                        receipt=dict(contractVersion=1, scope=18446744073709551615, operationId=9007199254740993,
                                     sessionVersion=dict(kind="content-sha256", digest=DIGEST),
                                     requestVersion=dict(kind="content-sha256", digest=DIGEST))))
                result = self.invoke("source.receipt.cancel", "3", url=url)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(requests[-1], dict(schemaVersion=1, command="source.receipt.cancel", taskId=3))
            finally:
                server.shutdown()
                worker.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
