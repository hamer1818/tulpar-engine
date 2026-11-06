#!/usr/bin/env python3
"""kit_acilis atlasi — studyo acilisi (splash; 1536x1024 PNG). Kaynak: Kupler ile Kureler tools/kup_kure_acilis.py.

Tek resim yuvasi (motorun 32 yuvasindan biri): ORTATEPE studyo kartinin isareti (renkli + beyaz
siluet), Tulpar logosu (beyaz siluet; cizimde renk carpilir), 11 harflik geometrik yazi (ORTATEPE ve
TULPAR ENGINE harfleri; beyaz), yumusak isilti ve ince ayrac. Her sey YERELDE PIL ile cizilir;
disaridan hicbir sey indirilmez. Tulpar logosu kullanicinin kendi varligi:
/mnt/veri/yazilim/Tulpar/installer/assets/tulpar-logo.png (yoksa --logo ile verilir).

Harf sirasi ve genislikleri tulpar/kit/acilis.tpr'deki AC_HARF / AC_GEN tablolariyla AYNI olmali:
betik sonunda tabloyu basar (elle kopyalanir; kapsam: harf ekleme cok seyrek).

Yeniden uretmek:  python3 tools/kit/acilis_atlasi.py [--logo <png>] [--onizleme <png>] [--cikti <png>]
Varsayilan cikti tulpar/kit/varlik/acilis.png (yeni oyun iskeleti oradan kopyalar).
"""
import argparse
import math
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
KOK = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(KOK, "tulpar", "kit", "varlik", "acilis.png")
LOGO = "/mnt/veri/yazilim/Tulpar/installer/assets/tulpar-logo.png"
W, H = 1536, 1024
SS = 4

# ---- yerlesim (atlas pikseli) -------------------------------------------------------------
ISARET = (0, 0, 512, 512)          # renkli isaret
ISARET_S = (512, 0, 512, 512)      # beyaz siluet (isik suzmesi)
TULPAR = (1024, 0, 512, 512)       # beyaz siluet (renk carpilir)
HARF_Y = 512                       # 11 harf, 128 px hucre, tek satir
HARF_C = 128
ISILTI = (0, 640, 384, 384)        # yumusak beyaz isilti (radyal)
AYRAC = (384, 640, 512, 32)        # iki ucu sonen ince cizgi
PARILTI = (384, 704, 128, 128)     # dort koseli parilti yildizi

HARFLER = "ORTAEPULNGI"
# harf tasarim birimi: buyuk harf boyu 100, govde kalinligi K
K = 15.0
YATAY = K * 0.88                   # yatay cubuklar optik olarak biraz ince
CAP_PX = 92.0                      # hucrede buyuk harf boyu (px)
TABAN_PX = 110.0                   # hucrede taban cizgisi (px)


def egim_dolgu(w, h, c0, c1, aci=90.0):
    """c0 -> c1 dogrusal egim (aci derece; 90 = yukaridan asagi)."""
    im = Image.new("RGBA", (w, h))
    px = im.load()
    ca, sa = math.cos(math.radians(aci)), math.sin(math.radians(aci))
    proj = [ca * x + sa * y for x in (0, w) for y in (0, h)]
    lo, hi = min(proj), max(proj)
    for y in range(h):
        for x in range(w):
            t = ((ca * x + sa * y) - lo) / (hi - lo)
            px[x, y] = tuple(int(round(c0[i] + (c1[i] - c0[i]) * t)) for i in range(4))
    return im


def egim_dolgu_hizli(w, h, c0, c1, aci=90.0):
    # buyuk tuvallerde: kucuk cizip buyut (egim dogrusal, kayip yok)
    k = 8
    im = egim_dolgu(max(2, w // k), max(2, h // k), c0, c1, aci)
    return im.resize((w, h), Image.BICUBIC)


def yuvarlak_tepe(pts, r, n=24):
    """Cokgen koselerini r yaricapla yuvarlar (pts: [(x,y)], r: kose basina liste ya da sayi)."""
    out = []
    m = len(pts)
    rs = r if isinstance(r, (list, tuple)) else [r] * m
    for i in range(m):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % m]
        rr = rs[i]
        if rr <= 0:
            out.append(p1)
            continue
        v0 = (p0[0] - p1[0], p0[1] - p1[1])
        v2 = (p2[0] - p1[0], p2[1] - p1[1])
        l0, l2 = math.hypot(*v0), math.hypot(*v2)
        u0 = (v0[0] / l0, v0[1] / l0)
        u2 = (v2[0] / l2, v2[1] / l2)
        cosa = max(-1.0, min(1.0, u0[0] * u2[0] + u0[1] * u2[1]))
        a = math.acos(cosa)
        d = rr / math.tan(a / 2)
        a0 = (p1[0] + u0[0] * d, p1[1] + u0[1] * d)
        a2 = (p1[0] + u2[0] * d, p1[1] + u2[1] * d)
        # ikinci dereceden Bezier (kontrol noktasi kosenin kendisi) — yumusak ve yeterli
        for k in range(n + 1):
            t = k / n
            x = (1 - t) ** 2 * a0[0] + 2 * (1 - t) * t * p1[0] + t * t * a2[0]
            y = (1 - t) ** 2 * a0[1] + 2 * (1 - t) * t * p1[1] + t * t * a2[1]
            out.append((x, y))
    return out


# ---- ORTATEPE isareti: uc katmanli tepe, ortadaki en yuksek (altin), tepede parilti ----------
def tepe(cx, taban, yar, tepe_y, sol_renk, sag_renk, r, S, kayma=0.0):
    """Tek tepe: iki yuz (sol acik, sag koyu) — duz, modern yuzey golgelemesi."""
    sc = S / 512.0
    pts = [(cx - yar, taban), (cx, tepe_y), (cx + yar, taban)]
    pts = [(x * sc, y * sc) for x, y in pts]
    yol = yuvarlak_tepe(pts, [0, r * sc, 0])
    maske = Image.new("L", (S, S), 0)
    ImageDraw.Draw(maske).polygon(yol, fill=255)
    # yuz ayrimi: tepeden tabana (biraz saga kayik) cizgi
    ayrim = Image.new("L", (S, S), 0)
    ax = (cx + kayma) * sc
    ImageDraw.Draw(ayrim).polygon([(cx * sc, tepe_y * sc - 40 * sc), (ax, taban * sc + 4 * sc), (S, taban * sc + 4 * sc), (S, 0)], fill=255)
    sol = egim_dolgu_hizli(S, S, sol_renk[0], sol_renk[1], 90)
    sag = egim_dolgu_hizli(S, S, sag_renk[0], sag_renk[1], 90)
    yuz = Image.composite(sag, sol, ayrim)
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    im.paste(yuz, (0, 0), maske)
    return im, maske


def parilti(S, cx, cy, r, ince=0.16):
    """Dort koseli parilti yildizi (beyaz maske)."""
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    pts = []
    for k in range(8):
        a = math.radians(-90 + k * 45)
        rr = r if k % 2 == 0 else r * ince
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    d.polygon(pts, fill=255)
    return m


def isaret_ciz():
    S = 512 * SS
    sc = SS
    taban = 404
    katman = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    siluet = Image.new("L", (S, S), 0)
    # arka plan diski: ince altin halka + koyu ic (amblem); halkanin alti tepelerle kesilir
    halka = Image.new("L", (S, S), 0)
    hd = ImageDraw.Draw(halka)
    hcx, hcy, hr = 256 * sc, 256 * sc, 214 * sc
    hd.ellipse((hcx - hr, hcy - hr, hcx + hr, hcy + hr), outline=255, width=int(7 * sc))
    # halka (altin egim)
    hdolgu = egim_dolgu_hizli(S, S, (255, 222, 150, 255), (190, 132, 60, 255), 60)
    # halkanin tepelerin altinda kalan kismi silinecek: tabanin altini kes
    kes = Image.new("L", (S, S), 0)
    ImageDraw.Draw(kes).rectangle((0, 0, S, taban * sc), fill=255)
    halka_m = Image.composite(halka, Image.new("L", (S, S), 0), kes)
    hkat = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    hkat.paste(hdolgu, (0, 0), halka_m)
    katman = Image.alpha_composite(katman, hkat)
    siluet = Image.composite(Image.new("L", (S, S), 255), siluet, halka_m)
    # orta tepe (arkada, en yuksek, altin)
    orta, om = tepe(256, taban, 168, 100, ((255, 214, 120, 255), (246, 170, 62, 255)),
                    ((214, 140, 44, 255), (168, 98, 30, 255)), 16, S, kayma=10)
    katman = Image.alpha_composite(katman, orta)
    siluet = Image.composite(Image.new("L", (S, S), 255), siluet, om)
    # yan tepeler (onde, alcak, arduvaz mavisi) — ortadakinin onunde ince koyu kontur ile ayrisir
    for cx, yar, ty in ((112, 104, 246), (400, 104, 246)):
        yan, ym = tepe(cx, taban, yar, ty, ((120, 146, 196, 255), (70, 90, 132, 255)),
                       ((62, 80, 120, 255), (40, 52, 82, 255)), 12, S, kayma=8)
        # ayirici: tepenin biraz buyutulmus koyu golgesi (ortadakinin ustunde)
        kont = ym.filter(ImageFilter.MaxFilter(int(9 * sc) | 1))
        kkat = Image.new("RGBA", (S, S), (16, 18, 26, 255))
        kmask = Image.composite(kont, Image.new("L", (S, S), 0), om)
        kk = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        kk.paste(kkat, (0, 0), kmask)
        katman = Image.alpha_composite(katman, kk)
        katman = Image.alpha_composite(katman, yan)
        siluet = Image.composite(Image.new("L", (S, S), 255), siluet, ym)
    # taban cizgisi: ince altin cizgi (halkanin kesildigi yerde)
    tb = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tb).rounded_rectangle((40 * sc, (taban + 10) * sc, 472 * sc, (taban + 17) * sc), 4 * sc, fill=255)
    tkat = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    tkat.paste(egim_dolgu_hizli(S, S, (190, 132, 60, 255), (255, 222, 150, 255), 0), (0, 0), tb)
    katman = Image.alpha_composite(katman, tkat)
    siluet = Image.composite(Image.new("L", (S, S), 255), siluet, tb)
    # tepede parilti
    pm = parilti(S, 256 * sc, 100 * sc, 34 * sc)
    pkat = Image.new("RGBA", (S, S), (255, 250, 236, 255))
    pp = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pp.paste(pkat, (0, 0), pm)
    katman = Image.alpha_composite(katman, pp)
    siluet = Image.composite(Image.new("L", (S, S), 255), siluet, pm)
    renkli = katman.resize((512, 512), Image.LANCZOS)
    sil = Image.new("RGBA", (S, S), (255, 255, 255, 0))
    sil.putalpha(siluet)
    return renkli, sil.resize((512, 512), Image.LANCZOS)


# ---- geometrik harfler ------------------------------------------------------------------
def harf_genislik(h):
    return {"O": 104, "R": 70, "T": 78, "A": 92, "E": 62, "P": 66, "U": 80, "L": 58, "N": 82, "G": 104, "I": K}[h]


def harf_ciz(h):
    """Tek harf: 128 px hucre, beyaz + alfa. Donus (resim, genislik_px)."""
    S = HARF_C * SS
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    s = CAP_PX / 100.0 * SS
    gw = harf_genislik(h)
    x0 = (HARF_C - gw * CAP_PX / 100.0) / 2.0 * SS   # hucrede ortala
    y0 = (TABAN_PX - CAP_PX) * SS

    def P(x, y):
        return (x0 + x * s, y0 + y * s)

    def kutu(x1, y1, x2, y2, f=255):
        a, b = P(x1, y1), P(x2, y2)
        d.rectangle((a[0], a[1], b[0] - 1, b[1] - 1), fill=f)

    def halka(cx, cy, ro, ri, f=255):
        a, b = P(cx - ro, cy - ro), P(cx + ro, cy + ro)
        d.ellipse((a[0], a[1], b[0], b[1]), fill=f)
        a, b = P(cx - ri, cy - ri), P(cx + ri, cy + ri)
        d.ellipse((a[0], a[1], b[0], b[1]), fill=0)

    def poly(pts, f=255):
        d.polygon([P(x, y) for x, y in pts], fill=f)

    if h == "O":
        halka(52, 50, 52, 52 - K)
    elif h == "G":
        halka(52, 50, 52, 52 - K)
        # sag ust kama (agiz)
        cx, cy = P(52, 50)
        rr = 60 * s
        a0, a1 = math.radians(-58), math.radians(4)
        pts = [(cx, cy)] + [(cx + math.cos(a0 + (a1 - a0) * k / 20) * rr, cy + math.sin(a0 + (a1 - a0) * k / 20) * rr) for k in range(21)]
        d.polygon(pts, fill=0)
        # yatay cubuk + alt kancanin ucu
        kutu(54, 50 - YATAY / 2, 104, 50 + YATAY / 2)
        kutu(104 - K, 50 - YATAY / 2, 104, 72)
    elif h in ("R", "P"):
        alt_k = 58 if h == "R" else 60
        kutu(0, 0, K, 100)
        r = alt_k / 2.0
        kx = gw - r                      # kasenin merkez x'i
        kutu(K - 1, 0, kx + 1, YATAY)
        kutu(K - 1, alt_k - YATAY, kx + 1, alt_k)
        # sag yarim halka
        a, b = P(kx - r, 0), P(kx + r, alt_k)
        tmp = Image.new("L", (S, S), 0)
        td = ImageDraw.Draw(tmp)
        td.ellipse((a[0], a[1], b[0], b[1]), fill=255)
        ri = r - K
        a2, b2 = P(kx - ri, YATAY), P(kx + ri, alt_k - YATAY)
        td.ellipse((a2[0], a2[1], b2[0], b2[1]), fill=0)
        td.rectangle((0, 0, P(kx, 0)[0] - SS, S), fill=0)
        m.paste(255, (0, 0), tmp)
        if h == "R":
            # bacak: kasenin altindan sag alta, dik kalinlik ~K
            bx = kx - 6
            dx, dy = gw - bx, 100 - alt_k
            L = math.hypot(dx, dy)
            gen = K * 1.05 * L / dy      # yatay kesitte genislik
            poly([(bx - gen / 2, alt_k - 2), (bx + gen / 2, alt_k - 2), (gw, 100), (gw - gen, 100)])
    elif h == "T":
        kutu(0, 0, gw, YATAY)
        kutu(gw / 2 - K / 2, 0, gw / 2 + K / 2, 100)
    elif h == "E":
        kutu(0, 0, K, 100)
        kutu(0, 0, gw, YATAY)
        kutu(0, 50 - YATAY / 2, gw - 8, 50 + YATAY / 2)
        kutu(0, 100 - YATAY, gw, 100)
    elif h == "L":
        kutu(0, 0, K, 100)
        kutu(0, 100 - YATAY, gw, 100)
    elif h == "I":
        kutu(0, 0, K, 100)
    elif h == "U":
        r = gw / 2.0
        kutu(0, 0, K, 100 - r + 1)
        kutu(gw - K, 0, gw, 100 - r + 1)
        tmp = Image.new("L", (S, S), 0)
        td = ImageDraw.Draw(tmp)
        a, b = P(0, 100 - 2 * r), P(gw, 100)
        td.ellipse((a[0], a[1], b[0], b[1]), fill=255)
        a, b = P(K, 100 - 2 * r + K), P(gw - K, 100 - K)
        td.ellipse((a[0], a[1], b[0], b[1]), fill=0)
        td.rectangle((0, 0, S, P(0, 100 - r)[1] - SS), fill=0)
        m.paste(255, (0, 0), tmp)
    elif h == "N":
        kutu(0, 0, K, 100)
        kutu(gw - K, 0, gw, 100)
        dx, dy = gw - K, 100.0
        L = math.hypot(dx, dy)
        gen = K * 1.08 * L / dy
        poly([(0, 0), (gen, 0), (gw, 100), (gw - gen, 100)])
    elif h == "A":
        # Λ: capraz cubuksuz sivri tepe (isaretin tepe bicimini yineler); tepe 4 birim tasar
        tepe_y = -4.0
        cx = gw / 2.0
        dx, dy = cx, 100 - tepe_y
        L = math.hypot(dx, dy)
        gen = K * 1.06 * L / dy
        poly([(0, 100), (cx, tepe_y), (gw, 100), (gw - gen, 100), (cx, tepe_y + gen * dy / dx), (gen, 100)])
    # ust/alt kirp (A tepesi haric)
    ust_kes = P(0, -4.5 if h == "A" else -2.5)[1]
    d.rectangle((0, 0, S, ust_kes), fill=0)
    d.rectangle((0, P(0, 100)[1], S, S), fill=0)
    im = Image.new("RGBA", (S, S), (255, 255, 255, 0))
    im.putalpha(m)
    return im.resize((HARF_C, HARF_C), Image.LANCZOS), gw * CAP_PX / 100.0


def isilti_ciz(w, h):
    im = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    px = im.load()
    cx, cy = w / 2.0, h / 2.0
    for y in range(h):
        for x in range(w):
            r = math.hypot((x + 0.5 - cx) / cx, (y + 0.5 - cy) / cy)
            a = max(0.0, 1.0 - r)
            a = a * a * (3 - 2 * a)
            px[x, y] = (255, 255, 255, int(round(255 * a)))
    return im


def ayrac_ciz(w, h):
    im = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    px = im.load()
    for x in range(w):
        t = abs((x + 0.5) / w * 2 - 1)
        a = max(0.0, 1.0 - t) ** 1.5
        for y in range(h):
            ty = abs((y + 0.5) / h * 2 - 1)
            ay = 1.0 if ty < 0.25 else max(0.0, 1.0 - (ty - 0.25) / 0.2)
            px[x, y] = (255, 255, 255, int(round(255 * a * ay)))
    return im


def tulpar_ciz(yol):
    src = Image.open(yol).convert("RGBA")
    a = src.getchannel("A")
    if a.getextrema()[0] == 255:
        # beyaz zeminli surum: renkten alfa (beyaz -> saydam)
        gri = src.convert("L")
        a = gri.point(lambda v: 255 - v)
        a = a.point(lambda v: min(255, v * 2))
    bb = a.getbbox()
    a = a.crop(bb)
    # kare tuvale ortala, 6 px kenar payi
    k = max(a.size)
    tuval = Image.new("L", (k, k), 0)
    tuval.paste(a, ((k - a.size[0]) // 2, (k - a.size[1]) // 2))
    tuval = tuval.resize((500, 500), Image.LANCZOS)
    out = Image.new("L", (512, 512), 0)
    out.paste(tuval, (6, 6))
    im = Image.new("RGBA", (512, 512), (255, 255, 255, 0))
    im.putalpha(out)
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logo", default=LOGO)
    ap.add_argument("--onizleme", default="")
    ap.add_argument("--cikti", default="")
    a = ap.parse_args()
    atlas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    renkli, sil = isaret_ciz()
    atlas.paste(renkli, ISARET[:2])
    atlas.paste(sil, ISARET_S[:2])
    atlas.paste(tulpar_ciz(a.logo), TULPAR[:2])
    genler = []
    for i, h in enumerate(HARFLER):
        im, gw = harf_ciz(h)
        atlas.paste(im, (i * HARF_C, HARF_Y))
        genler.append(gw)
    atlas.paste(isilti_ciz(ISILTI[2], ISILTI[3]), ISILTI[:2])
    atlas.paste(ayrac_ciz(AYRAC[2], AYRAC[3]), AYRAC[:2])
    pm = parilti(PARILTI[2] * SS, PARILTI[2] * SS / 2, PARILTI[3] * SS / 2, PARILTI[2] * SS * 0.46, 0.12)
    pm = pm.filter(ImageFilter.GaussianBlur(SS * 1.5)).resize(PARILTI[2:], Image.LANCZOS)
    pi = Image.new("RGBA", PARILTI[2:], (255, 255, 255, 0))
    pi.putalpha(pm)
    atlas.paste(pi, PARILTI[:2])
    out = a.cikti or OUT
    os.makedirs(os.path.dirname(out), exist_ok=True)
    atlas.save(out, optimize=True)
    print(f"yazildi {out} ({W}x{H})")
    print("str AC_HARF = \"" + HARFLER + "\";")
    print("float[] AC_GEN = [" + ", ".join(f"{g:.1f}" for g in genler) + "];   // px, 128 px hucrede (buyuk harf " + f"{CAP_PX:.0f} px)")
    if a.onizleme:
        on = Image.new("RGBA", (W, H), (16, 18, 26, 255))
        on = Image.alpha_composite(on, atlas)
        on.save(a.onizleme)


if __name__ == "__main__":
    main()
