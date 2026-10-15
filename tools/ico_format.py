#!/usr/bin/env python3
"""ICO container and DIB frame encoding.

Takes RGBA PIL images and returns .ico bytes. No SVG/rasteriser dependency -
tools/make_icons.py renders the images and calls in here to pack them.

Frame layouts produced:
  dib_frame          BITMAPINFOHEADER + bottom-up 32bpp BGRA XOR + AND mask
  dib_frame_indexed  BITMAPINFOHEADER + RGBQUAD table + bottom-up indexed XOR
                     + AND mask
"""

import struct

# Windows 16-colour VGA palette, read from the RT_ICON colour table of
# clock.exe / explorer.exe in the Sharp Mobilon HC4100 CE 2.0 ROM
# (references/extracted-roms/sharp_mobilon_hc4100_ce2/nk.bin/fs/Windows).
WIN16_PALETTE = [
    (0x00, 0x00, 0x00), (0x80, 0x00, 0x00), (0x00, 0x80, 0x00),
    (0x80, 0x80, 0x00), (0x00, 0x00, 0x80), (0x80, 0x00, 0x80),
    (0x00, 0x80, 0x80), (0xC0, 0xC0, 0xC0), (0x80, 0x80, 0x80),
    (0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0xFF, 0xFF, 0x00),
    (0x00, 0x00, 0xFF), (0xFF, 0x00, 0xFF), (0x00, 0xFF, 0xFF),
    (0xFF, 0xFF, 0xFF),
]


def dib_frame(im, ce_mask=False):
    """32bpp frame from an RGBA image.

    Without ce_mask the AND mask is all-zero and transparency is carried by the
    BGRA alpha channel. With ce_mask the AND mask is built from alpha and the
    colour under transparent pixels is zeroed."""
    from PIL import Image
    size = im.width
    r, g, b, a = im.split()
    bgra = Image.merge("RGBA", (b, g, r, a))

    and_stride = ((size + 31) // 32) * 4
    if ce_mask:
        px = bytearray(bgra.tobytes())
        for i in range(0, len(px), 4):
            if px[i + 3] < 128:
                px[i] = px[i + 1] = px[i + 2] = 0
        bgra = Image.frombytes("RGBA", (size, size), bytes(px))
        and_mask = _and_mask(a, size, and_stride)
    else:
        and_mask = b"\x00" * (and_stride * size)

    xor = bgra.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    return _header(size, 32, len(xor) + len(and_mask), 0) + xor + and_mask


def dib_frame_indexed(im, bpp=4, dither=False, palette=None):
    """Palettised frame from an RGBA image, at 1/2/4/8 bpp.

    palette is a list of (r, g, b) the indices resolve through; None picks an
    adaptive one. Transparent pixels are forced to palette entry 0 before
    quantising and the AND mask carries the transparency."""
    from PIL import Image
    size = im.width
    colors = 1 << bpp

    alpha = im.getchannel("A")
    rgb = bytearray(im.convert("RGB").tobytes())
    a = alpha.tobytes()
    zero = palette[0] if palette else (0, 0, 0)
    for i in range(size * size):
        if a[i] < 128:
            rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2] = zero
    flat = Image.frombytes("RGB", (size, size), bytes(rgb))

    mode = (getattr(Image, "FLOYDSTEINBERG", 3) if dither
            else getattr(Image, "NONE", 0))
    if palette:
        ref = Image.new("P", (1, 1))
        table = [c for entry in palette[:colors] for c in entry]
        ref.putpalette(table + [0] * (768 - len(table)))
        try:
            pal_im = flat.quantize(palette=ref, dither=mode)
        except TypeError:
            pal_im = flat.quantize(palette=ref)
        pal = table
    else:
        pal_im = flat.convert("P", palette=getattr(Image, "ADAPTIVE", 1),
                              colors=colors, dither=mode)
        pal = (pal_im.getpalette() or [])[:colors * 3]
    pal = list(pal) + [0] * (colors * 3 - len(pal))

    table = bytearray()
    for i in range(colors):
        table += struct.pack("<BBBB", pal[i * 3 + 2], pal[i * 3 + 1],
                             pal[i * 3], 0)

    idx = pal_im.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    per_byte = 8 // bpp
    xor_stride = ((size * bpp + 31) // 32) * 4
    xor = bytearray()
    for y in range(size):
        row = bytearray(xor_stride)
        for x in range(size):
            v = idx[y * size + x] & (colors - 1)
            row[x // per_byte] |= v << (8 - bpp * (x % per_byte + 1))
        xor += row

    and_stride = ((size + 31) // 32) * 4
    and_mask = _and_mask(alpha, size, and_stride)
    return (_header(size, bpp, len(xor) + len(and_mask), 0)
            + bytes(table) + bytes(xor) + and_mask)


def build_ico(frames, bpp=32):
    """Pack [(size, blob), ...] into an .ico. bWidth/bHeight 0 means 256;
    bColorCount is 0 for depths with no palette."""
    from PIL import Image  # noqa: F401  (kept symmetrical with the encoders)
    frames = sorted(frames, key=lambda f: f[0])
    colors = 0 if bpp >= 8 else (1 << bpp)
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    offset = 6 + 16 * len(frames)
    for size, blob in frames:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, colors, 0, 1, bpp,
                           len(blob), offset)
        offset += len(blob)
    for _, blob in frames:
        out += blob
    return bytes(out)


def _and_mask(alpha, size, stride):
    from PIL import Image
    flipped = alpha.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    mask = bytearray()
    for y in range(size):
        row = bytearray(stride)
        for x in range(size):
            if flipped[y * size + x] < 128:
                row[x >> 3] |= 0x80 >> (x & 7)
        mask += row
    return bytes(mask)


def _header(size, bpp, image_bytes, clr_used):
    return struct.pack("<IiiHHIIiiII",
                       40,                 # biSize
                       size, size * 2,     # biWidth, biHeight (XOR + AND)
                       1, bpp,             # biPlanes, biBitCount
                       0,                  # biCompression = BI_RGB
                       image_bytes,
                       0, 0, clr_used, 0)
