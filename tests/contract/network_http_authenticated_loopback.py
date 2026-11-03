"""Verify actual engine Client POST wire and ordinary lifecycle, not a mock client."""
from pathlib import Path
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "fixtures"))
from http_authenticated_fixture import create_authenticated_server


def main():
    server = create_authenticated_server()
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}"], timeout=30)
        assert result.returncode == 0, result.returncode
        assert server.posts == 26 and not server.redirect_followed
        assert server.preflights == 0
        print("PASS actual engine POST wire, authorization, UTF-8, no redirect/retry and 20 ordinary cycles")
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    main()
