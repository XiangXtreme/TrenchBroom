#!/usr/bin/env python3
"""Generate original tutorial textures; pack with the GoldSrc WAD asset tool."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def base(name, color, line, label=None, stripes=False):
    image = Image.new("RGB", (128, 128), color)
    draw = ImageDraw.Draw(image)
    if stripes:
        for x in range(-128, 256, 32):
            draw.polygon([(x, 0), (x + 14, 0), (x - 114, 128), (x - 128, 128)], fill=line)
    else:
        for y in range(0, 128, 32):
            draw.line((0, y, 127, y), fill=line, width=2)
            offset = 32 if (y // 32) % 2 else 0
            for x in range(offset, 128, 64):
                draw.line((x, y, x, y + 31), fill=line, width=2)
    if label:
        draw.rectangle((5, 45, 122, 83), fill=(25, 28, 31), outline=(225, 225, 215), width=2)
        font = ImageFont.load_default()
        bounds = draw.textbbox((0, 0), label, font=font)
        width = bounds[2] - bounds[0]
        draw.text(((128 - width) // 2, 57), label, fill=(255, 255, 245), font=font)
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    output = parser.parse_args().directory
    output.mkdir(parents=True, exist_ok=True)
    materials = {
        "WALL": ((151, 153, 146), (115, 118, 112), None, False),
        "FLOOR": ((104, 107, 101), (72, 76, 72), None, False),
        "TRIM": ((174, 170, 155), (123, 119, 108), None, False),
        "BLUE": ((65, 122, 150), (43, 87, 110), None, False),
        "ORANGE": ((198, 135, 66), (151, 94, 46), None, False),
        "LAMP": ((250, 214, 151), (207, 166, 104), "LIGHT", False),
        "HINT": ((226, 180, 44), (65, 57, 35), "HINT", True),
        "SKIP": ((115, 104, 142), (49, 43, 63), "SKIP", True),
        "CLIP": ((193, 74, 70), (73, 35, 35), "CLIP", True),
        "NULL": ((75, 75, 75), (35, 35, 35), "NULL", True),
        "NOCLIP": ((87, 148, 112), (35, 66, 49), "NOCLIP", True),
    }
    for name, values in materials.items():
        base(name, *values).save(output / f"{name}.png")
    print(f"Wrote {len(materials)} original textures to {output}")


if __name__ == "__main__":
    main()
