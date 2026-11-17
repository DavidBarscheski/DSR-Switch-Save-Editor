#!/usr/bin/env python3
"""Draws the editor's screens from the draw-command dumps written by `DSR_DUMP=<dir> make -C tests`.
Usage: python3 -I tools/render_dump.py <dump_dir> <out_dir> [icons_dir] [font.ttf]
Uses Pillow. Icons are optional: without them the editor's placeholder boxes show."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

dump, out = sys.argv[1], sys.argv[2]
icons = sys.argv[3] if len(sys.argv) > 3 else ""
fontpath = sys.argv[4] if len(sys.argv) > 4 else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
os.makedirs(out, exist_ok=True)
cache = {}
def font(size):
    if size not in cache: cache[size] = ImageFont.truetype(fontpath, size)
    return cache[size]

for fn in sorted(os.listdir(dump)):
    if not fn.endswith(".jsonl"): continue
    img = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
    for line in open(os.path.join(dump, fn), encoding="utf-8"):
        o = json.loads(line)
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        if o["op"] == "fill":
            x, y, w, h = o["r"]; d.rectangle([x, y, x + w - 1, y + h - 1], fill=tuple(o["c"]))
        elif o["op"] == "outline":
            x, y, w, h = o["r"]; d.rectangle([x, y, x + w - 1, y + h - 1], outline=tuple(o["c"]), width=o["t"])
        elif o["op"] == "rfill":
            x, y, w, h = o["r"]; d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=o["rad"], fill=tuple(o["c"]))
        elif o["op"] == "routline":
            x, y, w, h = o["r"]; d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=o["rad"], outline=tuple(o["c"]), width=o["t"])
        elif o["op"] == "text":
            f = font(o["size"]); anchor = ["l", "m", "r"][o["a"]] + "t"
            d.text((o["x"], o["y"]), o["s"], font=f, fill=tuple(o["c"]), anchor=anchor)
        elif o["op"] == "icon":
            p = os.path.join(icons, "%08x_%d.png" % (o["tag"], o["id"])) if icons else ""
            if p and os.path.exists(p):
                x, y, w, h = o["r"]; im = Image.open(p).convert("RGBA").resize((w, h)); layer.paste(im, (x, y))
            else:
                x, y, w, h = o["r"]; s = min(w, h) * 5 // 6; x += (w - s) // 2; y += (h - s) // 2
                d.rectangle([x, y, x + s - 1, y + s - 1], fill=(35, 39, 47, 255), outline=(52, 58, 69, 255))
        img = Image.alpha_composite(img, layer)
    img.convert("RGB").save(os.path.join(out, fn[:-6] + ".png"))
    print(fn[:-6])
