"""Original six Shader files through real engine HTTP; exact request closure."""
from pathlib import Path
import sys

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'fixtures'))
from source_file_http_smoke import run


def main(executable, directory):
    publication = 'scene_2d_sprites.artifact-set.yaml'
    document = yaml.safe_load((directory / publication).read_text(encoding='utf-8'))
    assert document['type'] == 'GeneratedShaderArtifactSet' and document['contractVersion'] == 1
    expected = {'/' + publication: 1}
    for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
        path = document['files'][role]['path']
        assert not Path(path).is_absolute() and '..' not in Path(path).parts
        assert '/' + path not in expected
        expected['/' + path] = 1
    return run(executable, directory, expected)


if __name__ == '__main__':
    raise SystemExit(main(*(Path(arg).resolve() for arg in sys.argv[1:])))
