"""Actual Fetch Gameplay9 + Content10; typed ownership and ordinary lifecycle."""
import argparse
from pathlib import Path
import shutil
import sys

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from browser_contract_runner import BrowserContractRunner
from survivors_content_files import ContentFiles


def run(args):
    directory = args.directory.resolve(strict=True)
    target = 'arp_survivors_content_source_lifecycle'
    script = (directory / (target + '.js')).read_text(encoding='utf-8')
    assert target + '.data' not in script
    assert not (directory / (target + '.data')).exists()
    wasm = (directory / (target + '.wasm')).read_bytes()
    assets = ROOT / 'examples/survivors/assets'
    content = ContentFiles(assets)
    package = yaml.safe_load((assets / 'script-package.yaml').read_bytes())
    # Mirror the existing explicit script package order, never scan a directory.
    assert package['type'] == 'arp.survivors-script-package' and package['contractVersion'] == 1
    names = ['game.yaml', 'script-package.yaml'] + package['sources']
    assert len(names) == 9
    all_names = names + content.names
    assert len(set(all_names)) == 19
    for name in all_names:
        assert (assets / name).read_bytes() not in wasm, name
    for mode in ('ok', 'wave', 'missing'):
        fixture = directory / 'survivors-content-fixture' / mode
        fixture.mkdir(parents=True, exist_ok=True)
        for name in all_names:
            destination = fixture / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(assets / name, destination)
        if mode == 'wave':
            (fixture / 'hit.wav').write_bytes(b'invalid explicit Wave input')
        elif mode == 'missing':
            (fixture / 'dungeon.png').unlink()
        report = BrowserContractRunner(args.browser, directory,
            'survivors_content_source_lifecycle.html?mode=' + mode, args.output / mode, 130,
            disable_background_timer_throttling=True, cross_origin_isolated=True).run()
        assert report.get('success'), report
        assert report['beforeBytes'] == report['afterBytes'], report
        assert report['beforeLive'] == report['afterLive'], report
        if mode == 'ok':
            assert report['cycles'] == 20 and report['typedOwnerAfterIo'], report
            assert report['decoderPeak'] > 0 and not report['rejection'], report
            # Observed Fetch invocation counts, not server GETs/cache misses.
            assert report['sourceRequests'] == {name: 21 for name in all_names}, report
        else:
            assert report['cycles'] == 1 and report['rejection'], report
            assert not report['typedOwnerAfterIo'], report
            failed_path = 'hit.wav' if mode == 'wave' else 'dungeon.png'
            assert failed_path in report['sourceRequests'], report
            expected = 'AUDIO-IMPORT-FORMAT-MISMATCH' if mode == 'wave' else 'IO-HTTP-STATUS'
            assert expected in report['rejection'], report
    print('PASS actual Fetch19, typed Content/PCM after IO retirement, ordinary20 and fail-closed faults')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--output', type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
