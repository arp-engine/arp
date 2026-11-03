"""Actual Fetch13, original typed UI/font/Shader owners and bounded lifecycle."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'editor/tests/fixtures'))
from browser_contract_runner import BrowserContractRunner
from ui_source_files import UiSourceFiles


def verify_report(report, names, outcome):
    assert report.get('success'), report
    assert report['beforeBytes'] == report['afterBytes'], report
    assert report['beforeLive'] == report['afterLive'], report
    if outcome == 'ok':
        assert report['cycles'] == 20 and report['typedOwnerAfterIo'] and not report['rejection'], report
        assert report['sourceRequests'] == {name: 21 for name in names}, report
    else:
        assert report['cycles'] == 1 and report['rejection'] and not report['typedOwnerAfterIo'], report
        expected = 'IO-HTTP-STATUS' if outcome == 'missing' else 'ARP-RESOURCE-SOURCE-BUDGET'
        assert expected in report['rejection'], report
    samples = report['memory']
    assert len(samples) == (21 if outcome == 'ok' else 2), report
    assert len({(m['bytes'], m['live'], m['miBytes'], m['miBlocks']) for m in samples}) == 1, report


def run(args):
    directory = args.directory.resolve(strict=True)
    target = 'arp_editor_ui_source_lifecycle'
    script = (directory / (target + '.js')).read_text(encoding='utf-8')
    assert target + '.data' not in script and not (directory / (target + '.data')).exists()
    wasm = (directory / (target + '.wasm')).read_bytes()
    files = UiSourceFiles(ROOT / 'editor/resources', args.font_root, args.shader_root)
    for name, path in files.paths.items():
        assert path.read_bytes() not in wasm, name
    for outcome in (('ok', 'missing', 'empty') if args.case == 'all' else (args.case,)):
        mode = outcome
        # Exact virtual same-origin routes read originals in place and never
        # copy/repackage fonts. A declared missing input cannot fall back to disk.
        routes = {'/ui-fixture/' + mode + '/' + name: path.read_bytes()
                  for name, path in files.paths.items()}
        if outcome == 'missing':
            routes['/ui-fixture/' + mode + '/Deng.ttf'] = None
        elif outcome == 'empty':
            routes['/ui-fixture/' + mode + '/' + files.document] = b''
        # This is a finite Debug ownership test, not a loading-performance SLA.
        # The measured cold/source cycle is ~7.5s. Admit 10s per required cycle,
        # preserving one warmup plus20 successes or one failure repetition.
        source_cycles = 21 if outcome == 'ok' else 2
        report = BrowserContractRunner(args.browser, directory,
            'ui_source_lifecycle.html?mode=' + mode, args.output / mode, 10 * source_cycles,
            disable_background_timer_throttling=True, cross_origin_isolated=True, capture_progress=True,
            source_files=routes).run()
        verify_report(report, files.paths, outcome)
    print('PASS Fetch13, original typed UI/font/Shader after IO retirement, ordinary20 and fail-closed faults')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('directory', 'font-root', 'shader-root', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--case', choices=('all', 'ok', 'missing', 'empty'), default='all')
    raise SystemExit(run(parser.parse_args()))
