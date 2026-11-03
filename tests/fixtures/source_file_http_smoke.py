"""Finite loopback raw-file fixture shared by resource graph smoke tests."""
import functools
import http.server
import pathlib
import subprocess
import sys
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        with self.server.count_lock:
            self.server.gets[self.path] = self.server.gets.get(self.path, 0) + 1
        super().do_GET()


def run(executable, directory, expected, timeout=90, *, arguments=None, verify=None, cwd=None):
    """Run one exact-request oracle; optional verifier owns expected exit status.

    Arguments receives the bound URL. Existing probes keep their original
    --http interface; hosts can exercise their public CLI without a second server.
    """
    handler = functools.partial(Handler, directory=str(directory))
    server = create_browser_http_server(handler)
    server.gets = {}
    server.count_lock = threading.Lock()
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/"
        selected = arguments(url) if arguments else ["--http", url]
        result = subprocess.run([str(executable), *selected], timeout=timeout,
                                capture_output=verify is not None, text=verify is not None, cwd=cwd)
        if verify is not None:
            verify(result)
        elif result.returncode:
            return result.returncode
        assert server.gets == expected, server.gets
        print(f"PASS {sum(expected.values())} real GETs; exact file requests")
        return 0
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
