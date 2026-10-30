"""A sealed package's explicit files acquired by real engine HTTP, then loaded atomically."""
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
        self.server.observed_paths.add(self.path)
        self.server.path_order.append(self.path)
        super().do_GET()


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="arp-package-http-") as folder:
        package = pathlib.Path(folder) / "snapshot"
        subprocess.run([executable, "--prepare-closure", str(package)], check=True, timeout=20)
        server = create_browser_http_server(functools.partial(Handler, directory=folder))
        server.observed_paths = set()
        server.path_order = []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/"
            command = ([executable, "--async-http", url] if len(sys.argv) > 2 and sys.argv[2] == "--async"
                       else [executable, "--source-http", str(package), url])
            result = subprocess.run(command, check=False, timeout=45)
            if result.returncode:
                print("Observed package HTTP paths:", sorted(server.observed_paths), flush=True)
                return result.returncode
            for path in ("manifest.arpkg.yaml", "closure.arpkg.yaml", "catalog/catalog.cooked.yaml", "catalog/geometry.bin"):
                assert "/snapshot/" + path in server.observed_paths
            assert any(path.startswith("/snapshot/resources/") for path in server.observed_paths)
            if len(sys.argv) > 2 and sys.argv[2] == "--async":
                assert server.path_order[0] == "/snapshot/manifest.arpkg.yaml"
            return 0
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    raise SystemExit(main())
