#!/usr/bin/env python3
"""Turns the game's menu icon sheets into one small PNG per item, for YOUR OWN local use.

  python3 export_icons.py <folder with the dvdbnd files> <output folder> [size]

Needs: Python 3, numpy and Pillow (pip install numpy pillow), tools/scan_archives.py and tools/tpf_switch.py
next to this file, and research/param_items.csv (the item list with each item's icon number).
Output: <output>/<type>_<id>.png, e.g. 40000000_100.png (type 40000000 = goods, 20000000 = ring,
10000000 = armor, 00000000 = weapon).  The icons are the game's artwork: keep them on your own computer and
SD card. Do NOT publish them (not in the repo, not in a release)."""
import csv, os, struct, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from scan_archives import read_bhd5, undcx
import tpf_switch as T
from PIL import Image

# TPF packs in dvdbnd1 that hold the Icon sheets (found by tools/scan_archives.py)
PACKS = [(1, 0x505A064C), (1, 0xC09F3EEF)]
# Every icon sheet is a grid of 12 columns of 164 x 184 pixel cells (measured: icon positions repeat every
# 164 / 184 px, and the last 80 px column and the last rows are empty). A 2048 px tall sheet holds 11 rows
# (132 icons), a 1024 px tall one holds 5 rows (60). Icon numbers continue from one sheet to the next:
# weapons 0.., armor 1000.., goods 2000.., spells 3000.., rings 4000..
CELL_W, CELL_H, COLS = 164, 184, 12
SHEET_FIRST = {"Icon00": 0, "Icon01": 132, "Icon10": 1000, "Icon11": 1132, "Icon12": 1264,
               "Icon20": 2000, "Icon21": 2132, "Icon30": 3000, "Icon40": 4000}
SHEETS = SHEET_FIRST
TAGS = {"goods": 0x40000000, "ring": 0x20000000, "armor": 0x10000000, "weapon": 0x0}

def load_sheets(folder):
    sheets = {}
    ents = {}
    for arch, h in PACKS:
        if arch not in ents:
            ents[arch] = {hh: (s, o) for hh, s, o in read_bhd5(os.path.join(folder, "dvdbnd%d.bhd5" % arch))}
        size, off = ents[arch][h]
        with open(os.path.join(folder, "dvdbnd%d.bdt" % arch), "rb") as f:
            f.seek(off); raw = f.read(size)
        data = undcx(raw) if raw[:4] == b"DCX\0" else raw
        for t in T.read_tpf(data):
            if t.name in SHEETS:
                sheets[t.name] = T.to_image(t)
    return sheets

def find_cell(icon_id, sheets):
    for name, first in SHEET_FIRST.items():
        if name in sheets:
            rows = sheets[name].height // CELL_H
            if first <= icon_id < first + COLS * rows:
                return name, icon_id - first
    return None

def crop(sheets, icon_id, size):
    hit = find_cell(icon_id, sheets)
    if not hit: return None
    name, n = hit
    c, r = n % COLS, n // COLS
    cell = sheets[name].crop((c * CELL_W, r * CELL_H, (c + 1) * CELL_W, (r + 1) * CELL_H))
    if cell.getchannel("A").getbbox() is None: return None  # empty cell
    side = max(cell.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(cell, ((side - cell.width) // 2, (side - cell.height) // 2))
    return sq.resize((size, size), Image.LANCZOS)

def main():
    folder, out = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 96
    os.makedirs(out, exist_ok=True)
    sheets = load_sheets(folder)
    print("sheets loaded:", ", ".join(sorted(sheets)))
    csvp = os.path.join(here, "..", "research", "param_items.csv")
    n = skipped = 0
    for r in csv.DictReader(open(csvp, encoding="utf-8")):
        if not r["iconId"]: continue
        im = crop(sheets, int(r["iconId"]), size)
        if im is None: skipped += 1; continue
        im.save(os.path.join(out, "%08x_%s.png" % (TAGS[r["category"]], r["id"])))
        n += 1
    print("wrote %d icons (%d items have no icon in these sheets)" % (n, skipped))

if __name__ == "__main__":
    if len(sys.argv) < 3: raise SystemExit(__doc__)
    main()
