#!/usr/bin/env python3
"""Generate small PNG sample images without external dependencies."""
from pathlib import Path
import struct
import zlib

OUT = Path('data/sample_images')
OUT.mkdir(parents=True, exist_ok=True)


def write_png(path, width, height, fn):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(fn(x, y, width, height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n'
    data += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    data += chunk(b'IEND', b'')
    path.write_bytes(data)


def plastic(x, y, w, h):
    bg = (235, 245, 240)
    if w*0.25 < x < w*0.75 and h*0.18 < y < h*0.82:
        return (80, 160, 235)
    if w*0.36 < x < w*0.64 and h*0.45 < y < h*0.58:
        return (250, 250, 250)
    return bg


def paper(x, y, w, h):
    if w*0.18 < x < w*0.82 and h*0.20 < y < h*0.80:
        line = 180 if abs((y % 28) - 2) < 2 else 248
        return (line, line, line)
    return (230, 238, 230)


def can(x, y, w, h):
    cx, cy = w/2, h/2
    d = ((x-cx)/(w*.25))**2 + ((y-cy)/(h*.32))**2
    if d < 1:
        shade = int(170 + 55 * (x / w))
        return (shade, shade, shade)
    return (238, 240, 236)


def glass(x, y, w, h):
    if w*0.35 < x < w*0.65 and h*0.12 < y < h*0.88:
        return (75, 160 + int(50*x/w), 115)
    return (230, 240, 233)


def food(x, y, w, h):
    cx, cy = w/2, h/2
    d = ((x-cx)/(w*.30))**2 + ((y-cy)/(h*.24))**2
    if d < 1:
        return (145, 85 + int(25*x/w), 45)
    return (236, 238, 230)


samples = {
    'plastic_bottle_label.png': plastic,
    'paper_sheet.png': paper,
    'metal_can.png': can,
    'green_glass_bottle.png': glass,
    'food_residue.png': food,
}
for name, fn in samples.items():
    write_png(OUT / name, 320, 240, fn)
print(f'Generated {len(samples)} sample images in {OUT}')
