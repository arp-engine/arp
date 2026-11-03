"""Real WebGL1 product uses FileService/Fetch, never preloaded character GLBs."""
import argparse
import hashlib
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from browser_contract_runner import BrowserContractRunner


def package(directory):
    original = ROOT / 'examples/build_a_queen/assets/characters'
    names = ('character_manifest.yaml', 'Casual.glb', 'Formal.glb')
    data_path = directory / 'arp_build_a_queen_webgl.data'
    data = data_path.read_bytes() if data_path.exists() else b''
    script = (directory / 'arp_build_a_queen_webgl.js').read_text(encoding='utf-8')
    assert 'arp_build_a_queen_webgl.data' not in script, 'Product retains MEMFS preload loader'
    for name in ('font.atlas.yaml', 'font.png', 'font.yaml'):
        raw = (ROOT / 'examples/build_a_queen/assets/web' / name).read_bytes()
        assert (directory / 'fonts' / name).read_bytes() == raw, name
        assert raw not in data, f'Font input silently preloaded: {name}'
    hashes = {}
    for name in names:
        raw = (original / name).read_bytes()
        assert (directory / 'characters' / name).read_bytes() == raw, name
        assert raw not in data, f'Character input silently preloaded: {name}'
        hashes[name] = 'sha256:' + hashlib.sha256(raw).hexdigest()
    for name in ('queen_portable_sky', 'queen_portable_shadow', 'queen_portable', 'gui_bitmap'):
        raw = (directory / 'artifacts' / (name + '.webgl-program.yaml')).read_bytes()
        assert raw not in data, f'Shader publication silently preloaded: {name}'
    return hashes


def run(args):
    directory = args.directory.resolve(strict=True)
    hashes = package(directory)
    if args.mode == 'package':
        print('PASS exact character publication, no character inputs in MEMFS preload')
        return 0
    if args.mode == 'lifecycle':
        report = BrowserContractRunner(args.browser, directory,
            'queen_character_source_lifecycle.html', args.output, 130,
            disable_background_timer_throttling=True).run()
        assert report.get('success') and report['cycles'] == 20, report
        assert report['retiredCoreBytes'] == 0 and report['retiredCoreLive'] == 0, report
        assert report['sample'] == dict(manifest=hashes['character_manifest.yaml'],
            player=hashes['Casual.glb'], rival=hashes['Formal.glb']), report
        print('PASS actual Fetch one warm +20 success/destroyed-task cycles, core owners 0B/0 live')
        return 0
    if args.mode == 'font-lifecycle':
        import yaml
        report = BrowserContractRunner(args.browser, directory,
            'queen_web_font_source_lifecycle.html', args.output, 130,
            disable_background_timer_throttling=True).run()
        assert report.get('success') and report['cycles'] == 20 and report['files'] == 3, report
        assert report['baselineBytes'] == report['retiredBytes'] and report['baselineLive'] == report['retiredLive'], report
        metadata = yaml.safe_load((directory / 'fonts/font.yaml').read_text(encoding='utf-8'))
        assert report['sample']['font.rgba8'] == 'sha256:' + metadata['rgba8Sha256'], report
        print('PASS actual Fetch font ordinary20; decoded owner survives IO shutdown, core returns to metadata baseline')
        return 0
    if args.mode == 'shader-lifecycle':
        report = BrowserContractRunner(args.browser, directory,
            'queen_web_shader_source_lifecycle.html', args.output, 130,
            disable_background_timer_throttling=True).run()
        assert report.get('success') and report['cycles'] == 20, report
        assert report['baselineBytes'] == report['retiredBytes'] and report['baselineLive'] == report['retiredLive'], report
        for name, digest in report['sample'].items():
            assert digest == 'sha256:' + hashlib.sha256((directory / 'artifacts' / name).read_bytes()).hexdigest()
        print(f'PASS actual Fetch Shader ordinary20; {report["files"]} inputs; core returns to metadata baseline')
        return 0
    shutil.copyfile(ROOT / 'tests/fixtures/queen_character_browser_smoke.html',
                    directory / 'queen-character-browser-fixture.html')
    for mode in ('missing', 'manifest'):
        folder = directory / ('queen-source-fixture-' + mode)
        folder.mkdir(exist_ok=True)
        (folder / 'character_manifest.yaml').write_bytes(
            (directory / 'characters/character_manifest.yaml').read_bytes()
            if mode == 'missing' else b'bad: [')
    if args.mode in ('shader-missing', 'shader-hash'):
        folder = directory / ('queen-source-fixture-' + args.mode)
        folder.mkdir(exist_ok=True)
        if args.mode == 'shader-hash':
            shutil.copytree(directory / 'artifacts', folder, dirs_exist_ok=True)
            target = folder / 'Vertex/queen_portable_sky.vertexMain.entry.glsl-es-100'
            raw = bytearray(target.read_bytes())
            raw[0] ^= 1
            target.write_bytes(raw)
    if args.mode in ('font-missing', 'font-hash'):
        folder = directory / ('queen-source-fixture-' + args.mode)
        folder.mkdir(exist_ok=True)
        if args.mode == 'font-hash':
            shutil.copytree(directory / 'fonts', folder, dirs_exist_ok=True)
            target = folder / 'font.yaml'
            text = target.read_text(encoding='utf-8')
            offset = text.index('rgba8Sha256: ') + len('rgba8Sha256: ')
            text = text[:offset] + ('1' if text[offset] == '0' else '0') + text[offset+1:]
            target.write_text(text, encoding='utf-8')
    report = BrowserContractRunner(args.browser, directory,
        'queen-character-browser-fixture.html?mode=' + args.mode,
        args.output, 60, disable_background_timer_throttling=True).run()
    assert report.get('success'), report
    assert report['pendingObserved'], report
    if args.mode == 'success':
        assert report['sources']['manifest'] == hashes['character_manifest.yaml'], report
        assert report['sources']['player'] == hashes['Casual.glb'], report
        assert report['sources']['rival'] == hashes['Formal.glb'], report
    print(f'PASS actual Queen WebGL1 {args.mode}, {report["sources"]["reads"]} Fetch requests')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--browser')
    parser.add_argument('--mode', choices=('package', 'success', 'missing', 'manifest', 'cancel', 'lifecycle',
        'shader-missing', 'shader-hash', 'shader-cancel', 'shader-lifecycle',
        'font-missing', 'font-hash', 'font-cancel', 'font-lifecycle'), required=True)
    raise SystemExit(run(parser.parse_args()))
