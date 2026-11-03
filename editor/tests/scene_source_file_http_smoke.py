"""Real HTTP Scene reads; directory/Shell HTTP admission remains a separate gate."""
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
    verify_only = len(sys.argv) > 2 and sys.argv[2] == "--verify-only"
    with tempfile.TemporaryDirectory(prefix="arp-scene-source-file-") as folder:
        subprocess.run([executable, "--emit", folder], check=True, timeout=30)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/"],
                           check=True, timeout=150)
            dependencies = ["/workspace/project.arp.yaml", "/workspace/primary.arp",
                            "/workspace/secondary.arp"]
            expected = (dependencies + dependencies + ([] if verify_only else ["/workspace/scene.yaml"])) * 40
            assert server.observed == expected, server.observed
            print(f"PASS mounted source file task: {len(expected)} real GETs; exact dependency pins")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
