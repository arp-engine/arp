"""Cook an explicit ASCII font with real source metrics and a fixed grid budget."""
import argparse
import hashlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
import yaml

from cook_ui_bitmap_font import BitmapFontCook


class StaticBitmapFontRasterizer:
    def publish(self, source, output, token, size, atlas_id, font_id):
        source, output = Path(source), Path(output)
        font = ImageFont.truetype(str(source), size)
        codepoints = range(32, 127)
        cell, columns = size * 2, 16
        rows = (len(codepoints) + columns - 1) // columns
        image = Image.new('RGBA', (cell * columns, cell * rows))
        draw = ImageDraw.Draw(image)
        slices, glyphs = [], []
        for index, cp in enumerate(codepoints):
            char = chr(cp)
            left, top, right, bottom = font.getbbox(char, anchor='la')
            width, height = max(1, right - left), max(1, bottom - top)
            if width > cell or height > cell:
                raise ValueError('UI-BITMAP-FONT-RASTER-CELL-CAPACITY')
            x, y = index % columns * cell, index // columns * cell
            draw.text((x - left, y - top), char, font=font, fill='white', anchor='la')
            name = f'glyph_{cp}'
            slices.append(dict(name=name, x=x, y=y, width=width, height=height,
                               pivotX=0, pivotY=0, pixelsPerUnit=size))
            glyphs.append(dict(codepoint=cp, slice=name, advanceX=font.getlength(char),
                               bearingX=left, bearingY=top))
        atlas = dict(type='arp.sprite-atlas', contractVersion=1, resourceId=atlas_id,
                     width=image.width, height=image.height, filter='nearest', alpha='straight',
                     srgb=False, slices=slices)
        authoring = dict(type='arp.ui-bitmap-font-authoring', contractVersion=1,
                         resourceId=font_id, atlasResourceId=atlas_id,
                         cookProfile='UiBitmapFontCook/1', glyphPolicy='explicit-static-codepoints',
                         fontToken=token, atlasPage=0, sizePx=size, format='Rgba8Unorm',
                         alpha='straight', filter='nearest', srgb=False, glyphs=glyphs,
                         sourceFontSha256=hashlib.sha256(source.read_bytes()).hexdigest())
        cooked = BitmapFontCook.publish(authoring, atlas, image)
        output.mkdir(parents=True, exist_ok=True)
        image.save(output/'font.png')
        for name, document in [('font.atlas.yaml', atlas), ('font.authoring.yaml', authoring),
                               ('font.yaml', cooked)]:
            (output/name).write_text(yaml.safe_dump(document, sort_keys=False), encoding='utf8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'output', 'token', 'atlas-id', 'font-id'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--size', type=int, required=True)
    args = parser.parse_args()
    StaticBitmapFontRasterizer().publish(args.source, args.output, args.token, args.size,
                                         args.atlas_id, args.font_id)
