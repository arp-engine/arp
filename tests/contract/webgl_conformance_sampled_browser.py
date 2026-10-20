"""Common YAML intent through the shared recorder and original ordered issuer."""
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
        raise ValueError('CONFORMANCE-SAMPLED-SCRIPT-IDENTITY')
    package = EmscriptenPreloadPackage(directory / scripts[0])
    storage_source = package.read('/tests/fixtures/rhi_conformance_storage_mrt.yaml')
    storage_gate = dict(workloadSha256='sha256:' + hashlib.sha256(storage_source).hexdigest(),
        diagnostic='RHI-CONFORMANCE-STORAGE-MRT-COMPUTE-UNAVAILABLE', resourceCreationAttempted=False, readinessPromoted=False)
    for version, filename in ((1, 'rhi_conformance_sampled.yaml'), (2, 'rhi_conformance_pingpong.yaml')):
        source = package.read('/tests/fixtures/' + filename)
        result = BrowserContractRunner(args.browser, directory,
            'pipeline.html?conformanceSampledOnly=1&conformanceSampledVersion=' + str(version),
            str(Path(args.output) / ('v' + str(version))), 60).run()
        if result.get('storageMrtGate') != storage_gate:
            raise RuntimeError('CONFORMANCE-STORAGE-WEBGL-GATE: ' + repr(result))
        if (result.get('type') != 'ConformanceSampledExecution' or result.get('contractVersion') != 1
                or result.get('frames') != 3 or result.get('verifiedPixels') != 9728
                or result.get('workloadSha256') != 'sha256:' + hashlib.sha256(source).hexdigest()
                or any(result.get(key) is not True for key in ('success', 'webGpuUnavailable', 'subscriptionsRetired', 'sealedOutputValidated', 'retiredAuditRejected'))
                or any(result.get(key) is not False for key in
                    ('gpuCompletionClaimed', 'productionReady', 'readinessPromoted', 'validatorOnly'))):
            raise RuntimeError('CONFORMANCE-SAMPLED-EXECUTION: ' + repr(result))
    rejected = BrowserContractRunner(args.browser, directory,
        'pipeline.html?conformanceSampledOnly=1&conformanceSampledVersion=2&conformanceSampledBudgetFault=1',
        str(Path(args.output) / 'upload-budget-rejected'), 60).run()
    if (rejected.get('success') is not False or rejected.get('frames') != 0
            or rejected.get('diagnostic') != 'RHI-CONFORMANCE-SAMPLED-SOURCE-CREATION: RHI-WEBGL1-RESOURCE-CPU-ALLOCATION'
            or rejected.get('subscriptionsRetired') is not True
            or rejected.get('gpuCompletionClaimed') is not False):
        raise RuntimeError('CONFORMANCE-SAMPLED-UPLOAD-BUDGET: ' + repr(rejected))
    print('Common frozen YAML v1/v2: sampled + four-draw ping-pong, 19456 actual output pixels')



if __name__ == '__main__':
    main()
