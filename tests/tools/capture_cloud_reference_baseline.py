"""Capture and replay the original P0-P3 cloud reference exit, without P4-P6 work."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import yaml


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', default='build/win-vulkan-debug')
    parser.add_argument('--out-dir', required=True)
    parser.add_argument('--cases', nargs='+')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = (root/args.build_dir).resolve()
    out = Path(args.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = root/'tests/fixtures/cloud/reference-baseline.yaml'
    spec = yaml.safe_load(manifest.read_text())
    assert spec['type'] == 'CloudReferenceBaseline' and spec['contractVersion'] == 1
    selected = args.cases or spec['cases']
    assert selected and len(set(selected)) == len(selected) and set(selected) <= set(spec['cases'])
    exe = build/'arp_cloud_original_frame_graph_tests.exe'
    rd = Path('C:/Program Files/RenderDoc')
    env = dict(os.environ, PATH='C:/mingw64/bin;'+os.environ['PATH'])
    for name in ('ARP_CLOUD_SCENE_OPAQUE', 'ARP_CLOUD_SCENE_RESOURCE_MODE',
                 'ARP_RENDERDOC_CAPTURE_PATH', 'ARP_CLOUD_ORACLE_PATH'):
        env.pop(name, None)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    records = []
    result = dict(success=False, scope=spec['scope'], manifest=spec,
        executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        manifestSha256=hashlib.sha256(manifest.read_bytes()).hexdigest(), records=records)

    def save():
        (out/'result.json').write_text(json.dumps(result, indent=2))

    def run(command, log, run_env, timeout):
        start = time.monotonic()
        with log.open('x') as output:
            status = subprocess.run(command, cwd=root, env=run_env, stdout=output,
                stderr=subprocess.STDOUT, timeout=timeout, startupinfo=startup)
        record = dict(command=command, exitCode=status.returncode, seconds=time.monotonic()-start)
        return record

    save()
    for name in selected:
        directory = out/name
        directory.mkdir()
        prefix, oracle = directory/name, directory/'oracle.json'
        capture_env = dict(env, ARP_RENDERDOC_CAPTURE_PATH=str(prefix),
            ARP_CLOUD_ORACLE_PATH=str(oracle), ARP_CLOUD_SCENE_RESOURCE_MODE=spec['resourceMode'])
        command = [str(rd/'renderdoccmd.exe'), 'capture', '-w', '--opt-api-validation',
            '-d', str(build), '-c', str(prefix), str(exe), '--reference-case', name]
        record = dict(case=name)
        records.append(record)
        record['native'] = run(command, directory/'native.log', capture_env, 180)
        save()
        assert record['native']['exitCode'] == 0, (name, record['native'])
        captures = list(directory.glob('*.rdc'))
        assert len(captures) == 1
        capture = captures[0]
        replay_env = dict(env, ARP_REPO_ROOT=str(root), ARP_RENDERDOC_ANALYZE_CAPTURE=str(capture),
            ARP_RENDERDOC_API_VALIDATION='1', ARP_CLOUD_CAPTURE_GRAPH='1',
            ARP_CLOUD_CAPTURE_EXECUTABLE=str(exe.relative_to(root)).replace('\\', '/'),
            ARP_CLOUD_ORACLE_PATH=str(oracle),
            ARP_CLOUD_SCENE_ASSET_BYTES=str(oracle)+'.asset.bin',
            ARP_CLOUD_REFERENCE_SPEC=json.dumps(dict(spec, case=name)))
        record['replay'] = run([str(rd/'qrenderdoc.exe'), '--python',
            str(root/'tests/tools/analyze_cloud_reference_renderdoc.py')],
            directory/'replay.log', replay_env, 180)
        save()
        assert record['replay']['exitCode'] == 0, (name, record['replay'])
        report = json.loads(capture.with_suffix('.json').read_text())
        assert report['success'] and not report['debugMessages']
        assert report['referenceBaseline']['rawEqualsResolved']
        record['report'] = report
        record['capture'] = str(capture)
        record['captureSha256'] = hashlib.sha256(capture.read_bytes()).hexdigest()
        save()
        print(name, 'capture/replay PASS', flush=True)
    result['success'] = True
    result['allBaselineCases'] = selected == spec['cases']
    result['checkedHdrTexels'] = sum(r['report']['checkedHdrTexels'] for r in records)
    result['checkedReadbackBytes'] = sum(r['report']['checkedReadbackBytes'] for r in records)
    save()
    print('CLOUD_P03_REFERENCE_BASELINE_PASS', len(records), flush=True)


if __name__ == '__main__':
    main()
