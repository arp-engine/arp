"""Actual CLI: deterministic complete outputs, input failures preserve existing files."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

import yaml

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_FILES = {'basic.gen.cpp', 'basic.gen.h', 'math_dep.gen.cpp', 'math_dep.gen.h',
                  'package.aot-v5.yaml', 'package.d', 'package.report.txt', 'package_registry.gen.cpp'}


def hashes(directory):
    return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob('*') if path.is_file()}


def run(args):
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    inputs = directory / 'source-来源 & space'
    inputs.mkdir(exist_ok=True)
    for name in ('basic.package.yaml', 'basic.arp', 'math_dep.arp'):
        shutil.copyfile(ROOT / 'tests/fixtures/aot' / name, inputs / name)
    output = directory / 'compiled'
    manifest = inputs / 'basic.package.yaml'
    source = inputs / 'basic.arp'
    reports = []

    def invoke(name, command='compile', options=(), expected=0):
        process = subprocess.run([str(args.executable.resolve()), command, str(manifest),
            '--output', str(output), *options], cwd=ROOT, capture_output=True, text=True,
            encoding='utf-8', errors='replace', timeout=60)
        (directory / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        assert process.returncode == expected, (name, process.returncode, process.stdout, process.stderr)
        reports.append(dict(case=name, returncode=process.returncode,
                            stdout=process.stdout, stderr=process.stderr))

    copied_tool = directory / '工具-argv0.exe'
    shutil.copyfile(args.executable.resolve(), copied_tool)
    copied = subprocess.run([str(copied_tool), 'validate', str(manifest)], cwd=directory,
        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    assert copied.returncode == 0 and copied.stdout.strip() == 'valid: basic', (
        copied.returncode, copied.stdout, copied.stderr)
    reports.append(dict(case='unicode-executable-and-input', returncode=copied.returncode,
                        stdout=copied.stdout, stderr=copied.stderr))
    invoke('validate', 'validate')
    invoke('compile')
    baseline = hashes(output)
    assert set(baseline) == EXPECTED_FILES, baseline
    for command in ('package', 'report', 'compile'):
        invoke('repeat-' + command, command)
        assert hashes(output) == baseline
    for option in ('--max-manifest-bytes', '--max-modules', '--max-file-bytes',
                   '--max-source-bytes', '--max-text-bytes'):
        invoke(option[2:], options=[option, '1'], expected=1)
        assert hashes(output) == baseline
    for option, value in [('--max-file-bytes', '0'), ('--max-modules', '12oops'),
                          ('--unknown-option', '2'), ('--预算', '2')]:
        invoke('invalid-' + option[2:], options=[option, value], expected=2)
        assert hashes(output) == baseline
    original = source.read_bytes()
    for label, payload in [('utf8', b'\xff'), ('nul', b'a\x00b')]:
        source.write_bytes(payload)
        invoke(label, expected=1)
        assert hashes(output) == baseline
    source.unlink()  # Explicit isolated fixture path; no workspace source is removed.
    invoke('missing-source', expected=1)
    assert hashes(output) == baseline
    source.write_bytes(original)
    original_manifest = manifest.read_bytes()
    manifest.write_bytes(b'\xff')
    invoke('invalid-manifest', expected=1)
    assert hashes(output) == baseline
    manifest.write_bytes(original_manifest)
    invoke('final')
    assert hashes(output) == baseline
    (directory / 'report.yaml').write_text(yaml.safe_dump(dict(cases=reports, outputs=baseline,
        explicitNativeHostFileService=True, completeOutputPublicationMigrated=False,
        runtimeAotExecutionValidated=False, productionReady=False), sort_keys=False), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
