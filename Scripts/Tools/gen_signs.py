#!/usr/bin/env python3
"""Sign lettering masks (Round 3B): Generated/Assets/Textures/T_HG_Sign_<key>.png

UE's TextRender distance-field font has no Vietnamese stacked diacritics (Đ, Ố, Ẩ, Ế, Ả came out as gaps), so the
viewpoint signs get their lettering baked here with a system font that has them, as a coverage mask (white =
letter) that the sign materials tint (materials.sign_text). Usage: .venv/bin/python Scripts/Tools/gen_signs.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Generated" / "Assets" / "Textures"
FONTS = ["/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
         "/Library/Fonts/Arial Unicode.ttf"]

SIGNS = {
    "MaPiLeng": "ĐÈO MÃ PÍ LÈNG",
    "ThamMa": "DỐC THẨM MÃ",
    "TuSan": "SÔNG NHO QUẾ · TU SẢN",
}
W, H = 1024, 192


def font(size):
    for f in FONTS:
        if Path(f).exists():
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for key, text in SIGNS.items():
        size = 120
        while size > 20:
            fnt = font(size)
            box = ImageDraw.Draw(Image.new("L", (W, H))).textbbox((0, 0), text, font=fnt)
            if box[2] - box[0] <= W * 0.92 and box[3] - box[1] <= H * 0.8:
                break
            size -= 4
        img = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(img)
        box = d.textbbox((0, 0), text, font=fnt)
        x = (W - (box[2] - box[0])) / 2 - box[0]
        y = (H - (box[3] - box[1])) / 2 - box[1]
        d.text((x, y), text, font=fnt, fill=255)
        img.save(OUT / f"T_HG_Sign_{key}.png")
        print(f"T_HG_Sign_{key}: '{text}' at {size} px")


if __name__ == "__main__":
    main()
