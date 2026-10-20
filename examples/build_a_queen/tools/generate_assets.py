from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1] / "assets"
CELL, COLS = 32, 16
ICONS = ["backdrop", "runner_body", "hair_bob", "hair_curl", "hair_long", "top_rose", "top_ocean", "top_lime", "top_violet", "top_gold", "bottom_rose", "bottom_ocean", "bottom_lime", "bottom_violet", "bottom_gold", "shoes", "accessory", "token_rose", "token_ocean", "token_lime", "token_violet", "token_gold", "obstacle", "rival_body", "crown", "sparkle", "rail", "stage"]
CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,:+-/<>() '#"
COLORS = {"rose":(245,69,137,255), "ocean":(46,170,223,255), "lime":(133,205,72,255), "violet":(145,93,218,255), "gold":(255,190,50,255), "ink":(54,39,74,255), "skin":(248,190,151,255), "white":(255,250,248,255)}

def paint(draw, name, x, y):
    if name == "backdrop":
        for j in range(256):
            t=j/255
            draw.line((x,y+j,x+255,y+j),fill=(int(247-30*t),int(202-47*t),int(235-4*t),255))
        draw.ellipse((x+30,y+18,x+100,y+88),fill=(255,232,174,255))
        draw.polygon([(x,y+128),(x+256,y+128),(x+230,y+256),(x+26,y+256)],fill=(255,247,252,255))
        for px in (x+85,x+171): draw.line((x+128,y+128,px,y+256),fill=(238,184,215,255),width=3)
    elif name.startswith("token_"):
        draw.ellipse((x+3,y+3,x+28,y+28),fill=COLORS[name[6:]],outline=COLORS["white"],width=2)
        draw.polygon([(x+16,y+9),(x+21,y+15),(x+16,y+23),(x+11,y+15)],fill=COLORS["white"])
    elif name.startswith("top_"):
        draw.polygon([(x+7,y+9),(x+16,y+12),(x+25,y+9),(x+29,y+18),(x+23,y+21),(x+22,y+30),(x+10,y+30),(x+9,y+21),(x+3,y+18)],fill=COLORS[name[4:]])
    elif name.startswith("bottom_"):
        draw.polygon([(x+8,y+5),(x+24,y+5),(x+21,y+30),(x+17,y+30),(x+15,y+17),(x+13,y+30),(x+9,y+30)],fill=COLORS[name[7:]])
    elif name.startswith("hair_"):
        draw.ellipse((x+5,y+3,x+27,y+25),fill=(84,46,75,255))
        if name == "hair_long": draw.rectangle((x+5,y+14,x+10,y+31),fill=(84,46,75,255)); draw.rectangle((x+22,y+14,x+27,y+31),fill=(84,46,75,255))
        draw.ellipse((x+10,y+8,x+21,y+23),fill=COLORS["skin"])
    elif name in ("runner_body","rival_body"):
        draw.ellipse((x+9,y+1,x+23,y+16),fill=COLORS["skin"])
        color=COLORS["rose"] if name=="runner_body" else COLORS["violet"]
        draw.rounded_rectangle((x+7,y+14,x+25,y+34),radius=6,fill=color)
        draw.line((x+11,y+32,x+9,y+42),fill=COLORS["ink"],width=4)
        draw.line((x+21,y+32,x+23,y+42),fill=COLORS["ink"],width=4)
    elif name == "shoes":
        draw.rounded_rectangle((x+5,y+11,x+15,y+25),radius=3,fill=COLORS["gold"])
        draw.rounded_rectangle((x+18,y+11,x+28,y+25),radius=3,fill=COLORS["gold"])
    elif name == "accessory": draw.ellipse((x+6,y+8,x+26,y+28),outline=COLORS["gold"],width=4)
    elif name == "obstacle":
        draw.polygon([(x+16,y+2),(x+30,y+29),(x+2,y+29)],fill=(222,72,94,255))
        draw.rectangle((x+14,y+10,x+18,y+21),fill=COLORS["white"])
    elif name == "crown": draw.polygon([(x+3,y+11),(x+10,y+18),(x+16,y+4),(x+22,y+18),(x+29,y+11),(x+26,y+29),(x+6,y+29)],fill=COLORS["gold"])
    elif name == "sparkle": draw.polygon([(x+16,y+2),(x+20,y+12),(x+29,y+16),(x+20,y+20),(x+16,y+30),(x+12,y+20),(x+3,y+16),(x+12,y+12)],fill=COLORS["white"])
    elif name == "rail": draw.rounded_rectangle((x+13,y+1,x+18,y+31),radius=2,fill=(255,255,255,220))
    elif name == "stage": draw.rounded_rectangle((x+2,y+10,x+29,y+30),radius=4,fill=COLORS["gold"])

def main():
    ROOT.mkdir(parents=True,exist_ok=True)
    names=ICONS+[f"glyph_{ord(c):02x}" for c in CHARS]
    start=8*COLS
    rows=8+(len(names)+COLS-1)//COLS
    image=Image.new("RGBA",(COLS*CELL,rows*CELL),(0,0,0,0))
    draw=ImageDraw.Draw(image)
    font=ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf",24)
    paint(draw,"backdrop",0,0)
    for i,name in enumerate(ICONS[1:],start): paint(draw,name,i%COLS*CELL,i//COLS*CELL)
    for i,char in enumerate(CHARS,start+len(ICONS)-1):
        x,y=i%COLS*CELL,i//COLS*CELL
        bounds=draw.textbbox((0,0),char,font=font)
        width,height=bounds[2]-bounds[0],bounds[3]-bounds[1]
        draw.text((x+(CELL-width)//2,y+(CELL-height)//2-bounds[1]),char,font=font,fill=COLORS["ink"])
    image.save(ROOT/"queen_atlas.png",optimize=False)
    lines=["type: arp.sprite-atlas","contractVersion: 1","resourceId: 00000000-0000-4000-8000-000000000301",f"width: {COLS*CELL}",f"height: {rows*CELL}","filter: nearest","alpha: straight","srgb: false","slices:"]
    for i,name in enumerate(names):
        cell=start+i-1
        width,height=(256,256) if name=="backdrop" else (CELL,CELL)
        x,y=(0,0) if name=="backdrop" else (cell%COLS*CELL,cell//COLS*CELL)
        lines.extend([f"  - name: {name}",f"    x: {x}",f"    y: {y}",f"    width: {width}",f"    height: {height}","    pivotX: 0.5","    pivotY: 0.5","    pixelsPerUnit: 1"])
    (ROOT/"queen_atlas.yaml").write_text("\n".join(lines)+"\n",encoding="ascii")

if __name__ == "__main__":
    main()

