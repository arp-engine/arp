"""Actual RenderSystem -> FrameGraph -> original WebGL1 queue/canvas smoke."""
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
    for suffix, query in (('normal', ''), ('loss', '&orderedLoss=1'), ('partial-issue', '&orderedFault=1')):
        result = BrowserContractRunner(args.browser, args.directory,
            'pipeline.html?orderedOnly=1' + query, Path(args.output) / suffix, 60).run()
        if (result.get('type') != 'RenderSystemOrderedGraphicsSmoke'
            or result.get('contractVersion') != 1 or result.get('frames') != 8
            or any(result.get(k) is not True for k in
                   ('success', 'subscriptionsRetired', 'webGpuUnavailable'))
            or result.get('gpuCompletionClaimed') is not False
            or result.get('productionReady') is not False):
            raise RuntimeError('ORDERED-GRAPHICS-ENGINE: ' + repr(result))
    print('Eight actual engine graph frames, ordered pool/staging reuse, resize and pause/resume passed')


if __name__ == '__main__':
    main()
