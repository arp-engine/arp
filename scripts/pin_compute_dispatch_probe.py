"""Pin the build's Slang probe publication into its backend translation unit."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    # Reuse the binary/layout verifier; a publication alone cannot bless its ABI.
    subprocess.run([sys.executable, str(root / 'tests/contract/compute_dispatch_grid_probe_shader_tests.py'),
                    '--directory', str(args.directory)], check=True)
    stem = args.directory / 'compute_dispatch_grid_probe'
    suffixes = {'source': '.slang', 'spirv': '.spv', 'metal': '.metal', 'wgsl': '.wgsl',
                'reflection': '.reflection.yaml', 'publication': '.computeMain.entry.yaml'}
    assert stem.with_suffix('.slang').read_bytes() == (root / 'tests/fixtures/compute_dispatch_grid_probe.slang').read_bytes()
    lines = ['// Generated from verified Slang outputs. Do not edit.', '#pragma once',
             'namespace arp::rhi_metal::dispatch_probe_pin {']
    for name, suffix in suffixes.items():
        digest = hashlib.sha256(Path(str(stem) + suffix).read_bytes()).hexdigest()
        lines.append('inline constexpr char ' + name + '[] = "' + digest + '";')
    lines.append('}')
    payload = '\n'.join(lines) + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != payload:
        args.output.write_text(payload, encoding='utf-8')
    print('DISPATCH_PROBE_BUILD_PIN_PASS six immutable payload hashes; no runtime caller selection')


if __name__ == '__main__':
    main()
