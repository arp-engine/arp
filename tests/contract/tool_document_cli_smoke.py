"""Actual tool CLI: formal YAML output via Native/HTTP and failed-source preservation."""
import argparse
from collections import Counter
import http.server
from pathlib import Path
import subprocess
import sys
import threading
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import create_browser_http_server


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.server.requests.append(self.path)
        status, body = self.server.response
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    args.output = args.output.resolve()
    tool = str(args.executable.resolve())
    source = ROOT / 'tests/fixtures/tool-documents/main.arpui.ir.yaml'
    original = source.read_bytes()
    local = args.output / source.name
    local.write_bytes(original)
    output = args.output / 'generated.arpui'
    sentinel = b'preserve the observed previous document'
    reports = []
    server = create_browser_http_server(Handler)
    server.response, server.requests = (200, original), []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f'http://127.0.0.1:{server.server_port}/'

    def invoke(name, values, success=True, diagnostic=None):
        if args.mode == 'http':
            values = values + ['--source-http-base', base]
        result = subprocess.run([tool] + values, cwd=args.output, capture_output=True,
            text=True, encoding='utf-8', errors='replace', timeout=60)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
        if diagnostic:
            assert diagnostic in result.stderr, (name, result.stderr)
        reports.append({'case': name, 'status': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
        return result

    values = ['--ui-ir', source.name if args.mode == 'http' else str(local), '--output-ui', str(output),
              '--max-document-bytes', '65536']
    try:
        output.write_bytes(sentinel)
        invoke('create', values)
        first = output.read_bytes()
        doc = yaml.safe_load(first.decode('utf-8'))
        assert doc['type'] == 'ui.document' and doc['contractVersion'] == 'ui.document/v1'
        assert not first.lstrip().startswith(b'{')
        assert doc['canvas']['id'] == 'tool-document-画布'
        assert doc['textures'][0]['sampler'] == 'linear-clamp'
        invoke('replace', values)
        assert output.read_bytes() == first
        cases = [('utf8', b'#\xff\n', 'UTF8'), ('nul', b'x\x00y', 'NUL'),
                 ('version', original.replace(b'arpui.ir.v1', b'arpui.ir.v99'), 'arpui.ir.v1'),
                 ('budget', b'x' * 65537, 'BUDGET')]
        for name, raw, diagnostic in cases:
            output.write_bytes(sentinel)
            if args.mode == 'http': server.response = (200, raw)
            else: local.write_bytes(raw)
            invoke(name, values, success=False, diagnostic=diagnostic)
            assert output.read_bytes() == sentinel, name
        if args.mode == 'http':
            for status in (404, 403):
                server.response = (status, b'')
                invoke(f'http-{status}', values, success=False, diagnostic='IO-HTTP')
                assert output.read_bytes() == sentinel
            server.response = (200, original)
        local.write_bytes(original)
        # Legacy UI input is rejected before IO unless explicitly selected.
        invoke('legacy-gate', ['--ui-psd', 'not-a-source', '--ui-manifest', 'not-a-manifest',
            '--output-ui', str(output)], success=False, diagnostic='LEGACY-JSON-OPT-IN-REQUIRED')
        # Keep one authoritative generated document for the formal visual consumer.
        invoke('final', values)
        assert output.read_bytes() == first
        if args.mode == 'http':
            assert server.requests == ['/' + source.name] * 9, server.requests
        (args.output / 'report.yaml').write_text(yaml.safe_dump({'cases': reports,
            'requests': dict(Counter(server.requests)), 'productionReady': False}, sort_keys=False), encoding='utf-8')
    finally:
        server.shutdown(); server.server_close(); worker.join()
    print(f'PASS actual {args.mode} document CLI, formal YAML/typed fields, repeat and refusal preserves output')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('native', 'http'), required=True)
    run(parser.parse_args())
