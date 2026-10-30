"""Serve a writer-generated terrain fixture; exercise real engine HTTP acquisition."""
import functools
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed_paths.add(self.path)
        scenario, separator, remainder = self.path.lstrip("/").partition("/")
        if separator and scenario in {"bad-utf8", "missing-block", "corrupt-block"}:
            self.path = "/" + remainder
            is_manifest = remainder in {"terrain.asset.yaml", "catalog/catalog.cooked.yaml"}
            if not is_manifest and scenario == "missing-block":
                self.send_error(404)
                return
            body = pathlib.Path(self.translate_path(self.path)).read_bytes()
            if is_manifest and scenario == "bad-utf8":
                body += b"\xff"
            if not is_manifest and scenario == "corrupt-block":
                body = bytes([body[0] ^ 1]) + body[1:]
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="arp-terrain-http-") as folder:
        subprocess.run([executable, "--write-fixture", folder], check=True, timeout=20)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed_paths = set()
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = subprocess.run(
                [executable, "--http", f"http://127.0.0.1:{server.server_port}/", folder],
                check=False, timeout=45)
            if result.returncode:
                print("Observed terrain HTTP paths:", sorted(server.observed_paths), flush=True)
                return result.returncode
            assert "/terrain.asset.yaml" in server.observed_paths
            assert any(path.startswith("/tiles/") for path in server.observed_paths)
            assert "/catalog/catalog.cooked.yaml" in server.observed_paths
            assert urllib.parse.quote("/catalog/几何.bin") in server.observed_paths
            assert urllib.parse.quote("/tiles/高度.bin") in server.observed_paths
            for scenario in ("bad-utf8", "missing-block", "corrupt-block"):
                assert f"/{scenario}/terrain.asset.yaml" in server.observed_paths
                assert f"/{scenario}/catalog/catalog.cooked.yaml" in server.observed_paths
            return result.returncode
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    raise SystemExit(main())
