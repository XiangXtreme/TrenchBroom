#!/usr/bin/env python3
"""Draw original exhibit textures as PNGs, ready for the GoldSrc WAD tool."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from showcase import SIGNS


def find_font(requested):
    candidates = [requested] if requested else [
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
    ]
    for path in candidates:
        if path and Path(path).is_file():
            return str(path)
    raise FileNotFoundError("A Chinese-capable font is required; pass --font PATH")


def font_that_fits(draw, font_path, text, size, max_width):
    while size >= 16:
        font = ImageFont.truetype(font_path, size)
        if draw.textbbox((0, 0), text, font=font)[2] <= max_width:
            return font
        size -= 1
    raise ValueError(f"Text is too long for the sign: {text}")


def sign_image(font_path, category, title, line1, line2):
    image = Image.new("RGB", (512, 256), (20, 31, 40))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 511, 255), outline=(91, 121, 128), width=3)
    draw.rectangle((10, 10, 501, 245), outline=(39, 62, 70), width=2)
    draw.rectangle((13, 14, 20, 241), fill=(49, 188, 177))
    draw.rectangle((32, 24, 478, 25), fill=(60, 88, 93))
    draw.rectangle((32, 74, 480, 75), fill=(60, 88, 93))
    draw.text((32, 32), category, font=font_that_fits(draw, font_path, category, 24, 440), fill=(91, 214, 201))
    draw.text((31, 82), title, font=font_that_fits(draw, font_path, title, 38, 450), fill=(248, 244, 224))
    for y, line in ((149, line1), (192, line2)):
        draw.text((32, y), line, font=font_that_fits(draw, font_path, line, 26, 446), fill=(202, 217, 213))
    draw.rectangle((454, 225, 479, 229), fill=(233, 171, 83))
    return image


def tiled(color, seam, *, kind="panel"):
    image = Image.new("RGB", (128, 128), color)
    draw = ImageDraw.Draw(image)
    if kind == "panel":
        draw.rectangle((1, 1, 126, 126), outline=seam, width=3)
        draw.line((0, 63, 127, 63), fill=seam, width=2)
        for x in (8, 119):
            for y in (8, 55, 72, 119):
                draw.ellipse((x - 2, y - 2, x + 2, y + 2), fill=seam)
    elif kind == "floor":
        for n in range(0, 129, 32):
            draw.line((n, 0, n, 127), fill=seam, width=2)
            draw.line((0, n, 127, n), fill=seam, width=2)
        for x in range(16, 128, 32):
            for y in range(16, 128, 32):
                draw.ellipse((x - 1, y - 1, x + 1, y + 1), fill=seam)
    return image


def tool_texture(font_path, label, color):
    image = Image.new("RGB", (128, 128), color)
    draw = ImageDraw.Draw(image)
    for x in range(-128, 256, 32):
        draw.polygon([(x, 0), (x + 12, 0), (x - 116, 128), (x - 128, 128)], fill=(31, 38, 42))
    draw.rectangle((3, 43, 124, 85), fill=(21, 29, 33), outline=(240, 240, 218), width=2)
    font = font_that_fits(draw, font_path, label, 21, 112)
    bounds = draw.textbbox((0, 0), label, font=font)
    draw.text(((128 - (bounds[2] - bounds[0])) / 2, 49), label, font=font, fill=(250, 248, 231))
    return image


def write_textures(output, font_path):
    output.mkdir(parents=True, exist_ok=True)
    materials = {
        "WALL": ((115, 129, 128), (76, 93, 94), "panel"),
        "FLOOR": ((72, 85, 88), (48, 61, 65), "floor"),
        "TRIM": ((157, 162, 150), (97, 112, 109), "panel"),
        "BLUE": ((56, 125, 150), (31, 84, 102), "panel"),
        "ORANGE": ((207, 142, 65), (145, 89, 38), "panel"),
        "LAMP": ((247, 220, 167), (217, 172, 106), "panel"),
        "MINLIT": ((96, 110, 112), (73, 88, 91), "panel"),
        "MINBASE": ((96, 110, 112), (73, 88, 91), "panel"),
    }
    for name, (base, seam, kind) in materials.items():
        tiled(base, seam, kind=kind).save(output / f"{name}.png")
    for name, color in {
        "HINT": (225, 173, 47), "SKIP": (130, 108, 157),
        "CLIP": (210, 81, 70), "NULL": (101, 106, 107),
        "NOCLIP": (74, 166, 126), "ORIGIN": (94, 177, 191),
        "AAATRIGGER": (196, 127, 56),
    }.items():
        tool_texture(font_path, name, color).save(output / f"{name}.png")
    sky = Image.new("RGB", (128, 128))
    draw = ImageDraw.Draw(sky)
    for y in range(128):
        t = y / 127
        draw.line((0, y, 127, y), fill=(int(77 + 58*t), int(136 + 49*t), int(177 + 39*t)))
    sky.save(output / "SKY.png")
    for name, sign in SIGNS.items():
        sign_image(font_path, *sign).save(output / f"{name}.png")
    print(f"Wrote {len(materials) + 8 + len(SIGNS)} textures to {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--font", type=Path, help="Chinese-capable TrueType/OpenType font")
    args = parser.parse_args()
    write_textures(args.directory, find_font(args.font))


if __name__ == "__main__":
    main()
