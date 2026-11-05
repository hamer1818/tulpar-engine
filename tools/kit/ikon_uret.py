#!/usr/bin/env python3
"""Yeni oyun ikonu (512x512 PNG): koyu arduvaz zemin, altin halka, adin bas harfleri.

Oyun fabrikasi (tools/yeni_oyun.sh) ilk gun icin uretir; gercek ikon sanatci isidir.
Yerelde PIL + depodaki DejaVuSans; disaridan hicbir sey indirilmez.

  python3 tools/kit/ikon_uret.py --ad "Oyun Adi" --cikti ikon.png
"""
import argparse, os
from PIL import Image, ImageDraw, ImageFont

MOTOR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT = os.path.join(MOTOR, "assets", "fonts", "DejaVuSans.ttf")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ad", required=True)
    ap.add_argument("--cikti", required=True)
    a = ap.parse_args()
    S, SS = 512, 4
    im = Image.new("RGBA", (S * SS, S * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for y in range(S * SS):   # dikey egim
        t = y / (S * SS)
        c = (int(30 + 14 * (1 - t)), int(36 + 14 * (1 - t)), int(52 + 18 * (1 - t)), 255)
        d.line([(0, y), (S * SS, y)], fill=c)
    m = 40 * SS
    d.ellipse([m, m, S * SS - m, S * SS - m], outline=(236, 190, 96, 255), width=18 * SS)
    harf = "".join(w[0] for w in a.ad.split() if w)[:2].upper() or "T"
    f = ImageFont.truetype(FONT, 210 * SS if len(harf) == 1 else 170 * SS)
    bb = d.textbbox((0, 0), harf, font=f)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    d.text(((S * SS - w) / 2 - bb[0], (S * SS - h) / 2 - bb[1]), harf, font=f, fill=(255, 226, 158, 255))
    im = im.resize((S, S), Image.LANCZOS)
    os.makedirs(os.path.dirname(os.path.abspath(a.cikti)), exist_ok=True)
    im.save(a.cikti, optimize=True)
    print("yazildi", a.cikti, harf)

if __name__ == "__main__":
    main()
