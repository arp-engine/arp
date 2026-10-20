"""Validate the platform-owned factory, native GPU frames and lifecycle."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from browser_contract_runner import BrowserContractRunner


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--browser', default='C:/Program Files/Google/Chrome/Application/chrome.exe')
    args = parser.parse_args()
    result = BrowserContractRunner(args.browser, args.directory, 'platform.html',
        args.output, 60).run()
    if (result.get('type') != 'WebGlPlatformIntegrationSmoke' or result.get('frames') != 6
            or any(result.get(key) is not True for key in
                   ('success', 'webGpuUnavailable', 'subscriptionsRetired'))
            or result.get('gpuCompletionClaimed') is not False):
        raise RuntimeError('WEBGL-PLATFORM-SMOKE: ' + repr(result))
    print('Platform factory gates, six GPU frames, resize, pause/resume and teardown passed.')
