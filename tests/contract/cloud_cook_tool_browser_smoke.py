"""Real Fetch recipe -> original cook /3 -> FileService Memory publication, CPU only."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def run(args):
    directory = args.directory.resolve(strict=True)
    target = 'arp_cloud_cook_tool_fetch_lifecycle'
    js = (directory / (target + '.js')).read_text(encoding='utf-8')
    assert target + '.data' not in js and not (directory / (target + '.data')).exists()
    recipe = (ROOT / 'tests/fixtures/cloud/reference-v3.cook.yaml').read_bytes()
    assert recipe not in (directory / (target + '.wasm')).read_bytes()
    for mode in ('ok', 'missing', 'utf8'):
        body = recipe if mode == 'ok' else None if mode == 'missing' else b'#\xff\n'
        path = f'/cloud-cook-sources/{mode}/recipe.yaml'
        report = BrowserContractRunner(args.browser, directory,
            'cloud_cook_tool_lifecycle.html?mode=' + mode, args.output / mode,
            180, cross_origin_isolated=True, disable_background_timer_throttling=True,
            capture_progress=True, source_files={path: body}).run()
        assert report['success'], report
        samples = 21 if mode == 'ok' else 2
        assert report['cycles'] == samples - 1 and len(report['memory']) == samples, report
        assert report['requests'] == {'recipe.yaml': samples}, report
        assert len({(m['bytes'], m['live'], m['miBytes'], m['miBlocks']) for m in report['memory']}) == 1, report
        if mode == 'ok':
            assert not report['rejection'], report
        else:
            assert report['rejection'], report
            if mode == 'utf8':
                assert 'UTF8' in report['rejection'], report
    print('PASS real Fetch /3 original source/cooked bytes, Memory save, ordinary20 and missing/UTF8 refusal')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--browser', required=True)
    run(parser.parse_args())
