#!/usr/bin/env python3
"""Survey tool: lists what is inside the game's dvdbnd*.bhd5 / .bdt archives WITHOUT writing any game
files out. Run it on your PC against the folder that holds dvdbnd0.bhd5/.bdt ... dvdbnd3.bhd5/.bdt
(the game's RomFS). It writes one small text file you can send back:  archive_index.csv

  python3 scan_archives.py <folder with the dvdbnd files> [output.csv]

For every entry it records the archive, path hash, offset, size, what it is (DCX-wrapped or not,
TPF texture pack, BND3/BND4 bundle, ...) and, for texture packs and bundles, the names inside them.
Nothing is uploaded anywhere; the CSV contains only names, sizes and hashes, no game data."""
import csv, os, struct, sys, zlib

def u32(b, o): return struct.unpack_from("<I", b, o)[0]

def read_bhd5(path):
    d = open(path, "rb").read()
    if d[:4] != b"BHD5": raise SystemExit("%s is not a BHD5 file" % path)
    count, off = u32(d, 0x10), struct.unpack_from("<Q", d, 0x18)[0]
    ents = []
    for i in range(count):
        n, _, eoff = struct.unpack_from("<IIQ", d, off + 16 * i)
        for j in range(n):
            h, size, offset = struct.unpack_from("<IIQ", d, eoff + 16 * j)
            ents.append((h, size, offset))
    return ents

def undcx(b):
    """DCX container -> decompressed bytes (zlib flavour used by Dark Souls)."""
    if b[:4] != b"DCX\0": return None
    csize = u32(b, 0x20)
    try: return zlib.decompress(b[0x4C:0x4C + csize])
    except zlib.error: return None

def cstr(b, o, wide):
    if o <= 0 or o >= len(b): return ""
    if wide:
        e = o
        while e + 1 < len(b) and b[e:e + 2] != b"\0\0": e += 2
        return b[o:e].decode("utf-16-le", "replace")
    e = b.find(b"\0", o)
    return b[o:e if e >= 0 else len(b)].decode("shift_jis", "replace")

def bnd3_names(b, limit=40):
    flags = b[0xC]
    count = u32(b, 0x10)
    ids, names, longoff, comp = flags & 0x40, flags & 0xC, flags & 0x10, flags & 0x20  # checked on GameParam (flags 0x7C: 28-byte entries)
    pos, out = 0x20, []
    for _ in range(count):
        pos += 8 + (8 if longoff else 4)
        if ids: pos += 4
        nameoff = u32(b, pos) if names else 0
        if names: pos += 4
        if comp: pos += 4
        out.append(cstr(b, nameoff, False).replace("\\", "/").split("/")[-1])
    return count, out[:limit]

def tpf_info(b, limit=40):
    count, plat, flag2, enc = u32(b, 8), b[0xC], b[0xD], b[0xE]
    stride = 16 if flag2 == 0 else 20
    out = []
    for i in range(min(count, 5000)):
        o = 0x10 + i * stride
        foff, fsize = u32(b, o), u32(b, o + 4)
        fmt, nameoff = b[o + 8], u32(b, o + 12)
        name = cstr(b, nameoff, enc == 1)
        wh = ""
        if b[foff:foff + 4] == b"DDS ":
            wh = "%dx%d" % (u32(b, foff + 16), u32(b, foff + 12))
        out.append("%s[%s,f%d]" % (name, wh or "?", fmt))
    return count, plat, flag2, enc, out[:limit]

def describe(raw):
    """-> (wrapper, kind, detail)"""
    wrapper, data = "raw", raw
    if raw[:4] == b"DCX\0":
        data = undcx(raw)
        wrapper = "dcx"
        if data is None: return "dcx", "undecodable", ""
    m = data[:4]
    try:
        if m == b"TPF\0":
            c, plat, f2, enc, nm = tpf_info(data)
            return wrapper, "TPF", "platform=%d flag2=%d enc=%d textures=%d | %s" % (plat, f2, enc, c, "; ".join(nm))
        if m == b"BND3":
            c, nm = bnd3_names(data)
            return wrapper, "BND3", "files=%d | %s" % (c, "; ".join(nm))
    except Exception as e:  # never let one odd file stop the survey
        return wrapper, m.decode("latin1", "replace").strip("\0") + "(parse error)", str(e)
    if m == b"BND4": return wrapper, "BND4", ""
    if m == b"DDS ": return wrapper, "DDS", "%dx%d" % (u32(data, 16), u32(data, 12))
    return wrapper, "other:" + data[:4].hex(), ""

def main():
    folder = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "archive_index.csv"
    rows, kinds = [], {}
    for n in range(0, 8):
        bhd, bdt = os.path.join(folder, "dvdbnd%d.bhd5" % n), os.path.join(folder, "dvdbnd%d.bdt" % n)
        if not (os.path.exists(bhd) and os.path.exists(bdt)): continue
        ents = read_bhd5(bhd)
        print("dvdbnd%d: %d entries" % (n, len(ents)), flush=True)
        with open(bdt, "rb") as f:
            for i, (h, size, offset) in enumerate(ents):
                f.seek(offset)
                raw = f.read(size)
                w, k, det = describe(raw)
                kinds[(n, w, k)] = kinds.get((n, w, k), 0) + 1
                rows.append(["dvdbnd%d" % n, "%08x" % h, offset, size, w, k, det])
                if i % 2000 == 0: print("  %d/%d" % (i, len(ents)), flush=True)
    with open(out, "w", newline="", encoding="utf-8") as fh:
        wr = csv.writer(fh)
        wr.writerow(["archive", "hash", "offset", "size", "wrapper", "kind", "detail"])
        wr.writerows(rows)
    print("\nWrote", out)
    for k, v in sorted(kinds.items()): print(k, v)

if __name__ == "__main__":
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    main()
