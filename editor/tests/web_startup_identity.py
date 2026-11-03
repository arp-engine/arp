"""Build-only hashes and original paths, never embedded resource payloads."""
import argparse
import hashlib
import io
from pathlib import Path
import sys
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / 'fixtures'))
from web_startup_files import WebStartupFiles


def generate(args):
    files = WebStartupFiles(args.root, args.shaders, args.mono, args.cjk)
    digest = lambda data: 'sha256:' + hashlib.sha256(data).hexdigest()
    quote = lambda value: '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'
    lines = ['#pragma once', '#include <array>',
             'namespace arp::tests::editor_web_startup_identity {',
             'struct File { const char* path; const char* host; const char* hash; int role; };',
             'inline constexpr std::array<File,' + str(len(files.paths)) + '> files{{']
    for name, path in files.paths.items():
        lines.append('    {' + ','.join((quote(name), quote(path),
                     quote(digest(path.read_bytes())), str(files.roles[name]))) + '},')
    lines += ['}};', 'inline constexpr const char* atlas = ' + quote(digest(
        Image.open(io.BytesIO(files.paths[files.atlas].read_bytes())).convert('RGBA').tobytes())) + ';',
        '} // namespace arp::tests::editor_web_startup_identity', '']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines), encoding='utf-8')
    print('Web startup original closure:', len(files.paths), 'files,',
          sum(path.stat().st_size for path in files.paths.values()), 'bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('root', 'shaders', 'mono', 'cjk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    generate(parser.parse_args())
