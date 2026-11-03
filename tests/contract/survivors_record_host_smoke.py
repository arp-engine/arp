"""Actual game record commit, cold restart and corrupt-input preservation."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

import yaml

exe = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='arp-survivors-record-host-') as folder:
    root = Path(folder)
    save = root / 'save'

    def run(name, extra, success=True):
        result = subprocess.run([str(exe), '--simulate', '--mute', '--fixed-step',
                                 '--save-dir', str(save), *extra],
                                capture_output=True, text=True, timeout=60)
        (output / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == (0 if success else 1), (name, result.stdout, result.stderr)
        if success:
            assert 'recordSource: NativeDirectory' in result.stdout
            assert 'recordIo: FileService' in result.stdout
        else:
            assert 'ready: true' not in result.stdout, result.stdout
            assert result.stderr, result
        return result

    run('death', ['--autoplay', '--scenario', 'death'])
    path = save / 'record.yaml'
    assert path.is_file()
    original = path.read_bytes()
    document = yaml.safe_load(original)
    assert document['type'] == 'arp.survivors-record' and document['contractVersion'] == 1
    assert document['bestSeconds'] >= 1 and document['wins'] == 0
    restarted = run('restart', ['--frames', '1'])
    loaded = next(line.removeprefix('recordLoaded: ') for line in restarted.stdout.splitlines()
                  if line.startswith('recordLoaded: '))
    restored = yaml.safe_load(loaded)
    for field in ('bestKills', 'bestSeconds', 'wins', 'unlockPoints', 'volume'):
        assert restored[field] == document[field], (field, restored, document)
    assert path.read_bytes() == original
    for name in ('version', 'corrupt', 'utf8'):
        if name == 'version':
            changed = dict(document, contractVersion=99)
            bad = yaml.safe_dump(changed).encode()
        elif name == 'utf8':
            bad = b'\xff' + original
        else:
            bad = b'invalid: ['
        path.write_bytes(bad)
        before = hashlib.sha256(bad).hexdigest()
        run(name, ['--frames', '1'], success=False)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == before
    print('PASS actual record commit, restart restore and three invalid inputs preserved')
