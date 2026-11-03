"""Actual Editor native startup: remote viewports and failure before GPU creation."""
from collections import Counter
import functools
import http.server
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

from viewport_shader_sources_http_smoke import ROOT, expected_files

sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.paths.append(self.path)
        publication = '/editor_viewport_pbr.artifact-set.yaml'
        fault = self.server.fault
        if self.path == publication and fault == 'missing':
            self.send_error(404)
            return
        if ((self.path == publication and fault in ('version', 'abi'))
                or (self.path == '/editor_viewport_pbr.spv' and fault == 'hash')):
            if fault == 'abi':
                body = (Path(self.directory) / 'editor_viewport_sprite.artifact-set.yaml').read_bytes()
            else:
                body = Path(self.translate_path(self.path)).read_bytes()
                body = (body.replace(b'contractVersion: 1', b'contractVersion: 99', 1)
                        if fault == 'version' else bytes([body[0] ^ 1]) + body[1:])
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


def run(executable, directory, output):
    output.mkdir(parents=True, exist_ok=True)
    server = create_browser_http_server(functools.partial(Handler, directory=str(directory)))
    server.paths = []
    server.fault = 'none'
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/'
        # Valid cold publications stay in the Editor build root throughout.
        with tempfile.TemporaryDirectory(prefix='arp-editor-shader-host-') as current:
            for mode in ('none', 'missing', 'hash', 'version', 'abi', 'empty', 'duplicate'):
                server.fault = mode
                server.paths.clear()
                args = [str(executable), '--smoke', '--viewport-runtime-smoke',
                        '--project-root', str(ROOT / 'tests/fixtures/editor_tilemap_project'),
                        '--viewport-shader-base-url', '' if mode == 'empty' else url]
                if mode == 'duplicate':
                    args += ['--viewport-shader-base-url', url]
                result = subprocess.run(args, cwd=current, capture_output=True, text=True, timeout=60)
                (output / (mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
                if mode == 'none':
                    assert result.returncode == 0, (result.stdout, result.stderr)
                    assert dict(Counter(server.paths)) == expected_files(directory), server.paths
                    assert 'files: 18' in result.stdout and 'ioRetired: true' in result.stdout
                    assert 'viewport mesh/texture/Sprite cache owner retained' in result.stdout
                else:
                    assert result.returncode == (2 if mode == 'duplicate' else 1), (mode, result.returncode, result.stdout, result.stderr)
                    assert 'editorViewportShaderSource:' not in result.stdout
                    assert '[vulkan-swapchain]' not in result.stdout + result.stderr
                    expected = {'missing':'IO-HTTP-STATUS', 'hash':'HASH',
                                'version':'SHADER-GENERATED-PUBLICATION-CONTRACT',
                                'abi':'EDITOR-VIEWPORT-SHADER-CANDIDATE',
                                'empty':'EDITOR-VIEWPORT-SHADER-URL-EMPTY',
                                'duplicate':'EDITOR-VIEWPORT-SHADER-URL-DUPLICATE'}[mode]
                    assert expected in result.stdout + result.stderr, (mode, result.stdout, result.stderr)
                    if mode in ('missing','version'):
                        assert server.paths == ['/editor_viewport_pbr.artifact-set.yaml'], server.paths
                    if mode in ('empty','duplicate'):
                        assert not server.paths, server.paths
                print(f'PASS actual Editor viewport shader host {mode}: {len(server.paths)} GETs')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(*(Path(value).resolve() for value in sys.argv[1:])))
