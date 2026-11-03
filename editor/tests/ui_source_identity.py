"""Build-only hashes of explicitly bound original UI inputs, never embed fonts."""
import argparse
import hashlib
import io
from pathlib import Path
import sys

from PIL import Image
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent / 'fixtures'))
from ui_source_files import UiSourceFiles


def generate(ui_root, font_root, shader_root, output):
    files = UiSourceFiles(ui_root, font_root, shader_root)
    profile = yaml.safe_load(files.paths[files.profile].read_bytes())
    fonts = {face['token']: face['path'] for face in profile['fonts']}
    shader = yaml.safe_load(files.paths[files.publication].read_bytes())
    digest = lambda data: 'sha256:' + hashlib.sha256(data).hexdigest()
    original_document = files.paths[files.document].read_bytes()
    assert not original_document.startswith(b'\xef\xbb\xbf')
    atlas = Image.open(io.BytesIO(files.paths[files.atlas].read_bytes())).convert('RGBA')
    lines = ['#pragma once', '#include <array>', 'namespace arp::tests::editor_ui_identity {',
             'inline constexpr const char* document = "' + digest(original_document) + '";',
             'inline constexpr const char* atlas = "' + digest(atlas.tobytes()) + '";',
             'inline constexpr std::array<const char*,4> fonts{{']
    for token in ('font.editor', 'font.editor.semibold', 'font.code', 'font.code.cjk'):
        lines.append('    "' + digest(files.paths[fonts[token]].read_bytes()) + '",')
    lines += ['}};', 'inline constexpr std::array<const char*,5> shader{{']
    for role in ('source', 'spirv', 'metal', 'wgsl', 'reflection'):
        path = str(Path(files.publication).parent / shader['files'][role]['path']).replace('\\', '/')
        lines.append('    "' + digest(files.paths[path].read_bytes()) + '",')
    lines += ['}};', '} // namespace arp::tests::editor_ui_identity', '']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('ui-root', 'font-root', 'shader-root', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    generate(args.ui_root, args.font_root, args.shader_root, args.output)
