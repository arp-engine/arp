"""Original Scene2D atlas resources through real RenderSystem/FrameGraph draws."""
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
        'pipeline.html?scene2dOnly=1', args.output, 60).run()
    if (result.get('type') != 'Scene2DOrderedProductSmoke'
        or result.get('contractVersion') != 1 or result.get('frames') != 6
        or any(result.get(k) is not True for k in
               ('success', 'subscriptionsRetired', 'webGpuUnavailable'))
        or result.get('gpuCompletionClaimed') is not False
        or result.get('productionReady') is not False):
        raise RuntimeError('SCENE2D-ORDERED-PRODUCT: ' + repr(result))
    print('Scene2D A/B/A draws, alpha/flip/clip pixel oracle, CPU graph ownership, resize and pause/resume passed')


if __name__ == '__main__':
    main()
