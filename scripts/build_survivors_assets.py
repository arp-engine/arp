"""Deterministic offline authoring of Survivors' CC0 pixel resources.

Input grid is Kenney's Tilesheet.txt: 12 x 11, 16px, packed PNG (no gap).
Selections and animation edits below are explicit art decisions, not inferred
resource semantics. Runtime consumes the generated PNG and versioned YAML.
"""
from pathlib import Path
import hashlib
import math
import shutil
import struct
import wave
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples/survivors/assets'
SOURCE = OUT / 'source/tiny_dungeon.png'

def uid(n):
    return f'20000000-0000-4000-8000-{n:012d}'

# Original 5x7 uppercase bitmap alphabet, stored here as authoring source.
FONT = {
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'G':['01111','10000','10000','10111','10001','10001','01111'],
'H':['10001','10001','10001','11111','10001','10001','10001'],
'I':['111','010','010','010','010','010','111'],
'J':['00111','00010','00010','00010','10010','10010','01100'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'M':['10001','11011','10101','10101','10001','10001','10001'],
'N':['10001','11001','10101','10011','10001','10001','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'P':['11110','10001','10001','11110','10000','10000','10000'],
'Q':['01110','10001','10001','10001','10101','10010','01101'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'V':['10001','10001','10001','10001','01010','01010','00100'],
'W':['10001','10001','10001','10101','10101','10101','01010'],
'X':['10001','10001','01010','00100','01010','10001','10001'],
'Y':['10001','10001','01010','00100','00100','00100','00100'],
'Z':['11111','00001','00010','00100','01000','10000','11111'],
'0':['01110','10001','10011','10101','11001','10001','01110'],
'1':['010','110','010','010','010','010','111'],
'2':['01110','10001','00001','00010','00100','01000','11111'],
'3':['11110','00001','00001','01110','00001','00001','11110'],
'4':['00010','00110','01010','10010','11111','00010','00010'],
'5':['11111','10000','10000','11110','00001','00001','11110'],
'6':['01110','10000','10000','11110','10001','10001','01110'],
'7':['11111','00001','00010','00100','01000','01000','01000'],
'8':['01110','10001','10001','01110','10001','10001','01110'],
'9':['01110','10001','10001','01111','00001','00001','01110'],
':':['0','1','0','0','1','0','0'], '-':['00000','00000','00000','11111','00000','00000','00000'],
'/':['00001','00001','00010','00100','01000','10000','10000'],
'+':['00000','00100','00100','11111','00100','00100','00000'],
'%':['11001','11010','00100','01000','10110','00110','00000'],
'.':['0','0','0','0','0','1','1'], ' ':['0']*7,
}

def build():
    OUT.mkdir(parents=True, exist_ok=True)
    src=Image.open(SOURCE).convert('RGBA')
    assert src.size==(192,176), 'explicit packed-grid contract'
    atlas=Image.new('RGBA',(512,256))
    atlas.paste(src,(0,0))
    slices=[];sprites=[];animations=[]
    def add(name,x,y,w=16,h=16,ppu=16,pivot=(.5,.5)):
        resource=uid(len(sprites)+2)
        sprites.append((name,resource))
        slices.append(f'  - name: {name}\n    x: {x}\n    y: {y}\n    width: {w}\n    height: {h}\n    pivotX: {pivot[0]}\n    pivotY: {pivot[1]}\n    pixelsPerUnit: {ppu}\n')
        return resource
    tiles={i:add(f'tile_{i:04}',i%12*16,i//12*16) for i in range(132)}
    for j,(name,index) in enumerate([('hero',84),('slime',108),('crawler',120),('ghost',121),('boss',110)]):
        image=src.crop((index%12*16,index//12*16,index%12*16+16,index//12*16+16))
        pose=Image.new('RGBA',(16,16))
        # Authored second pose: stationary silhouette upper 12 rows; alternate
        # bottom four rows one pixel to the right for a stepping/swaying pose.
        pose.paste(image.crop((0,0,16,12)),(0,0))
        pose.paste(image.crop((0,12,15,16)),(1,12))
        atlas.paste(pose,(j*16,176))
        frame=add(name+'_step',j*16,176)
        animation=uid(2000+j)
        animations.append((name,animation,[tiles[index],frame]))
    for j,(name,index) in enumerate([('slime',108),('crawler',120),('ghost',121),('boss',110)]):
        image=src.crop((index%12*16,index//12*16,index%12*16+16,index//12*16+16))
        frames=[]
        for stage,height in enumerate((12,7,2)):
            x,y=j*48+stage*16,192
            atlas.paste(image.resize((16,height),Image.Resampling.NEAREST),(x,y+16-height))
            frames.append(add(name+'_death_'+str(stage),x,y))
        animations.append((name+'_death',uid(2010+j),frames))
    animations.append(('torch',uid(2005),[tiles[29],tiles[32]]))
    animations.append(('blade',uid(2006),[tiles[104],tiles[105]]))
    draw=ImageDraw.Draw(atlas)
    for j,(char,rows) in enumerate(FONT.items()):
        x,y=208+(j%32)*8,(j//32)*10
        for row,line in enumerate(rows):
            for col,pixel in enumerate(line):
                if pixel=='1':draw.point((x+col,y+row),fill=(255,255,255,255))
        add(f'glyph_{ord(char)}',x,y,6,8,pivot=(0,1))
    draw.rectangle((208,32,223,47),fill=(255,255,255,255));add('white',208,32)
    draw.ellipse((225,33,238,46),outline=(120,235,226,255),width=2);add('pulse',224,32)
    draw.polygon([(249,32),(252,37),(257,40),(252,43),(249,47),(246,43),(241,40),(246,37)],fill=(255,226,138,255));add('spark',240,32)
    atlas.save(OUT/'dungeon.png')
    (OUT/'dungeon.atlas.yaml').write_text(f'type: arp.sprite-atlas\ncontractVersion: 1\nresourceId: {uid(1)}\nwidth: 512\nheight: 256\nfilter: nearest\nalpha: straight\nsrgb: true\nslices:\n'+''.join(slices),encoding='utf-8')
    content=f'type: arp.survivors-content\ncontractVersion: 1\natlasId: {uid(1)}\natlasMetadata: dungeon.atlas.yaml\natlasImage: dungeon.png\nsprites:\n'
    for name,resource in sprites:content+=f'  - name: {name}\n    resourceId: {resource}\n'
    content+='animations:\n'
    for name,resource,frames in animations:
        content+=f'  - name: {name}\n    resourceId: {resource}\n    loop: {str(not name.endswith("_death")).lower()}\n    frames:\n'
        for frame in frames:content+=f'      - spriteId: {frame}\n        seconds: 0.16\n        eventId: 1\n'
    content+=f'tileSetId: {uid(3000)}\ntileMapId: {uid(3001)}\n'
    (OUT/'content.yaml').write_text(content,encoding='utf-8')
    # Explicit hierarchy and local reference, separate from character prefabs.
    prefab=f'type: arp.scene-prefab\ncontractVersion: 1\nresourceId: {uid(5000)}\nname: orbit-rig\nobjects:\n'
    for i in range(9):
        prefab+=f'  - id: {i+1}\n    parent: {0 if i==0 else 1}\n    name: {"orbit-root" if i==0 else "blade-"+str(i-1)}\n    active: {"true" if i<=3 else "false"}\n    transform:\n      x: 0.0\n      y: 0.0\n      z: 0.0\n      qx: 0.0\n      qy: 0.0\n      qz: 0.0\n      qw: 1.0\n      sx: {1.0 if i==0 else .7}\n      sy: {1.0 if i==0 else .7}\n      sz: 1.0\n    components:'
        if i==0:prefab+=' []\n'
        else:prefab+=f'\n      - type: arp.sprite/1\n        spriteId: {tiles[104]}\n        r: 1.0\n        g: 1.0\n        b: 1.0\n        a: 1.0\n        sortingLayer: 2\n        sortingOrder: 0\n        ySort: false\n        flipX: false\n        flipY: false\n'
    (OUT/'orbit.prefab.yaml').write_text(prefab,encoding='utf-8')
    tileSet=f'type: arp.tile-set\ncontractVersion: 1\nresourceId: {uid(3000)}\ntileWorldSize: 1.0\ntiles:\n'
    for ident,tile,solid,animated in [(1,49,False,False),(2,51,False,False),(3,64,True,False),(4,29,False,True),(5,77,True,False)]:
        visual=uid(2005) if animated else tiles[tile]
        tileSet+=f'  - id: {ident}\n    visualId: {visual}\n    animated: {str(animated).lower()}\n    solid: {str(solid).lower()}\n'
        if solid:tileSet+='    collision:\n      shape: box\n      halfWidth: 0.45\n      halfHeight: 0.45\n      centerX: 0.0\n      centerY: 0.0\n      friction: 0.8\n      restitution: 0.1\n      category: 2\n      mask: 65535\n'
    (OUT/'dungeon.tileset.yaml').write_text(tileSet,encoding='utf-8')
    tileMap=f'type: arp.tile-map\ncontractVersion: 1\nresourceId: {uid(3001)}\ntileSetId: {uid(3000)}\noriginX: -16.0\noriginY: -16.0\nchunkTiles: 8\nchunks:\n'
    for cy in range(4):
      for cx in range(4):
        tileMap+=f'  - x: {cx}\n    y: {cy}\n    layers:\n'
        for layer,name in enumerate(['ground','obstacles','foreground']):
          cells=[]
          for y in range(8):
            for x in range(8):
              gx,gy=cx*8+x,cy*8+y
              tile=1+(gx*13+gy*7)%7//6 if layer==0 else (3 if gx in (0,31) or gy in (0,31) else (5 if gx in (8,24) and gy in (8,24) else 0)) if layer==1 else (4 if gx in (4,27) and gy in (4,27) else 0)
              if tile:cells.append(f'          - x: {x}\n            y: {y}\n            tileId: {tile}\n            quarterTurns: 0\n            flipX: false\n            flipY: false\n')
          tileMap+=f'      - name: {name}\n        sortingLayer: {(-10,0,5)[layer]}\n        sortingOrder: 0\n        cells:'+ ('\n'+''.join(cells) if cells else ' []\n')
    (OUT/'dungeon.tilemap.yaml').write_text(tileMap,encoding='utf-8')
    # Original two-voice loop and event arpeggios, PCM WAV prepared offline.
    for name,notes,beat in [('hit',[880,440],.04),('upgrade',[523.25,659.25,783.99,1046.5],.09),('menu',[659.25,880],.07),('music',[220,261.63,329.63,293.66,220,261.63,392,329.63],.35)]:
      pcm=bytearray()
      for note in notes:
        for i in range(int(48000*beat)):
          t=i/48000;envelope=min(1,t/.005)*max(0,1-t/beat)
          value=.12*envelope*(math.sin(2*math.pi*note*t)+.25*math.sin(2*math.pi*note*.5*t))
          pcm.extend(struct.pack('<h',int(max(-1,min(1,value))*32767)))
      with wave.open(str(OUT/(name+'.wav')),'wb') as output:
        output.setnchannels(1);output.setsampwidth(2);output.setframerate(48000);output.writeframes(pcm)
    (OUT/'PROVENANCE.yaml').write_text(f'''type: arp.asset-provenance
contractVersion: 1
source: https://kenney.nl/assets/tiny-dungeon
download: https://kenney.nl/media/pages/assets/tiny-dungeon/f8422efb44-1674742415/kenney_tiny-dungeon.zip
license: CC0-1.0
sourcePngSha256: {hashlib.sha256(SOURCE.read_bytes()).hexdigest()}
derivatives: Explicit second animation poses, bitmap alphabet, UI pixels and effects authored by ARP contributors.
audio: Original offline synthesized event tones and two-voice loop by ARP contributors; CC0-1.0.
''',encoding='utf-8')

if __name__=='__main__':build()
