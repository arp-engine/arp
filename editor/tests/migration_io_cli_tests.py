"""Actual converters preserve frozen original output bytes and exclusive publication."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
IDENTITY = 'a75b3294-d283-4b12-92d8-7d522b14eae2'


def digest_tree(directory):
    return {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob('*') if path.is_file()}


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    reference = yaml.safe_load((ROOT / 'tests/fixtures/editor_migration/original-bytes.yaml').read_text(encoding='utf-8'))
    reports = []

    def invoke(name, case, source, output, success, diagnostic=None, extra=None):
        tool = args.project_tool if case['kind'] == 'project' else args.scene_tool
        command = [str(tool.resolve()), str(source), str(output)]
        if case['kind'] == 'project':
            command += ['--project-id', IDENTITY, '--name', '迁移工程',
                        '--engine-contract', 'migration-fixture/1', '--entry-scene', 'none']
        command += case['extra'] if extra is None else extra
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True,
            encoding='utf-8', errors='strict', timeout=30)
        (directory / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
        if diagnostic:
            assert diagnostic in result.stderr, (name, result.stderr)
        if success:
            assert result.stdout == case['stdout'], (name, result.stdout, case['stdout'])
            assert digest_tree(output) == case['outputs'], (name, digest_tree(output), case['outputs'])
        reports.append(dict(case=name, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))

    for case in reference['cases']:
        folder = directory / case['name']
        folder.mkdir()
        source = folder / '来源 & space.yaml'
        original = case['source'].encode('utf-8')
        assert hashlib.sha256(original).hexdigest() == case['sourceSha256']
        source.write_bytes(original)
        output = folder / '迁移结果'
        invoke(case['name'], case, source, output, True)
        assert source.read_bytes() == original
        baseline = digest_tree(output)
        invoke(case['name'] + '-existing', case, source, output, False)
        assert digest_tree(output) == baseline and source.read_bytes() == original
        # Encoding metadata is consumed explicitly; line endings are never guessed.
        bom_crlf = b'\xef\xbb\xbf' + original.replace(b'\n', b'\r\n')
        source.write_bytes(bom_crlf)
        invoke(case['name'] + '-bom-crlf', case, source, folder / 'bom-result', True)
        assert source.read_bytes() == bom_crlf
        source.write_bytes(original)
        for label, raw, diagnostic in [('utf8', b'\xff', 'UTF8'), ('nul', b'a\x00b', 'NUL'),
                ('budget', b' ' * ((1024 * 1024 if case['kind'] == 'project' else 4 * 1024 * 1024) + 1), 'BUDGET')]:
            source.write_bytes(raw)
            destination = folder / ('rejected-' + label)
            invoke(case['name'] + '-' + label, case, source, destination, False, diagnostic)
            assert not destination.exists()
            assert source.read_bytes() == raw
            assert digest_tree(output) == baseline
            assert not Path(str(destination) + '.arp-staging').exists()
        source.unlink()  # Isolated fixture owned by this invocation.
        invoke(case['name'] + '-missing', case, source, folder / 'missing-result', False)
        assert not (folder / 'missing-result').exists()
        source.write_bytes(original)
        destination = folder / 'staging-conflict'
        occupied = Path(str(destination) + '.arp-staging')
        occupied.mkdir()
        sentinel = occupied / 'owned-by-someone-else.txt'
        sentinel.write_bytes(b'preserve existing staging directory')
        invoke(case['name'] + '-staging-conflict', case, source, destination, False)
        assert not destination.exists() and sentinel.read_bytes() == b'preserve existing staging directory'
        assert source.read_bytes() == original
        if case['name'] == 'project-metadata':
            # Actual Windows filesystem identity catches a case alias without guessed renaming.
            collision = folder / 'metadata-case-alias'
            invoke('metadata-case-alias', case, source, collision, False,
                   extra=['--editor-metadata', 'Project.arp.yaml'])
            assert not collision.exists() and not Path(str(collision) + '.arp-staging').exists()
    (args.output / 'report.yaml').write_text(yaml.safe_dump(dict(
        runDirectory=str(directory), cases=reports, originalOutputCases=4, originalOutputFiles=5,
        sourcePreserved=True, exclusiveNamespacePublication=True, productionReady=False),
        allow_unicode=True, sort_keys=False), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-tool', type=Path, required=True)
    parser.add_argument('--scene-tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
