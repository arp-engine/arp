"""Load a runtime character package through real engine HTTP with no host hint."""
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
    with tempfile.TemporaryDirectory(prefix="arp-prefab-http-") as folder:
        subprocess.run([executable, "--prepare", str(pathlib.Path(folder) / "snapshot")],
                       check=True, timeout=20)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = subprocess.run([executable, "--http", f"http://127.0.0.1:{server.server_port}/"],
                                    check=False, timeout=45)
            assert result.returncode == 0, (result.returncode, server.observed)
            expected = {"/snapshot/manifest.arpkg.yaml", "/snapshot/blobs/prefab-runtime.arpa.bin",
                        "/snapshot/resources/runtime-payload.arpres.yaml",
                        "/snapshot/resources/animator.arpres.yaml", "/snapshot/resources/prefab.arpres.yaml"}
            assert set(server.observed) == expected, server.observed
            assert len(server.observed) == 40 * len(expected)
            for offset in range(0, len(server.observed), len(expected)):
                assert server.observed[offset] == "/snapshot/manifest.arpkg.yaml"
            print("Real HTTP prefab manifest-first acquisition: 40 cycles / 200 requests PASS")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
