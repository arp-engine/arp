"""Actual shared recorder -> original WebGPU canvas/copy/map/present audit."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    for key in ('directory', 'browser', 'output'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--storage', action='store_true')
    args = parser.parse_args()
    if args.storage:
        return storage(args)
    result = BrowserContractRunner(args.browser, Path(args.directory),
        'webgpu_shared_conformance_runner.html', args.output, 60).run()
    if (result.get('success') is not True or result.get('nativeErrors') != []
            or result.get('diagnostic') != ''
            or any(result.get(key) is not False for key in
                ('productionReady', 'readinessPromoted', 'nativeSyncPromoted'))
            or 'WebGpuSharedConformance/1 nativeNegatives15 callbacksDrained quotaBaseline acceptedDeadlineRetained ordinaryQueueClosed coldObserverRejected'
                not in result.get('messages', [])
            or not any(message == 'WebGpuSharedConformance/1 ClearResize frames3 actualBytes38912 originalCanvasPresent3 sealedAudit quotaRetirement deviceRetirement'
                for message in result.get('messages', []))):
        raise RuntimeError('WEBGPU-SHARED-CONFORMANCE: ' + repr(result))
    print('Actual shared Clear/Resize: 3 original canvases, 38912 bytes, native queue/scopes/map/unmap/present, sealed audit')
    loss = BrowserContractRunner(args.browser, Path(args.directory),
        'webgpu_shared_conformance_runner.html?nativeLoss=1',
        str(Path(args.output) / 'native-loss'), 60).run()
    if (loss.get('success') is not True or loss.get('nativeErrors') != []
            or loss.get('diagnostic') != ''
            or any(loss.get(key) is not False for key in
                ('productionReady', 'readinessPromoted', 'nativeSyncPromoted'))
            or 'WebGpuSharedConformance/1 actualDeviceLoss acceptedDeadlineRetained callbacksDrained quotaBaseline noOutputAudit'
                not in loss.get('messages', [])):
        raise RuntimeError('WEBGPU-SHARED-CONFORMANCE-LOSS: ' + repr(loss))
    print('Actual native device loss: accepted work retained, callbacks drained, quota restored, audit denied')


def storage(args):
    expected = 'WebGpuSharedConformance/1 StorageMrt frames3 dispatch3 draw3 outputs9 actualBytes155648 originalCanvasPresent3 sealedAudit graphicsComputeArtifactRetirement'
    for loss in (False, True):
        result = BrowserContractRunner(args.browser, Path(args.directory),
            'webgpu_shared_conformance_runner.html?conformanceStorageOnly=1' + ('&nativeLoss=1' if loss else ''),
            str(Path(args.output) / ('native-loss' if loss else 'storage')), 90).run()
        messages = result.get('messages', [])
        marker = ('WebGpuSharedConformance/1 storageActualDeviceLoss acceptedDeadlineRetained callbacksDrained quotaBaseline noOutputAudit'
            if loss else expected)
        if (result.get('success') is not True or result.get('nativeErrors') != []
                or result.get('diagnostic') != '' or marker not in messages
                or any(result.get(key) is not False for key in
                    ('productionReady', 'readinessPromoted', 'nativeSyncPromoted'))):
            raise RuntimeError('WEBGPU-SHARED-STORAGE: ' + repr(result))
        if 'storageAcceptedFault29 success=0 accepted=1 diagnostic=RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE' not in messages:
            raise RuntimeError('WEBGPU-SHARED-STORAGE-DEADLINE: ' + repr(result))
        if not loss and ('storageAcceptedFault28 success=0 accepted=1 diagnostic=RHI-CONFORMANCE-STORAGE-MRT-OUTPUT-MISMATCH' not in messages or 'WebGpuSharedConformance/1 storageNativeNegatives30 originalGraphicsComputeInputs dispatch MRT threeOutputs callbackDrain acceptedDeadline' not in messages
                or len([m for m in messages if m.startswith('sharedStorageFrame')]) != 9):
            raise RuntimeError('WEBGPU-SHARED-STORAGE-COVERAGE: ' + repr(result))
        print('Actual Storage/MRT ' + ('device loss' if loss else '3 dispatch, 3 MRT draw, 9 outputs, 30 negatives') + ': PASS')


if __name__ == '__main__':
    main()
