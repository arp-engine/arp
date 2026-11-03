"""SHA-only constants for the original /3 recipe and frozen original tool output."""
import argparse
import hashlib
from pathlib import Path
import yaml


def run(args):
    data = yaml.safe_load((args.root / 'tests/fixtures/cloud/cook-tool-byte-baseline.yaml').read_text(encoding='utf-8'))
    assert data['type'] == 'arp.cloud.cook-tool-byte-baseline' and data['contractVersion'] == 1
    case, = [c for c in data['cases'] if c['algorithmVersion'] == 3]
    assert hashlib.sha256((args.root / case['source']).read_bytes()).hexdigest() == case['sourceSha256']
    text = ('#pragma once\nnamespace arp::tests::cloud_cook_tool_identity {\n'
            f'inline constexpr const char* sourceHash = "{case["sourceSha256"]}";\n'
            f'inline constexpr const char* cookedHash = "sha256:{case["cookedSha256"]}";\n'
            f'inline constexpr unsigned long long documentBytes = {case["cookedBytes"]};\n'
            '}\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != text:
        args.output.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
