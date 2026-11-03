"""Real Fetch of original gameplay text; no MEMFS payload or GPU evidence."""
import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from browser_contract_runner import BrowserContractRunner
from survivors_gameplay_files import GameplayFiles


def run(args):
    directory = args.directory.resolve(strict=True)
    fixture = GameplayFiles(ROOT / 'examples/survivors/assets')
    # This browser target must acquire every original input through Fetch.
    script = (directory / 'arp_survivors_gameplay_source_lifecycle.js').read_text(encoding='utf-8')
    assert 'arp_survivors_gameplay_source_lifecycle.data' not in script
    assert not (directory / 'arp_survivors_gameplay_source_lifecycle.data').exists()
    wasm = (directory / 'arp_survivors_gameplay_source_lifecycle.wasm').read_bytes()
    assert all(data not in wasm for data in fixture.files.values())
    fixture.stage(directory / 'survivors-gameplay-fixture/payload')
    report = BrowserContractRunner(args.browser, directory,
        'survivors_gameplay_source_lifecycle.html', args.output, 130,
        disable_background_timer_throttling=True).run()
    assert report.get('success') and report['cycles'] == 20 and report['files'] == 9, report
    assert report['baselineBytes'] == report['retiredBytes'], report
    assert report['baselineLive'] == report['retiredLive'], report
    assembled = b''
    for name in fixture.names[2:]:
        if assembled:
            assembled += b'\n'
        assembled += fixture.files[name]
    expected = dict(source='sha256:' + hashlib.sha256(assembled).hexdigest(),
                    config='sha256:' + hashlib.sha256(fixture.files['game.yaml']).hexdigest())
    assert report['sample'] == expected, report
    attempts = report['fetchAttempts']
    assert set(attempts) == set(fixture.names), attempts
    # Fetch calls are observed at the Web API boundary. Cancellation can stop
    # an attempt before the same-origin fixture server receives its GET.
    for name in fixture.names:
        count = attempts[name]
        assert count == 21 if name != 'game.yaml' else 21 <= count <= 42, attempts
    import yaml
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'requests.yaml').write_text(yaml.safe_dump(dict(
        type='arp.survivors-gameplay-fetch-attempts', contractVersion=1,
        fetchAttempts=attempts, sameOrigin=True, preloadedInputs=False), sort_keys=False),
        encoding='utf-8')
    print('PASS actual same-origin Fetch original gameplay bytes, ordinary20, destroyed accepted tasks; '
          'core returns to metadata baseline, no GPU claim')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--browser', required=True)
    raise SystemExit(run(parser.parse_args()))
