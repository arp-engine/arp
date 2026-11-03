"""Check the declared source/redistribution identity without a new font parser."""
import hashlib
from pathlib import Path
import sys

import yaml
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
directory = ROOT / 'examples/build_a_queen/assets/native'
provenance = yaml.safe_load((directory / 'sources.yaml').read_text(encoding='utf-8'))
assert provenance['type'] == 'arp.asset-provenance' and provenance['contractVersion'] == 1
font = provenance['font']
raw = (directory / font['file']).read_bytes()
assert raw == (ROOT / font['source']).read_bytes()
assert hashlib.sha256(raw).hexdigest() == font['sha256']
assert font['license'] == 'Apache-2.0' and not font['modified']
license_text = (directory / font['licenseFile']).read_text(encoding='utf-8')
assert 'Apache License' in license_text and 'Version 2.0, January 2004' in license_text
assert 'END OF TERMS AND CONDITIONS' in license_text
notice = (directory / font['noticeFile']).read_text(encoding='utf-8')
assert font['copyright'] in notice and font['licenseUrl'] in notice
metadata = yaml.safe_load((directory / 'hud.atlas.arpui').read_text(encoding='utf-8'))
assert metadata['type'] == 'ui.atlas' and metadata['contractVersion'] == 'ui.atlas/v2'
assert len(metadata['fontFaces']) == 1
face = metadata['fontFaces'][0]
assert face['sourcePath'] == font['file'] and face['family'] == font['family']
assert face['tokenId'] == 'queen.font' and face['sizeRangePx'] == {'min': 18, 'max': 18}
assert face['lineHeightPx'] == 22
base = provenance['baseAtlas']
image = Image.open(directory / base['file']).convert('RGBA')
assert image.size == (base['width'], base['height']) == (1, 1)
assert list(image.getpixel((0, 0))) == base['rgba8'] == [255, 255, 255, 255]
assert metadata['baseImageAtlas']['sourcePath'] == base['file']
assert metadata['baseImageAtlas']['colorSpace'] == base['colorSpace'] == 'srgb'
print('PASS Native HUD declared font identity, Apache-2.0 notices and original UI metadata')
