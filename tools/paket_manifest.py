#!/usr/bin/env python3
"""Paket manifesti: SURUM.txt + DOSYALAR.txt -- editor ici guncelleyicinin
dayandigi iki dosya (app/updater.hpp).

NEDEN VAR: guncelleyici kurulu bir paketi yenisiyle degistirirken iki soru
soruyor ve ikisinin de cevabi PAKETIN ICINDE durmali:
  * "Bu klasor hangi surum, hangi platform?"            -> SURUM.txt
  * "Kullanici bu dosyayi degistirdi mi, ezebilir miyim?" -> DOSYALAR.txt
DOSYALAR.txt yanlissa guncelleyici ya kullanicinin degistirdigi bir dosyayi
"degismemis" sanip EZER, ya da temiz bir paketi "bozuk" sanip durur. Ikisi de
sessiz; bu yuzden dosyayi yazan kod ile denetleyen kod AYNI dosyada ve
denetim paketin ICINE bakiyor (planina degil).

SOZLESME (app/updater.* bunu ayristirir -- degistirmek iki tarafi kirar):

  SURUM.txt     tek satir, ASCII, LF ile biter, baska satir YOK:
                    <surum> <platform>\\n
                <surum>    = vX.Y.Z[-onek] (release.yml / CMake kalibi) ya da
                             `kaynak` (TULPAR_SURUM bos derlenmis ikili)
                <platform> = linux-x86_64 | linux-aarch64 | macos-arm64 |
                             macos-x86_64 | windows-x86_64
                             (core/build_info.cpp build_platform() etiketleri)
                Ornek: `v0.2.0 linux-x86_64`, `kaynak windows-x86_64`.

  DOSYALAR.txt  `sha256sum` metin bicimi, her dosya icin bir satir:
                    <64 kucuk hex><iki bosluk><goreli yol>\\n
                * paketteki HER normal dosya (SURUM.txt DAHIL), DOSYALAR.txt
                  HARIC (kendi ozetini tasiyamaz);
                * yol ayiricisi `/` (Windows paketinde de), bas `./` yok;
                * satirlar yola gore BAYT sirasinda (LC_ALL=C sort), tekrar yok;
                * satir sonu `\\n` -- CR YOK (Windows'ta da; bkz. package.sh
                  `tr -d '\\r'` notu: MSYS2 python'u metin kipinde \\n'i \\r\\n
                  yapiyor, dosya bu yuzden IKILI kipte yazilir);
                * yol parcalari yalniz [A-Za-z0-9._+-], parca `.` ile
                  BASLAMAZ (gizli dosya, `.`/`..` yok -> yol gecisi yok,
                  `.guncelleme/` guncelleyiciye ayrilmis), hicbir yol
                  `.yeni` ile bitmez (guncelleyici kullanicinin degistirdigi
                  dosyanin yenisini `<ad>.yeni` diye yazar), buyuk/kucuk harf
                  duyarsiz iki yol ayni olamaz (macOS/Windows dosya sistemi).
                `sha256sum -c DOSYALAR.txt` paket kokunde oldugu gibi gecer.

NE YAZILMAZ, NE REDDEDILIR:
  * Sembolik baglanti: HATA. Arsivde (tar) baglanti olarak, zip'te hedefin
    kopyasi olarak gider, Windows'ta olusturmak yetki ister -- ayni paket
    platforma gore farkli acilirdi. package.sh kutuphaneleri zaten `cp -L`
    ile KOPYALIYOR, yani mesru bir paket baglanti icermez.
  * Bos dizin: HATA. DOSYALAR.txt yalniz dosya listeler; bos dizin manifestte
    gorunmez, guncelleyici onu ne kurar ne kaldirir. Paketleyici bir dizin
    acip doldurmadiysa bu bir paketleme hatasidir.
  * Normal olmayan dosya (FIFO, aygit): HATA.

SURUM KAYNAGI: paketlenen `engine_editor[.exe]` icindeki
`tulpar-engine-surum:<surum>` isaret dizgisi (core/build_info.cpp). Surum
elle ya da ortamdan VERILMEZ: SURUM.txt ile ikili ayrisamasin diye tek kaynak
ikilinin kendisi. Isaret yoksa ya da birden cok FARKLI deger varsa HATA.

PLATFORM KAYNAGI: yine ikili -- ELF e_machine / Mach-O cputype / PE Machine
(package.sh'in kurali: "konak degil PAKET ICERIGI karar verir"; Linux'ta
paketlenen bir Windows yapisi `uname`e bakan bir betikte linux-x86_64 diye
etiketlenirdi). Ayrica etiketin build_platform() dizgisi olarak ikilide
DURDUGU dogrulanir -- guncelleyici arsiv adini o dizgiden kuruyor.
TULPAR_PAKET_PLATFORM ortam degiskeni verilmisse (release.yml/ci.yml verir)
turetilen etiket ona ESIT olmali, yoksa HATA. (`PLATFORM` degil: Windows'ta
Visual Studio komut istemi ve bazi OEM kurulumlari `Platform=x64` gibi bir
degisken tanimliyor, kapi yabanci bir degerle kirmizi olurdu.)

Kullanim:
  python3 tools/paket_manifest.py yaz <paket-koku>      SURUM.txt + DOSYALAR.txt yaz
  python3 tools/paket_manifest.py denetle <paket-koku>  yalniz kapi
  python3 tools/paket_manifest.py --oz-sinama           kapinin kendi sinamasi
Donus: 0 temiz, 1 kapi kirmizi, 2 kullanim hatasi.
"""
import hashlib
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

# Cikti satir sonu platformdan bagimsiz (package.sh'teki CRLF notu).
try:
    sys.stdout.reconfigure(newline="\n")
    sys.stderr.reconfigure(newline="\n")
except AttributeError:  # py<3.7
    pass

MANIFEST = "DOSYALAR.txt"
SURUM_DOSYASI = "SURUM.txt"
EDITOR_ADLARI = ("engine_editor", "engine_editor.exe")
KAYNAK = "kaynak"

# core/build_info.cpp build_platform() etiketleri (android/bilinmiyor haric:
# paketlenmez). Release'in urettigi uc tanesi: linux-x86_64, macos-arm64,
# windows-x86_64.
PLATFORMLAR = ("linux-x86_64", "linux-aarch64", "macos-arm64", "macos-x86_64", "windows-x86_64")

# release.yml `hazirlik` ve CMakeLists.txt TULPAR_SURUM kalibinin AYNISI.
SURUM_KALIBI = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$")
ISARET = re.compile(rb"tulpar-engine-surum:([0-9A-Za-z.-]*)\x00")
SATIR = re.compile(r"^([0-9a-f]{64})  (.+)$")
PARCA = re.compile(r"^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$")


class Hata(Exception):
    pass


# --------------------------------------------------------------------------
# Ikilinin kimligi
# --------------------------------------------------------------------------
def ikili_platformu(veri, ad):
    """Dosya basligindan platform etiketi. Tanimadigimiz bicim HATA."""
    if veri[:4] == b"\x7fELF":
        if len(veri) < 20 or veri[4] != 2 or veri[5] != 1:
            raise Hata("%s: 64 bit little-endian ELF degil" % ad)
        makine = struct.unpack_from("<H", veri, 18)[0]
        etiket = {0x3E: "linux-x86_64", 0xB7: "linux-aarch64"}.get(makine)
        if not etiket:
            raise Hata("%s: taninmayan ELF e_machine 0x%x" % (ad, makine))
        return etiket
    if veri[:4] == b"\xcf\xfa\xed\xfe":  # MH_MAGIC_64, little-endian
        cpu = struct.unpack_from("<I", veri, 4)[0]
        etiket = {0x0100000C: "macos-arm64", 0x01000007: "macos-x86_64"}.get(cpu)
        if not etiket:
            raise Hata("%s: taninmayan Mach-O cputype 0x%x" % (ad, cpu))
        return etiket
    if veri[:4] in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
        raise Hata("%s: evrensel (fat) Mach-O -- tek platform etiketi secilemez" % ad)
    if veri[:2] == b"MZ" and len(veri) >= 0x40:
        pe = struct.unpack_from("<I", veri, 0x3C)[0]
        if veri[pe:pe + 4] != b"PE\x00\x00":
            raise Hata("%s: MZ var ama PE imzasi yok" % ad)
        makine = struct.unpack_from("<H", veri, pe + 4)[0]
        etiket = {0x8664: "windows-x86_64"}.get(makine)
        if not etiket:
            raise Hata("%s: taninmayan PE Machine 0x%x" % (ad, makine))
        return etiket
    raise Hata("%s: ne ELF ne Mach-O ne PE (ilk baytlar %s)" % (ad, veri[:4].hex()))


def ikili_surumu(veri, ad):
    """Isaret dizgisindeki surum; kaynak derlemesinde ''."""
    degerler = sorted({m.group(1).decode("ascii") for m in ISARET.finditer(veri)})
    if not degerler:
        raise Hata("%s: 'tulpar-engine-surum:' isareti YOK (core/build_info.cpp baglanmamis "
                   "ya da eski bir ikili) -- surum turetilemez" % ad)
    if len(degerler) > 1:
        raise Hata("%s: birden cok FARKLI surum isareti: %s -- hangisinin gercek oldugu "
                   "bilinemez" % (ad, ", ".join(repr(d) for d in degerler)))
    s = degerler[0]
    if s and not SURUM_KALIBI.match(s):
        raise Hata("%s: gomulu surum '%s' vX.Y.Z[-onek] kalibina uymuyor" % (ad, s))
    return s


def editor_ikilisi(kok):
    var = [a for a in EDITOR_ADLARI if os.path.isfile(os.path.join(kok, a))]
    if not var:
        raise Hata("pakette engine_editor[.exe] yok -- surum ve platform turetilemez")
    if len(var) > 1:
        raise Hata("pakette hem engine_editor hem engine_editor.exe var -- hangisi?")
    return var[0]


def beklenen_surum_satiri(kok):
    """(satir, surum, platform) -- ikiliden turetilir, ortamla karsilastirilir."""
    ad = editor_ikilisi(kok)
    with open(os.path.join(kok, ad), "rb") as f:
        veri = f.read()
    plat = ikili_platformu(veri, ad)
    if (ad.endswith(".exe")) != plat.startswith("windows-"):
        raise Hata("%s adi platformla celisiyor (%s)" % (ad, plat))
    if (plat.encode("ascii") + b"\x00") not in veri:
        raise Hata("%s: basligi %s diyor ama build_platform() dizgisi '%s' ikilide YOK -- "
                   "guncelleyici baska bir arsiv adi kurardi" % (ad, plat, plat))
    surum = ikili_surumu(veri, ad)
    istenen = os.environ.get("TULPAR_PAKET_PLATFORM", "")
    if istenen and istenen != plat:
        raise Hata("TULPAR_PAKET_PLATFORM='%s' ama paketteki %s bir %s ikilisi" % (istenen, ad, plat))
    return "%s %s\n" % (surum or KAYNAK, plat), surum, plat


# --------------------------------------------------------------------------
# Paket agaci
# --------------------------------------------------------------------------
def yol_sorunlari(yollar):
    """Saf denetim (dosya sistemi yok): ad kurallari + harf duyarsiz cakisma."""
    sorun = []
    gorulen = {}
    for y in yollar:
        parcalar = y.split("/")
        if y.startswith("/") or any(not PARCA.match(p) for p in parcalar):
            sorun.append("gecersiz yol '%s' (parcalar yalniz [A-Za-z0-9._+-], '.' ile baslamaz)" % y)
        elif y.endswith(".yeni"):
            sorun.append("'%s' .yeni ile bitiyor -- guncelleyicinin <ad>.yeni adlariyla cakisir" % y)
        k = y.lower()
        if k in gorulen and gorulen[k] != y:
            sorun.append("'%s' ile '%s' yalniz harf buyuklugunde ayrisiyor -- macOS/Windows'ta "
                         "ayni dosya" % (gorulen[k], y))
        gorulen.setdefault(k, y)
    return sorun


def paket_dosyalari(kok):
    """(goreli yollar -- bayt sirali, sorunlar). DOSYALAR.txt listeye girmez."""
    dosyalar, sorun, dolu_dizinler, tum_dizinler = [], [], set(), []
    for dizin, altlar, adlar in os.walk(kok):
        rel_dizin = os.path.relpath(dizin, kok).replace(os.sep, "/")
        if rel_dizin != ".":
            tum_dizinler.append(rel_dizin)
        for a in list(altlar):
            p = os.path.join(dizin, a)
            if os.path.islink(p):
                sorun.append("sembolik baglanti (dizin): %s" % os.path.relpath(p, kok).replace(os.sep, "/"))
                altlar.remove(a)
        for a in adlar:
            p = os.path.join(dizin, a)
            rel = os.path.relpath(p, kok).replace(os.sep, "/")
            kip = os.lstat(p).st_mode
            if stat.S_ISLNK(kip):
                sorun.append("sembolik baglanti: %s" % rel)
                continue
            if not stat.S_ISREG(kip):
                sorun.append("normal dosya degil: %s" % rel)
                continue
            if rel == MANIFEST:
                continue
            dosyalar.append(rel)
            d = os.path.dirname(rel)
            while d:
                dolu_dizinler.add(d)
                d = os.path.dirname(d)
    for d in tum_dizinler:
        if d not in dolu_dizinler:
            sorun.append("bos dizin: %s/ (manifestte gorunmez)" % d)
    dosyalar.sort(key=lambda s: s.encode("utf-8"))
    sorun.extend(yol_sorunlari(dosyalar))
    return dosyalar, sorun


def ozet(yol):
    h = hashlib.sha256()
    with open(yol, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


# --------------------------------------------------------------------------
# yaz / denetle
# --------------------------------------------------------------------------
def yaz(kok):
    satir, _, _ = beklenen_surum_satiri(kok)
    with open(os.path.join(kok, SURUM_DOSYASI), "wb") as f:
        f.write(satir.encode("ascii"))
    dosyalar, sorun = paket_dosyalari(kok)
    if sorun:
        raise Hata("paket manifeste yazilamaz:\n  " + "\n  ".join(sorun))
    govde = "".join("%s  %s\n" % (ozet(os.path.join(kok, y)), y) for y in dosyalar)
    with open(os.path.join(kok, MANIFEST), "wb") as f:
        f.write(govde.encode("ascii"))
    return satir.rstrip("\n"), len(dosyalar)


def denetle(kok):
    """Sorun listesi (bos = temiz) ve olculen dosya sayisi."""
    sorun = []
    mp = os.path.join(kok, MANIFEST)
    if os.path.islink(mp) or not os.path.isfile(mp):
        return ["%s YOK (ya da normal dosya degil) -- guncelleyici bu paketi Disabled sayar" % MANIFEST], 0
    ham = open(mp, "rb").read()
    if not ham:
        return ["%s BOS" % MANIFEST], 0
    if b"\r" in ham:
        sorun.append("%s CR iceriyor (satir sonu yalniz \\n olmali)" % MANIFEST)
    if not ham.endswith(b"\n"):
        sorun.append("%s son satiri \\n ile bitmiyor" % MANIFEST)
    try:
        metin = ham.decode("ascii")
    except UnicodeDecodeError:
        return sorun + ["%s ASCII degil" % MANIFEST], 0

    satirlar = metin.replace("\r", "").split("\n")
    if metin.endswith("\n"):
        satirlar = satirlar[:-1]
    listelenen = {}
    onceki = None
    for no, s in enumerate(satirlar, 1):
        m = SATIR.match(s)
        if not m:
            sorun.append("%s:%d bicim bozuk (<64 kucuk hex>  <yol> bekleniyor): %r" % (MANIFEST, no, s[:100]))
            continue
        h, y = m.group(1), m.group(2)
        if y in listelenen:
            sorun.append("%s:%d '%s' iki kez listelenmis" % (MANIFEST, no, y))
        elif onceki is not None and y.encode("utf-8") < onceki.encode("utf-8"):
            sorun.append("%s:%d bayt sirali degil ('%s', '%s'dan sonra)" % (MANIFEST, no, y, onceki))
        onceki = y
        listelenen[y] = h
    sorun.extend(yol_sorunlari(list(listelenen)))
    if MANIFEST in listelenen:
        sorun.append("%s kendini listeliyor (kendi ozetini tasiyamaz)" % MANIFEST)
    if SURUM_DOSYASI not in listelenen:
        sorun.append("%s %s'i listelemiyor" % (MANIFEST, SURUM_DOSYASI))

    dosyalar, agac_sorun = paket_dosyalari(kok)
    sorun.extend(agac_sorun)
    for y in dosyalar:
        if y not in listelenen:
            sorun.append("pakette var, %s'de YOK: %s" % (MANIFEST, y))
    mevcut = set(dosyalar)
    for y, h in listelenen.items():
        if y == MANIFEST:
            continue
        if y not in mevcut:
            sorun.append("%s'de var, pakette YOK: %s" % (MANIFEST, y))
            continue
        gercek = ozet(os.path.join(kok, y))
        if gercek != h:
            sorun.append("ozet tutmuyor: %s (liste %s..., dosya %s...)" % (y, h[:12], gercek[:12]))

    # SURUM.txt: bicim + ikiliyle esitlik (+ istenirse ortamla).
    sp = os.path.join(kok, SURUM_DOSYASI)
    if os.path.isfile(sp):
        ham_s = open(sp, "rb").read()
        try:
            beklenen, _, _ = beklenen_surum_satiri(kok)
        except Hata as e:
            sorun.append(str(e))
            beklenen = None
        m = re.match(rb"^([0-9A-Za-z.-]+) ([0-9a-z_-]+)\n\Z", ham_s)
        if not m:
            sorun.append("%s bicimi bozuk (tek satir '<surum> <platform>\\n' bekleniyor): %r"
                         % (SURUM_DOSYASI, ham_s[:100]))
        else:
            s_surum, s_plat = m.group(1).decode(), m.group(2).decode()
            if s_surum != KAYNAK and not SURUM_KALIBI.match(s_surum):
                sorun.append("%s surumu '%s' ne vX.Y.Z[-onek] ne '%s'" % (SURUM_DOSYASI, s_surum, KAYNAK))
            if s_plat not in PLATFORMLAR:
                sorun.append("%s platformu '%s' bilinen etiketlerden degil (%s)"
                             % (SURUM_DOSYASI, s_plat, ", ".join(PLATFORMLAR)))
            if beklenen is not None and ham_s != beklenen.encode("ascii"):
                sorun.append("%s '%s' ama ikiliden turetilen '%s' -- surum/platform ikiliyle ayrisiyor"
                             % (SURUM_DOSYASI, ham_s.decode("ascii", "replace").rstrip("\n"), beklenen.rstrip("\n")))
    else:
        sorun.append("%s YOK" % SURUM_DOSYASI)
    return sorun, len(listelenen)


# --------------------------------------------------------------------------
# --oz-sinama: kapinin KENDI pozitif/negatif kontrolleri. package.sh
# --denetle her kosumda once bunu kosar; kapi bozuk paketi YAKALAMALI,
# dogrusunu GECIRMELI. "Kapi yesil" ancak kirmiziyi gorebildigi gosterilmisse
# bir sey soyler (Tuzaklar 8bd).
# --------------------------------------------------------------------------
def _elf(surum, makine=0x3E, etiket=b"linux-x86_64"):
    return (b"\x7fELF\x02\x01\x01" + b"\x00" * 9 + struct.pack("<HH", 2, makine) + b"\x00" * 40
            + b"..." + etiket + b"\x00..." + b"tulpar-engine-surum:" + surum + b"\x00...")


def _macho(surum):
    return (b"\xcf\xfa\xed\xfe" + struct.pack("<I", 0x0100000C) + b"\x00" * 24
            + b"macos-arm64\x00" + b"tulpar-engine-surum:" + surum + b"\x00")


def _pe(surum):
    pe = 0x80
    govde = bytearray(b"MZ" + b"\x00" * (pe - 2))
    struct.pack_into("<I", govde, 0x3C, pe)
    govde += b"PE\x00\x00" + struct.pack("<H", 0x8664) + b"\x00" * 30
    return bytes(govde) + b"windows-x86_64\x00" + b"tulpar-engine-surum:" + surum + b"\x00"


def _paket(kok, editor_ad="engine_editor", editor=None):
    os.makedirs(os.path.join(kok, "assets", "fonts"))
    os.makedirs(os.path.join(kok, "tests", "assets"))
    os.makedirs(os.path.join(kok, "lisanslar", "glfw"))
    dosyalar = {
        editor_ad: editor if editor is not None else _elf(b"v1.2.3"),
        "OKUBENI.md": b"# paket\n",
        "assets/fonts/a.ttf": b"\x00\x01yazi tipi",
        "assets/fonts-LICENSE": b"lisans\n",
        "tests/assets/editor.sahne": b"sahne 1\n",
        "lisanslar/glfw/LICENSE.md": b"zlib\n",
        "BASLAT.bat": b"@echo off\r\n",
    }
    for y, v in dosyalar.items():
        with open(os.path.join(kok, *y.split("/")), "wb") as f:
            f.write(v)


def _satirlar(kok):
    return open(os.path.join(kok, MANIFEST), "rb").read().split(b"\n")[:-1]


def _yaz_satirlar(kok, satirlar, son=b"\n"):
    with open(os.path.join(kok, MANIFEST), "wb") as f:
        f.write(b"".join(s + son for s in satirlar))


def _ekle(kok, y, v=b"x"):
    p = os.path.join(kok, *y.split("/"))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(v)


def _surum_yaz_ve_yeniden_listele(kok, satir):
    """SURUM.txt'yi degistirir, DOSYALAR.txt'yi ona gore TAZELER -- boylece
    kapiyi kirmizi yapan tek sey SURUM.txt ile ikilinin ayrismasi olur."""
    with open(os.path.join(kok, SURUM_DOSYASI), "wb") as f:
        f.write(satir)
    satirlar = []
    for s in _satirlar(kok):
        h, y = s.split(b"  ", 1)
        if y == SURUM_DOSYASI.encode():
            h = ozet(os.path.join(kok, SURUM_DOSYASI)).encode()
        satirlar.append(h + b"  " + y)
    _yaz_satirlar(kok, satirlar)


def _std_arac():
    """Standart dogrulayici: sha256sum (Linux, MSYS2) ya da shasum (macOS)."""
    if shutil.which("sha256sum"):
        return ["sha256sum", "-c", MANIFEST]
    if shutil.which("shasum"):
        return ["shasum", "-a", "256", "-c", MANIFEST]
    return None


def oz_sinama():
    def bayt_degistir(k):
        p = os.path.join(k, "tests", "assets", "editor.sahne")
        v = bytearray(open(p, "rb").read())
        v[0] ^= 0x01
        open(p, "wb").write(bytes(v))

    def satir_sil(k):
        _yaz_satirlar(k, [s for s in _satirlar(k) if not s.endswith(b"  OKUBENI.md")])

    def sira_boz(k):
        s = _satirlar(k)
        s[0], s[1] = s[1], s[0]
        _yaz_satirlar(k, s)

    def buyuk_hex(k):
        s = _satirlar(k)
        s[0] = s[0][:64].upper() + s[0][64:]
        _yaz_satirlar(k, s)

    def kacak_yol(k):
        s = _satirlar(k)
        _yaz_satirlar(k, s + [b"0" * 64 + b"  ../kacak"])

    def kendini_listele(k):
        s = _satirlar(k)
        _yaz_satirlar(k, sorted(s + [b"0" * 64 + b"  DOSYALAR.txt"], key=lambda x: x[66:]))

    def ikili_surum_farkli(k):
        _surum_yaz_ve_yeniden_listele(k, b"v1.2.4 linux-x86_64\n")

    def ikili_platform_farkli(k):
        _surum_yaz_ve_yeniden_listele(k, b"v1.2.3 macos-arm64\n")

    def surum_crlf(k):
        _surum_yaz_ve_yeniden_listele(k, b"v1.2.3 linux-x86_64\r\n")

    def surum_iki_satir(k):
        _surum_yaz_ve_yeniden_listele(k, b"v1.2.3 linux-x86_64\nfazla\n")

    def bos_dizin(k):
        os.makedirs(os.path.join(k, "assets", "bos"))

    def sembolik(k):
        os.symlink("OKUBENI.md", os.path.join(k, "baglanti.md"))

    def manifest_sil(k):
        os.remove(os.path.join(k, MANIFEST))

    # (ad, bozma, beklenen alt dizgi | None = temiz kalmali, ortam)
    durumlar = [
        ("temiz paket -> YESIL (pozitif: kapi dogruyu geciriyor)", None, None, {}),
        ("temiz paket + TULPAR_PAKET_PLATFORM dogru -> YESIL", None, None,
         {"TULPAR_PAKET_PLATFORM": "linux-x86_64"}),
        ("bir dosyada bir bayt degisti -> KIRMIZI", bayt_degistir, "ozet tutmuyor: tests/assets/editor.sahne", {}),
        ("pakete dosya eklendi -> KIRMIZI", lambda k: _ekle(k, "assets/fonts/fazla.ttf"),
         "pakette var, DOSYALAR.txt'de YOK: assets/fonts/fazla.ttf", {}),
        ("DOSYALAR.txt'den satir silindi -> KIRMIZI", satir_sil, "pakette var, DOSYALAR.txt'de YOK: OKUBENI.md", {}),
        ("listelenen dosya silindi -> KIRMIZI", lambda k: os.remove(os.path.join(k, "OKUBENI.md")),
         "DOSYALAR.txt'de var, pakette YOK: OKUBENI.md", {}),
        ("satir sirasi bozuk -> KIRMIZI", sira_boz, "bayt sirali degil", {}),
        ("CRLF satir sonu -> KIRMIZI", lambda k: _yaz_satirlar(k, _satirlar(k), b"\r\n"), "CR iceriyor", {}),
        ("buyuk harf hex -> KIRMIZI", buyuk_hex, "bicim bozuk", {}),
        ("'../kacak' yolu -> KIRMIZI", kacak_yol, "gecersiz yol '../kacak'", {}),
        ("DOSYALAR.txt kendini listeliyor -> KIRMIZI", kendini_listele, "kendini listeliyor", {}),
        ("DOSYALAR.txt yok -> KIRMIZI", manifest_sil, "DOSYALAR.txt YOK", {}),
        ("SURUM.txt surumu ikiliden farkli -> KIRMIZI", ikili_surum_farkli, "ikiliyle ayrisiyor", {}),
        ("SURUM.txt platformu ikiliden farkli -> KIRMIZI", ikili_platform_farkli, "ikiliyle ayrisiyor", {}),
        ("SURUM.txt CRLF -> KIRMIZI", surum_crlf, "SURUM.txt bicimi bozuk", {}),
        ("SURUM.txt iki satir -> KIRMIZI", surum_iki_satir, "SURUM.txt bicimi bozuk", {}),
        ("TULPAR_PAKET_PLATFORM farkli -> KIRMIZI", None, "TULPAR_PAKET_PLATFORM='windows-x86_64'",
         {"TULPAR_PAKET_PLATFORM": "windows-x86_64"}),
        ("bos dizin -> KIRMIZI", bos_dizin, "bos dizin: assets/bos/", {}),
        ("sembolik baglanti -> KIRMIZI", sembolik, "sembolik baglanti: baglanti.md", {}),
    ]

    tut = 0
    toplam = 0
    atlanan = []
    gecici = tempfile.mkdtemp(prefix="paket_manifest_oz_")
    eski_ortam = os.environ.get("TULPAR_PAKET_PLATFORM")
    os.environ.pop("TULPAR_PAKET_PLATFORM", None)
    try:
        for i, (ad, boz, bekle, ortam) in enumerate(durumlar):
            kok = os.path.join(gecici, "d%02d" % i)
            _paket(kok)
            yaz(kok)
            if boz is not None:
                try:
                    boz(kok)
                except (OSError, NotImplementedError) as e:
                    # Windows'ta sembolik baglanti yetki ister. Kosmayan kapi
                    # yesil degil: GORUNUR atlanir, sayaca girer.
                    atlanan.append("%s (%s)" % (ad, e))
                    print("paket_manifest --oz-sinama: ATLANDI: %s -- %s" % (ad, e))
                    continue
            os.environ.update(ortam)
            try:
                sorun, _ = denetle(kok)
            finally:
                for anahtar in ortam:
                    os.environ.pop(anahtar, None)
            toplam += 1
            if bekle is None:
                ok = not sorun
            else:
                ok = any(bekle in s for s in sorun)
            tut += ok
            print("paket_manifest --oz-sinama: %s: %d sorun %s" % (ad, len(sorun), "OK" if ok else "HATA"))
            if not ok:
                for s in sorun:
                    print("    " + s)
                if bekle:
                    print("    (beklenen: '%s')" % bekle)

        # Yazicinin KESIN ciktisi: sozlesme metni yalniz yorumda kalmasin.
        kok = os.path.join(gecici, "bicim")
        _paket(kok)
        yaz(kok)
        surum = open(os.path.join(kok, SURUM_DOSYASI), "rb").read()
        manifest = open(os.path.join(kok, MANIFEST), "rb").read()
        yollar = [s.split(b"  ", 1)[1] for s in manifest.split(b"\n")[:-1]]
        beklenen_yollar = sorted([b"BASLAT.bat", b"OKUBENI.md", b"SURUM.txt", b"assets/fonts-LICENSE",
                                  b"assets/fonts/a.ttf", b"engine_editor", b"lisanslar/glfw/LICENSE.md",
                                  b"tests/assets/editor.sahne"])
        ilk = (hashlib.sha256(b"@echo off\r\n").hexdigest() + "  BASLAT.bat\n").encode()
        ok = (surum == b"v1.2.3 linux-x86_64\n" and yollar == beklenen_yollar
              and manifest.startswith(ilk) and b"\r" not in manifest)
        # `assets/fonts-LICENSE` < `assets/fonts/a.ttf`: '-' (0x2D) < '/' (0x2F).
        # Yerel duyarli bir siralama ('/'yi ayirici sayan) bunu tersine cevirirdi.
        toplam += 1
        tut += ok
        print("paket_manifest --oz-sinama: kesin bicim (SURUM.txt baytlari, bayt sirasi, satir sonu): %s"
              % ("OK" if ok else "HATA"))
        if not ok:
            print("    SURUM.txt=%r" % surum)
            print("    yollar=%r" % yollar)

        # Diger iki platformun basligi + kaynak derlemesi.
        for ad, editor_ad, ikili, bekle in (
                ("PE x86_64 -> windows-x86_64", "engine_editor.exe", _pe(b"v2.0.0-rc.1"), b"v2.0.0-rc.1 windows-x86_64\n"),
                ("Mach-O arm64 -> macos-arm64", "engine_editor", _macho(b"v0.3.0"), b"v0.3.0 macos-arm64\n"),
                ("bos isaret -> 'kaynak' (kaynak derlemesi)", "engine_editor", _elf(b""), b"kaynak linux-x86_64\n")):
            kok = os.path.join(gecici, "p%d" % toplam)
            _paket(kok, editor_ad, ikili)
            yaz(kok)
            sorun, _ = denetle(kok)
            surum = open(os.path.join(kok, SURUM_DOSYASI), "rb").read()
            ok = surum == bekle and not sorun
            toplam += 1
            tut += ok
            print("paket_manifest --oz-sinama: %s: %s" % (ad, "OK" if ok else "HATA %r %r" % (surum, sorun)))

        # Yazici bozuk ikiliyi REDDETMELI (isaretsiz, iki farkli isaret, bozuk kalip).
        for ad, ikili, bekle in (
                ("isaretsiz ikili -> yaz REDDEDER", b"\x7fELF\x02\x01\x01" + b"\x00" * 9 + struct.pack("<HH", 2, 0x3E)
                 + b"\x00" * 40 + b"linux-x86_64\x00", "isareti YOK"),
                ("iki farkli isaret -> yaz REDDEDER", _elf(b"v1.0.0") + b"tulpar-engine-surum:v1.0.1\x00",
                 "birden cok FARKLI"),
                ("platform dizgisi ikilide yok -> yaz REDDEDER", _elf(b"v1.0.0", etiket=b"baska"), "ikilide YOK"),
                ("ELF aarch64 basligi, x86_64 dizgisi -> yaz REDDEDER", _elf(b"v1.0.0", makine=0xB7),
                 "linux-aarch64' ikilide YOK")):
            kok = os.path.join(gecici, "r%d" % toplam)
            _paket(kok, "engine_editor", ikili)
            try:
                yaz(kok)
                ok, neden = False, "yazdi"
            except Hata as e:
                ok, neden = bekle in str(e), str(e)
            toplam += 1
            tut += ok
            print("paket_manifest --oz-sinama: %s: %s" % (ad, "OK" if ok else "HATA (%s)" % neden))

        # Saf ad kurallari (buyuk/kucuk harf cakismasi Windows/macOS'ta dosya
        # sisteminde KURULAMAZ; kural yine de olculsun).
        for ad, yollar, bekle in (
                ("harf buyuklugu cakismasi -> KIRMIZI", ["a/B.txt", "a/b.txt"], "harf buyuklugunde"),
                ("gizli dosya -> KIRMIZI", [".guncelleme/x"], "gecersiz yol"),
                ("ters bolu -> KIRMIZI", ["a\\b"], "gecersiz yol"),
                ("bosluk -> KIRMIZI", ["a b.txt"], "gecersiz yol"),
                (".yeni soneki -> KIRMIZI", ["tests/assets/editor.sahne.yeni"], ".yeni ile bitiyor"),
                ("olagan yollar -> YESIL", ["engine_editor.exe", "libstdc++-6.dll", "tests/assets/x_1.gltf"], None)):
            s = yol_sorunlari(yollar)
            ok = (not s) if bekle is None else any(bekle in x for x in s)
            toplam += 1
            tut += ok
            print("paket_manifest --oz-sinama: ad kurali, %s: %s" % (ad, "OK" if ok else "HATA %r" % s))

        # Standart aracin gozunden: `sha256sum -c` temiz paketi GECIRIYOR, bir
        # bayti degismisi DUSURUYOR mu. Kullanicinin elle dogrulama yolu ve
        # guncelleyicinin okudugu bicim bu; "bizim ayristiricimiz kabul ediyor"
        # yetmez.
        arac = _std_arac()
        if arac is None:
            atlanan.append("standart arac yok (sha256sum/shasum)")
            print("paket_manifest --oz-sinama: ATLANDI: sha256sum/shasum yok -- standart arac uyumu olculmedi")
        else:
            kok = os.path.join(gecici, "std")
            _paket(kok)
            yaz(kok)
            r1 = subprocess.run(arac, cwd=kok, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            bayt_degistir(kok)
            r2 = subprocess.run(arac, cwd=kok, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            ok = r1.returncode == 0 and r2.returncode != 0
            toplam += 1
            tut += ok
            print("paket_manifest --oz-sinama: '%s': temiz paket %d, bozuk paket %d: %s"
                  % (" ".join(arac[:-1]), r1.returncode, r2.returncode, "OK" if ok else "HATA"))
            if not ok:
                print("    " + r1.stdout.decode("utf-8", "replace").replace("\n", "\n    "))
    finally:
        if eski_ortam is not None:
            os.environ["TULPAR_PAKET_PLATFORM"] = eski_ortam
        shutil.rmtree(gecici, ignore_errors=True)

    print("paket_manifest --oz-sinama: %d/%d sinama tuttu, %d atlandi" % (tut, toplam, len(atlanan)))
    return 0 if tut == toplam and toplam > 0 else 1


# --------------------------------------------------------------------------
def main(argv):
    if len(argv) == 2 and argv[1] == "--oz-sinama":
        return oz_sinama()
    if len(argv) != 3 or argv[1] not in ("yaz", "denetle"):
        sys.stderr.write(__doc__.split("Kullanim:")[1])
        return 2
    kok = argv[2]
    if not os.path.isdir(kok):
        sys.stderr.write("HATA: paket dizini yok: %s\n" % kok)
        return 1
    if argv[1] == "yaz":
        try:
            satir, n = yaz(kok)
        except Hata as e:
            sys.stderr.write("HATA: %s\n" % e)
            return 1
        print("manifest yazildi: SURUM.txt = '%s', DOSYALAR.txt = %d dosya" % (satir, n))
        return 0
    sorun, n = denetle(kok)
    if sorun:
        for s in sorun:
            sys.stderr.write("MANIFEST: %s\n" % s)
        sys.stderr.write("manifest kapisi KIRMIZI: %d sorun\n" % len(sorun))
        return 1
    surum = open(os.path.join(kok, SURUM_DOSYASI), "rb").read().decode("ascii").rstrip("\n")
    print("manifest kapisi TAMAM: %s satiri, her ozet tuttu, fazla/eksik dosya yok; SURUM.txt = '%s'"
          % (n, surum))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
