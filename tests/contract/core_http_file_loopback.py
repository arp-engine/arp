"""Run the native IO contract against a real loopback server; no external service."""
import pathlib
import subprocess
import sys
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from browser_contract_runner import create_browser_http_server
from http_file_fixture_server import HttpFileFixtureHandler


def main():
    server = create_browser_http_server(HttpFileFixtureHandler)
    server.redirect_followed = False
    server.request_lines = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}/fixture/"],
                                timeout=40, check=False)
        if server.redirect_followed:
            raise AssertionError("IO-HTTP-REDIRECT-WAS-FOLLOWED")
        if result.returncode:
            print("Observed HTTP requests:", server.request_lines, flush=True)
        return result.returncode
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    raise SystemExit(main())
