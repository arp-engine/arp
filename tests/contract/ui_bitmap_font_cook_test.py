"""Deterministic cooked upload identity and explicit unsupported-input rejection."""
import copy
from pathlib import Path
import sys

from PIL import Image
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from cook_ui_bitmap_font import BitmapFontCook


def main():
    folder = ROOT / 'tests/fixtures/scene_2d'
    authoring = yaml.safe_load((folder / 'glyph.font.authoring.yaml').read_text())
    atlas = yaml.safe_load((folder / 'glyph.yaml').read_text())
    image = Image.open(folder / 'glyph.png')
    cooked = BitmapFontCook.publish(authoring, atlas, image)
    assert cooked == yaml.safe_load((folder / 'glyph.font.yaml').read_text())
    assert yaml.safe_dump(cooked) == yaml.safe_dump(BitmapFontCook.publish(authoring, atlas, image))
    for mutate in (
            lambda x: x.update(cookProfile='SdfCook/1'),
            lambda x: x.update(srgb=True),
            lambda x: x['glyphs'][0].update(slice='missing'),
            lambda x: x['glyphs'][0].update(codepoint=0xd800),
            lambda x: x['glyphs'].append(copy.deepcopy(x['glyphs'][0])),
            lambda x: x['glyphs'][0].update(advanceX=0)):
        bad = copy.deepcopy(authoring)
        mutate(bad)
        try:
            BitmapFontCook.publish(bad, atlas, image)
        except ValueError:
            pass
        else:
            raise AssertionError('unsupported authoring accepted')
    print('UiBitmapFontCook/1 deterministic artifact and unsupported-input tests passed')


if __name__ == '__main__':
    main()
