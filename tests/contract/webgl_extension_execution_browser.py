"""Real original-domain execution of the five explicit WebGL1 extensions."""
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
    result = BrowserContractRunner(args.browser, args.directory,
        'pipeline.html?extensionsOnly=1', args.output, 60).run()
    if (result.get('type') != 'WebGlExtensionExecutionSmoke'
            or result.get('contractVersion') != 1
            or any(result.get(k) is not True for k in
                ('success', 'subscriptionsRetired', 'webGpuUnavailable'))
            or result.get('negatives') != 10
            or result.get('gpuCompletionClaimed') is not False
            or result.get('productionReady') is not False):
        raise RuntimeError('WEBGL-EXTENSION-EXECUTION: ' + repr(result))
    scopes = result.get('native') or []
    if len(scopes) != 2:
        raise RuntimeError('WEBGL-EXTENSION-DOMAIN-COVERAGE: ' + repr(scopes))
    for native in scopes:
        if (native.get('uint32Draws') != 1 or native.get('instances') != 2
                or native.get('vaoBinds') != 2 or native.get('divisorWrites', 0) < 4
                or native.get('depthAttachments') != 1):
            raise RuntimeError('WEBGL-EXTENSION-NATIVE-PAYLOAD: ' + repr(native))
    print('Five original-domain extensions, distinct instances/depth/derivative pixels and 10 negatives passed')


if __name__ == '__main__':
    main()
