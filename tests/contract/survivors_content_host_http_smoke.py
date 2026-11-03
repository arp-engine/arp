"""Actual host consumes Content from HTTP with no local Content files."""
import argparse
from pathlib import Path
import shutil
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from source_file_http_smoke import run
from survivors_content_files import ContentFiles


def main(args):
    fixture = ContentFiles(args.assets)
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='arp-survivors-content-host-') as folder:
        root = Path(folder)
        local = root / 'local-gameplay'
        local.mkdir()
        for name in ('game.yaml', 'script-package.yaml'):
            shutil.copyfile(args.assets / name, local / name)
        shutil.copytree(args.assets / 'scripts', local / 'scripts')
        for mode in (('success',) if args.render else
                     ('success', 'missing', 'version', 'escape', 'duplicate', 'png', 'wave', 'empty-url')):
            remote = root / mode
            fixture.stage(remote)
            expected = {'/' + name: 1 for name in fixture.names}
            if mode in ('version', 'escape', 'duplicate'):
                node = yaml.safe_load(fixture.files['content.yaml'])
                if mode == 'version': node['contractVersion'] = 99
                if mode == 'escape': node['atlasImage'] = '../outside.png'
                if mode == 'duplicate': node['atlasImage'] = 'content.yaml'
                (remote / 'content.yaml').write_text(yaml.safe_dump(node), encoding='utf-8')
                expected = {'/content.yaml': 1}
            if mode == 'missing':
                (remote / fixture.names[2]).unlink()
                expected = {'/' + name: 1 for name in fixture.names[:3]}
            if mode in ('png', 'wave'):
                name = fixture.names[2] if mode == 'png' else 'hit.wav'
                (remote / name).write_bytes(b'\xff' + fixture.files[name][1:])
            if mode == 'empty-url': expected = {}

            def arguments(url):
                return ['--autoplay', '--fixed-step', '--mute', '--frames', '140',
                        '--assets', str(local if mode == 'success' else args.assets),
                        '--save-dir', str(root / ('save-' + mode)),
                        '--content-base-url', '' if mode == 'empty-url' else url,
                        '--hidden' if args.render else '--simulate']

            def verify(result):
                (args.output / (mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
                assert result.returncode == (0 if mode == 'success' else 1), result
                if mode == 'success':
                    for text in ('contentSource: HTTP', 'contentReads: 10', 'contentIoRetired: true',
                                 'contentSourceIdentity: ' + fixture.identity(), 'ready: true'):
                        assert text in result.stdout, (text, result.stdout, result.stderr)
                else:
                    assert result.stderr and 'ready: true' not in result.stdout
                    assert 'contentSource:' not in result.stdout and '[vulkan-' not in result.stderr
            assert run(args.executable, remote, expected, timeout=60, arguments=arguments, verify=verify) == 0
            print('PASS actual Content host', mode)
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--render', action='store_true')
    raise SystemExit(main(parser.parse_args()))
