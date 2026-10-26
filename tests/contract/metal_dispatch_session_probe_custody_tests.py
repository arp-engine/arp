"""A valid, coherently loaded publication still cannot replace the build pin."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    parser.add_argument('--publication', type=Path, required=True)
    args = parser.parse_args()
    original = args.publication.read_bytes()
    try:
        args.publication.write_bytes(original + b'\n# Untrusted replacement; unchanged shader payloads and declarations.\n')
        result = subprocess.run([args.executable, '--reject-probe-pin', str(args.publication)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert 'UNPINNED_ARTIFACT_LOAD_PASS' in result.stdout, result.stdout
        assert 'METAL_DISPATCH_PROBE_PIN_REJECT_PASS' in result.stdout, result.stdout
    finally:
        args.publication.write_bytes(original)
    assert args.publication.read_bytes() == original
    print('METAL_DISPATCH_SESSION_PROBE_CUSTODY_PASS valid replacement rejected; original publication restored')


if __name__ == '__main__':
    main()
