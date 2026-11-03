"""Real DLL-hosted CLI document reads; no UI publication success is claimed."""
import argparse
from pathlib import Path
import subprocess
import threading

import yaml

from tool_document_cli_smoke import Handler, create_browser_http_server


class RoutedHandler(Handler):
    def do_GET(self):
        self.server.response = self.server.routes.get(self.path, (404, b''))
        super().do_GET()


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    directory = args.output.resolve()
    executable = str(args.executable.resolve())
    server = create_browser_http_server(RoutedHandler)
    server.response, server.requests = (200, b''), []
    server.routes = {'/manifest.txt': (200, b'\xff')}
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f'http://127.0.0.1:{server.server_port}/'
    reports, expected_requests = [], []
    material = (b'\xef\xbb\xbf{\r\n"type":"SceneStylizedPbr","name":"RoutedMaterial",'
                b'"baseColorTexture":"declared.ktx2"}\r\n')
    mesh = (b'{"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":'
            b'{"POSITION":[0,0,0,1,0,0,0,1,0]},"indices":[0,1,2]}]}]}')
    # Existing external Legacy contract, never a new production authoring format.
    layout = (b'{"type":"GuiLayout","name":"RoutedLayout","nodes":['
              b'{"id":1,"type":"Panel","width":240,"height":120}]}')
    filename = 'document.txt'
    local = directory / filename
    artifact = directory / 'must-remain.arpui'
    sentinel = b'existing output stays unchanged'
    artifact.write_bytes(sentinel)

    def invoke(name, flag, raw, success=False, diagnostic=None, extra=(), status=200,
               reads=None):
        local.write_bytes(raw)
        server.routes['/' + filename] = status, raw
        source = filename if args.mode == 'http' else str(local)
        command = [executable, flag, source, '--max-document-bytes', '4096', *extra]
        if args.mode == 'http':
            command += ['--source-http-base', base]
            expected_requests.extend(reads if reads is not None else ['/' + filename])
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=60)
        (directory / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
        if diagnostic:
            assert diagnostic in result.stdout + result.stderr, (name, result.stdout, result.stderr)
        assert artifact.read_bytes() == sentinel, name
        reports.append(dict(case=name, returncode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))

    try:
        invoke('material', '--material-json', material, True, 'material ok: RoutedMaterial')
        invoke('mesh', '--gltf-json', mesh, True, 'mesh ok: vertices=3 indices=3')
        invoke('legacy-layout', '--ui-layout-json', layout, True, 'Legacy/GuiLayoutJson/1',
               extra=['--legacy-ui-json'])
        for name, raw, diagnostic in [('utf8', b'\xff', 'UTF8'), ('nul', b'x\x00y', 'NUL'),
                                      ('budget', b'x' * 4097, 'BUDGET'),
                                      ('type', b'{"type":"unknown"}', 'type')]:
            invoke(name, '--material-json', raw, diagnostic=diagnostic)
        invoke('legacy-opt-in', '--ui-layout-json', layout,
               diagnostic='LEGACY-JSON-OPT-IN-REQUIRED', reads=[])
        for name, values in [('empty-http', ['--source-http-base', '']),
                             ('empty-ir', ['--ui-ir', '']),
                             ('invalid-budget', ['--max-document-bytes', 'bad',
                                                 '--max-document-bytes', '4096'])]:
            invoke(name, '--material-json', material, diagnostic='TOOL-DOCUMENT-OPTIONS',
                   extra=values, reads=[])
        invoke('formal-unavailable', '--ui-ir', b'type: ArpUiAuthoringIR\n',
               diagnostic='TOOL-UI-AUTHORING-SHARED-API-UNAVAILABLE',
               extra=['--output-ui', str(artifact)])
        # Both legacy inputs still pass through Text1 even while publication is gated.
        manifest = directory / 'manifest.txt'
        manifest.write_bytes(b'\xff')
        manifest_arg = 'manifest.txt' if args.mode == 'http' else str(manifest)
        invoke('manifest-text', '--ui-psd', b'{}',
               diagnostic='UTF8', extra=['--legacy-ui-json', '--ui-manifest', manifest_arg,
                                         '--output-ui', str(artifact)],
               reads=['/' + filename, '/manifest.txt'])
        if args.mode == 'http':
            # A valid same-name local file cannot replace a rejected HTTP source.
            for status in (403, 404):
                invoke(f'http-{status}', '--material-json', material,
                       diagnostic='IO-HTTP', status=status)
            assert server.requests == expected_requests, server.requests
        (directory / 'report.yaml').write_text(yaml.safe_dump(dict(
            cases=reports, requests=server.requests, formalUiPublication='UnavailablePendingApproval',
            productionReady=False), sort_keys=False), encoding='utf-8')
    finally:
        server.shutdown()
        worker.join(timeout=5)
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=['native', 'http'], required=True)
    run(parser.parse_args())
