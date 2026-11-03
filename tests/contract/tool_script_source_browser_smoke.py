"""CPU Fetch of original Unity tool scripts, finite ordinary20 and retirement."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from browser_contract_runner import BrowserContractRunner
from tool_source_files import ToolSourceFiles


def run(args):
    files = ToolSourceFiles(ROOT)
    directory = args.directory.resolve(strict=True)
    target = 'arp_tool_script_source_lifecycle'
    js = (directory / (target + '.js')).read_text(encoding='utf-8')
    assert target + '.data' not in js and not (directory / (target + '.data')).exists()
    wasm = (directory / (target + '.wasm')).read_bytes()
    for path in files.paths.values():
        assert path.read_bytes() not in wasm, path
    for mode in (('ok', 'missing', 'utf8', 'cancel', 'destroy') if args.case == 'all' else (args.case,)):
        prefix = '/tool-sources/' + mode + '/'
        routes = files.routes(prefix)
        if mode == 'missing':
            routes[prefix + files.entry + '.arp'] = None
        elif mode == 'utf8':
            routes[prefix + 'unity_import_common.arp'] = b'#\xff\n'
        cycles = 21 if mode == 'ok' else 2
        report = BrowserContractRunner(args.browser, directory,
            'tool_script_source_lifecycle.html?mode=' + mode, args.output / mode,
            20 * cycles, cross_origin_isolated=True, disable_background_timer_throttling=True,
            capture_progress=True, source_files=routes).run()
        assert report['success'], report
        assert report['cycles'] == cycles - 1 and len(report['memory']) == cycles, report
        assert len({(m['bytes'], m['live'], m['miBytes'], m['miBlocks'])
                    for m in report['memory']}) == 1, report
        if mode == 'ok':
            assert report['requests'] == {name.removeprefix(prefix): cycles for name in routes}, report
            assert not report['rejection'], report
        elif mode in ('missing', 'cancel', 'destroy'):
            assert report['requests'] == {'unity_import.arp': 2}, report
            if mode != 'destroy':
                assert report['rejection'], report
        else:
            assert report['requests'] == {'unity_import.arp': 2, 'unity_import_common.arp': 2}, report
            assert report['rejection'] and 'UTF8' in report['rejection'], report
    print('PASS real original tool Fetch24, original bytes/Text1, ordinary20, faults and in-flight retirement')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('directory', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--case', choices=('all', 'ok', 'missing', 'utf8', 'cancel', 'destroy'), default='all')
    run(parser.parse_args())
