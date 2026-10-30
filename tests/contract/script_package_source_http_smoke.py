"""Real HTTP manifest/source acquisition followed by source-only AOT generation."""
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
    with tempfile.TemporaryDirectory(prefix="arp-script-source-") as folder:
        subprocess.run([executable, "--write-fixtures", folder], check=True, timeout=30)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/"],
                           check=True, timeout=110)
            expected = ["/snapshot/package.yaml", "/snapshot/basic.arp", "/snapshot/math_dep.arp"] * 40
            assert server.observed == expected, server.observed
            print("Script package HTTP: 120 real GETs; source-only AOT generation matched")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
