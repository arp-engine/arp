"""Original C++ pipeline creation and complete ES2 state issue/switch checks."""
import argparse
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def validate_raf(result):
    stamps = result.get('rafStamps')
    if (not isinstance(stamps, list) or len(stamps) != 18
            or any(not isinstance(s, (int, float)) or not math.isfinite(s) for s in stamps)
            or any(b <= a for a,b in zip(stamps, stamps[1:]))
            or type(result.get('repeatedRafTimestamps')) is not int
            or result['repeatedRafTimestamps'] < 0):
        raise RuntimeError('WEBGL-ACTUAL-RAF-OBSERVATIONS')


def validate_quality(result):
    quality = result.get('quality') or {}
    scopes = quality.get('scopes', [])
    if not scopes or any(s.get('success') is not True or s.get('forbidden') for s in scopes):
        raise RuntimeError('WEBGL-QUALITY-AUDIT: ' + repr(quality))
    if any(not isinstance(s.get('cpuTimeMs'), (int, float))
           or not math.isfinite(s['cpuTimeMs']) or s['cpuTimeMs'] < 0 for s in scopes):
        raise RuntimeError('WEBGL-QUALITY-CPU-TIMING-EVIDENCE')
    for label in ('ordered-updates', 'record-finish', 'submit', 'present', 'cold-prepare'):
        group = [s for s in scopes if s['label'] == label]
        if len(group) != 14:
            raise RuntimeError('WEBGL-QUALITY-SCOPE-COVERAGE: ' + repr((label, group)))
        if label != 'cold-prepare' and any(s['hostAllocations'] for s in group):
            raise RuntimeError('WEBGL-QUALITY-HOT-ALLOCATION: ' + repr(group))
        if label in ('record-finish', 'cold-prepare') and any(s['nativeCalls'] for s in group):
            raise RuntimeError('WEBGL-QUALITY-HOT-GL: ' + repr(group))
        if label == 'cold-prepare' and any(s['hostAllocations'] <= 0 for s in group):
            raise RuntimeError('WEBGL-QUALITY-COLD-ALLOCATION-BOUNDARY: ' + repr(group))
        # The original and restored domains each keep a fixed live working set.
        for domain in (group[:7], group[7:]):
            if len({(s['hostLiveBytes'], s['liveObjects']) for s in domain}) != 1:
                raise RuntimeError('WEBGL-QUALITY-WORKING-SET-GROWTH: ' + repr(domain))
        for s in group:
            if label == 'submit' and (s['draws'] != 1 or s['uniformBytes'] <= 0):
                raise RuntimeError('WEBGL-QUALITY-DRAW-UNIFORM-PAYLOAD: ' + repr(s))
            if label == 'ordered-updates' and (s['textureUpdateCalls'] != 32
                    or s['textureUpdateBytes'] != 32 * 32 * 4 or s['bufferUpdateCalls'] != 1):
                raise RuntimeError('WEBGL-QUALITY-UPDATE-PAYLOAD: ' + repr(s))
    rejected = [s for s in scopes if s['label'] == 'diagnostic-rejection']
    if len(rejected) != 14 or any(s['nativeCalls'] or s['hostAllocations'] > 1 for s in rejected):
        raise RuntimeError('WEBGL-QUALITY-BOUNDED-REJECTION: ' + repr(rejected))
    budgets = quality.get('budgets', [])
    if (len(budgets) != 10 or [b['mode'] for b in budgets] != list(range(10))
            or [b['accepted'] for b in budgets] != [True, True] + [False] * 8
            or budgets[0]['cpuPeakBytes'] != budgets[1]['cpuPeakBytes']
            or budgets[1]['uniformCapacityBytes'] != 320 or budgets[1]['recordedOperations'] != 4):
        raise RuntimeError('WEBGL-QUALITY-BUDGET-BOUNDARIES: ' + repr(budgets))
    expected_rejections = {
        2: 'RHI-WEBGL1-PREPARED-CPU-ALLOCATION', 3: 'SHADER-WEBGL1-SNAPSHOT-BUDGET',
        4: 'RHI-WEBGL1-PREPARED-COMMAND-BUDGET', 5: 'RHI-WEBGL1-PREPARED-COMMAND-BUDGET',
        6: 'RHI-ORDERED-ISSUE-REFERENCE-CAPACITY', 7: 'RHI-ORDERED-ISSUE-OWNER-CAPACITY',
        8: 'RHI-INDEX-INPUT-ANALYSIS-CAPACITY', 9: 'RHI-WEBGL1-PREPARED-COMMAND-BUDGET'}
    if any(budgets[mode].get('diagnostic') != diagnostic for mode, diagnostic in expected_rejections.items()):
        raise RuntimeError('WEBGL-QUALITY-BUDGET-DIAGNOSTICS: ' + repr(budgets))
    outcomes = quality.get('outcomes', [])
    if (len(outcomes) != 16
            or sum(x['accepted'] for x in outcomes) != 14
            or sum(x['failed'] for x in outcomes) != 15
            or sum(x['lost'] for x in outcomes) != 1):
        raise RuntimeError('WEBGL-QUALITY-OUTCOMES: ' + repr(outcomes))


def main():
    parser = argparse.ArgumentParser()
    for name in ('directory', 'output', 'browser'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    for mode in (1, 2, 3):
        retired = BrowserContractRunner(args.browser, args.directory, 'pipeline.html?retirementOnly=' + str(mode), Path(args.output) / ('cpu-retirement-' + str(mode)), 60).run()
        if (retired.get('type') != 'WebGlCpuRetirementSmoke'
                or any(retired.get(k) is not True for k in ('success', 'cpuRetirement', 'subscriptionsRetired', 'webGpuUnavailable'))
                or retired.get('gpuCompletionClaimed') is not False):
            raise RuntimeError('WEBGL-CPU-RETIREMENT: ' + repr(retired))
    surface_result = BrowserContractRunner(args.browser, args.directory, 'pipeline.html?surfaceOnly=1', Path(args.output) / 'surface-lifecycle', 60).run()
    if (surface_result.get('type') != 'WebGlSurfaceLifecycleSmoke'
            or any(surface_result.get(k) is not True for k in ('success', 'surfaceLifecycle', 'cpuRetirement', 'subscriptionsRetired', 'webGpuUnavailable'))
            or surface_result.get('surfaceFrames') != 2):
        raise RuntimeError('WEBGL-SURFACE-LIFECYCLE: ' + repr(surface_result))
    fault_result = BrowserContractRunner(args.browser, args.directory, 'pipeline.html?recoveryFault=1', Path(args.output) / 'requery-failure', 60).run()
    if not fault_result.get('success') or not fault_result.get('contextRecovered') or not fault_result.get('subscriptionsRetired') or fault_result.get('recoveryRequeryFailureValidated') is not True:
        raise RuntimeError('WEBGL-CONTEXT-REQUERY-FAIL-CLOSED: ' + repr(fault_result))
    validate_raf(fault_result)
    validate_quality(fault_result)
    result = BrowserContractRunner(args.browser, args.directory, 'pipeline.html', args.output, 60).run()
    if result.get('type') != 'WebGlPipelineSmoke' or result.get('contractVersion') != 1:
        raise RuntimeError('WEBGL-PIPELINE-REPORT: ' + repr(result))
    if any(result.get(name) is not True for name in ('success', 'cpuRetirement', 'webGpuUnavailable', 'subscriptionsRetired', 'originalDevice', 'pipelineCatalog', 'multiPass', 'offscreenSampling', 'canvasLease')):
        raise RuntimeError('WEBGL-PIPELINE-SMOKE: ' + repr(result))
    if (result.get('preparedDraws') != 2 or result.get('immutableConstants') is not True
            or result.get('queuePreflight') is not True):
        raise RuntimeError('WEBGL-PREPARED-EXECUTION: ' + repr(result))
    if result.get('contextRecovered') is not True or result.get('browserFrames') != 16:
        raise RuntimeError('WEBGL-CONTEXT-RECOVERY-RAF: ' + repr(result))
    if result.get('recoveryBudgetValidated') is not True:
        raise RuntimeError('WEBGL-CONTEXT-RECOVERY-BUDGET: ' + repr(result))
    if result.get('surfaceLifecycle') is not True or result.get('surfaceFrames') != 2:
        raise RuntimeError('WEBGL-SURFACE-RESUME-RESIZE: ' + repr(result))
    if result.get('repeatedFrames') != 8:
        raise RuntimeError('WEBGL-CANVAS-REUSE-FRAMES: ' + repr(result))
    if result.get('variants') != 3:
        raise RuntimeError('WEBGL-PIPELINE-STATE-SWITCH: ' + repr(result))
    if any(result.get(name) is not False for name in ('productionBackendValidated', 'visualAcceptanceClaimed', 'gpuCompletionClaimed')):
        raise RuntimeError('WEBGL-PIPELINE-SCOPE: ' + repr(result))
    validate_raf(result)
    validate_quality(result)
    print('Original artifact/layout, native full state lowering, pipeline switches and exact retirement passed')


if __name__ == '__main__':
    main()
