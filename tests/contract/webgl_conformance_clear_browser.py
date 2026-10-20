"""Common YAML intent consumed by the original ordered engine path."""
import argparse
import hashlib
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner
from emscripten_preload_package import EmscriptenPreloadPackage


def main():
    parser = argparse.ArgumentParser()
    for name in ('directory', 'output', 'browser'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    directory = Path(args.directory)
    scripts = re.findall(r'<script src="([^"]+\.js)"></script>', (directory / 'pipeline.html').read_text())
    if len(scripts) != 1:
        raise ValueError('CONFORMANCE-CLEAR-SCRIPT-IDENTITY')
    source = EmscriptenPreloadPackage(directory / scripts[0]).read('/tests/fixtures/rhi_conformance_clear.yaml')
    result = BrowserContractRunner(args.browser, directory,
        'pipeline.html?conformanceClearOnly=1', args.output, 60).run()
    if (result.get('type') != 'ConformanceClearExecution' or result.get('contractVersion') != 1
            or result.get('frames') != 3 or result.get('verifiedPixels') != 9728
            or result.get('workloadSha256') != 'sha256:' + hashlib.sha256(source).hexdigest()
            or any(result.get(key) is not True for key in ('success', 'webGpuUnavailable', 'subscriptionsRetired', 'sealedOutputValidated', 'retiredAuditRejected'))
            or any(result.get(key) is not False for key in
                ('gpuCompletionClaimed', 'productionReady', 'readinessPromoted', 'validatorOnly'))):
        raise RuntimeError('CONFORMANCE-CLEAR-EXECUTION: ' + repr(result))
    print('Common frozen YAML: three clear/resize frames, 9728 actual output pixels, original ownership retired')


if __name__ == '__main__':
    main()
