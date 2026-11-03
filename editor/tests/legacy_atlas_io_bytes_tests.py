"""Compare the actual Legacy builder with its frozen platform-specific CPU bytes."""
import argparse
from pathlib import Path
import subprocess

import yaml


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--node', type=Path)
    parser.add_argument('--profile', choices=('windows', 'web'), required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    baseline = yaml.safe_load((Path(__file__).parent / 'fixtures/legacy_atlas_io/original.yaml')
                              .read_text(encoding='utf-8'))
    assert baseline['type'] == 'LegacyEditorAtlasIoBaseline' and baseline['contractVersion'] == 1
    executable = args.executable.resolve()
    command = ([str(args.node.resolve())] if args.node else []) + [str(executable), '--baseline']
    result = subprocess.run(command, cwd=executable.parent, capture_output=True, timeout=90)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert not result.stderr, result.stderr
    actual = yaml.safe_load(result.stdout.decode('utf-8'))
    expected = baseline['profiles'][args.profile]
    assert actual == expected, (expected, actual)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(yaml.safe_dump(dict(type='LegacyEditorAtlasIoComparison', contractVersion=1,
                                             profile=args.profile, matchesOriginal=True,
                                             gpuValidationClaim=False, actual=actual), sort_keys=False),
                           encoding='utf-8')
    print(f'PASS {args.profile} original PNG and full 24 MiB CPU atlas bytes match')


if __name__ == '__main__':
    main()
