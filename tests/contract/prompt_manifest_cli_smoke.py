"""Offline CLI YAML publication and failure preservation, with no provider key."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

import yaml

ROOT = Path(__file__).resolve().parents[2]


def run(args):
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / 'asset_manifest.yaml'
    legacy = directory / 'asset_manifest.json'
    legacy_bytes = b'pre-existing legacy external data must remain unchanged'
    legacy.write_bytes(legacy_bytes)
    environment = os.environ.copy()
    environment.pop('MESHY_API_KEY', None)
    reports = []

    def invoke(name, values, success, diagnostic=None):
        process = subprocess.run([str(args.executable.resolve()), *values], cwd=directory,
            env=environment, capture_output=True, text=True, encoding='utf-8',
            errors='replace', timeout=45)
        (directory / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        assert (process.returncode == 0) == success, (name, process.returncode, process.stdout, process.stderr)
        if diagnostic:
            assert diagnostic in process.stdout + process.stderr, (name, process.stdout, process.stderr)
        assert legacy.read_bytes() == legacy_bytes
        reports.append(dict(case=name, returncode=process.returncode,
                            stdout=process.stdout, stderr=process.stderr))

    command = ['--manifest-only', '--out', str(directory)]
    # Missing mode never silently starts a generation operation.
    invoke('mode', ['--out', str(directory)], False, 'MODE-REQUIRED')
    invoke('self-test', ['--self-test'], True)
    if target.exists():
        target.unlink()  # Fixture reset of this explicitly named test artifact only.
    invoke('create', command, True, 'offline prompts only')
    original = target.read_bytes()
    generated = yaml.safe_load(original.decode('utf-8'))
    expected = yaml.safe_load((ROOT / 'tests/fixtures/prompt_manifest/reference.yaml').read_text(encoding='utf-8'))
    expected['outputDirectory'] = str(directory)
    assert generated == expected and not original.lstrip().startswith(b'{')
    invoke('replace', command, True, 'asset_manifest.yaml')
    assert target.read_bytes() == original
    for name, options in [('zero', ['--max-document-bytes', '0']),
                          ('washed-budget', ['--max-document-bytes', 'bad', '--max-document-bytes', '4096']),
                          ('empty-out', ['--out', '']),
                          ('unknown', ['--unknown-option'])]:
        invoke(name, command + options, False, 'AI-MANIFEST-OPTIONS')
        assert target.read_bytes() == original
    target.write_bytes(b'old')
    invoke('candidate-budget', command + ['--max-document-bytes', '64'], False, 'DOCUMENT-BUDGET')
    assert target.read_bytes() == b'old'
    oversized = b'x' * 4097
    target.write_bytes(oversized)
    invoke('baseline-budget', command + ['--max-document-bytes', '4096'], False, 'BUDGET')
    assert target.read_bytes() == oversized
    missing = directory / 'must-not-be-created'
    assert not missing.exists()
    invoke('missing-directory', ['--manifest-only', '--out', str(missing)], False)
    assert not missing.exists()
    target.write_bytes(original)
    invoke('final', command, True)
    assert target.read_bytes() == original
    (directory / 'report.yaml').write_text(yaml.safe_dump(dict(
        cases=reports, source='offline explicit defaults', outputBytes=len(original),
        outputSha256=hashlib.sha256(original).hexdigest(),
        generatedAssets=False, providerCredentials=False, productionReady=False),
        allow_unicode=True, sort_keys=False), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
