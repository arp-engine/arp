"""Original C++ publication through real native ES100 compile/link/interface custody."""
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
    result = BrowserContractRunner(args.browser, args.directory, 'program.html', args.output, 60).run()
    if result.get('type') != 'WebGlOwnedProgramSmoke' or result.get('contractVersion') != 1:
        raise RuntimeError('WEBGL-OWNED-PROGRAM-REPORT: ' + repr(result))
    if any(result.get(name) is not True for name in ('success', 'webGpuUnavailable', 'subscriptionsRetired')):
        raise RuntimeError('WEBGL-OWNED-PROGRAM-SMOKE: ' + repr(result))
    native = result.get('native') or {}
    # Three actual native links: deliberately hostile query, valid program,
    # and a new owner after retirement. Two real source compile failures add
    # one compile each and publish no native program. Failed preflights create none.
    if native != dict(created=3, deleted=3, compiled=8, linked=3, live=0):
        raise RuntimeError('WEBGL-OWNED-PROGRAM-NATIVE-CUSTODY: ' + repr(result))
    if not isinstance(result.get('webGlVersion'), str) or not result['webGlVersion'].startswith('WebGL 1.0'):
        raise RuntimeError('WEBGL-OWNED-PROGRAM-NATIVE-VERSION: ' + repr(result))
    if any(result.get(name) is not False for name in ('productionBackendValidated', 'visualAcceptanceClaimed', 'gpuCompletionClaimed')):
        raise RuntimeError('WEBGL-OWNED-PROGRAM-SCOPE: ' + repr(result))
    print('Original C++ artifacts, real compile/link, hostile interface rejection and exact native retirement passed')


if __name__ == '__main__':
    main()
