"""Actual same-origin Fetch of four WAVs, followed by owned PCM import."""
import argparse
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def run(args):
    directory = args.directory.resolve(strict=True)
    script = (directory / 'arp_audio_memory_source_lifecycle.js').read_text(encoding='utf-8')
    assert 'arp_audio_memory_source_lifecycle.data' not in script
    assert not (directory / 'arp_audio_memory_source_lifecycle.data').exists()
    wasm = (directory / 'arp_audio_memory_source_lifecycle.wasm').read_bytes()
    fixture = directory / 'audio-memory-fixture'
    fixture.mkdir(exist_ok=True)
    names = ('hit.wav', 'upgrade.wav', 'menu.wav', 'music.wav')
    for name in names:
        source = ROOT / 'examples/survivors/assets' / name
        assert source.read_bytes() not in wasm
        shutil.copyfile(source, fixture / name)
    report = BrowserContractRunner(args.browser, directory,
        'audio_memory_source_lifecycle.html', args.output, 130,
        disable_background_timer_throttling=True).run()
    assert report.get('success') and report['cycles'] == 20 and report['files'] == 4, report
    assert report['beforeBytes'] == report['afterBytes'], report
    assert report['beforeLive'] == report['afterLive'], report
    assert report['pcmMatchesGolden'] and report['decoderPeak'] > 0, report
    # These are observed Fetch calls, not the fixture server's GET count.
    assert report['sourceRequests'] == {name: 21 for name in names}, report
    print('PASS actual Fetch4, original PCM hashes, IO/source retirement, ordinary20 core baseline')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--output', type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
