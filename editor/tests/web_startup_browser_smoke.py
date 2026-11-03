"""CPU-only actual Web startup Fetch86, ordinary20 and ownership faults."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'editor/tests/fixtures'))
from browser_contract_runner import BrowserContractRunner
from web_startup_files import WebStartupFiles


def run(args):
    directory = args.directory.resolve(strict=True)
    files = WebStartupFiles(ROOT, args.shaders, args.mono, args.cjk)
    target = 'arp_editor_web_startup_tests'
    script = (directory / (target + '.js')).read_text(encoding='utf-8')
    assert target + '.data' not in script and not (directory / (target + '.data')).exists()
    wasm = (directory / (target + '.wasm')).read_bytes()
    for name, path in files.paths.items():
        assert path.read_bytes() not in wasm, name
    cases = ('ok', 'missing', 'module', 'cancel', 'destroy', 'cancel-inflight', 'destroy-inflight')
    for mode in (cases if args.case == 'all' else (args.case,)):
        routes = {'/startup-fixture/' + mode + '/' + name: path.read_bytes()
                  for name, path in files.paths.items()}
        if mode == 'missing':
            routes['/startup-fixture/' + mode + '/cjk.ttf'] = None
        elif mode == 'module':
            path = '/startup-fixture/' + mode + '/pbr/editor_viewport_pbr.reflection.yaml'
            routes[path] = b'!' + routes[path][1:]
        cycles = 21 if mode == 'ok' else 2
        report = BrowserContractRunner(args.browser, directory,
            'web_startup_lifecycle.html?mode=' + mode, args.output / mode, 30 * cycles,
            disable_background_timer_throttling=True, cross_origin_isolated=True,
            capture_progress=True, source_files=routes).run()
        assert report['success'], report
        assert report['cycles'] == cycles - 1 and len(report['memory']) == cycles, report
        assert len({(m['bytes'], m['live'], m['miBytes'], m['miBlocks'])
                    for m in report['memory']}) == 1, report
        if mode == 'ok':
            assert not report['rejection'], report
            assert report['sourceRequests'] == {name: cycles for name in files.paths}, report
        elif mode not in ('destroy', 'destroy-inflight'):
            assert report['rejection'], report
        if mode == 'missing':
            assert 'IO-HTTP-STATUS' in report['rejection'], report
        elif mode == 'module':
            assert 'HASH' in report['rejection'], report
        elif mode in ('cancel', 'destroy'):
            assert not report['sourceRequests'], report
        elif mode in ('cancel-inflight', 'destroy-inflight'):
            assert report['sourceRequests'] == {files.profile: 2}, report
    print('PASS real Web startup86, original ES100+module/UI/font identities, ordinary20 and faults')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('directory', 'shaders', 'mono', 'cjk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--case', default='all', choices=(
        'all', 'ok', 'missing', 'module', 'cancel', 'destroy', 'cancel-inflight', 'destroy-inflight'))
    run(parser.parse_args())
