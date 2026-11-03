"""Compare the actual producer with bytes frozen before its IO migration."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

import yaml


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    expected = yaml.safe_load((Path(__file__).parent / 'fixtures/scene_export_io/original-files.yaml')
                              .read_text(encoding='utf-8'))
    reports = []
    with tempfile.TemporaryDirectory(prefix='arp-export-io-') as temporary:
        for index in range(2):
            output = Path(temporary) / f'package-{index}'
            result = subprocess.run([str(args.executable.resolve()), '--io-baseline', str(output)],
                                    capture_output=True, timeout=60)
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            assert result.stdout.replace(b'\r\n', b'\n') == b'PASS original scene-only package producer\n'
            assert not result.stderr, result.stderr
            actual = {file.relative_to(output).as_posix():
                      dict(bytes=file.stat().st_size, sha256=hashlib.sha256(file.read_bytes()).hexdigest())
                      for file in sorted(output.rglob('*')) if file.is_file()}
            assert actual == expected, (expected, actual)
            assert sorted(path.name for path in Path(temporary).iterdir()) == [
                f'package-{i}' for i in range(index + 1)]
            reports.append(dict(returncode=result.returncode, files=actual))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(yaml.safe_dump(dict(type='SceneExportIoByteComparison',
                                             contractVersion=1, runs=reports,
                                             wholeClosureMigrationClaim=False), sort_keys=False),
                           encoding='utf-8')
    print('PASS two actual producer runs match all four original files; no staging residue')


if __name__ == '__main__':
    main()
