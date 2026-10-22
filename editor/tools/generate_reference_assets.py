"""Author the reference UI document and atlas. Requires Pillow, not Godot.

Only the center viewport comes from the supplied reference. Editor chrome,
icons and every interactive control are independent Arp UI authoring data.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1] / "resources"
NODES = []
TEXTURES = []
ATLAS_REGIONS = {}
ATLAS_SIZE = (2048, 1024)
GLYPH_PAGE_HEIGHT = 2048
# The runtime atlas appends one glyph page below the base image.
ATLAS_COMBINED_HEIGHT = ATLAS_SIZE[1] + GLYPH_PAGE_HEIGHT
ATLAS_GRID_ORIGIN = (1312, 0)
ATLAS_CELL_SIZE = 32
ATLAS_GUTTER = 4
ATLAS_SPRITE_SIZE = 24
ATLAS_COLUMNS = (ATLAS_SIZE[0] - ATLAS_GRID_ORIGIN[0]) // ATLAS_CELL_SIZE


def node(id, kind, x, y, w, h, text='', style='plain', texture=None):
    n = dict(id=id, type=kind, layer=id,
             rect=dict(x=x, y=y, width=w, height=h), style=style,
             state='enabled', event='activate' if kind == 'Button' else 'none')
    if kind in ('Label', 'Button', 'TextInput'):
        n['fontToken'] = 'font.editor'
        n['text'] = text
    if texture:
        n.update(resourceRole='texture', resourceId=texture)
    NODES.append(n)
    return n


def label(id, text, x, y, w=180, h=24, style='text'):
    return node(id, 'Label', x, y, w, h, text, style)


def button(id, text, x, y, w, h=26, style='plain'):
    return node(id, 'Button', x, y, w, h, text, style)


def panel(id, x, y, w, h, style='panel'):
    return node(id, 'Panel', x, y, w, h, style=style)


def texture(id, x, y, w, h):
    TEXTURES.append(dict(resourceId=id, layer=id, atlas='texture/editor-reference',
                         rect=dict(x=x, y=y, width=w, height=h), pivotX=0, pivotY=0,
                         sliceLeft=0, sliceTop=0, sliceRight=0, sliceBottom=0,
                         alpha='straight', srgb=False, mips='none',
                         compression='ui-lossless', sampler='linear-clamp'))

def normalize_sprite(image, content_size=20):
    """Trim transparent bounds, preserve aspect, and center in the canonical sprite cell."""
    image = image.convert('RGBA')
    alpha = image.getchannel('A')
    bbox = alpha.point(lambda value: 255 if value >= 24 else 0).getbbox()
    if bbox is None:
        raise ValueError('atlas sprite has no visible pixels')
    content = image.crop(bbox)
    content.thumbnail((content_size, content_size), Image.Resampling.LANCZOS)
    out = Image.new('RGBA', (ATLAS_SPRITE_SIZE, ATLAS_SPRITE_SIZE), (0, 0, 0, 0))
    out.alpha_composite(content, ((ATLAS_SPRITE_SIZE-content.width)//2,
                                  (ATLAS_SPRITE_SIZE-content.height)//2))
    return out


def pack_sprite(atlas, name, image, index, normalize=True):
    col, row = index % ATLAS_COLUMNS, index // ATLAS_COLUMNS
    cell_x = ATLAS_GRID_ORIGIN[0] + col * ATLAS_CELL_SIZE
    cell_y = ATLAS_GRID_ORIGIN[1] + row * ATLAS_CELL_SIZE
    if cell_x + ATLAS_CELL_SIZE > ATLAS_SIZE[0] or cell_y + ATLAS_CELL_SIZE > ATLAS_SIZE[1]:
        raise ValueError(f'atlas capacity exceeded while packing {name}')
    sprite = normalize_sprite(image) if normalize else image.convert('RGBA').resize(
        (ATLAS_SPRITE_SIZE, ATLAS_SPRITE_SIZE), Image.Resampling.LANCZOS)
    x, y = cell_x + ATLAS_GUTTER, cell_y + ATLAS_GUTTER
    atlas.alpha_composite(sprite, (x, y))
    ATLAS_REGIONS[name] = (x, y, ATLAS_SPRITE_SIZE, ATLAS_SPRITE_SIZE)
    texture('icon-' + name, x, y, ATLAS_SPRITE_SIZE, ATLAS_SPRITE_SIZE)
    return sprite


def extract_icon(reference, box):
    """Recover a small source-screenshot glyph as straight-alpha RGBA."""
    crop = reference.crop(box).convert('RGB')
    pixels = list(crop.getdata())
    w, h = crop.size
    border = [crop.getpixel((x, y)) for x, y in
              [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]]
    background = tuple(sorted(c[i] for c in border)[len(border) // 2] for i in range(3))
    candidates = [p for p in pixels if max(abs(p[i] - background[i]) for i in range(3)) > 80]
    if not candidates:
        candidates = [p for p in pixels if max(abs(p[i] - background[i]) for i in range(3)) > 25]
    if not candidates:
        raise ValueError(f'icon crop has no foreground pixels: {box}')
    foreground = tuple(sorted(p[i] for p in candidates)[len(candidates) // 2] for i in range(3))
    direction = tuple(float(foreground[i] - background[i]) for i in range(3))
    norm = sum(v * v for v in direction)
    out = Image.new('RGBA', crop.size)
    rgba = []
    for pixel in pixels:
        delta = tuple(float(pixel[i] - background[i]) for i in range(3))
        alpha = 0.0 if norm == 0 else max(0.0, min(1.0, sum(delta[i] * direction[i] for i in range(3)) / norm))
        if max(abs(pixel[i] - background[i]) for i in range(3)) < 4:
            alpha = 0.0
        rgba.append((*foreground, round(alpha * 255)))
    out.putdata(rgba)
    return out.resize((24, 24), Image.Resampling.LANCZOS)


def extract_color_icon(reference, box):
    """Preserve a multicolor source glyph while keying out the panel background."""
    crop = reference.crop(box).convert('RGB')
    w, h = crop.size
    corners = [crop.getpixel((x, y)) for x, y in
               [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]]
    background = tuple(sorted(c[i] for c in corners)[len(corners) // 2] for i in range(3))
    rgba = []
    for pixel in crop.getdata():
        delta = max(abs(pixel[i] - background[i]) for i in range(3))
        alpha = max(0.0, min(1.0, delta / 150.0))
        if delta < 5:
            alpha = 0.0
        rgba.append((*pixel, round(alpha * 255)))
    out = Image.new('RGBA', crop.size)
    out.putdata(rgba)
    return out.resize((24, 24), Image.Resampling.LANCZOS)


def emit_mapping(n, indent):
    out = []
    for key, value in n.items():
        if key == 'children':
            out.append(' ' * indent + 'children:')
            for child in value:
                lines = emit_mapping(child, indent + 4)
                lines[0] = ' ' * (indent + 2) + '- ' + lines[0].lstrip()
                out.extend(lines)
        else:
            out.append(' ' * indent + key + ': ' + json.dumps(value, ensure_ascii=False))
    return out


def main():
    args = argparse.ArgumentParser()
    args.add_argument('reference', type=Path)
    ref = Image.open(args.parse_args().reference).convert('RGBA')
    if ref.size != (1920, 1080):
        raise ValueError('Reference contract requires 1920 x 1080 pixels')
    atlas = Image.new('RGBA', ATLAS_SIZE, (0, 0, 0, 0))
    atlas.paste(ref.crop((287, 116, 1583, 1034)), (0, 0))
    texture('viewport-reference', 0, 0, 1296, 918)
    icons = ('node', 'probe', 'folder', 'search', 'plus', 'link', 'eye', 'multiplayer-spawner',
             'cube', 'move', 'rotate', 'select', 'play', 'pause', 'stop',
             'dots', 'sound', 'world', 'file', 'grid', 'save', 'check', 'arrow',
             'chevron-right', 'chevron-down',
             'workspace-2d', 'workspace-3d', 'workspace-script', 'workspace-game', 'workspace-assetlib',
             'tool-viewport', 'tool-move', 'tool-rotate', 'tool-scale', 'tool-select',
             'tool-script', 'tool-lock', 'tool-pivot', 'tool-ruler', 'tool-cube',
             'tool-joint', 'tool-light', 'tool-view',
             'scene-tab-menu', 'scene-tab-level', 'scene-tab-player', 'scene-tab-close', 'reset',
             'run-display', 'run-movie', 'run-animation', 'run-debug')
    source_icons = {
        'node': (40, 166, 59, 185), 'probe': (48, 309, 68, 329),
        'folder': (27, 699, 49, 721), 'search': (184, 85, 207, 107),
        'plus': (13, 85, 35, 107), 'link': (45, 85, 67, 107),
        'eye': (248, 212, 270, 234), 'cube': (47, 428, 69, 450),
        'play': (1597, 13, 1619, 35), 'pause': (1628, 13, 1650, 35),
        'stop': (1659, 13, 1681, 35), 'dots': (264, 48, 286, 70),
        'sound': (40, 377, 64, 401), 'save': (1656, 83, 1679, 107),
        'chevron-right': (20, 725, 39, 745), 'chevron-down': (20, 283, 39, 305),
        'workspace-2d': (766, 12, 787, 33), 'workspace-3d': (827, 12, 849, 33),
        'workspace-script': (891, 12, 911, 33), 'workspace-game': (977, 12, 998, 33),
        'workspace-assetlib': (1063, 12, 1085, 33),
        'tool-viewport': (299, 84, 316, 106), 'tool-move': (329, 84, 348, 106),
        'tool-rotate': (360, 84, 380, 106), 'tool-scale': (391, 84, 411, 106),
        'tool-select': (423, 84, 442, 106), 'tool-script': (467, 84, 487, 106),
        'tool-lock': (499, 84, 519, 106), 'tool-pivot': (531, 84, 551, 106),
        'tool-ruler': (564, 84, 584, 106), 'tool-cube': (607, 84, 627, 106),
        'tool-joint': (640, 84, 660, 106), 'tool-light': (683, 84, 703, 106),
        'tool-view': (715, 84, 736, 106),
        'scene-tab-menu': (300, 49, 323, 71), 'scene-tab-level': (391, 49, 414, 71),
        'scene-tab-player': (502, 49, 526, 72), 'scene-tab-close': (453, 49, 475, 71),
        'reset': (1874, 343, 1897, 366),
        'run-display': (1689, 12, 1714, 35), 'run-movie': (1722, 12, 1747, 35),
        'run-animation': (1754, 12, 1779, 35), 'run-debug': (1786, 12, 1811, 35),
    }
    for index, name in enumerate(icons):
        # Supersampling is asset authoring only; runtime uses the regular image atlas.
        icon = Image.new('RGBA', (96, 96))
        d = ImageDraw.Draw(icon)
        red, gray, blue = '#ef646b', '#b8b9bb', '#81a6d9'
        if name == 'node': d.ellipse((24, 24, 72, 72), outline=red, width=10)
        elif name == 'probe':
            d.line([(18, 25), (18, 70), (77, 70), (77, 25)], fill=red, width=9)
            d.line([(29, 43), (46, 58), (68, 24)], fill=red, width=10)
        elif name == 'folder':
            d.polygon([(12, 27), (39, 27), (48, 36), (83, 36), (83, 76), (12, 76)], fill=blue)
        elif name == 'search':
            d.ellipse((16, 16, 60, 60), outline=gray, width=10)
            d.line((56, 56, 80, 80), fill=gray, width=11)
        elif name == 'plus': d.line((48, 15, 48, 81), fill=gray, width=10); d.line((15, 48, 81, 48), fill=gray, width=10)
        elif name == 'eye':
            d.ellipse((12, 29, 84, 68), outline=gray, width=8); d.ellipse((38, 37, 58, 58), fill=gray)
        elif name == 'play': d.polygon([(30, 19), (76, 48), (30, 77)], fill=gray)
        elif name == 'pause': d.rectangle((26, 22, 39, 75), fill=gray); d.rectangle((55, 22, 68, 75), fill=gray)
        elif name == 'stop': d.rectangle((26, 26, 70, 70), fill=gray)
        elif name == 'dots':
            for y in (22, 48, 74): d.ellipse((42, y-5, 52, y+5), fill=gray)
        elif name == 'sound':
            d.polygon([(16, 36), (35, 36), (55, 20), (55, 78), (35, 62), (16, 62)], fill=gray)
            d.arc((44, 16, 83, 83), 280, 80, fill=gray, width=8)
        elif name == 'check': d.line([(20, 49), (40, 68), (79, 24)], fill='white', width=10)
        elif name == 'arrow': d.line([(27, 19), (61, 48), (27, 77)], fill=gray, width=9)
        elif name == 'chevron-right': d.line([(37, 22), (62, 48), (37, 74)], fill=gray, width=12, joint='curve')
        elif name == 'chevron-down': d.line([(22, 37), (48, 62), (74, 37)], fill=gray, width=12, joint='curve')
        elif name == 'select': d.polygon([(24, 15), (73, 55), (49, 56), (38, 79)], fill=gray)
        elif name in ('world', 'rotate'):
            d.ellipse((15, 15, 80, 80), outline=blue if name == 'world' else gray, width=8)
            d.line((48, 18, 48, 77), fill=blue if name == 'world' else gray, width=6)
            d.line((18, 48, 77, 48), fill=blue if name == 'world' else gray, width=6)
        elif name == 'move':
            d.line((12, 48, 84, 48), fill=gray, width=8); d.line((48, 12, 48, 84), fill=gray, width=8)
            for pts in [[(7,48),(25,35),(25,61)],[(89,48),(71,35),(71,61)],[(48,7),(35,25),(61,25)],[(48,89),(35,71),(61,71)]]: d.polygon(pts, fill=gray)
        elif name == 'link':
            d.ellipse((13, 37, 58, 80), outline=gray, width=8); d.ellipse((38, 13, 83, 56), outline=gray, width=8)
        elif name == 'cube':
            d.line([(48,12),(80,30),(80,66),(48,85),(16,66),(16,30),(48,12),(48,49),(80,30)],fill=red,width=8)
            d.line([(16,30),(48,49),(48,85)],fill=red,width=8)
        elif name == 'grid':
            for v in (20, 48, 76): d.line((v,15,v,81),fill=gray,width=6); d.line((15,v,81,v),fill=gray,width=6)
        else: d.rectangle((22,16,74,80),outline=gray,width=8); d.line((32,39,63,39),fill=gray,width=6)
        if name == 'multiplayer-spawner':
            icon = extract_color_icon(ref, (39, 116, 62, 140))
        elif name in source_icons:
            icon = extract_icon(ref, source_icons[name])
        pack_sprite(atlas, name, icon, index)
    rounded = Image.new('RGBA', (96, 96), (0, 0, 0, 0))
    ImageDraw.Draw(rounded).rounded_rectangle((0, 0, 95, 95), radius=22, fill=(255, 255, 255, 255))
    rounded = rounded.resize((24, 24), Image.Resampling.LANCZOS)
    pack_sprite(atlas, 'rounded-selection', rounded, len(icons), normalize=False)
    atlas.save(ROOT / 'editor_atlas.png')

    cells = {}
    for sprite_name, (sx, sy, sw, sh) in ATLAS_REGIONS.items():
        cell_x = sx - ATLAS_GUTTER
        cell_y = sy - ATLAS_GUTTER
        if (cell_x - ATLAS_GRID_ORIGIN[0]) % ATLAS_CELL_SIZE or (cell_y - ATLAS_GRID_ORIGIN[1]) % ATLAS_CELL_SIZE:
            raise ValueError(f'non-canonical atlas region for {sprite_name}')
        if sx-cell_x != ATLAS_GUTTER or sy-cell_y != ATLAS_GUTTER or sw != ATLAS_SPRITE_SIZE or sh != ATLAS_SPRITE_SIZE:
            raise ValueError(f'non-canonical atlas region for {sprite_name}')
        cell = (cell_x, cell_y)
        if cell in cells:
            raise ValueError(f'atlas cells overlap: {sprite_name} and {cells[cell]}')
        cells[cell] = sprite_name
        gutter = atlas.crop((cell_x, cell_y, cell_x+ATLAS_CELL_SIZE, cell_y+ATLAS_CELL_SIZE))
        for gy in range(ATLAS_CELL_SIZE):
            for gx in range(ATLAS_CELL_SIZE):
                inside = (ATLAS_GUTTER <= gx < ATLAS_GUTTER+ATLAS_SPRITE_SIZE
                          and ATLAS_GUTTER <= gy < ATLAS_GUTTER+ATLAS_SPRITE_SIZE)
                if not inside and gutter.getpixel((gx, gy))[3] != 0:
                    raise ValueError(f'atlas gutter is not transparent for {sprite_name}')
        if sprite_name != 'rounded-selection':
            bounds = atlas.crop((sx, sy, sx+sw, sy+sh)).getchannel('A').getbbox()
            if bounds and (abs((bounds[0]+bounds[2])*0.5-ATLAS_SPRITE_SIZE*0.5)>1.0
                           or abs((bounds[1]+bounds[3])*0.5-ATLAS_SPRITE_SIZE*0.5)>1.0):
                raise ValueError(f'atlas sprite is not centered: {sprite_name} {bounds}')

    header = [
        '#pragma once', '#include <cstdint>', 'namespace arp::editor::editor_atlas {',
        'struct Region { std::uint32_t x, y, width, height; };',
        f'inline constexpr std::uint32_t kWidth = {ATLAS_SIZE[0]};',
        f'inline constexpr std::uint32_t kBaseHeight = {ATLAS_SIZE[1]};',
        f'inline constexpr std::uint32_t kGlyphPageHeight = {GLYPH_PAGE_HEIGHT};',
        f'inline constexpr std::uint32_t kHeight = {ATLAS_COMBINED_HEIGHT};',
        f'inline constexpr std::uint32_t kCellSize = {ATLAS_CELL_SIZE};',
        f'inline constexpr std::uint32_t kGutter = {ATLAS_GUTTER};',
        'inline constexpr std::uint32_t kRoundedCorner = 6;',
    ]
    for region_name, symbol in [('check', 'kCheck'), ('rounded-selection', 'kRoundedSelection')]:
        x, y, w, h = ATLAS_REGIONS[region_name]
        header.append(f'inline constexpr Region {symbol}{{{x}, {y}, {w}, {h}}};')
    header.append('} // namespace arp::editor::editor_atlas')
    (ROOT.parent / 'ui' / 'editor_atlas_layout.h').write_text('\n'.join(header)+'\n', encoding='utf-8')

    def icon(id, name, x, y, size=18):
        return node(id, 'Image', x, y, size, size, style='image', texture='icon-' + name)

    panel('top', 0, 0, 1920, 42, 'top')
    panel('left_scene', 4, 43, 280, 513)
    panel('left_files', 4, 560, 280, 517)
    panel('scene_tabs', 287, 42, 1296, 35, 'top')
    panel('toolbar', 287, 77, 1296, 39)
    panel('inspector', 1586, 43, 330, 1034)
    node('viewport', 'Image', 287, 116, 1296, 918, style='image', texture='viewport-reference')
    panel('bottom', 287, 1038, 1296, 39)
    for name, x, w in [('Scene',10,50),('Project',67,56),('Debug',130,52),('Editor',190,48),('Help',245,45)]: button('menu_' + name, name,x,9,w)
    workspace = [('3D',818,54,797,'workspace-3d'),
                 ('Game',904,66,883,'workspace-game'),
                 ('Script',992,66,971,'workspace-script')]
    for name, x, w, icon_x, icon_name in workspace:
        button('workspace_' + name,name,x,8,w)
        icon('workspace_icon_' + name,icon_name,icon_x,12,20)
    for name,x in [('play',1597),('pause',1630),('stop',1661),('file',1696),('save',1730),('grid',1763),('world',1795)]:
        button('run_' + name,'',x-3,7,27); icon('run_icon_' + name,name,x,14,16)
    label('renderer', 'Forward+ v',1825,10,94,25,'green')
    button('tab_scene','Scene',14,46,61,31,'tab'); button('tab_import','Import',91,46,66,31)
    icon('scene_dots','dots',265,49,18)
    icon('scene_plus','plus',15,87); icon('scene_link','link',47,87)
    node('scene_search','TextInput',77,82,139,29,'','input'); label('scene_search_hint','Filter Nodes',84,86,112,23,'muted'); icon('scene_search_icon','search',188,86)
    icon('scene_add','file',227,87); icon('scene_more','dots',259,87)
    button('scene_object_add','+',224,87,24)
    button('scene_object_delete','-',254,87,24)
    rows = [('MultiplayerSpawner','multiplayer-spawner',0),('Core','node',0),('Structure','node',0),('Props','node',0),('Lights','node',0),('WorldEnvironment','world',0),('VoxelGI','probe',0),('ReflectionProbes','node',0),('ReflectionProbe1','probe',1),('ReflectionProbe2','probe',1),('ReflectionProbe3','probe',1),('Music','sound',0),('SoundOutside','probe',0),('CollisionPolygon3D','cube',1),('SoundReactorRoom','probe',0),('CollisionShape3D','cube',1),('FlyingForklifts','node',0),('FlyingForklift1','node',1)]
    for i,(name,kind,depth) in enumerate(rows):
        y=115+i*24
        button('node_' + name,name,58+depth*12,y,190-depth*12,24)
        icon_x = 37 + depth * 12 - (2.5 if kind == 'world' else 0)
        icon('node_icon_' + name,kind,icon_x,y+4,16)
        icon('node_eye_' + name,'eye',251,y+5,15)
        if name in ('ReflectionProbes','SoundOutside','SoundReactorRoom','FlyingForklifts'): icon('branch_' + name,'chevron-down',20,y+5,12)
    panel('tree_scroll_track',10,549,244,4,'input'); panel('tree_scroll_thumb',12,549,225,4,'scroll')
    button('tab_files','FileSystem',8,563,92,30,'tab'); button('tab_history','History',100,563,65,30)
    button('files_build_terrain','Build',166,563,52,30); button('files_terrain_assets','Assets',220,563,62,30)
    button('files_back','<',15,603,20); button('files_forward','>',38,603,20)
    node('files_path','TextInput',57,600,190,28,'res://','input'); icon('files_view','file',256,605,18)
    node('file_search','TextInput',10,633,238,28,'','input'); label('file_search_hint','Filter Files',18,636,160,22,'muted'); icon('file_search_icon','search',222,638,17)
    label('favorites','*  Favorites:',25,676,160,23)
    button('file_root','res://',25,700,250,24)
    icon('root_folder','folder',26,702,18)
    folders=['door','effects_shared','enemies','level','main','menu','player']
    for i,name in enumerate(folders):
        y=726+24*i
        icon('folder_arrow_' + name,'chevron-right',24,y+6.5,12); icon('folder_icon_' + name,'folder',37,y+4,17)
        button('folder_' + name,name,60,y,211,24)
    for i,name in enumerate(['default_bus_layout.tres','icon.png','LICENSE.md','README.md']):
        y=895+i*24; icon('asset_icon_' + str(i),'file',37,y+3,17); button('asset_' + str(i),name,59,y,217,24)
    button('scene_menu','     menu',302,46,72,31); button('scene_level','        level',379,46,108,31,'tab'); button('scene_player','     player',504,46,91,31); icon('new_scene','plus',594,51,18)
    icon('scene_menu_icon','scene-tab-menu',301,52,18); icon('scene_level_icon','scene-tab-level',391,52,18); icon('scene_player_icon','scene-tab-player',502,52,18); icon('scene_level_close','scene-tab-close',453,52,18)
    toolbar = [('tool-viewport',307),('tool-move',338),('tool-rotate',369),
               ('tool-scale',400),('tool-select',432),('tool-script',476),
               ('tool-lock',509),('tool-pivot',541),('tool-ruler',574),
               ('tool-cube',617),('tool-joint',650),('tool-light',693),('tool-view',725)]
    for i,(name,x) in enumerate(toolbar):
        button('tool_' + str(i),'',x-10,82,27,29); icon('tool_icon_' + str(i),name,x-9,87,18)
    icon('toolbar_dots','dots',748,88,16); button('transform','Transform',793,83,83); button('view','View',878,83,50)
    panel('perspective',299,125,120,33,'viewport_overlay'); label('perspective_text','Perspective',325,130,94,25); icon('perspective_more','dots',305,132,15)
    for name,x,w in [('Inspector',1601,79),('Signals',1697,70),('Groups',1780,75)]: button('dock_' + name,name,x,46,w,30,'tab' if name=='Inspector' else 'plain')
    icon('inspector_dots','dots',1898,50,17)
    for name,x in [('file',1600),('folder',1632),('save',1663),('dots',1696)]: icon('inspector_tool_'+name,name,x,86,16)
    button('inspector_back','<',1834,82,22); button('inspector_forward','>',1862,82,22)
    panel('selected_header',1594,110,285,32,'selected'); icon('selected_icon','probe',1601,117,18); label('selected_name','ReflectionProbe2',1628,114,242,25)
    node('property_search','TextInput',1594,146,285,29,'','input'); label('property_search_hint','Filter Properties',1603,150,231,23,'muted'); icon('property_search_icon','search',1849,150,18)
    start=len(NODES)
    panel('probe_header',1594,185,302,30,'section'); icon('probe_header_icon','probe',1677,191,17); label('probe_type','ReflectionProbe',1700,188,187,26)
    label('mode_label','Update Mode',1600,222,142,27); button('update_mode','Once (Fast)   v',1744,220,152,29,'input')
    for id,text,y,value in [('intensity','Intensity',252,'1.0'),('blend','Blend Distance',283,'1.0 m'),('max_distance','Max Distance',315,'0.0 m')]:
        label(id+'_label',text,1600,y,142,28); node(id,'TextInput',1744,y,152,27,value,'input'); panel(id+'_line',1751,y+24,134,2,'scroll')
    for id,thumb_x,y in [('intensity',1883,252),('blend',1801,283),('max_distance',1753,315)]:
        panel(id+'_slider_thumb',thumb_x,y+20,4,8,'slider-thumb')
    label('size_label','Size',1600,348,180,26); icon('size_reset','reset',1875,349,17)
    for i,(axis,val) in enumerate([('x','71.634'),('y','100.0 m'),('z','129.154')]):
        x=1594+i*101; panel('size_bg_'+axis,x,370,101,29,'input'); label('size_axis_'+axis,axis,x+7,374,15,24,axis); node('size_'+axis,'TextInput',x+27,373,72,24,val,'plain')
    label('origin_label','Origin Offset',1600,405,200,25)
    for i,axis in enumerate('xyz'):
        x=1594+i*101; panel('origin_bg_'+axis,x,430,101,28,'input'); label('origin_axis_'+axis,axis,x+7,433,16,24,axis); node('origin_'+axis,'TextInput',x+27,433,72,24,'0.0 m','plain')
    for id,text,y in [('projection','Box Projection',464),('interior','Interior',495),('shadows','Enable Shadows',526)]:
        label(id+'_label',text,1600,y,145,26); panel(id+'_background',1744,y-2,152,29,'input'); node(id,'Checkbox',1752,y+1,17,17,style='checkbox'); label(id+'_on','On',1778,y,86,24)
    def masks(id,title,y):
        label(id+'_label',title,1600,y-29,225,26)
        panel(id+'_bg',1594,y-3,302,64,'input')
        for row in range(2):
            for col in range(10):
                n=col+1+row*5 if col<5 else col+6+row*5
                button(id+'_'+str(n),str(n),1595+col*25,y+row*25,24,24,'mask')
        icon(id+'_more','dots',1882,y+15,16)
    masks('cull','Cull Mask',585); masks('reflection','Reflection Mask',677)
    label('lod_label','Mesh LOD Threshold',1600,741,147,26); node('lod','TextInput',1744,740,152,27,'1.0','input')
    button('ambient','>  Ambient',1598,768,290,24)
    panel('visual_header',1594,793,302,30,'section'); label('visual_type',': :  VisualInstance3D',1672,797,222,26)
    masks('layers','Layers',860)
    panel('node3d_header',1594,921,302,29,'section'); label('node3d_title','o  Node3D',1706,924,190,25)
    button('transform_header','v  Transform',1596,955,289,24)
    label('position_label','Position',1600,984,190,24)
    for i,(axis,val) in enumerate([('x','73.997'),('y','0.0 m'),('z','-12.209')]):
        x=1594+i*101; panel('position_bg_'+axis,x,1007,101,28,'input'); label('position_axis_'+axis,axis,x+7,1010,16,24,axis); node('position_'+axis,'TextInput',x+27,1010,72,24,val,'plain')
    label('rotation_label','Rotation',1600,1041,190,26)
    inspector_children=NODES[start:]; del NODES[start:]
    group=node('properties','ScrollView',1594,185,302,840,style='transparent')
    group['event']='none'
    for n in inspector_children: n['rect']['x']-=1594; n['rect']['y']-=185
    content_height=max(n['rect']['y']+n['rect']['height'] for n in inspector_children)
    group['children']=[dict(id='inspector_content',type='Panel',layer='inspector_content',rect=dict(x=0,y=0,width=302,height=content_height),style='transparent',state='enabled',event='none',children=inspector_children)]
    node('scene_name','TextInput',1600,218,290,28,'GameObject','input')
    label('scene_name_label','Name',1600,190,280,24)
    label('scene_transform_header','Transform',1600,255,280,25,'text')
    for axis,y in [('X',288),('Y',320),('Z',352)]:
        label('scene_position_label_' + axis,axis,1600,y,24,26)
        node('scene_position_' + axis,'TextInput',1630,y,250,27,'0.0','input')
    button('scene_component_add','Add Component',1600,392,290,28,'input')
    button('scene_component_add_camera','Add Camera',1600,428,140,28,'input')
    button('scene_component_add_light','Add Light',1750,428,140,28,'input')
    group=panel('signals_content',1594,185,302,887,'panel'); group['children']=[dict(id='signals_title',type='Label',layer='signals_title',rect=dict(x=15,y=20,width=280,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='Signals'),dict(id='signal_visibility_changed',type='Label',layer='signal_visibility_changed',rect=dict(x=15,y=50,width=280,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='visibility_changed()'),dict(id='signal_tree_entered',type='Label',layer='signal_tree_entered',rect=dict(x=15,y=76,width=280,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='tree_entered()')]
    group=panel('groups_content',1594,185,302,887,'panel'); group['children']=[dict(id='groups_title',type='Label',layer='groups_title',rect=dict(x=15,y=20,width=280,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='Groups'),dict(id='groups_empty',type='Label',layer='groups_empty',rect=dict(x=15,y=50,width=280,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='No groups assigned')]
    panel('inspector_scroll_track',1900,186,6,840,'input'); button('inspector_scroll','',1900,187,6,607,'scroll')
    for name,x,w in [('Output',299,59),('Debugger',364,80),('Audio',450,49),('Animation',508,80),('Shader Editor',596,110)]: button('bottom_'+name.replace(' ','_'),name,x,1042,w,28,'tab' if name=='Output' else 'plain')
    label('version','ARP Editor',1465,1046,116,24,'muted')
    p=panel('output_panel',287,805,1296,230,'panel'); p['children']=[dict(id='output_text',type='Label',layer='output_text',rect=dict(x=18,y=18,width=1260,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='Output'),dict(id='output_status',type='Label',layer='output_status',rect=dict(x=18,y=48,width=1260,height=24),style='text',state='enabled',event='none',fontToken='font.editor',text='ARP Editor initialized.'),dict(id='output_source',type='Label',layer='output_source',rect=dict(x=18,y=76,width=1260,height=24),style='muted',state='enabled',event='none',fontToken='font.editor',text='Viewport: static reference preview.')]
    p=panel('menu_popup',8,39,230,145,'section'); p['children']=[]
    for i,(id,title) in enumerate([('save_scene','Save Scene  Ctrl+S'),('open_scene','Open Scene'),('close_menu','Close Menu')]):
        p['children'].append(dict(id=id,type='Button',layer=id,rect=dict(x=12,y=10+i*39,width=205,height=32),style='plain',state='enabled',event='activate',fontToken='font.editor',text=title))
    semibold_ids = {
        'menu_Scene', 'menu_Project', 'menu_Debug', 'menu_Editor', 'menu_Help',
        'tab_scene', 'tab_import', 'tab_files', 'tab_history',
        'scene_menu', 'scene_level', 'scene_player',
        'dock_Inspector', 'dock_Signals', 'dock_Groups',
        'selected_name', 'probe_type', 'visual_type', 'node3d_title',
        'bottom_Output', 'bottom_Debugger', 'bottom_Audio', 'bottom_Animation', 'bottom_Shader_Editor',
        'workspace_Script', 'workspace_Game',
    }
    def apply_font_hierarchy(nodes):
        for n in nodes:
            if n['id'] in semibold_ids and 'fontToken' in n:
                n['fontToken'] = 'font.editor.semibold'
            apply_font_hierarchy(n.get('children', []))
    apply_font_hierarchy(NODES)
    root=dict(id='root',type='Panel',layer='root',rect=dict(x=0,y=0,width=1920,height=1080),style='root',state='enabled',event='none',children=NODES)
    lines=['# Generated authoring data; regenerate with generate_reference_assets.py.', 'type: ui.document', 'contractVersion: ui.document/v1', 'canvas: { id: arp-editor, width: 1920, height: 1080, dpi: 96 }','textures:']
    for t in TEXTURES:
        lines+=['  - '+json.dumps(t)]
    lines+=['root:']+emit_mapping(root,2)
    (ROOT/'editor.arpui').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    metadata=dict(
        type='editor.reference', contractVersion='arp-editor-reference/v1', referenceSize=[1920,1080],
        viewportCrop=[287,116,1583,1034], atlasSize=list(ATLAS_SIZE),
        atlasCombinedHeight=ATLAS_COMBINED_HEIGHT,
        atlasLayout=dict(origin=list(ATLAS_GRID_ORIGIN),cellSize=ATLAS_CELL_SIZE,
                         gutter=ATLAS_GUTTER,spriteSize=ATLAS_SPRITE_SIZE,
                         columns=ATLAS_COLUMNS,regions={k:list(v) for k,v in ATLAS_REGIONS.items()}),
        glyphPageSize=[2048,512],viewportKind='StaticReferencePreview',
        source='User supplied Godot editor screenshot')
    (ROOT/'reference.yaml').write_text('\n'.join(emit_mapping(metadata,0))+'\n',encoding='utf-8')
    print('Generated',len(NODES),'root controls and',len(TEXTURES),'atlas bindings')


if __name__ == '__main__': main()


