"""Real Vulkan HUD startup over engine HTTP, with no local/system-font fallback."""
import functools
from pathlib import Path
import subprocess
import sys
import threading

from queen_character_host_http_smoke import Handler, create_browser_http_server


class HudHandler(Handler):
    def do_GET(self):
        fault = self.server.fault
        metadata_fault = fault in ('version', 'escape', 'token') and self.path == '/hud.atlas.arpui'
        payload_fault = ((fault in ('missing', 'font') and self.path == '/DroidSans.ttf')
                         or (fault == 'png' and self.path == '/white.png'))
        if metadata_fault or payload_fault:
            self.server.gets.append(self.path)
            if fault == 'missing':
                self.send_error(404)
                return
            body = Path(self.translate_path(self.path)).read_bytes()
            if fault == 'version':
                body = body.replace(b'ui.atlas/v2', b'ui.atlas/v99', 1)
            elif fault == 'escape':
                body = body.replace(b'DroidSans.ttf', b'../outside.ttf', 1)
            elif fault == 'token':
                body = body.replace(b'queen.font', b'wrong.font', 1)
            else:
                body = bytes([body[0] ^ 1]) + body[1:]
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()


def run(executable, directory):
    server = create_browser_http_server(functools.partial(HudHandler, directory=str(directory)))
    server.gets = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        for fault in ('none', 'missing', 'version', 'font', 'png', 'escape', 'token'):
            server.gets.clear()
            server.fault = fault
            result = subprocess.run([str(executable), '--hidden', '--autoplay', '--frames', '18',
                '--hud-base-url', f'http://127.0.0.1:{server.server_port}/'],
                capture_output=True, text=True, timeout=60)
            count = 2 if fault == 'missing' else 1 if fault in ('version', 'escape') else 3
            assert len(server.gets) == count and len(set(server.gets)) == count, (fault, server.gets)
            if fault == 'none':
                assert server.gets == ['/hud.atlas.arpui', '/DroidSans.ttf', '/white.png'], server.gets
                assert result.returncode == 0, (result.stdout, result.stderr)
                for field in ('hudSource: HTTP', 'hudReads: 3', 'hudFontFamily: Droid Sans',
                              'frames: 18', 'phase: RUNNING'):
                    assert field in result.stdout, result.stdout
            else:
                assert result.returncode == 1, (fault, result.returncode, result.stdout, result.stderr)
                assert 'type: arp.build-a-queen.run' not in result.stdout, result.stdout
                assert '[vulkan-validation]' not in result.stderr, result.stderr
                expected = dict(missing='io-http-status', version='contract', font='ui-font-cache-face',
                                png='decode', escape='source-path', token='queen-hud-font-binding')[fault]
                assert expected in result.stderr.lower(), (fault, result.stderr)
            print(f'PASS Native HUD actual Vulkan {fault}: {count} exact GETs, no source fallback')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1]), Path(sys.argv[2])))
