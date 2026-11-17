#!/usr/bin/env python3
"""Pulls a few named entries out of the game's dvdbnd archives, un-compresses them (DCX), and packs them
into small zip files you can send back. Nothing leaves your PC unless you upload the zips yourself.

  python3 extract_entries.py <folder with the dvdbnd files> <out folder>

Edit WANT below to change which entries are taken. Format: (archive number, hash, short label)."""
import os, struct, sys, zipfile, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scan_archives import read_bhd5, undcx

# Texture pages named Icon00..Icon50 (item icon sheets), the header-only texture list that names them,
# and the five UI layout (DRB) files, all from dvdbnd1.  Group name -> entries.
WANT = {
    "upload_1_small": [(1, "67754fc1", "tpf_header_list"), (1, "ab359678", "drb_a"), (1, "29edb046", "drb_b"),
                       (1, "e0691517", "drb_c"), (1, "d2700e33", "drb_d"), (1, "9001a220", "drb_e")],
    "upload_2_iconsheets_a": [(1, "505a064c", "tpf_Icon00_40_01_20_30")],
    "upload_3_iconsheets_b": [(1, "c09f3eef", "tpf_Icon21_50_10_11_12")],
}

def main():
    folder, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cache = {}
    for group, items in WANT.items():
        zpath = os.path.join(out, group + ".zip")
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            for arch, hx, label in items:
                if arch not in cache:
                    cache[arch] = {h: (s, o) for h, s, o in read_bhd5(os.path.join(folder, "dvdbnd%d.bhd5" % arch))}
                size, off = cache[arch][int(hx, 16)]
                with open(os.path.join(folder, "dvdbnd%d.bdt" % arch), "rb") as f:
                    f.seek(off)
                    raw = f.read(size)
                data = undcx(raw) if raw[:4] == b"DCX\0" else raw
                if data is None: raise SystemExit("could not decompress %s" % hx)
                z.writestr("dvdbnd%d_%s_%s.bin" % (arch, hx, label), data)
                print("  %s: %s  %d bytes" % (group, label, len(data)))
        print("wrote", zpath, os.path.getsize(zpath), "bytes")

if __name__ == "__main__":
    if len(sys.argv) < 3: raise SystemExit(__doc__)
    main()
