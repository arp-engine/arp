"""Actual engine HTTP reads of the original 13 inputs, with exact request closure."""
from collections import Counter
from functools import partial
import http.server
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'editor/tests/fixtures'))
from browser_contract_runner import create_browser_http_server
from ui_source_files import UiSourceFiles


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.gets.append(self.path)
        name = self.path.removeprefix('/')
        if name not in self.server.files.paths:
            self.send_error(404)
            return
        body = self.server.files.paths[name].read_bytes()
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Content-Type', 'application/octet-stream')
        self.end_headers()
        self.wfile.write(body)


def run(executable, ui_root, font_root, shader_root):
    files = UiSourceFiles(ui_root, font_root, shader_root)
    server = create_browser_http_server(Handler)
    server.files, server.gets = files, []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([str(executable), '--http', f'http://127.0.0.1:{server.server_port}/'], timeout=90)
        assert result.returncode == 0, result.returncode
        assert dict(Counter(server.gets)) == files.requests(), server.gets
        print('PASS 13 exact engine HTTP GETs; original UI/PNG/font/Shader bytes')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(*(Path(value).resolve() for value in sys.argv[1:])))
