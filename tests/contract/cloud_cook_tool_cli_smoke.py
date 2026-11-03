"""Actual cook CLI: frozen original bytes, routed HTTP reads, no failure overwrite."""
import argparse
from collections import Counter
import hashlib
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
        status, body = self.server.routes.get(self.path, (404, b''))
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            pass


def run(args):
    baseline = yaml.safe_load((ROOT / 'tests/fixtures/cloud/cook-tool-byte-baseline.yaml').read_text(encoding='utf-8'))
    args.output.mkdir(parents=True, exist_ok=True)
    executable = str(args.executable.resolve())
    server = create_browser_http_server(Handler)
    server.requests, server.routes = [], {}
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f'http://127.0.0.1:{server.server_port}/'
    reports = []

    def invoke(name, source, output, budget, http=False, success=True, diagnostic=None):
        command = [executable, 'cook', str(source), str(output.resolve()), str(budget)]
        if http:
            command += ['--source-http-base', base]
        result = subprocess.run(command, cwd=args.output, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=45)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
        if diagnostic:
            assert diagnostic in result.stderr, (name, result.stderr)
        if success:
            assert f'source={"http" if http else "native"}' in result.stdout, result.stdout
        reports.append({'case': name, 'returncode': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr})

    try:
        for case in baseline['cases']:
            source = ROOT / case['source']
            raw = source.read_bytes()
            assert hashlib.sha256(raw).hexdigest() == case['sourceSha256']
            version = case['algorithmVersion']
            output = args.output / f'v{version}.cooked.yaml'
            output.write_bytes(b'pre-existing output baseline')
            for iteration in range(2):
                source_arg = source if args.mode == 'native' else source.name
                if args.mode == 'http':
                    server.routes['/' + source.name] = (200, raw)
                invoke(f'v{version}-{iteration}', source_arg, output, baseline['documentBudget'],
                       http=args.mode == 'http')
                cooked = output.read_bytes()
                assert len(cooked) == case['cookedBytes']
                assert hashlib.sha256(cooked).hexdigest() == case['cookedSha256'], version

        raw = (ROOT / baseline['cases'][1]['source']).read_bytes()
        sentinel = b'preserve this exact previous output'
        output = args.output / 'protected.cooked.yaml'
        local = args.output / 'recipe.yaml'
        local.write_bytes(raw)  # Valid local input must never mask a failed HTTP source.
        if args.mode == 'http':
            for name, status, body, diagnostic in (
                    ('missing', 404, b'', 'IO-HTTP'),
                    ('forbidden', 403, b'', 'IO-HTTP'),
                    ('utf8', 200, b'#\xff\n', 'UTF8'),
                    ('nul', 200, b'x\x00y', 'NUL'),
                    ('schema', 200, b'type: unknown\ncontractVersion: 99\n', 'CLOUD'),
                    ('oversize', 200, b'x' * 8193, 'IO-HTTP')):
                server.routes['/recipe.yaml'] = (status, body)
                output.write_bytes(sentinel)
                invoke(name, 'recipe.yaml', output, 8192, http=True, success=False, diagnostic=diagnostic)
                assert output.read_bytes() == sentinel, name
            counts = dict(Counter(server.requests))
            assert counts == {'/reference.cook.yaml': 2, '/reference-v3.cook.yaml': 2, '/recipe.yaml': 6}, counts
        else:
            output.write_bytes(sentinel)
            invoke('missing', args.output / 'absent.yaml', output, 8192,
                   success=False, diagnostic='IO-LOCAL-RELATIVE-OPEN')
            assert output.read_bytes() == sentinel
            for name, payload, diagnostic in (('utf8', b'#\xff\n', 'UTF8'),
                    ('nul', b'x\x00y', 'NUL'),
                    ('schema', b'type: unknown\ncontractVersion: 99\n', 'CLOUD')):
                local.write_bytes(payload)
                invoke(name, local.resolve(), output, 8192, success=False, diagnostic=diagnostic)
                assert output.read_bytes() == sentinel
            local.write_bytes(raw)
            output.write_bytes(b'x' * 8193)
            invoke('baseline-budget', local.resolve(), output, 8192, success=False, diagnostic='BUDGET')
            assert output.read_bytes() == b'x' * 8193
            output.write_bytes(sentinel)
            invoke('output-budget', local.resolve(), output, 8192, success=False, diagnostic='BUDGET')
            assert output.read_bytes() == sentinel
        (args.output / 'report.yaml').write_text(yaml.safe_dump({'cases': reports,
            'requests': dict(Counter(server.requests)), 'productionReady': False}, sort_keys=False), encoding='utf-8')
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    print(f'PASS actual {args.mode} cloud CLI /2 and /3 original cooked bytes, repeat and failure preservation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('native', 'http'), required=True)
    run(parser.parse_args())
