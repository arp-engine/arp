"""Actual HTTP script document reads, shared native conversion and core baselines."""
import functools
import http.server
from pathlib import Path
import subprocess
import sys
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
    server = create_browser_http_server(functools.partial(Handler,
        directory=str(ROOT / "tests/fixtures/project_resources")))
    server.observed = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        subprocess.run([str(Path(sys.argv[1]).resolve()), "--script-http",
            f"http://127.0.0.1:{server.server_port}/"], check=True, timeout=150)
        expected = ["/workspace/project.arp.yaml", "/workspace/scripts/gameplay.arp",
            "/workspace/scripts/gameplay.arp"] * 40
        assert server.observed == expected, server.observed
        print("PASS source script input/reload: 120 real GETs; exact raw/BOM/CRLF baseline; no parse rereads")
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
