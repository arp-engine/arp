"""Validate cold ordinary depth pipelines through actual public WebGPU factories."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--browser', required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--closure', action='store_true')
    mode.add_argument('--execution', action='store_true')
    mode.add_argument('--rewrite', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.execution or args.rewrite
    entry = ('bindless_webgpu_depth_execution.html' if executable else
             'bindless_webgpu_depth_closure.html' if args.closure else 'bindless_webgpu_depth_pipeline.html')
    inputs = {'index.html': ROOT / 'tests/fixtures' / entry}
    for suffix in ('js', 'wasm', 'data'):
        name = 'arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests.' + suffix
        inputs[name] = args.runner / name
    hashes = {}
    for name, source in inputs.items():
        shutil.copyfile(source, args.output / name)
        hashes[name] = hashlib.sha256((args.output / name).read_bytes()).hexdigest()
    (args.output / 'inputs-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    result = BrowserContractRunner(args.browser, args.output,
                                  'index.html?rewrite=1' if args.rewrite else 'index.html', args.output, 90).run()
    print(json.dumps(result, indent=2))
    scope = ('completed-sampled-depth-write-return1' if args.rewrite else
             'canonical-ordinary-fragment-depth-execution14' if args.execution else
             'canonical-ordinary-fragment-depth-closure19' if args.closure else 'ordinary-fragment-depth-pipeline-declaration')
    return 0 if (result.get('success') and result.get('executionGranted') is executable
                 and result.get('scope') == scope
                 and not result.get('diagnostics') and not result.get('nativeErrors')) else 1


if __name__ == '__main__':
    raise SystemExit(main())
