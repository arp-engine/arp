"""Real Vulkan host: prepared Shader HTTP success and failure without cold fallback."""
from collections import Counter
import functools
from pathlib import Path
import subprocess
import sys
import threading

from queen_character_host_http_smoke import Handler, create_browser_http_server


class ShaderHandler(Handler):
    def do_GET(self):
        fault = self.server.fault
        if ((fault in ('missing', 'version') and self.path == '/queen_scene.artifact-set.yaml')
                or (fault == 'hash' and self.path == '/queen_scene.spv')):
            self.server.gets.append(self.path)
            if fault == 'missing':
                self.send_error(404)
                return
            body = Path(self.translate_path(self.path)).read_bytes()
            if fault == 'version':
                body = body.replace(b'contractVersion: 1', b'contractVersion: 99', 1)
            else:
                body = bytes([body[0] ^ 1]) + body[1:]
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()


def run(executable, directory):
    server = create_browser_http_server(functools.partial(ShaderHandler, directory=str(directory)))
    server.gets = []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        for fault in ('none', 'missing', 'hash', 'version'):
            server.gets.clear()
            server.fault = fault
            result = subprocess.run([str(executable), '--hidden', '--autoplay', '--frames', '18',
                '--shader-base-url', f'http://127.0.0.1:{server.server_port}/'],
                capture_output=True, text=True, timeout=60)
            assert max(Counter(server.gets).values(), default=0) == 1, server.gets
            if fault == 'none':
                assert result.returncode == 0, (result.stdout, result.stderr)
                assert len(server.gets) == 36, server.gets
                for field in ('shaderSource: HTTP', 'shaderReads: 36', 'frames: 18', 'phase: RUNNING'):
                    assert field in result.stdout, result.stdout
            else:
                assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
                assert 'type: arp.build-a-queen.run' not in result.stdout, result.stdout
                expected = {'missing': 'IO-HTTP-STATUS', 'hash': 'HASH', 'version': 'SHADER-GENERATED-PUBLICATION-CONTRACT'}[fault]
                assert expected in result.stderr, result.stderr
                if fault in ('missing', 'version'):
                    assert server.gets == ['/queen_scene.artifact-set.yaml'], server.gets
            print(f'PASS Native Shader actual Vulkan host {fault}: {len(server.gets)} exact HTTP GETs')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1]), Path(sys.argv[2])))
