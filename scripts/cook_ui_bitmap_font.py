"""Publish UiBitmapFontCook/1 from explicit font/sprite-atlas YAML and PNG."""
import argparse
import copy
import hashlib
import math
from pathlib import Path
import uuid

from PIL import Image
import yaml


class BitmapFontCook:
    @staticmethod
    def publish(authoring, atlas, image):
        if (authoring['type'] != 'arp.ui-bitmap-font-authoring' or authoring['contractVersion'] != 1
                or authoring['cookProfile'] != 'UiBitmapFontCook/1'
                or authoring['glyphPolicy'] != 'explicit-static-codepoints'
                or any(authoring[k] != v for k, v in
                       dict(format='Rgba8Unorm', alpha='straight', filter='nearest', srgb=False).items())
                or atlas['type'] != 'arp.sprite-atlas' or atlas['contractVersion'] != 1
                or atlas['resourceId'] != authoring['atlasResourceId']
                or atlas['alpha'] != 'straight' or atlas['filter'] != 'nearest' or atlas['srgb'] is not False
                or image.mode != 'RGBA' or image.size != (atlas['width'], atlas['height'])):
            raise ValueError('UI-BITMAP-FONT-COOK-PROFILE')
        for field in ('resourceId', 'atlasResourceId'):
            if str(uuid.UUID(authoring[field])) != authoring[field]:
                raise ValueError('UI-BITMAP-FONT-RESOURCE-ID')
        if (not authoring['fontToken'] or type(authoring['sizePx']) is not int or authoring['sizePx'] <= 0
                or type(authoring['atlasPage']) is not int or authoring['atlasPage'] < 0):
            raise ValueError('UI-BITMAP-FONT-METRICS')
        slices = {s['name']: s for s in atlas['slices']}
        seen = set()
        if not authoring['glyphs']:
            raise ValueError('UI-BITMAP-FONT-GLYPH-BUDGET')
        for glyph in authoring['glyphs']:
            cp = glyph['codepoint']
            if type(cp) is not int or cp < 0 or cp > 0x10ffff or 0xd800 <= cp <= 0xdfff or cp in seen:
                raise ValueError('UI-BITMAP-FONT-CODEPOINT')
            seen.add(cp)
            if glyph['slice'] not in slices:
                raise ValueError('UI-BITMAP-FONT-SLICE-MISSING')
            s = slices[glyph['slice']]
            if (min(s['x'], s['y']) < 0 or min(s['width'], s['height']) <= 0
                    or s['x'] + s['width'] > image.width or s['y'] + s['height'] > image.height):
                raise ValueError('UI-BITMAP-FONT-SLICE-RANGE')
            if (any(type(glyph[k]) not in (int, float) or not math.isfinite(glyph[k])
                    for k in ('advanceX', 'bearingX', 'bearingY')) or glyph['advanceX'] <= 0):
                raise ValueError('UI-BITMAP-FONT-METRICS')
        output = copy.deepcopy(authoring)
        output['type'] = 'arp.ui-bitmap-font'
        output['rgba8Sha256'] = hashlib.sha256(image.tobytes()).hexdigest()
        return output


def main():
    parser = argparse.ArgumentParser()
    for key in ('authoring', 'atlas', 'image', 'output'):
        parser.add_argument('--' + key, required=True, type=Path)
    args = parser.parse_args()
    output = BitmapFontCook.publish(yaml.safe_load(args.authoring.read_text(encoding='utf-8')),
        yaml.safe_load(args.atlas.read_text(encoding='utf-8')), Image.open(args.image))
    args.output.write_text(yaml.safe_dump(output, sort_keys=False), encoding='utf-8')


if __name__ == '__main__':
    main()
