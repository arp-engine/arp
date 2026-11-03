"""Actual Editor UI/viewport startup with explicit remote inputs and no local rescue."""
from collections import Counter
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import threading

import yaml

from ui_sources_http_smoke import Handler, ROOT
from fixtures.ui_source_files import UiSourceFiles

sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import create_browser_http_server


class UiHandler(Handler):
    def do_GET(self):
        fault = self.server.fault
        name = self.path.removeprefix('/')
        files = self.server.files
        if ((fault == 'missing' and name == 'Deng.ttf')
                or (fault == 'profile_missing' and name == files.profile)):
            self.server.gets.append(self.path)
            self.send_error(404)
            return
        altered = ((fault in ('version', 'duplicate', 'escape') and name == files.profile)
                   or (fault == 'gui_contract' and name == files.publication)
                   or (fault in ('empty', 'document') and name == files.document)
                   or (fault == 'atlas' and name == files.atlas)
                   or (fault == 'font' and name == 'Deng.ttf')
                   or (fault == 'shader' and name == 'gui_overlay.spv'))
        if altered:
            self.server.gets.append(self.path)
            body = files.paths[name].read_bytes()
            if fault == 'gui_contract':
                body = files.paths['editor_viewport_sprite.artifact-set.yaml'].read_bytes()
            elif fault in ('version', 'duplicate', 'escape'):
                node = yaml.safe_load(body)
                if fault == 'version':
                    node['contractVersion'] = 99
                elif fault == 'duplicate':
                    node['fonts'][1]['token'] = node['fonts'][0]['token']
                else:
                    node['atlas'] = '../outside.png'
                body = yaml.safe_dump(node, sort_keys=False, allow_unicode=True).encode('utf-8')
            elif fault == 'empty':
                body = b''
            else:
                body = bytes([body[0] ^ 0xff]) + body[1:]
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


def add_viewports(files, shader_root):
    for publication in ('editor_viewport_pbr.artifact-set.yaml',
                        'editor_viewport_sprite.artifact-set.yaml',
                        'editor_environment/editor_viewport_environment.artifact-set.yaml'):
        files.add(shader_root, publication)
        node = yaml.safe_load(files.paths[publication].read_bytes())
        assert node['type'] == 'GeneratedShaderArtifactSet' and node['contractVersion'] == 1
        for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
            files.add(shader_root, (PurePosixPath(publication).parent /
                                   files.safe(node['files'][role]['path'])).as_posix())


def run(executable, ui_root, font_root, shader_root, output):
    output.mkdir(parents=True, exist_ok=True)
    files = UiSourceFiles(ui_root, font_root, shader_root)
    ui_expected = files.requests()
    add_viewports(files, shader_root)
    server = create_browser_http_server(UiHandler)
    server.files, server.gets, server.fault = files, [], 'none'
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/'
        with tempfile.TemporaryDirectory(prefix='arp-editor-ui-host-') as directory:
            for mode in ('none', 'all', 'profile_missing', 'missing', 'version', 'duplicate',
                         'escape', 'empty', 'document', 'atlas', 'font', 'shader', 'gui_contract', 'url_empty', 'url_duplicate'):
                server.fault = mode
                server.gets.clear()
                args = [str(executable), '--smoke', '--viewport-runtime-smoke', '--project-root',
                        str(ROOT / 'tests/fixtures/editor_tilemap_project'),
                        '--ui-base-url', '' if mode == 'url_empty' else url]
                if mode == 'all':
                    args += ['--viewport-shader-base-url', url]
                if mode == 'url_duplicate':
                    args += ['--ui-base-url', url]
                result = subprocess.run(args, cwd=directory, capture_output=True, text=True, timeout=60)
                text = result.stdout + result.stderr
                (output / (mode + '.log')).write_text(text, encoding='utf-8')
                if mode in ('none', 'all'):
                    assert result.returncode == 0, text
                    assert 'editorUiSource: { profile: NativeEditorUiSources/1, files: 13' in text
                    assert 'ioRetired: true' in text and 'viewport mesh/texture/Sprite cache owner retained' in text
                    assert dict(Counter(server.gets)) == (files.requests() if mode == 'all' else ui_expected), server.gets
                else:
                    assert result.returncode == (2 if mode == 'url_duplicate' else 1), (mode, result.returncode, text)
                    assert 'editorUiSource:' not in text and '[vulkan-swapchain]' not in text, text
                    expected = {'profile_missing':'IO-HTTP-STATUS', 'missing':'IO-HTTP-STATUS',
                                'version':'EDITOR-UI-SOURCE-PROFILE-CONTRACT',
                                'duplicate':'EDITOR-UI-SOURCE-PROFILE-INVALID',
                                'escape':'EDITOR-UI-SOURCE-PROFILE-INVALID',
                                'empty':'UI-SOURCES', 'document':'UI-SOURCES',
                                'atlas':'EDITOR-UI-SOURCE-ATLAS-CONTRACT', 'font':'UI-FONT-CACHE-FACE',
                                'gui_contract':'RHI-GENERATED-PACKAGE-ROLE-BINDING',
                                'shader':'HASH', 'url_empty':'EDITOR-UI-SOURCE-URL-EMPTY',
                                'url_duplicate':'EDITOR-UI-SOURCE-URL-DUPLICATE'}[mode]
                    assert expected in text, (mode, text)
                    if mode.startswith('url_'):
                        assert not server.gets, server.gets
                print(f'PASS actual Editor UI source {mode}: {len(server.gets)} GETs')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == '__main__':
    raise SystemExit(run(*(Path(value).resolve() for value in sys.argv[1:])))
