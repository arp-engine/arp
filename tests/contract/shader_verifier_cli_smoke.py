"""Original SPIR-V CLI facts, byte-exact Native/engine HTTP acquisition and refusal."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import threading

import yaml

from tool_document_cli_smoke import Handler, create_browser_http_server

ROOT = Path(__file__).resolve().parents[2]


def run(args):
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    reference = yaml.safe_load((ROOT / 'tests/fixtures/shader_verifier/original-facts.yaml').read_text(encoding='utf-8'))
    original = args.source.read_bytes()
    assert len(original) == reference['spirvBytes']
    assert hashlib.sha256(original).hexdigest() == reference['spirvSha256']
    assert hashlib.sha256((ROOT / reference['source']).read_bytes()).hexdigest() == reference['sourceSha256']
    local = directory / 'shader.spv'
    local.write_bytes(original)
    server = create_browser_http_server(Handler)
    server.response, server.requests = (200, original), []
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = f'http://127.0.0.1:{server.server_port}/'
    reports, expected = [], []

    def invoke(name, tool, mode, success=True, diagnostic=None, raw=None, status=200,
               size=None, path=None, tail=(), requested=True):
        payload = original if raw is None else raw
        if args.mode == 'native':
            local.write_bytes(payload)
        server.response = status, payload
        location = path or ('shader.spv' if args.mode == 'http' else str(local))
        command = [str(tool.resolve()), location, str(size if size is not None else len(original)), *mode]
        if args.mode == 'http':
            command += ['--source-http-base', base]
            if requested:
                expected.append('/' + location)
        command += list(tail)
        process = subprocess.run(command, cwd=directory, capture_output=True, text=True,
                                 encoding='utf-8', errors='replace', timeout=45)
        (directory / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        assert (process.returncode == 0) == success, (name, process.returncode, process.stdout, process.stderr)
        if success:
            golden = next(value for value in reference['cases'] if value['mode'] == mode)
            assert process.stdout == golden['stdout'], (name, process.stdout, golden['stdout'])
            assert yaml.safe_load(process.stdout) == golden['facts']
        else:
            assert not process.stdout
        if diagnostic:
            assert diagnostic in process.stderr, (name, process.stderr)
        reports.append(dict(case=name, returncode=process.returncode, stdout=process.stdout, stderr=process.stderr))

    try:
        for kind, tool in [('proof', args.proof_tool), ('layout', args.layout_tool)]:
            mode = next(value['mode'] for value in reference['cases'] if value['name'] == kind)
            for iteration in range(2):
                invoke(f'{kind}-{iteration}', tool, mode)
            invoke(f'{kind}-budget', tool, mode, success=False,
                   diagnostic='BUDGET', size=len(original) - 1)
            invoke(f'{kind}-binary', tool, mode, success=False, diagnostic='SHADER-',
                   raw=b'bad!' + original[4:])
            invoke(f'{kind}-zero', tool, mode, success=False,
                   diagnostic='TOOL-BUDGET', size=0, requested=False)
            if args.mode == 'http':
                # Keep a valid same-name local file throughout remote failures.
                assert local.read_bytes() == original
                for status in (403, 404):
                    invoke(f'{kind}-http-{status}', tool, mode, success=False,
                           diagnostic='IO-HTTP', status=status)
                invoke(f'{kind}-escape', tool, mode, success=False,
                       diagnostic='SHADER-GRAPHICS-WRITE-TOOL-READ',
                       path='../escape.spv', requested=False)
                invoke(f'{kind}-empty-base', tool, mode, success=False,
                       diagnostic='SHADER-VERIFIER-SOURCE-BASE',
                       tail=['--source-http-base', ''], requested=False)
        if args.mode == 'http':
            assert server.requests == expected, server.requests
        (directory / 'report.yaml').write_text(yaml.safe_dump(dict(cases=reports,
            requests=server.requests, spirvSha256=reference['spirvSha256'],
            byteExactOriginalStdout=True, gpuExecution=False, productionReady=False),
            sort_keys=False), encoding='utf-8')
    finally:
        server.shutdown()
        worker.join(timeout=5)
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof-tool', type=Path, required=True)
    parser.add_argument('--layout-tool', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=['native', 'http'], required=True)
    run(parser.parse_args())
