#!/usr/bin/env python3
"""Oyun metni glif kapisi: Tulpar oyununun yazdigi her harf cizilebilsin.

NEDEN (Geri bildirim #20, 2026-10-07): motorun HUD fontu (content/font.hpp)
yalniz ASCII + Latin-1 + Turkce harfleri atlasa koyuyordu; `—` (U+2014) gibi
bir karakter HATA VERMEDEN `?` diye ciziliyordu. Editorun ikonlari icin ayni
sinifin kapisi var (tools/icon_check.py); oyun metni icin yoktu.

Kapi uc sey olcer:
  1. Tulpar kaynaklarindaki (tulpar/**/*.tpr) her DIZE SABITI kod noktasi
     motorun atlas araliklarinda (kFontRangeFirst/Count) ya da ASCII esleme
     tablosunda (kFontAsciiFallback) mi. Degilse dosya:satir ile KIRMIZI.
  2. Paketlenen masaustu fontu (assets/fonts/DejaVuSans.ttf, eklenti paketine
     giden) atlas araliklarinin HER kod noktasini iceriyor mu — icermiyorsa
     stb_truetype bos glif paketler ve metin yine sessizce bozulur.
  3. Tablo font.hpp'den OKUNUR (kopya yok): aralik degisirse kapi kendiliginden
     onu olcer.

Android'de font sistemden gelir (Roboto) ve bu kapinin GORMEDIGI bir dosyadir;
olculdu (Huawei P20 Pro, 2026-10-08): "Savas — “Cag” • 3… ‘tirnak’ – kisa"
ekran goruntusunde dogru, kapanis raporunda '?' satiri yok (sayac 0).

Kullanim:  python tools/glyph_check.py [kok]
           python tools/glyph_check.py --oz-sinama
Donus:     0 = temiz, 1 = cizilemeyecek karakter / eksik glif
"""
import os
import re
import sys

sys.dont_write_bytecode = True  # kaynak agacina __pycache__ birakma
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_check import covered, font_ranges, string_literals  # noqa: E402

FONT_HPP = "content/font.hpp"
FONT = "assets/fonts/DejaVuSans.ttf"
SCAN_DIRS = ["tulpar"]


def parse_table(hdr):
    first = re.search(r"kFontRangeFirst\[kFontRanges\]\s*=\s*\{([^}]*)\}", hdr)
    count = re.search(r"kFontRangeCount\[kFontRanges\]\s*=\s*\{([^}]*)\}", hdr)
    if not first or not count:
        raise ValueError("font.hpp'de kFontRangeFirst/Count bulunamadi")
    fs = [int(x.strip(), 0) for x in first.group(1).split(",") if x.strip()]
    cs = [int(x.strip(), 0) for x in count.group(1).split(",") if x.strip()]
    if len(fs) != len(cs):
        raise ValueError("aralik tablolari esit uzunlukta degil")
    ranges = [(f, f + c - 1) for f, c in zip(fs, cs)]
    fb = re.search(r"kFontAsciiFallback\[kFontFallbackCount\]\[2\]\s*=\s*\{(.*?)\};", hdr, re.S)
    fallback = set()
    if fb:
        for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,", fb.group(1)):
            fallback.add(int(m.group(1), 0))
    return ranges, fallback


def scan_text(name, text, ranges, fallback):
    out = []
    for ln, line in enumerate(text.split("\n"), 1):
        for s in string_literals(line):
            for ch in s:
                cp = ord(ch)
                if cp < 0x80 or covered(ranges, cp) or cp in fallback:
                    continue
                out.append("%s:%d: U+%04X '%s' motorun fontunda yok -> '?' cizilir (content/font.hpp araliklari)" % (name, ln, cp, ch))
    return out


def font_gaps(ranges, font_cmap):
    return ["U+%04X" % cp for s, e in ranges for cp in range(s, e + 1) if not covered(font_cmap, cp)]


def oz_sinama(root):
    hdr = open(os.path.join(root, FONT_HPP), encoding="utf-8").read()
    ranges, fallback = parse_table(hdr)
    cases = [
        ("Turkce + tipografik (— “ ” • …) -> YESIL", 'yazi("Savaş — “Çağ” • 3…");', 0),
        ("ASCII eslemesi (→) -> YESIL", 'yazi("a → b");', 0),
        ("aralik disi (≠ U+2260) -> KIRMIZI (pozitif kontrol)", 'yazi("a ≠ b");', 1),
        ("yorumdaki karakter sayilmaz -> YESIL", '// a ≠ b\nyazi("x"); // ≠', 0),
        ("Turkce disi harf (Ж) -> KIRMIZI", 'print("Ж");', 1),
    ]
    fails = 0
    for name, src, want in cases:
        probs = scan_text("fikstur.tpr", src, ranges, fallback)
        ok = len(probs) == want
        print("glyph_check --oz-sinama: %s: %d sorun %s" % (name, len(probs), "OK" if ok else "HATA"))
        fails += 0 if ok else 1
    # Font denetimi: araliga fontta olmayan bir kod noktasi eklenince KIRMIZI.
    cmap = font_ranges(os.path.join(root, FONT))
    gaps = font_gaps(ranges + [(0x0F00, 0x0F00)], cmap)  # Tibet harfi: DejaVuSans'ta yok
    ok = gaps == ["U+0F00"]
    print("glyph_check --oz-sinama: fontta olmayan aralik kod noktasi -> KIRMIZI: %s %s" % (gaps, "OK" if ok else "HATA"))
    fails += 0 if ok else 1
    # Eski tablo (Geri bildirim #20 oncesi) — yalniz 4 aralik: `—` KIRMIZI olmali.
    eski = ranges[:4]
    probs = scan_text("eski.tpr", 'yazi("Savaş — son");', eski, set())
    ok = len(probs) == 1 and "U+2014" in probs[0]
    print("glyph_check --oz-sinama: eski tabloyla `—` -> KIRMIZI: %d sorun %s" % (len(probs), "OK" if ok else "HATA"))
    fails += 0 if ok else 1
    total = len(cases) + 2
    print("glyph_check --oz-sinama: %d/%d sinama tuttu" % (total - fails, total))
    return 1 if fails else 0


def main():
    # Windows konsolu (cp1252) Turkce/tipografik karakteri basamaz: UTF-8 + yedek.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) > 1 and sys.argv[1] == "--oz-sinama":
        root = sys.argv[2] if len(sys.argv) > 2 else "."
        return oz_sinama(root)
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    hdr = open(os.path.join(root, FONT_HPP), encoding="utf-8").read()
    ranges, fallback = parse_table(hdr)
    problems = []
    n_files = 0
    for d in SCAN_DIRS:
        for dp, _, files in os.walk(os.path.join(root, d)):
            for f in sorted(files):
                if not f.endswith(".tpr"):
                    continue
                p = os.path.join(dp, f)
                n_files += 1
                problems.extend(scan_text(os.path.relpath(p, root), open(p, encoding="utf-8").read(), ranges, fallback))
    gaps = font_gaps(ranges, font_ranges(os.path.join(root, FONT)))
    if gaps:
        problems.append("%s su atlas kod noktalarini icermiyor: %s" % (FONT, ", ".join(gaps[:20])))
    n_cp = sum(e - s + 1 for s, e in ranges)
    print("oyun metni glif kapisi: %d .tpr dosyasi, %d atlas kod noktasi (%d aralik) + %d ASCII eslemesi, font %s"
          % (n_files, n_cp, len(ranges), len(fallback), FONT))
    if not problems:
        print("oyun metni glif kapisi: 0 sorun")
        return 0
    print("oyun metni glif kapisi: %d SORUN" % len(problems))
    for x in problems:
        print("  " + x)
    return 1


if __name__ == "__main__":
    sys.exit(main())
