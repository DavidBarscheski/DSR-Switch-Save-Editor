"""Reads Dark Souls Remastered (Switch) texture packs: TPF with platform 4, textures stored in the console's
native block-linear layout. Only what is needed for the menu icon sheets (BC1/BC3/BC4/BC5 ...)."""
import struct, sys
import numpy as np

def u32(b, o): return struct.unpack_from("<I", b, o)[0]

class Tex:
    pass

def read_tpf(d):
    assert d[:4] == b"TPF\0"
    count, plat, flag2, enc = u32(d, 8), d[0xC], d[0xD], d[0xE]
    out = []
    for i in range(count):
        o = 0x10 + 20 * i
        t = Tex()
        t.off, t.size = u32(d, o), u32(d, o + 4)
        t.fmt, t.kind, t.mips, t.flags = d[o + 8], d[o + 9], d[o + 10], d[o + 11]
        no = u32(d, o + 12)
        e = d.index(b"\0", no)
        t.name = d[no:e].decode("shift_jis", "replace")
        blk = d[t.off:t.off + t.size]
        t.hdr_ok = blk[:4] == b"DFvN"
        if t.hdr_ok:
            t.width, t.height, t.depth, t.nmips = u32(blk, 0x40), u32(blk, 0x44), u32(blk, 0x48), u32(blk, 0x4C)
            t.nvn_fmt, t.image_size = u32(blk, 0x50), u32(blk, 0x58)
            t.data = blk[0x200:0x200 + t.image_size]
        out.append(t)
    return out

BPB = {1: 8, 3: 16}  # bytes per 4x4 block: BC1 / BC3 (TPF format 0/1 = DXT1, 3/5 = DXT5; confirmed from data size)

def deswizzle(data, w_blocks, h_blocks, bpb, block_height):
    """Tegra X1 block-linear -> pitch-linear. One entry per 4x4 block (bpb bytes each)."""
    by, bx = np.mgrid[0:h_blocks, 0:w_blocks]
    x = bx * bpb  # byte column
    width_in_gobs = (w_blocks * bpb + 63) // 64
    gob = (by // (8 * block_height)) * 512 * block_height * width_in_gobs + (x // 64) * 512 * block_height \
        + ((by % (8 * block_height)) // 8) * 512
    xx, yy = x % 64, by % 8
    addr = gob + ((xx % 64) // 32) * 256 + ((yy % 8) // 2) * 64 + ((xx % 32) // 16) * 32 + (yy % 2) * 16 + (xx % 16)
    src = np.frombuffer(data, dtype=np.uint8)
    idx = addr[..., None] + np.arange(bpb)
    return src[idx.reshape(-1)].reshape(h_blocks, w_blocks, bpb)

def dds_bytes(w, h, fourcc, payload):
    hdr = b"DDS " + struct.pack("<7I", 124, 0x1007 | 0x80000, h, w, len(payload), 0, 1) + b"\0" * 44 \
        + struct.pack("<II4s5I", 32, 4, fourcc, 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return hdr + payload

def to_image(t, block_height=None):
    from PIL import Image
    import io
    bpb = 16 if t.fmt in (3, 5) else 8
    wb, hb = (t.width + 3) // 4, (t.height + 3) // 4
    if block_height is None:
        block_height = min(16, max(1, 1 << (max(0, (hb + 7) // 8 - 1)).bit_length()))
    sw = deswizzle(t.data, wb, hb, bpb, block_height)
    four = b"DXT5" if bpb == 16 else b"DXT1"
    img = Image.open(io.BytesIO(dds_bytes(t.width, t.height, four, sw.tobytes())))
    return img.convert("RGBA")

if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    for t in read_tpf(d):
        print(t.name, t.fmt, getattr(t, "width", None), getattr(t, "height", None), getattr(t, "nmips", None), t.size, getattr(t, "image_size", None))
