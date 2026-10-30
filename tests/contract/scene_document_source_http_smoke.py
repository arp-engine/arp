"""Serve real engine-encoded documents, then exercise formal Scene resources."""
import functools
import http.server
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="arp-scene-source-") as folder:
        subprocess.run([executable, "--write-fixtures", folder], check=True, timeout=30)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/"],
                           check=True, timeout=110)
            assert server.observed == ["/scene.yaml", "/prefab.yaml"] * 40, server.observed
            print("Scene document HTTP: 80 real reads; Scene/Prefab Passive and Runtime checks passed")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
