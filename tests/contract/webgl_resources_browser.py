"""Real C++/Wasm resource custody, native upload and deterministic negative cases."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    for name in ('directory', 'output', 'browser'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    result = BrowserContractRunner(args.browser, args.directory, 'resources.html', args.output, 60).run()
    if result.get('type') != 'WebGlOwnedResourcesSmoke' or result.get('contractVersion') != 1:
        raise RuntimeError('WEBGL-RESOURCE-REPORT-CONTRACT: ' + repr(result))
    if any(result.get(name) is not True for name in ('success', 'webGpuUnavailable', 'subscriptionsRetired')):
        raise RuntimeError('WEBGL-RESOURCE-SMOKE: ' + repr(result))
    if not isinstance(result.get('checks'), int):
        raise RuntimeError('WEBGL-RESOURCE-REPORT-CHECKS: ' + repr(result))
    if any(result.get(name) is not False for name in ('productionBackendValidated', 'gpuCompletionClaimed', 'visualAcceptanceClaimed')):
        raise RuntimeError('WEBGL-RESOURCE-SCOPE: ' + repr(result))
    print('Actual WebGL1 buffers/textures/packed-depth/FBO, row bytes, exact ownership and bounded quotas passed')


if __name__ == '__main__':
    main()
