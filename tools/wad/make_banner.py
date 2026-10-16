#!/usr/bin/env python3
"""Build tools/wad/banner.bnr, WiiFlix's channel banner, from the template's.

    tools/.venv/bin/python tools/wad/make_banner.py

The banner (content 0 of a channel, opening.bnr) of tools/wad/template.wad
is WiiFin's.  This keeps its layouts, animations and sound and puts
WiiFlix's pictures in its two textures, then names the channel WiiFLIX:

    assets/channel_icon.png    170x96   the Wii Menu's icon   (icon.bin)
    assets/channel_banner.png  608x456  shown on opening it   (banner.bin)

Both PNGs must be those sizes (the template's textures and layouts).  The
textures are RGB5A3, as the template's.  make_wad.py puts banner.bnr in the
WAD; run this again after changing either picture.

The banner is 0x40 bytes of zeros, the IMET header (the channel's names in
ten languages, the three files' sizes, an MD5), then a U8 archive holding
meta/banner.bin, meta/icon.bin and meta/sound.bin.  banner.bin and icon.bin
are each an IMD5 header (size, MD5) then an LZ77-compressed U8 archive of
arc/blyt, arc/anim and arc/timg/*.tpl.
"""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

try:
    from libWiiPy.archive import lz77, u8
    import libWiiPy
except ImportError:
    sys.exit("make_banner.py needs libWiiPy (setup.sh installs it in tools/.venv)")

WAD = Path(__file__).resolve().parent
REPO = WAD.parent.parent
NAME = "WiiFLIX"
PICTURES = {"icon.bin": ("icon.tpl", REPO / "assets" / "channel_icon.png"),
            "banner.bin": ("banner.tpl", REPO / "assets" / "channel_banner.png")}


def read_png(path: Path):
    """width, height and RGBA bytes of an 8-bit RGB or RGBA PNG (no interlace)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or ctype not in (2, 6) or interlace:
                sys.exit(f"{path}: needs 8-bit RGB or RGBA, not interlaced")
            bpp = 4 if ctype == 6 else 3
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        ftype, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:   line[x] = (line[x] + a) & 0xFF
            elif ftype == 2: line[x] = (line[x] + b) & 0xFF
            elif ftype == 3: line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line) if bpp == 4 else
                    b"".join(line[k:k + 3] + b"\xff" for k in range(0, stride, 3)))
        prev = line
    return w, h, b"".join(rows)


def rgb5a3(w: int, h: int, rgba: bytes) -> bytes:
    """The GX RGB5A3 texture: 4x4 tiles, 16 bits a pixel (opaque: 1RRRRRGGGGGBBBBB,
    else 0AAARRRRGGGGBBBB)."""
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    if x >= w or y >= h:
                        out += b"\0\0"
                        continue
                    r, g, b, a = rgba[(y * w + x) * 4:(y * w + x) * 4 + 4]
                    if a >= 0xE0:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                    else:
                        v = (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
                    out += struct.pack(">H", v)
    return bytes(out)


def set_file(arc, name: str, data: bytes) -> None:
    """Replace a file of a U8Archive: its node's size too (dump() places the
    files by their nodes' sizes, not their data's)."""
    k = arc.file_name_list.index(name)
    arc.file_data_list[k] = data
    arc.u8_node_list[k].size = len(data)


def get_file(arc, name: str) -> bytes:
    return arc.file_data_list[arc.file_name_list.index(name)]


def new_tpl(tpl: bytes, png: Path) -> bytes:
    """The template's TPL with the PNG as its (only) image."""
    table = struct.unpack(">I", tpl[8:12])[0]
    hdr = struct.unpack(">I", tpl[table:table + 4])[0]
    th, tw, fmt, data_off = struct.unpack(">HHII", tpl[hdr:hdr + 12])
    w, h, rgba = read_png(png)
    if (w, h) != (tw, th) or fmt != 5:
        sys.exit(f"{png}: {w}x{h}, the texture is {tw}x{th} (format {fmt})")
    pixels = rgb5a3(w, h, rgba)
    return tpl[:data_off] + pixels + tpl[data_off + len(pixels):]


def new_part(part: bytes, tpl_name: str, png: Path) -> tuple[bytes, int]:
    """icon.bin or banner.bin with the new picture; and its archive's size."""
    if part[:4] != b"IMD5":
        sys.exit("banner part without its IMD5 header")
    body = part[32:]
    raw = lz77.decompress_lz77(body) if body[:4] == b"LZ77" else body
    arc = u8.U8Archive()
    arc.load(raw)
    set_file(arc, tpl_name, new_tpl(get_file(arc, tpl_name), png))
    raw = arc.dump()
    body = lz77.compress_lz77(raw)
    return b"IMD5" + struct.pack(">I", len(body)) + b"\0" * 8 + hashlib.md5(body).digest() + body, len(raw)


def main() -> None:
    title = libWiiPy.title.Title()
    title.load_wad((WAD / "template.wad").read_bytes())
    bnr = title.get_content_by_index(0)
    at = bnr.find(b"IMET") - 0x40            # the IMET header's 0x600 bytes start here
    imet = u8.IMETHeader()
    imet.load(bnr[at:at + 0x600])
    arc = u8.U8Archive()
    arc.load(bnr[at + 0x600:])

    sizes = dict(zip(("icon.bin", "banner.bin", "sound.bin"), imet.sizes))
    for part, (tpl_name, png) in PICTURES.items():
        data, sizes[part] = new_part(get_file(arc, part), tpl_name, png)
        set_file(arc, part, data)
    imet.sizes = [sizes["icon.bin"], sizes["banner.bin"], sizes["sound.bin"]]
    # the channel's name in every language that had one
    imet.channel_names = [NAME if n else "" for n in imet.channel_names]

    out = bnr[:at] + imet.dump() + arc.dump()
    (WAD / "banner.bnr").write_bytes(out)
    print(f"tools/wad/banner.bnr: {len(out)} bytes, channel \"{NAME}\"")


if __name__ == "__main__":
    main()
