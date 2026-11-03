"""Gameplay package through real engine HTTP, with exact request closure."""
from pathlib import Path
import sys

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from source_file_http_smoke import run


def main(executable, directory):
    package = yaml.safe_load((directory / 'script-package.yaml').read_bytes())
    assert package['type'] == 'arp.survivors-script-package' and package['contractVersion'] == 1
    expected = {'/game.yaml': 1, '/script-package.yaml': 1}
    for source in package['sources']:
        assert not Path(source).is_absolute() and '..' not in Path(source).parts
        assert '/' + source not in expected
        expected['/' + source] = 1
    return run(executable, directory, expected)


if __name__ == '__main__':
    raise SystemExit(main(*(Path(arg).resolve() for arg in sys.argv[1:])))
