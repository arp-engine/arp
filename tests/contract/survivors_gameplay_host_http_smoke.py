"""Actual game host consumes HTTP gameplay text before GPU creation."""
import argparse
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from source_file_http_smoke import run
from survivors_gameplay_files import GameplayFiles


def check(result, output, fixture, mode):
    success = mode == 'success'
    output.write_text((result.stdout or '') + (result.stderr or ''), encoding='utf-8')
    assert result.returncode == (0 if success else 1), output.read_text(encoding='utf-8')
    if not success:
        assert result.stderr and 'ready: true' not in result.stdout, result
        assert 'gameplaySource:' not in result.stdout and '[vulkan-' not in result.stdout, result
        diagnostic = {'config-version': 'ARP-SURVIVORS-CONFIG-INVALID',
                      'missing': 'IO-HTTP-STATUS', 'utf8': 'TEXT-INVALID-UTF8',
                      'profile': 'SURVIVORS-GAMEPLAY-SOURCE-PROFILE',
                      'escape': 'ARP-SURVIVORS-SCRIPT-PACKAGE-INVALID',
                      'empty-url': 'SURVIVORS-GAMEPLAY-SOURCE-CONFIG'}[mode]
        assert diagnostic in result.stderr, result.stderr
        return
    source = b''
    for name in fixture.names[2:]:
        if source:
            source += b'\n'
        source += fixture.files[name]
    for field in ('gameplaySource: HTTP', 'gameplayReads: 9', 'gameplayIoRetired: true',
                  'scriptExecution: Interpreter', 'ready: true',
                  'gameplayScriptSha256: sha256:' + hashlib.sha256(source).hexdigest(),
                  'gameplayConfigSha256: sha256:' + hashlib.sha256(fixture.files['game.yaml']).hexdigest()):
        assert field in result.stdout, (field, result.stdout, result.stderr)


def main(args):
    fixture = GameplayFiles(args.assets)
    args.output.mkdir(parents=True, exist_ok=True)
    expected = {'/' + name: 1 for name in fixture.names}
    with tempfile.TemporaryDirectory(prefix='arp-survivors-gameplay-host-') as temp:
        work = Path(temp)
        # Positive case has no local gameplay text: only the original content
        # assets remain available to the still-unmigrated Content loader.
        local = work / 'local-content'
        shutil.copytree(args.assets, local,
                        ignore=shutil.ignore_patterns('game.yaml', 'script-package.yaml', 'scripts'))
        modes = ('success',) if args.render else ('success', 'config-version', 'missing', 'utf8',
                                                'profile', 'escape', 'empty-url')
        for mode in modes:
            remote = work / ('http-' + mode)
            fixture.stage(remote)
            requests = dict(expected)
            if mode in ('config-version', 'profile'):
                node = yaml.safe_load(fixture.files['game.yaml'])
                node['contractVersion' if mode == 'config-version' else 'maxSourceBytes'] = (
                    99 if mode == 'config-version' else 2097153)
                (remote / 'game.yaml').write_text(yaml.safe_dump(node), encoding='utf-8')
                requests = {'/game.yaml': 1, '/script-package.yaml': 1}
            elif mode == 'missing':
                (remote / fixture.names[2]).unlink()
                requests = {name: expected[name] for name in ('/game.yaml', '/script-package.yaml',
                                                            '/' + fixture.names[2])}
            elif mode == 'utf8':
                (remote / fixture.names[2]).write_bytes(b'\xff' + fixture.files[fixture.names[2]])
            elif mode == 'escape':
                node = yaml.safe_load(fixture.files['script-package.yaml'])
                node['sources'][0] = '../outside.arp'
                (remote / 'script-package.yaml').write_text(yaml.safe_dump(node), encoding='utf-8')
                requests = {'/game.yaml': 1, '/script-package.yaml': 1}
            elif mode == 'empty-url':
                requests = {}

            def arguments(url):
                result = ['--autoplay', '--fixed-step', '--mute', '--frames', '140',
                          '--assets', str(local if mode == 'success' else args.assets),
                          '--save-dir', str(work / ('save-' + mode)),
                          '--gameplay-base-url', '' if mode == 'empty-url' else url]
                result.append('--hidden' if args.render else '--simulate')
                return result

            assert run(args.executable, remote, requests, timeout=60, arguments=arguments,
                       verify=lambda result: check(result, args.output / (mode + '.log'),
                                                   fixture, mode)) == 0
            print(f'PASS actual Survivors host {mode}; {len(requests)} declared HTTP inputs; '
                  'valid local text cannot hide a remote failure')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--render', action='store_true')
    raise SystemExit(main(parser.parse_args()))
