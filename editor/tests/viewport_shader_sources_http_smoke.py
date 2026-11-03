"""NativeViewportSources/1 raw publications through the actual engine HTTP stack."""
from pathlib import Path, PurePosixPath
import sys

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/fixtures'))
from source_file_http_smoke import run


def expected_files(directory):
    expected = {}
    for publication in ('editor_viewport_pbr.artifact-set.yaml',
                        'editor_viewport_sprite.artifact-set.yaml',
                        'editor_environment/editor_viewport_environment.artifact-set.yaml'):
        document = yaml.safe_load((directory / publication).read_bytes())
        assert document['type'] == 'GeneratedShaderArtifactSet' and document['contractVersion'] == 1
        expected['/' + publication] = 1
        for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
            path = document['files'][role]['path']
            assert path and not PurePosixPath(path).is_absolute() and ':' not in path and '\\' not in path
            assert all(part not in ('.', '..', '') for part in path.split('/'))
            name = (PurePosixPath(publication).parent / path).as_posix()
            assert '/' + name not in expected
            expected['/' + name] = 1
    assert len(expected) == 18
    return expected


if __name__ == '__main__':
    directory = Path(sys.argv[2]).resolve(strict=True)
    raise SystemExit(run(sys.argv[1], directory, expected_files(directory)))
