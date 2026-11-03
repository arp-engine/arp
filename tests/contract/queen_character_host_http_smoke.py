"""Real hidden Vulkan host: prepared HTTP success and no local fallback on failure."""
import functools
import http.server
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.gets.append(self.path)
        if self.server.fault == 'missing' and self.path == '/Casual.glb':
            self.send_error(404)
        elif self.server.fault == 'manifest' and self.path == '/character_manifest.yaml':
            body = b'bad: ['
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()


def run(executable, directory):
    server = create_browser_http_server(functools.partial(Handler, directory=str(directory)))
    server.gets = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        for fault, expected in (
            ('none', ['/character_manifest.yaml', '/Casual.glb', '/Formal.glb']),
            ('missing', ['/character_manifest.yaml', '/Casual.glb']),
            ('manifest', ['/character_manifest.yaml']),
        ):
            server.gets.clear()
            server.fault = fault
            result = subprocess.run([str(executable), '--hidden', '--autoplay', '--frames', '18',
                '--character-base-url', f'http://127.0.0.1:{server.server_port}/'],
                capture_output=True, text=True, timeout=60)
            assert server.gets == expected, (fault, server.gets, result.stderr)
            if fault == 'none':
                assert result.returncode == 0, (result.stdout, result.stderr)
                for field in ('characterSource: HTTP', 'characterReads: 3',
                              'type: arp.build-a-queen.run', 'frames: 18', 'phase: RUNNING'):
                    assert field in result.stdout, result.stdout
            else:
                assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
                assert 'type: arp.build-a-queen.run' not in result.stdout, result.stdout
                diagnostic = ('IO-HTTP-STATUS' if fault == 'missing'
                              else 'ARP-QUEEN-SOURCE-MANIFEST-SCHEMA')
                assert diagnostic in result.stderr, result.stderr
            print(f'PASS Queen actual Vulkan host {fault}: {len(expected)} exact HTTP GETs')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1]), Path(sys.argv[2])))
