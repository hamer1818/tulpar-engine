#!/usr/bin/env python3
"""kit_arayuz atlasi (kaynak: Kupler ile Kureler tools/kup_kure_arayuz.py) — arayuz kiti atlasi (1024x1024 PNG; TASARIM "Arayuz dili").

Ust iki satir (128 px hucre, 8x2): 9 dilimli cerceveler — panel, birincil / ikincil / yikici /
devre disi dugme (basili halleriyle), secili halka, hap, daire, anahtar izi, topuz, golge.
Sonraki satirlar (128 px hucre, 8x6, 48 ikon — U2 ile DOLU): BEYAZ tek renk ikonlar (cizimde renk carpilir). Ikon
glifleri depodaki assets/fonts/MaterialIcons-Regular.ttf'ten (Apache 2.0, zaten vendored)
yerelde PIL ile basilir; disaridan hicbir sey indirilmez.

Hucre ici: 4 px kenar payi (dogrusal ornekleme komsu hucreden renk sizdirmasin), sekil 120x120.
Dikdortgen cercevelerde kose yaricapi 14 px, 9 dilim kose kaynagi 32 px; hap/daire 60 px.
Indeksler tulpar/kit/arayuz.tpr'deki K_* / IK_* sabitleriyle AYNI olmali.

Yeniden uretmek:  python3 tools/kit/arayuz_atlasi.py [--cikti <png>]  (varsayilan tulpar/kit/varlik/arayuz.png)
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MOTOR = os.path.dirname(os.path.dirname(HERE))   # motor deposu koku (font assets/fonts altinda)
OUT = os.path.join(MOTOR, "tulpar", "kit", "varlik", "arayuz.png")
FONT = os.path.join(MOTOR, "assets", "fonts", "MaterialIcons-Regular.ttf")
N = 1024
C = 128
SS = 4  # alt ornekleme


def egim(w, h, ust, alt):
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(ust[i] + (alt[i] - ust[i]) * t) for i in range(4))
        for x in range(w):
            px[x, y] = c
    return im


def cerceve(ust, alt, kenar, r=14, kalin=2, parlak=40, halka=False, golge=False):
    """120x120 sekil, 4x cozunurlukte cizilip kucultulur."""
    S = 120 * SS
    rr = r * SS
    maske = Image.new("L", (S, S), 0)
    ImageDraw.Draw(maske).rounded_rectangle((0, 0, S - 1, S - 1), rr, fill=255)
    if golge:
        g = Image.new("L", (S, S), 0)
        ImageDraw.Draw(g).rounded_rectangle((10 * SS, 10 * SS, S - 10 * SS, S - 10 * SS), rr, fill=150)
        g = g.filter(ImageFilter.GaussianBlur(8 * SS))
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        im.putalpha(g)
        return im.resize((120, 120), Image.LANCZOS)
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if not halka:
        dolgu = egim(S, S, ust, alt)
        im.paste(dolgu, (0, 0), maske)
        # ust ic parlama cizgisi (derinlik)
        p = Image.new("L", (S, S), 0)
        k = kalin * SS
        ImageDraw.Draw(p).rounded_rectangle((k, k, S - 1 - k, S - 1 - k), max(1, rr - k), outline=parlak, width=SS)
        ust_yari = Image.new("L", (S, S), 0)
        ImageDraw.Draw(ust_yari).rectangle((0, 0, S, S // 3), fill=255)
        p = Image.composite(p, Image.new("L", (S, S), 0), ust_yari)
        if parlak:
            kat = Image.new("RGBA", (S, S), (255, 255, 255, 0))
            kat.putalpha(p.point(lambda v: v * parlak // 255))
            im = Image.alpha_composite(im, kat)
    # kenar
    if kalin > 0:
        km = Image.new("L", (S, S), 0)
        ImageDraw.Draw(km).rounded_rectangle((0, 0, S - 1, S - 1), rr, outline=255, width=kalin * SS)
        kat = Image.new("RGBA", (S, S), kenar)
        im = Image.composite(kat, im, km)
    if halka:
        # disa dogru yumusak isilti
        gl = Image.new("L", (S, S), 0)
        ImageDraw.Draw(gl).rounded_rectangle((2 * SS, 2 * SS, S - 2 * SS, S - 2 * SS), rr, outline=200, width=6 * SS)
        gl = gl.filter(ImageFilter.GaussianBlur(3 * SS))
        isik = Image.new("RGBA", (S, S), kenar[:3] + (0,))
        isik.putalpha(gl)
        im = Image.alpha_composite(isik, im)
    return im.resize((120, 120), Image.LANCZOS)


ALTIN = (214, 170, 92, 255)
CERCEVELER = [
    # 0 panel
    dict(ust=(40, 45, 56, 255), alt=(22, 25, 32, 255), kenar=(168, 132, 74, 255), parlak=26),
    # 1 birincil (amber)
    dict(ust=(250, 204, 96, 255), alt=(214, 142, 36, 255), kenar=(122, 78, 18, 255), parlak=110),
    # 2 birincil basili
    dict(ust=(196, 142, 44, 255), alt=(170, 108, 26, 255), kenar=(102, 64, 14, 255), parlak=0),
    # 3 ikincil
    dict(ust=(56, 62, 76, 255), alt=(32, 36, 46, 255), kenar=(186, 148, 84, 255), parlak=30),
    # 4 ikincil basili
    dict(ust=(26, 29, 37, 255), alt=(34, 38, 48, 255), kenar=(240, 200, 120, 255), parlak=0),
    # 5 yikici (koyu kirmizi)
    dict(ust=(160, 50, 44, 255), alt=(108, 28, 28, 255), kenar=(226, 120, 96, 255), parlak=50),
    # 6 devre disi
    dict(ust=(62, 64, 70, 235), alt=(46, 48, 53, 235), kenar=(92, 94, 100, 255), parlak=0),
    # 7 secili halka (ici bos)
    dict(ust=(0, 0, 0, 0), alt=(0, 0, 0, 0), kenar=(255, 214, 120, 255), halka=True, kalin=3, parlak=0),
    # 8 hap (koyu)
    dict(ust=(18, 20, 26, 245), alt=(12, 14, 18, 245), kenar=(150, 118, 66, 220), r=60, parlak=0),
    # 9 daire (ikincil)
    dict(ust=(58, 64, 78, 255), alt=(30, 34, 44, 255), kenar=(196, 158, 90, 255), r=60, parlak=30),
    # 10 daire basili
    dict(ust=(26, 29, 37, 255), alt=(34, 38, 48, 255), kenar=(244, 206, 124, 255), r=60, parlak=0),
    # 11 anahtar izi acik / ilerleme dolgusu (beyaz-acik: renk carpilir)
    dict(ust=(255, 255, 255, 255), alt=(214, 214, 214, 255), kenar=(150, 150, 150, 255), r=60, kalin=0, parlak=0),
    # 12 anahtar izi kapali / iz zemini
    dict(ust=(14, 16, 21, 250), alt=(26, 29, 36, 250), kenar=(86, 90, 100, 255), r=60, parlak=0),
    # 13 topuz (beyaz daire, ince golge kenari)
    dict(ust=(255, 255, 255, 255), alt=(226, 228, 232, 255), kenar=(120, 112, 100, 255), r=60, kalin=1, parlak=0),
    # 14 golge (yumusak)
    dict(ust=(0, 0, 0, 0), alt=(0, 0, 0, 0), kenar=(0, 0, 0, 0), golge=True),
    # 15 satir zemini (hafif, kenarsiz)
    dict(ust=(255, 255, 255, 14), alt=(255, 255, 255, 6), kenar=(255, 255, 255, 30), kalin=1, r=10, parlak=0),
]

# Ikonlar: K_IK_* sirasi arayuz.tpr ile ayni
IKONLAR = [
    ("geri", 0xe5c4), ("ayar", 0xe8b8), ("duraklat", 0xe034), ("oynat", 0xe037), ("kapat", 0xe5cd),
    ("onay", 0xe5ca), ("kilit", 0xe897), ("yildiz", 0xe838), ("kupa", 0xea23), ("gorev", 0xe85d),
    ("dolap", 0xf19e), ("atolye", 0xe869), ("kitap", 0xe865), ("iki", 0xe7fb), ("sonsuz", 0xeb3d),
    ("dil", 0xe894), ("geri_yukle", 0xe8b3), ("reklam_yok", 0xe14b), ("ses", 0xe050), ("muzik", 0xe405),
    ("grafik", 0xe429), ("tekrar", 0xe042), ("harita", 0xe55b), ("bayrak", 0xe153), ("cikis", 0xe879),
    ("uyari", 0xe002), ("paylas", 0xe80d), ("sil", 0xe872), ("bilgi", 0xe88e), ("sag", 0xe5cc),
    ("film", 0xe02c), ("atla", 0xe044), ("kalkan", 0xe9e0), ("kilic", 0xe3e7), ("ev", 0xe88a),
    ("hizli", 0xe01f), ("madalya", 0xea3f), ("ates", 0xe80e), ("sure", 0xe425), ("goz", 0xe8f4),
    # U2 (savas HUD'u): kod None = elle cizilir (cizim_<ad>)
    ("kiliclar", None), ("kule", 0xeaad), ("cag", 0xeacf), ("altin", None), ("kum_saati", 0xe88b),
    ("yonet", 0xe913), ("kristal", 0xead5), ("halka", None),
]


def cizim_kiliclar(S):
    """Capraz iki kilic (MaterialIcons'ta yok): yukari bakan kilic +-45 derece dondurulup birlesir."""
    tek = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(tek)
    cx = S / 2
    b = S * 0.045                      # bicak yari genisligi
    d.polygon([(cx, S * 0.04), (cx + b, S * 0.12), (cx + b, S * 0.66), (cx - b, S * 0.66), (cx - b, S * 0.12)], fill=255)
    d.rectangle((cx - S * 0.15, S * 0.66, cx + S * 0.15, S * 0.71), fill=255)     # balcak
    d.rectangle((cx - b * 0.8, S * 0.71, cx + b * 0.8, S * 0.86), fill=255)       # kabza
    d.ellipse((cx - S * 0.05, S * 0.85, cx + S * 0.05, S * 0.95), fill=255)       # topuz
    a = tek.rotate(-45, resample=Image.BICUBIC, center=(cx, cx))
    c = tek.rotate(45, resample=Image.BICUBIC, center=(cx, cx))
    return Image.fromarray(__import__("numpy").maximum(__import__("numpy").array(a), __import__("numpy").array(c)))


def cizim_altin(S):
    """Altin sikke yigini (para simgesi $ ortacag savasina uymuyordu): iki ust uste disk, kabartma halkali."""
    im = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(im)
    for (x0, y0) in ((S * 0.30, S * 0.30), (S * 0.08, S * 0.08)):
        r = S * 0.31
        g = S * 0.03
        d.ellipse((x0 - g, y0 - g, x0 + 2 * r + g, y0 + 2 * r + g), fill=0)   # ondeki arkadakinden ayrilsin
        d.ellipse((x0, y0, x0 + 2 * r, y0 + 2 * r), fill=255)
        d.ellipse((x0 + r * 0.30, y0 + r * 0.30, x0 + r * 1.70, y0 + r * 1.70), outline=0, width=int(S * 0.035))
    return im


def cizim_halka(S):
    """Hazir isiltisi: ince halka + iceri/disari yumusak isik (ozel guc / Vur hazir)."""
    im = Image.new("L", (S, S), 0)
    k = S * 0.06
    ImageDraw.Draw(im).ellipse((k, k, S - k, S - k), outline=255, width=int(S * 0.035))
    gl = Image.new("L", (S, S), 0)
    ImageDraw.Draw(gl).ellipse((k, k, S - k, S - k), outline=170, width=int(S * 0.09))
    gl = gl.filter(ImageFilter.GaussianBlur(S * 0.025))
    return Image.fromarray(__import__("numpy").maximum(__import__("numpy").array(im), __import__("numpy").array(gl)))


def main():
    import sys
    out = OUT
    if "--cikti" in sys.argv:
        out = sys.argv[sys.argv.index("--cikti") + 1]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    atlas = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    for i, c in enumerate(CERCEVELER):
        im = cerceve(**c)
        atlas.paste(im, ((i % 8) * C + 4, (i // 8) * C + 4), im)
    f = ImageFont.truetype(FONT, 100 * SS // 2)
    for i, (ad, kod) in enumerate(IKONLAR):
        S = 120 * SS // 2
        if kod is None:
            im = globals()["cizim_" + ad](S)
        else:
            im = Image.new("L", (S, S), 0)
            d = ImageDraw.Draw(im)
            bb = d.textbbox((0, 0), chr(kod), font=f)
            w, h = bb[2] - bb[0], bb[3] - bb[1]
            d.text(((S - w) / 2 - bb[0], (S - h) / 2 - bb[1]), chr(kod), font=f, fill=255)
        im = im.resize((120, 120), Image.LANCZOS)
        beyaz = Image.new("RGBA", (120, 120), (255, 255, 255, 255))
        beyaz.putalpha(im)
        x, y = (i % 8) * C + 4, (2 + i // 8) * C + 4
        atlas.paste(beyaz, (x, y), beyaz)
    atlas.save(out, optimize=True)
    print(f"yazildi: {out} ({len(CERCEVELER)} cerceve, {len(IKONLAR)} ikon)")


if __name__ == "__main__":
    main()
