"""Real engine HTTP acquisition of the original Unity tool source closure."""
import argparse
from collections import Counter
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from browser_contract_runner import create_browser_http_server
from tool_source_files import ToolSourceFiles


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.gets.append(self.path)
        body = self.server.routes.get(self.path)
        status = 200 if body is not None else 404
        if self.server.mode == 'missing' and self.path == '/unity_import.arp':
            status, body = 404, b''
        elif self.server.mode == 'permission' and self.path == '/io.arp':
            status, body = 403, b''
        elif self.server.mode == 'utf8' and self.path == '/unity_import_common.arp':
            body = b'#\xff\n'
        body = body or b''
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (ConnectionAbortedError, BrokenPipeError, ConnectionResetError):
            pass


def run(args):
    files = ToolSourceFiles(ROOT)
    server = create_browser_http_server(Handler)
    server.routes, server.gets, server.mode = files.routes('/'), [], 'ok'
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        for mode in ('ok', 'missing', 'permission', 'utf8'):
            server.mode, server.gets = mode, []
            result = subprocess.run([str(args.executable.resolve()), '--http',
                f'http://127.0.0.1:{server.server_port}/', mode], capture_output=True,
                text=True, encoding='utf-8', errors='replace', timeout=60)
            (args.output / (mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            assert result.returncode == 0, (mode, result.stdout, result.stderr)
            counts = dict(Counter(server.gets))
            assert all(value == 1 for value in counts.values()), counts
            if mode == 'ok':
                assert counts == {name: 1 for name in server.routes}, counts
            elif mode == 'missing':
                assert counts == {'/unity_import.arp': 1}, counts
            elif mode == 'permission':
                assert counts['/io.arp'] == 1, counts
            else:
                assert counts == {'/unity_import.arp': 1, '/unity_import_common.arp': 1}, counts
            (args.output / (mode + '-requests.json')).write_text(json.dumps(counts, indent=2), encoding='utf-8')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    print('PASS original20 source/4 observed absences via engine HTTP, missing/403/UTF8 fail closed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
