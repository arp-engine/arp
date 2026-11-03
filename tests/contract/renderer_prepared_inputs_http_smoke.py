"""Exact original publication reads through the engine's real HTTP backend."""
from pathlib import Path
import sys

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from source_file_http_smoke import run


def main(executable, directory):
    expected = {}
    for name in ('fxaa', 'wocc_taa', 'ssao', 'display_transfer', 'scene_2d_sprites'):
        publication = name + '.artifact-set.yaml'
        document = yaml.safe_load((directory / publication).read_text(encoding='utf-8'))
        assert document['contractVersion'] == 1
        expected['/' + publication] = 1
        for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
            path = document['files'][role]['path']
            assert not Path(path).is_absolute() and '..' not in Path(path).parts
            assert '/' + path not in expected
            expected['/' + path] = 1
    return run(executable, directory, expected)


if __name__ == '__main__':
    raise SystemExit(main(*(Path(arg).resolve() for arg in sys.argv[1:])))
