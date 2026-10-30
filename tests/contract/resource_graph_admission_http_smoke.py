"""Real HTTP IO followed by owner admission, including final-IO cutoffs."""
import functools
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.observed.append(self.path)
        super().do_GET()


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    manager = len(sys.argv) == 3 and sys.argv[2] == "--manager"
    modes = ["--manager"] if manager else []
    with tempfile.TemporaryDirectory(prefix="arp-graph-owner-") as folder:
        (pathlib.Path(folder) / "node.yaml").write_bytes(b"abcdef")
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, *modes, "--http", f"http://127.0.0.1:{server.server_port}/"],
                           check=True, timeout=45)
            # Six final-IO rejection cases plus 20 warmup and 20 measured loads.
            count = 92 if manager else 46
            assert server.observed == ["/node.yaml"] * count, server.observed
            print(f"Owner admission HTTP smoke: {count} real reads; location gates perform no requests")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
