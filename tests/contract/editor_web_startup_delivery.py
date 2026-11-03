"""Compiled bundle/static-source custody, never a product execution claim."""
import argparse
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'editor/tests/fixtures'))
from web_startup_files import WebStartupFiles


def run(args):
    files = WebStartupFiles(ROOT, args.shaders, args.mono, args.cjk)
    directory = args.directory.resolve(strict=True)
    ninja = (directory / 'build.ninja').read_text(encoding='utf-8')
    link = re.search(r'^build arp_editor\.html:.*?(?=^\S|\Z)', ninja, re.M | re.S)
    assert link, 'EDITOR-WEB-DELIVERY-PRODUCT-LINK'
    for required in ('-sMIN_WEBGL_VERSION=1', '-sMAX_WEBGL_VERSION=1', '-pthread',
                     '@/workspace/default', '@/app/tests/fixtures/scene_2d'):
        assert required in link[0], required
    for prohibited in ('@/app/editor/resources', '@/artifacts', '@/fonts/',
                       'emdawnwebgpu', 'USE_WEBGPU', 'libarp_rhi_webgpu',
                       'libarp_rhi_vulkan', 'libarp_rhi_metal'):
        assert prohibited not in link[0], prohibited
    package = (directory / 'arp_editor.data').read_bytes()
    wasm = (directory / 'arp_editor.wasm').read_bytes()
    for name, original in files.paths.items():
        role = ('ui', 'fonts', 'shaders')[files.roles[name]]
        deployed = directory / 'editor-sources' / role / name
        expected = original.read_bytes()
        assert deployed.read_bytes() == expected, deployed
        assert expected not in package and expected not in wasm, name
    print('PASS WebGL1 compiled delivery: original86 static bytes; only workspace/fixture preload; product unexecuted')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('directory', 'shaders', 'mono', 'cjk'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
