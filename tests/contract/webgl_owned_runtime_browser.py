"""Execute the actual backend-owned C++ context fixture with WebGPU unavailable."""
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
    result = BrowserContractRunner(args.browser, args.directory, 'index.html', args.output, 60).run()
    required = ('success', 'webGpuUnavailable', 'foreignCurrentRejected', 'contextLostRejected',
                'originalEpochInvalidated', 'quotaReleased', 'subscriptionsRetired')
    if any(result.get(name) is not True for name in required):
        raise RuntimeError('WEBGL-OWNED-CONTEXT-SMOKE: ' + repr(result))
    if result.get('productionBackendValidated') is not False or result.get('gpuCompletionClaimed') is not False:
        raise RuntimeError('WEBGL-OWNED-CONTEXT-SCOPE: ' + repr(result))
    print('Original C++ WebGL1 context, duplicate/foreign ownership, native loss and quota retirement passed')


if __name__ == '__main__':
    main()
