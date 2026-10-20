"""Original payload/output join: real browser rejection before and after submit."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    for name in ('directory', 'output', 'browser'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    output = Path(args.output)
    prefix = 'frame graph RHI queue submit failed: '
    cases = [
        (1, 'CLEAR', 0, False),
        (2, 'UNIFORM-SNAPSHOT', 0, False),
        (3, 'DRAW', 0, False),
        (4, 'VERTICES', 0, False),
        (5, 'OUTPUT-MISMATCH', 0, True),
        (6, 'INDICES', 1, False),
        (7, 'SAMPLED-IDENTITY', 0, False),
        (8, 'PIPELINE', 0, False),
        (9, 'UNIFORM-SNAPSHOT', 0, False),
        (10, 'OPT-IN', 0, False),
        (11, 'OUTPUT-BUDGET', 0, False),
        (12, 'CPU-ALLOCATION', 0, False),
        (13, 'FRAME-BUDGET', 0, False),
        (14, 'RHI-ORDERED-GRAPHICS-CPU-QUOTA', 0, False),
        (15, 'QUOTA-OVERLAP', 0, False),
        (16, 'SHADER', 0, False),
    ]
    summaries = []
    for fault, label, frames, accepted in cases:
        version = 2 if fault == 7 else 1
        result = BrowserContractRunner(args.browser, args.directory,
            f'pipeline.html?conformanceSampledOnly=1&conformanceSampledVersion={version}&conformanceFault={fault}',
            output / f'fault-{fault:02}', 60).run()
        diagnostic = (prefix if fault < 10 or fault == 16 else '') + 'RHI-CONFORMANCE-ORIGINAL-' + label
        if fault == 14:
            diagnostic = label
        if (result.get('success') is not False or result.get('diagnostic') != diagnostic
                or result.get('frames') != frames or result.get('rejectedAfterSubmit') is not accepted
                or result.get('subscriptionsRetired') is not True
                or any(result.get(k) is not False for k in
                    ('sealedOutputValidated', 'gpuCompletionClaimed', 'productionReady', 'readinessPromoted'))):
            raise RuntimeError(f'CONFORMANCE-ORIGINAL-FAULT-{fault}: {result!r}')
        summaries.append(dict(fault=fault, diagnostic=diagnostic, frames=frames,
            rejectedAfterSubmit=accepted, sealedOutputValidated=False))
    lost = BrowserContractRunner(args.browser, args.directory,
        'pipeline.html?conformanceSampledOnly=1&conformanceFault=17', output / 'context-loss', 60).run()
    if (any(lost.get(k) is not True for k in ('success', 'sealedOutputValidated',
            'contextLossAuditRejected', 'retiredAuditRejected', 'subscriptionsRetired'))
            or any(lost.get(k) is not False for k in ('productionReady', 'gpuCompletionClaimed', 'readinessPromoted'))):
        raise RuntimeError(f'CONFORMANCE-ORIGINAL-CONTEXT-LOSS: {lost!r}')
    summaries.append(dict(contextLossAuditRejected=True, retiredAuditRejected=True))
    (output / 'negative-summary.json').write_text(json.dumps(summaries, indent=2), encoding='utf8')
    print('Original conformance: 16 native browser negative cases rejected; accepted output mismatch retained')


if __name__ == '__main__':
    main()
