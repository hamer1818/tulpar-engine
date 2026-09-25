#!/usr/bin/env bash
# ===========================================================================
# Tulpar Engine — dagitim paketi. CI de insan da AYNI kodu kosar: paketleme
# mantigi YAML'de degil burada durur (uc is akisi adiminda uc kopya, uc kere
# kayar).
#
# Kullanim:
#   tools/package.sh <yapi-dizini> <cikti-dizini>   # paketle + tamlik kapisi
#   tools/package.sh --denetle <cikti-dizini>       # YALNIZ kapi (paketlemez)
#
# Uretilen duzen (SOZLESME — surum is akisi ve OKUBENI buna dayaniyor):
#   <cikti>/engine_demo[.exe] engine_editor[.exe] engine_sahnec[.exe]
#           engine_texpack[.exe] engine_clodbake[.exe]
#           *.dll                  (yalniz Windows; ithalat tablosundan bulunur)
#           glfw3.dll / libglfw.so.3 / libglfw.3.dylib
#                                  (dlopen EDILEN kutuphane — kaynaktan turetilir)
#           lisanslar/<kutuphane>/ (gomulu kutuphanelerin lisanslari)
#           BASLAT-*.bat           (yalniz Windows; hata halinde konsolu acik tutar)
#           assets/fonts/<ttf + lisanslar>
#           tests/assets/<demo ve editorun yukledigi varliklar>
#           OKUBENI.md
#           SURUM.txt              "<surum> <platform>\n" (kaynak derlemesi:
#                                  "kaynak <platform>"); ikilideki isaretten
#           DOSYALAR.txt           her dosyanin SHA-256'si, `sha256sum` bicimi
#
# SURUM.txt + DOSYALAR.txt editor ici guncelleyicinin (app/updater.hpp)
# dayandigi MANIFESTTIR: "hangi surum" ve "kullanici bu dosyayi degistirdi
# mi" sorularinin cevabi. Kesin bicim, symlink/bos dizin kurali ve kapinin
# oz-sinamasi tools/paket_manifest.py basliginda. Paketin EN SON adimi olarak
# yazilir (strip'ten SONRA: ozet, dagitilan baytin ozeti olmali).
#
# NEDEN VARLIKLAR DA PAKETTE (olculdu): ikililer varlik yollarini
# "<exe dizini>/<goreli yol>" -> "<calisma dizini>/<goreli yol>" ->
# ENGINE_SOURCE_DIR sirasiyla ariyor. ENGINE_SOURCE_DIR DERLEME ZAMANI bir
# yol: paket yalniz ikili iceriyorsa, derleyen makinenin kaynak agaci olmayan
# her yerde engine_editor sahneyi bulamaz ve 1 ile cikar. Varliklar buraya,
# exe'nin YANINA, birinci adimin bulacagi yerlesimle konur.
#
# IKI AYRI BAGIMLILIK SINIFI VAR, ikisi de ELLE YAZILMAZ:
#   * LINK edilen kutuphaneler -> ikilinin ithalat tablosundan (ldd/objdump),
#   * dlopen EDILEN kutuphaneler -> KAYNAKTAKI `dl_open(...)` cagrilarindan.
# Ikincisi 2026-09-20'ye kadar HIC yoktu ve v0.1.0 Windows zip'i GLFW'siz
# yayinlandi: `ldd` dlopen'lanan bir kutuphaneyi goremez, kapi da ona bakiyordu.
#
# KAPI BOS BIR GREP DEGIL: gereken varlik listesi ELLE YAZILMIYOR — motorun
# kaynak dizgilerinden turetiliyor (asagidaki python; tests/ ve third_party/
# disarida, cunku test fiksturleri dagitima girmez),
# ayrica pakete giren her `.sahne` dosyasinin KENDI `kaynak "..."` satirlari
# okunuyor. Kod ya da sahne yeni bir varlik istemeye baslarsa liste kendiliginden
# buyur; paket eksik kalirsa is KIRMIZI olur. Liste bos donerse ya da bir yol
# cozulemezse bu da ariza sayilir (turetme bozulmus demektir).
# ===========================================================================
set -euo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Paketin ikili sozlesmesi. Uretilmeyen bir hedef UYARI degil HATA: eksik
# ikiliyle sessizce dagitilan bir paket tam olarak bu isin onlemek istedigi sey.
IKILILER=(engine_demo engine_editor engine_sahnec engine_texpack engine_clodbake)

OKUBENI_KAYNAK="$kok/tools/paket_OKUBENI.md"

hata() { echo "HATA: $*" >&2; exit 1; }
# CI'da satir "::error::" ile de gorunsun (yerelde zararsiz bir on ek).
ci_hata() { echo "::error::$*" >&2; echo "HATA: $*" >&2; exit 1; }

kullanim() {
  cat >&2 <<'KULLANIM'
kullanim:
  tools/package.sh <yapi-dizini> <cikti-dizini>   paketle + tamlik kapisi
  tools/package.sh --denetle <cikti-dizini>       yalniz tamlik kapisi
KULLANIM
  exit 2
}

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE=""     ;;
esac

# --- Gereken varlik yollari: KAYNAKTAN turetilir --------------------------
# Cikti: pakette bulunmasi gereken goreli yollar, satir basina bir tane.
varlik_yollari() {
  command -v python3 >/dev/null 2>&1 || hata "python3 yok — varlik listesi turetilemez (kapi sessizce bos kalmasin diye duruyoruz)."
  # `| tr -d '\r'`: MSYS2/MinGW python'u stdout'u METIN kipinde aciyor ve her
  # \n'i \r\n yapiyor; bash tarafinda \r yolun PARCASI oluyor ve paket kapisi
  # var olan dosyayi "YOK" sayiyor. Olculdu 2026-09-20, Windows CI:
  #     kod 'assets/fonts/DejaVuSans.ttf<CR>' varligini istiyor ama ... YOK
  # (mesajdaki satir kaymasi CR'nin ta kendisiydi). Asagida python tarafinda da
  # newline sabitleniyor; bu boru hatti IKINCI savunma — baska bir python da
  # ayni tuzaga dusurmesin.
  python3 - "$kok" <<'PY' | tr -d '\r'
import os, re, sys

# Cikti satir sonu PLATFORMDAN BAGIMSIZ olsun (bkz. yukaridaki not).
try:
    sys.stdout.reconfigure(newline="\n")
except AttributeError:  # py<3.7
    pass

kok = sys.argv[1]
# NEREYI TARIYORUZ: paketlenen ikililerin gorebildigi butun katmanlar.
# app/ + bridge/ bugunku tek adres, ama liste genis tutuluyor ki varlik
# cozumlemesi baska bir katmana tasinirsa turetme sessizce BOSALMASIN.
# tests/ ve third_party/ BILEREK disarida: test fiksturleri (pbr_plane.gltf,
# checker_64.ktx2 ...) dagitima girmez.
TARAMA = ("app", "bridge", "tools", "content", "renderer", "rhi", "sim",
          "audio", "gameplay", "core", "platform")
UZANTI = (".c", ".cc", ".cpp", ".h", ".hpp")

# C kaynagini kabaca tokenlere ayir: yorumlar ATILIR (icindeki tek tirnak
# dizgi taramasini kaydirmasin), dizgi birlestirmesi (bitisik literal ve
# makro) COZULUR — "%s/assets/fonts/" FONT_ICON_FILE_NAME_MD boyle okunuyor.
TOKEN = re.compile(r'''
    (?P<comment> //[^\n]* | /\*.*?\*/ )
  | (?P<string>  " (?:[^"\\\n]|\\.)* " )
  | (?P<char>    ' (?:[^'\\\n]|\\.)* ' )
  | (?P<ident>   [A-Za-z_][A-Za-z0-9_]* )
  | (?P<other>   . )
''', re.S | re.X)

DEFINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+"((?:[^"\\]|\\.)*)"\s*$', re.M)
# Goreli varlik yolu: "assets/..." ya da "tests/assets/...". Bicim dizgisinin
# basindaki "%s/" ve ENGINE_SOURCE_DIR bilerek disarida — paket ICI yol budur.
YOL = re.compile(r'(?:tests/)?assets/[A-Za-z0-9_./-]*')
KAYNAK_SATIRI = re.compile(r'^\s*kaynak\s+"([^"]+)"', re.M)


def kaynak_dosyalar(dizin):
    for dirpath, dirnames, filenames in os.walk(dizin):
        dirnames[:] = [d for d in dirnames if d not in (".git", "__pycache__")]
        for fn in sorted(filenames):
            if fn.endswith(UZANTI):
                yield os.path.join(dirpath, fn)


def makro_tablosu():
    t = {}
    for alt in ("app", "bridge", "tools", "content", "core", "platform",
                "renderer", "rhi", "third_party/iconfont"):
        d = os.path.join(kok, alt)
        if os.path.isdir(d):
            for p in kaynak_dosyalar(d):
                s = open(p, "r", encoding="utf-8", errors="replace").read()
                for m in DEFINE.finditer(s):
                    t.setdefault(m.group(1), m.group(2))
    return t


def birlesik_dizgiler(metin, makro):
    out, buf = [], None
    for m in TOKEN.finditer(metin):
        k = m.lastgroup
        if k == "comment":
            continue
        if k == "string":
            buf = (buf or "") + m.group(0)[1:-1]
        elif k == "other" and m.group(0).isspace():
            continue
        elif k == "ident" and buf is not None:
            buf += makro.get(m.group(0), "<COZULMEDI:%s>" % m.group(0))
        elif buf is not None:
            out.append(buf)
            buf = None
    if buf is not None:
        out.append(buf)
    return out


makro = makro_tablosu()
bulunan, cozulmedi = {}, []
for alt in TARAMA:
    d = os.path.join(kok, alt)
    if not os.path.isdir(d):
        continue
    for p in kaynak_dosyalar(d):
        metin = open(p, "r", encoding="utf-8", errors="replace").read()
        if "assets/" not in metin:
            continue
        rel = os.path.relpath(p, kok)
        for s in birlesik_dizgiler(metin, makro):
            for m in YOL.finditer(s):
                y = m.group(0)
                if "%" in y or "<COZULMEDI:" in y or y.endswith("/") or "." not in os.path.basename(y):
                    cozulmedi.append((rel, s))
                else:
                    bulunan.setdefault(y, rel)

# Sahnenin KENDI bagimliliklari: `kaynak "x.gltf"` satirlari sahne dosyasinin
# dizinine GORE cozuluyor (content::scene_dir_of). Sahne yeni bir model
# eklerse paket onu kendiliginden tasir.
for y in list(bulunan):
    if y.endswith(".sahne"):
        tam = os.path.join(kok, y)
        if not os.path.isfile(tam):
            cozulmedi.append((y, "sahne dosyasi kaynak agacinda yok"))
            continue
        d = os.path.dirname(y)
        for m in KAYNAK_SATIRI.finditer(open(tam, "r", encoding="utf-8", errors="replace").read()):
            bulunan.setdefault(os.path.normpath(os.path.join(d, m.group(1))).replace(os.sep, "/"), y)

if cozulmedi:
    for rel, s in cozulmedi:
        sys.stderr.write("COZULEMEYEN VARLIK YOLU: %s :: %s\n" % (rel, s))
    sys.stderr.write(
        "Bu yol calisma zamaninda olusuyor; paketleyici hangi dosyanin gerektigini BILEMEZ\n"
        "ve eksik varlikla sessizce dagitmaktansa duruyor. Cozum: ya yolu sabit dizgi yap,\n"
        "ya da varligi bir `.sahne` dosyasina koy (sahnenin `kaynak \"...\"` satirlari\n"
        "kendiliginden izleniyor).\n")
    sys.exit(1)
if not bulunan:
    sys.stderr.write("Hicbir varlik yolu turetilemedi — turetme bozulmus (kapi bos bir grep olurdu).\n")
    sys.exit(1)

for y in sorted(bulunan):
    print(y)
PY
}


# --- Calisma zamaninda YUKLENEN kutuphaneler: KAYNAKTAN turetilir ----------
# NEDEN VAR (olculdu 2026-09-20, YAYINLANMIS v0.1.0 Windows zip'i): kullanici
# arsivi indirdi, engine_editor.exe'ye cift tikladi, program acilmadi. Wine
# altinda ayni ikili sunu basiyor:
#     pencere: GLFW yok (glfw3.dll): masaustu pencere acilamaz
# Sebep asagidaki 3. adim: Windows DLL'leri `ldd` ciktisindan bulunuyordu ve
# `ldd` TANIMI GEREGI yalnizca ITHALAT TABLOSUNU gorur. GLFW ise link degil
# `dl_open("glfw3.dll")` ile yuklenir — ithalat tablosunda YOKTUR, dolayisiyla
# pakete hic girmedi. Uc MinGW DLL'i vardi, GLFW yoktu.
#
# Bu yuzden dlopen'lanan kutuphanelerin listesi de VARLIKLAR GIBI kaynaktan
# turetilir: asagidaki python `platform/` ve `rhi/` (ve butun ust katmanlar)
# icindeki her `dl_open(...)` cagrisini bulur, argumanindaki ad dizisini
# cozer ve dizinin ustundeki `// PAKET:` satirindan POLITIKAYI okur:
#     gomulu  -> pakete KONUR (ve lisansi da)
#     sistem  -> paketlenmez, kullanicinin sisteminden gelir (Vulkan loader)
#     turetilmis -> adlar yukaridaki diziden, yeni ad getirmez
# ISARETSIZ bir dl_open cagrisi HATADIR: ikinci bir liste tutmak yerine yeni
# bir calisma zamani bagimliligi eklendiginde KARAR VERMEYE zorluyoruz.
#
# Cikti satirlari:  <politika>\t<platform>\t<aile>\t<ad>\t<lisans>
dlopen_kutuphaneleri() {
  command -v python3 >/dev/null 2>&1 || hata "python3 yok — dlopen kutuphane listesi turetilemez (kapi sessizce bos kalmasin diye duruyoruz)."
  python3 - "$kok" <<'PY' | tr -d '\r'
import os, re, sys

try:
    sys.stdout.reconfigure(newline="\n")
except AttributeError:
    pass

kok = sys.argv[1]
TARAMA = ("app", "bridge", "tools", "content", "renderer", "rhi", "sim",
          "audio", "gameplay", "core", "platform")
UZANTI = (".c", ".cc", ".cpp", ".h", ".hpp")
PLATFORMLAR = ("windows", "macos", "linux")

# Kaynaktaki tek politika adresi. Ornek:
#   // PAKET: gomulu lisans=third_party/glfw/LICENSE.md
#   // PAKET: sistem
#   // PAKET: turetilmis
ISARET = re.compile(r'PAKET:\s*(gomulu|sistem|turetilmis)((?:\s+[a-z_]+=[^\s*]+)*)')
DIZI = re.compile(r'const\s+char\s*\*\s*(?:const\s*\*\s*)?([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{')
CAGRI = re.compile(r'\bdl_open\s*\(')
ARALIK_FOR = re.compile(r'for\s*\([^;()]*?\b(\w+)\s*:\s*([A-Za-z_]\w*)\s*\)')
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

hatalar = []


def kaynak_dosyalar(dizin):
    for dirpath, dirnames, filenames in os.walk(dizin):
        dirnames[:] = [d for d in dirnames if d not in (".git", "__pycache__")]
        for fn in sorted(filenames):
            if fn.endswith(UZANTI):
                yield os.path.join(dirpath, fn)


def kapatan_paren(metin, i):
    """metin[i] == '(' iken eslesen ')' konumunu dondurur (-1 = yok)."""
    derinlik = 0
    while i < len(metin):
        c = metin[i]
        if c == '(':
            derinlik += 1
        elif c == ')':
            derinlik -= 1
            if derinlik == 0:
                return i
        i += 1
    return -1


def kapatan_kase(metin, i):
    derinlik = 0
    while i < len(metin):
        c = metin[i]
        if c == '{':
            derinlik += 1
        elif c == '}':
            derinlik -= 1
            if derinlik == 0:
                return i
        i += 1
    return -1


def kosul_platformu(satir, rel):
    """#if/#elif kosulunu platform kumesine cevirir.

    TANIMADIGIMIZ kosul HATADIR: "bilmiyorsam hepsi" demek, kapinin yanlis
    platformu olcmesi demektir — sessizce gecen bir kapi istemiyoruz.
    """
    p = set()
    if "_WIN32" in satir or "_MSC_VER" in satir:
        p.add("windows")
    if "__APPLE__" in satir:
        p.add("macos")
    if "__linux__" in satir:
        p.add("linux")
    if not p:
        hatalar.append("%s :: dl_open ad dizisinde TANINMAYAN onislemci kosulu: %s"
                       % (rel, satir.strip()))
    return p


# Dosya duzeyindeki onislemci baglami: bir dizi `#if defined(__APPLE__)`
# blogunun ICINDE duruyorsa adlari yalniz macOS'ta gecerlidir. Bunu gormezsek
# kapi "libMoltenVK.dylib Windows'ta da gerekiyor" gibi bir sey soyler.
MAKRO_PLATFORM = {"_WIN32": "windows", "_MSC_VER": "windows",
                  "__APPLE__": "macos", "__linux__": "linux"}


def platform_kisiti(satir):
    """Kosulun getirdigi platform kisiti; None = kisit yok (platformdan bagimsiz)."""
    pos, neg = set(), set()
    m = re.match(r'\s*#\s*ifn?def\s+([A-Za-z_]\w*)', satir)
    if m:
        pl = MAKRO_PLATFORM.get(m.group(1))
        if not pl:
            return None
        return {pl} if "ifndef" not in satir else set(PLATFORMLAR) - {pl}
    for mm in re.finditer(r'(!?)\s*defined\s*\(?\s*([A-Za-z_]\w*)', satir):
        pl = MAKRO_PLATFORM.get(mm.group(2))
        if not pl:
            continue
        (neg if mm.group(1) == "!" else pos).add(pl)
    if pos:
        return pos
    if neg:
        return set(PLATFORMLAR) - neg
    return None


def dosya_baglami(metin, konum):
    """konum'daki satirin hangi platformlarda derlendigi."""
    hepsi = set(PLATFORMLAR)
    yigin = []
    for ln in metin[:konum].splitlines():
        s = ln.strip()
        if not s.startswith("#"):
            continue
        taban = yigin[-1]["aktif"] if yigin else hepsi
        if re.match(r'#\s*(if|ifdef|ifndef)\b', s):
            k = platform_kisiti(s)
            yigin.append({"taban": taban, "alinan": set(),
                          "aktif": taban & (k if k is not None else hepsi)})
        elif re.match(r'#\s*elif\b', s) and yigin:
            t = yigin[-1]
            t["alinan"] |= t["aktif"]
            k = platform_kisiti(s)
            t["aktif"] = (t["taban"] & (k if k is not None else hepsi)) - t["alinan"]
        elif re.match(r'#\s*else\b', s) and yigin:
            t = yigin[-1]
            t["alinan"] |= t["aktif"]
            t["aktif"] = t["taban"] - t["alinan"]
        elif re.match(r'#\s*endif\b', s) and yigin:
            yigin.pop()
    return yigin[-1]["aktif"] if yigin else hepsi


def dizi_adlari(govde, rel):
    """Dizi govdesindeki her dizgi literalini platform kumesiyle dondurur."""
    hepsi = set(PLATFORMLAR)
    out = []
    aktif = set(hepsi)
    alinan = set()
    for ln in govde.splitlines():
        s = ln.strip()
        if s.startswith("#if"):
            alinan = set()
            aktif = kosul_platformu(s, rel)
            alinan |= aktif
            continue
        if s.startswith("#elif"):
            aktif = kosul_platformu(s, rel) - alinan
            alinan |= aktif
            continue
        if s.startswith("#else"):
            aktif = hepsi - alinan
            alinan = set(hepsi)
            continue
        if s.startswith("#endif"):
            aktif = set(hepsi)
            continue
        if s.startswith("//"):
            continue
        for m in LITERAL.finditer(ln):
            lit = m.group(1)
            if lit:
                out.append((lit, tuple(sorted(aktif))))
    return out


def isaret_ara(metin, konum):
    """PAKET isaretini BITISIK yorum blogunda arar.

    Pencere "son N satir" DEGIL: N satirlik bir pencere, yorum blogu uzayinca
    isareti sessizce kaciriyor ve dizi hic gorulmemis gibi oluyordu (tam olarak
    bu oldu: vulkan loader dizisinin isareti 14 satir yukaridaydi, 10 satirlik
    pencere onu bulamadi ve aile ciktidan DUSTU). Kural artik kesin: isaret ya
    cagrinin/bildirimin KENDI satirinda, ya da hemen ustundeki kesintisiz `//`
    yorum blogunun icindedir.
    """
    satirlar = metin[:konum].split("\n")
    ayni = satirlar[-1] + metin[konum:].split("\n", 1)[0]
    m = ISARET.search(ayni)
    if m:
        return m
    i = len(satirlar) - 2
    while i >= 0:
        s = satirlar[i].strip()
        if not s.startswith("//"):
            break
        m = ISARET.search(s)
        if m:
            return m
        i -= 1
    return None


aileler = []      # (rel, satir, politika, lisans, [(ad, platformlar)])
kapsanmayan = []

for alt in TARAMA:
    d = os.path.join(kok, alt)
    if not os.path.isdir(d):
        continue
    for p in kaynak_dosyalar(d):
        metin = open(p, "r", encoding="utf-8", errors="replace").read()
        if "dl_open" not in metin:
            continue
        rel = os.path.relpath(p, kok).replace(os.sep, "/")

        # --- 1. PAKET isaretli ad dizileri --------------------------------
        isaretli = {}     # ident -> aile indeksi
        isaretsiz = {}    # ident -> satir (isareti OLMAYAN ad dizileri)
        for m in DIZI.finditer(metin):
            ident = m.group(1)
            im = isaret_ara(metin, m.start())
            if not im:
                isaretsiz.setdefault(ident, metin[:m.start()].count("\n") + 1)
                continue
            politika = im.group(1)
            ekler = dict(kv.split("=", 1) for kv in im.group(2).split())
            son = kapatan_kase(metin, metin.index("{", m.end() - 1))
            if son < 0:
                hatalar.append("%s :: '%s' dizisinin kapanisi bulunamadi" % (rel, ident))
                continue
            govde = metin[metin.index("{", m.end() - 1) + 1:son]
            satir = metin[:m.start()].count("\n") + 1
            dis = dosya_baglami(metin, m.start())
            adlar = [(ad, tuple(sorted(set(pl) & dis)))
                     for ad, pl in dizi_adlari(govde, rel)]
            adlar = [(ad, pl) for ad, pl in adlar if pl]
            if not adlar:
                hatalar.append("%s:%d :: PAKET isaretli '%s' dizisinde hic ad yok"
                               % (rel, satir, ident))
            isaretli.setdefault(ident, []).append(len(aileler))
            aileler.append({"rel": rel, "satir": satir, "ident": ident,
                            "politika": politika, "lisans": ekler.get("lisans", "-"),
                            "adlar": adlar, "kullanildi": False})

        # --- 2. Aralik-for takma adlari (for (const char *nm : names)) ----
        takma = {}
        for m in ARALIK_FOR.finditer(metin):
            takma[m.group(1)] = m.group(2)

        # --- 3. Her dl_open CAGRISI kapsanmis mi --------------------------
        for m in CAGRI.finditer(metin):
            ap = metin.index("(", m.end() - 1)
            kp = kapatan_paren(metin, ap)
            arg = metin[ap + 1:kp] if kp > 0 else ""
            satir = metin[:m.start()].count("\n") + 1
            # `inline void *dl_open(const char *name)` gibi BILDIRIMLER cagri degil.
            if re.search(r'\b(char|void)\b', arg):
                continue
            # Dogrudan literal: kendi isaretini ister.
            if arg.strip().startswith('"'):
                im = isaret_ara(metin, m.start())
                if not im:
                    kapsanmayan.append("%s:%d :: dl_open(%s) — PAKET isareti yok"
                                       % (rel, satir, arg.strip()))
                continue
            tm = re.match(r'\s*([A-Za-z_]\w*)', arg)
            taban = tm.group(1) if tm else ""
            hedef = taban
            for _ in range(4):
                if hedef in isaretli:
                    break
                hedef = takma.get(hedef, hedef)
            if hedef in isaretli:
                for i in isaretli[hedef]:
                    aileler[i]["kullanildi"] = True
                continue
            im = isaret_ara(metin, m.start())
            if im and im.group(1) == "turetilmis":
                continue
            if hedef in isaretsiz:
                kapsanmayan.append(
                    "%s:%d :: dl_open(%s) — '%s' ad dizisi %s:%d'de var ama PAKET "
                    "isareti YOK" % (rel, satir, arg.strip(), hedef, rel, isaretsiz[hedef]))
                continue
            kapsanmayan.append(
                "%s:%d :: dl_open(%s) — adlari turetemedim ve PAKET isareti yok"
                % (rel, satir, arg.strip()))

for a in aileler:
    if not a["kullanildi"]:
        hatalar.append("%s:%d :: PAKET isaretli '%s' dizisi hicbir dl_open cagrisinda "
                       "kullanilmiyor (bayat isaret)" % (a["rel"], a["satir"], a["ident"]))

if kapsanmayan:
    for s in kapsanmayan:
        sys.stderr.write("KAPSANMAYAN dl_open: %s\n" % s)
    sys.stderr.write(
        "Her dl_open cagrisi paket politikasini SOYLEMEK zorunda. Adlarin oldugu\n"
        "dizinin ustune bir satir koyun:\n"
        "  // PAKET: gomulu lisans=<depo-goreli lisans yolu>   -> pakete KONUR\n"
        "  // PAKET: sistem                                    -> sistemden gelir\n"
        "  // PAKET: turetilmis                                -> adlar yukaridaki diziden\n")
    sys.exit(1)
if hatalar:
    for s in hatalar:
        sys.stderr.write("HATA: %s\n" % s)
    sys.exit(1)
if not aileler:
    sys.stderr.write("Hicbir dl_open ailesi turetilemedi — turetme bozulmus "
                     "(kapi bos bir grep olurdu).\n")
    sys.exit(1)

for a in aileler:
    for ad, plats in a["adlar"]:
        for pl in plats:
            print("%s\t%s\t%s:%d\t%s\t%s" % (a["politika"], pl, a["rel"], a["satir"],
                                             ad, a["lisans"]))
PY
}

# Paketin HEDEF platformu. Konak degil PAKET ICERIGI karar verir — Linux'ta
# denetlenen bir Windows paketi de dogru olculsun (varlik kapisi da boyle).
paket_platformu() {
  local cikti="$1"
  if ls "$cikti"/*.exe >/dev/null 2>&1; then echo windows; return 0; fi
  local t ikili=""
  for t in "${IKILILER[@]}"; do
    if [ -f "$cikti/$t" ]; then ikili="$cikti/$t"; break; fi
  done
  [ -n "$ikili" ] || return 1
  local sihir
  sihir="$(od -An -tx1 -N4 < "$ikili" | tr -d ' \n')"
  case "$sihir" in
    7f454c46)                            echo linux ;;
    cffaedfe|cefaedfe|cafebabe|bebafeca) echo macos ;;
    *)                                   return 1 ;;
  esac
}

# Bir adayin KONAKTAKI kaynagi (bulursa tam yolu basar, yoksa 1 doner).
kutuphane_kaynagi() {
  local ad="$1" plat="$2" d p lc
  case "$ad" in
    /*) [ -f "$ad" ] && { printf '%s\n' "$ad"; return 0; }; return 1 ;;
  esac
  local -a dizinler=()
  [ -n "${TULPAR_PAKET_KUTUPHANE_DIZINI:-}" ] && dizinler+=("$TULPAR_PAKET_KUTUPHANE_DIZINI")
  case "$plat" in
    windows) dizinler+=("${MSYSTEM_PREFIX:-/mingw64}/bin") ;;
    macos)   dizinler+=(/opt/homebrew/lib /usr/local/lib) ;;
    linux)
      # ldconfig ONCE: dagitimin gercekten yukledigi kopya odur.
      lc=""
      command -v ldconfig >/dev/null 2>&1 && lc=ldconfig
      [ -z "$lc" ] && [ -x /sbin/ldconfig ] && lc=/sbin/ldconfig
      if [ -n "$lc" ]; then
        p="$("$lc" -p 2>/dev/null | awk -v n="$ad" '$1 == n { print $NF; exit }')"
        if [ -n "$p" ] && [ -f "$p" ]; then printf '%s\n' "$p"; return 0; fi
      fi
      dizinler+=(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
      ;;
  esac
  for d in "${dizinler[@]}"; do
    if [ -f "$d/$ad" ]; then printf '%s\n' "$d/$ad"; return 0; fi
  done
  return 1
}

# Lisansin PAKET ICI yeri. Paketleyici ve kapi AYNI yeri konusmali diye tek
# yerde: lisanslar/<kutuphane dizini>/<dosya>.
lisans_hedefi() {
  printf 'lisanslar/%s/%s\n' "$(basename "$(dirname "$1")")" "$(basename "$1")"
}

# gomulu politikali her aileyi pakete koyar. Aile = bir dl_open cagrisinin ad
# dizisi; adlar ALTERNATIFTIR, ilk bulunan yeter.
gomulu_kopyala() {
  local cikti="$1" plat="$2" liste="$3"
  local aile satir pol pl ail ad lis kaynak hedefad kopyalandi lisans
  while IFS= read -r aile; do
    [ -n "$aile" ] || continue
    kopyalandi=""
    lisans=""
    while IFS=$'\t' read -r pol pl ail ad lis; do
      [ -n "$ad" ] || continue
      lisans="$lis"
      kaynak="$(kutuphane_kaynagi "$ad" "$plat")" || continue
      hedefad="$(basename "$ad")"
      cp -fL "$kaynak" "$cikti/$hedefad"
      echo "  gomulu  $hedefad  <- $kaynak"
      kopyalandi="$hedefad"
      break
    done < <(printf '%s\n' "$liste" | awk -F'\t' -v p="$plat" -v a="$aile" '$1 == "gomulu" && $2 == p && $3 == a')
    if [ -z "$kopyalandi" ]; then
      ci_hata "$aile: dlopen edilen kutuphanenin adaylarindan HICBIRI konakta bulunamadi ($plat). Paket calismaz halde dagitilmaz. Kurulum: MSYS2 'pacman -S mingw-w64-x86_64-glfw', Ubuntu 'apt-get install libglfw3', macOS 'brew install glfw'."
    fi
    if [ -n "$lisans" ] && [ "$lisans" != "-" ]; then
      [ -f "$kok/$lisans" ] || ci_hata "$aile: PAKET isaretindeki lisans dosyasi kaynak agacinda YOK: $lisans"
      local hedef
      hedef="$(lisans_hedefi "$lisans")"
      mkdir -p "$cikti/$(dirname "$hedef")"
      cp -f "$kok/$lisans" "$cikti/$hedef"
    fi
  done < <(printf '%s\n' "$liste" | awk -F'\t' -v p="$plat" '$1 == "gomulu" && $2 == p { print $3 }' | awk '!gorulen[$0]++')
}

# Windows `.bat` baslatici. NEDEN VAR: .exe'ye CIFT TIKLAYAN kullanicinin
# konsolu surec biter bitmez kapanir — hata satiri onunla beraber kaybolur ve
# geriye "program acilmiyor" algisi kalir. Baslatici hatayi SUSTURMAZ, tam
# tersi: cikis kodu 0 degilse sebebi ekranda tutar ve `pause` ile bekler.
# (Ikinci yedek: ikili ayni hatayi <ikili dizini>/engine_hata.log'a da yazar.)
bat_yaz() {
  local yol="$1" exe="$2"
  # CRLF: .bat dosyalari cmd.exe icin satir sonu CRLF olmali.
  sed 's/$/\r/' > "$yol" <<BAT
@echo off
setlocal
cd /d "%~dp0"
"%~dp0$exe" %*
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo.
  echo [HATA] $exe %RC% koduyla cikti. Yukaridaki satirlar sebebi soyluyor.
  echo Ayni satir engine_hata.log dosyasina da yazildi.
  echo Vulkan surucusu ve paket butunlugu icin OKUBENI.md'ye bakin.
  echo.
  pause
)
exit /b %RC%
BAT
}

# --- Paketleme -------------------------------------------------------------
paketle() {
  local yapi="$1" cikti="$2"
  [ -d "$yapi" ] || hata "yapi dizini yok: $yapi"

  # Uzanti KONAKTAN degil YAPI DIZININDEN. uname MSYS2 disinda bir Windows
  # yapisini goremez; kapi da zaten "konak degil ICERIK karar verir" diyor
  # (bkz. paket_platformu). Capraz dogrulama da bu sayede paket uretebiliyor.
  if [ -f "$yapi/${IKILILER[0]}.exe" ]; then EXE=".exe"; fi

  # Temiz baslangic, ama yanlis dizini silmeden: kaynak agacina benzeyen bir
  # yolu (CMakeLists.txt / .git / deponun kendisi) silmeyi reddediyoruz.
  if [ -e "$cikti" ]; then
    [ -d "$cikti" ] || hata "cikti bir dizin degil: $cikti"
    if [ -e "$cikti/CMakeLists.txt" ] || [ -e "$cikti/.git" ] || [ "$(cd "$cikti" && pwd)" = "$kok" ]; then
      hata "cikti dizini kaynak agaci gibi gorunuyor, silmiyorum: $cikti"
    fi
    rm -rf "$cikti"
  fi
  mkdir -p "$cikti"

  # 1) Ikililer
  local t kaynak
  for t in "${IKILILER[@]}"; do
    kaynak="$yapi/$t$EXE"
    [ -f "$kaynak" ] || ci_hata "$t$EXE uretilmemis ($kaynak yok) — eksik ikiliyle paket dagitilmaz."
    cp -f "$kaynak" "$cikti/"
  done

  # 2) Varliklar (kaynaktan turetilen liste)
  local liste y d
  liste="$(varlik_yollari)" || ci_hata "varlik listesi turetilemedi (yukaridaki satirlara bak)."
  while IFS= read -r y; do
    [ -n "$y" ] || continue
    [ -f "$kok/$y" ] || ci_hata "kod '$y' varligini istiyor ama kaynak agacinda YOK."
    mkdir -p "$cikti/$(dirname "$y")"
    cp -f "$kok/$y" "$cikti/$y"
  done <<< "$liste"

  # Lisanslar varligin YANINDA durur (DejaVu, Material Icons: dagitimda atif
  # sart). Ad listesi degil, varligin dizini uzerinden bulunur.
  while IFS= read -r d; do
    [ -n "$d" ] || continue
    for lis in "$kok/$d"/*LICENSE* "$kok/$d"/*LISANS*; do
      if [ -f "$lis" ]; then cp -f "$lis" "$cikti/$d/"; fi
    done
  done < <(while IFS= read -r y; do [ -n "$y" ] && dirname "$y"; done <<< "$liste" | sort -u)

  # 3) Windows: MinGW calisma zamani DLL'leri. Liste ELLE YAZILMAZ — ikilinin
  # ITHALAT TABLOSUNDAN suzulur, boylece arac zinciri degisince kendiliginden
  # guncel kalir. MSYS2 disinda bu DLL'ler olmadan .exe hic acilmaz.
  # DIKKAT: bu adim yalnizca LINK edilen kutuphaneleri bulur. dlopen'lanan
  # kutuphaneler (GLFW) ithalat tablosunda YOKTUR; onlar 3b'de, kaynaktan
  # turetilen listeyle konur. v0.1.0 paketi tam olarak bu ayrimi kacirdi.
  local plat=""
  if ls "$cikti"/*.exe >/dev/null 2>&1; then
    local onek="${MSYSTEM_PREFIX:-/mingw64}"
    local exe dll d mingw_say=0
    # ldd birincil yol (MSYS2 icinde); ldd yoksa ya da bir sey bulamazsa PE
    # ithalat tablosu (objdump) — capraz dogrulamada da paket uretilebilsin.
    local adaylar=""
    if command -v ldd >/dev/null 2>&1; then
      adaylar="$(for exe in "$cikti"/*.exe; do ldd "$exe" 2>/dev/null | awk '/=>/ {print $3}'; done | sort -u | grep -i "^$onek/" || true)"
    fi
    if [ -z "$adaylar" ] && command -v objdump >/dev/null 2>&1; then
      adaylar="$(for exe in "$cikti"/*.exe; do objdump -p "$exe" 2>/dev/null | awk '/DLL Name:/ {print $3}'; done | sort -u | while IFS= read -r dll; do
        for d in "$onek/bin" "${TULPAR_PAKET_KUTUPHANE_DIZINI:-}"; do
          [ -n "$d" ] && [ -f "$d/$dll" ] && { printf '%s\n' "$d/$dll"; break; }
        done
      done)" || true
    fi
    while IFS= read -r dll; do
      [ -n "$dll" ] || continue
      [ -f "$dll" ] || continue
      cp -n "$dll" "$cikti/" || true
      mingw_say=$((mingw_say + 1))
    done < <(printf '%s\n' "$adaylar")
    # Kapi: hic DLL kopyalanmadiysa paket calismaz bir .exe yigini demektir.
    # `ls *.dll` ile OLCMUYORUZ: 3b adimi GLFW'yi koydugu icin o denetim artik
    # MinGW calisma zamani eksikken de yesil gorunurdu.
    [ "$mingw_say" -gt 0 ] || \
      ci_hata "Hicbir MinGW DLL'i kopyalanmadi ($onek) — bu paket MSYS2 disinda calismaz."
  fi

  # 3b) Calisma zamaninda dlopen EDILEN kutuphaneler (GLFW): liste kaynaktan
  # turetilir, bkz. dlopen_kutuphaneleri(). Uc platformda da kosar.
  plat="$(paket_platformu "$cikti")" || \
    ci_hata "paketin hedef platformu anlasilamadi (ne .exe ne de taninan bir ikili bicimi) — dlopen kutuphaneleri konamaz."
  local dl_liste
  dl_liste="$(dlopen_kutuphaneleri)" || ci_hata "dlopen kutuphane listesi turetilemedi (yukaridaki satirlara bak)."
  gomulu_kopyala "$cikti" "$plat" "$dl_liste"

  # 4) OKUBENI
  [ -f "$OKUBENI_KAYNAK" ] || ci_hata "OKUBENI kaynagi yok: $OKUBENI_KAYNAK"
  cp -f "$OKUBENI_KAYNAK" "$cikti/OKUBENI.md"

  # 4b) Windows baslaticilari: hata halinde konsolu ACIK tutarlar (bkz. bat_yaz).
  if [ "$plat" = "windows" ]; then
    for t in "${IKILILER[@]}"; do
      bat_yaz "$cikti/BASLAT-$t.bat" "$t.exe"
    done
  fi

  # 5) Semboller: engine_editor striplenmemis ~15 MB. Yalniz IKILILER
  # striplenir (varliklarin uzerinden gecmek anlamsiz).
  for t in "${IKILILER[@]}"; do
    [ -f "$cikti/$t$EXE" ] && strip "$cikti/$t$EXE" 2>/dev/null || true
  done

  # 6) Manifest: SURUM.txt + DOSYALAR.txt. EN SON adim — bundan sonra pakete
  # dokunan her sey (strip dahil) ozeti bayatlatir ve denetle() onu yakalar.
  # Surum ELLE verilmez: engine_editor'daki `tulpar-engine-surum:` isaretinden
  # okunur, platform ikilinin basligindan (bkz. tools/paket_manifest.py).
  python3 "$kok/tools/paket_manifest.py" yaz "$cikti" || \
    ci_hata "paket manifesti (SURUM.txt/DOSYALAR.txt) yazilamadi (yukaridaki satirlara bak)."
}

# --- Tamlik kapisi ---------------------------------------------------------
# Paketin ICINE bakar (planina degil): her turetilmis varlik yolu orada ve
# bos olmayan bir dosya mi? Eksik dosya = KIRMIZI.
denetle() {
  local cikti="$1"
  [ -d "$cikti" ] || ci_hata "paket dizini yok: $cikti"

  local liste y eksik=0 say=0
  liste="$(varlik_yollari)" || ci_hata "varlik listesi turetilemedi (yukaridaki satirlara bak)."

  echo "--- tamlik kapisi: $cikti ---"
  while IFS= read -r y; do
    [ -n "$y" ] || continue
    say=$((say + 1))
    if [ -s "$cikti/$y" ]; then
      echo "  var     $y"
    else
      echo "  EKSIK   $y"
      eksik=$((eksik + 1))
    fi
  done <<< "$liste"

  # Ikililer: .exe'li ve .exe'siz paket ayni kapidan gecer (kapi, konak degil
  # PAKET icerigine gore karar verir — Windows paketi Linux'ta da denetlenir).
  local t
  for t in "${IKILILER[@]}"; do
    if [ -f "$cikti/$t" ] || [ -f "$cikti/$t.exe" ]; then
      echo "  var     $t"
    else
      echo "  EKSIK   $t"
      eksik=$((eksik + 1))
    fi
  done

  if [ -s "$cikti/OKUBENI.md" ]; then echo "  var     OKUBENI.md"; else echo "  EKSIK   OKUBENI.md"; eksik=$((eksik + 1)); fi

  # Windows paketi: DLL'siz .exe hic acilmaz.
  if ls "$cikti"/*.exe >/dev/null 2>&1; then
    if ls "$cikti"/*.dll >/dev/null 2>&1; then
      echo "  var     *.dll ($(ls "$cikti"/*.dll | wc -l | tr -d ' ') adet)"
    else
      echo "  EKSIK   *.dll (MinGW calisma zamani)"
      eksik=$((eksik + 1))
    fi
  fi


  # --- Calisma zamaninda dlopen EDILEN kutuphaneler ------------------------
  # Kapi buraya kadar yalnizca LINK edilen DLL'lere bakiyordu; v0.1.0 tam da bu
  # yuzden eksik cikti ve yesil gecti. Liste kaynaktan turetilir (ikinci liste
  # YOK) ve paketin HEDEF platformuna gore olculur.
  local dl_liste dl_plat aile adlar bulundu ad lis lis2 pol pl ail hedef
  dl_liste="$(dlopen_kutuphaneleri)" || ci_hata "dlopen kutuphane listesi turetilemedi (yukaridaki satirlara bak)."
  dl_plat="$(paket_platformu "$cikti")" || \
    ci_hata "paketin hedef platformu anlasilamadi (ne .exe ne de taninan bir ikili bicimi) — dlopen kapisi olcemez."
  echo "  --- dlopen ile yuklenenler (hedef: $dl_plat) ---"
  while IFS= read -r aile; do
    [ -n "$aile" ] || continue
    say=$((say + 1))
    bulundu=""
    adlar=""
    lis="-"
    while IFS=$'\t' read -r pol pl ail ad lis2; do
      [ -n "$ad" ] || continue
      lis="$lis2"
      adlar="$adlar $(basename "$ad")"
      [ -n "$bulundu" ] && continue
      if [ -s "$cikti/$(basename "$ad")" ]; then bulundu="$(basename "$ad")"; fi
    done < <(printf '%s\n' "$dl_liste" | awk -F'\t' -v p="$dl_plat" -v a="$aile" '$1 == "gomulu" && $2 == p && $3 == a')
    if [ -n "$bulundu" ]; then
      echo "  var     $bulundu  (dlopen, $aile)"
    else
      echo "  EKSIK   dlopen kutuphanesi [$aile]: adaylarin hicbiri pakette yok ->$adlar"
      eksik=$((eksik + 1))
    fi
    if [ -n "$lis" ] && [ "$lis" != "-" ]; then
      say=$((say + 1))
      hedef="$(lisans_hedefi "$lis")"
      if [ -s "$cikti/$hedef" ]; then
        echo "  var     $hedef"
      else
        echo "  EKSIK   $hedef (gomulu kutuphanenin lisansi)"
        eksik=$((eksik + 1))
      fi
    fi
  done < <(printf '%s\n' "$dl_liste" | awk -F'\t' -v p="$dl_plat" '$1 == "gomulu" && $2 == p { print $3 }' | awk '!gorulen[$0]++')
  # `sistem` politikali aileler BILEREK paketlenmez (Vulkan loader surucuyle
  # gelir). Gorunur kalsinlar: sessizce yok olan bir bagimlilik, olculmeyen bir
  # bagimliliktir.
  while IFS= read -r aile; do
    [ -n "$aile" ] || continue
    echo "  sistem  $aile (paketlenmez: kullanicinin surucusu/yigini saglar)"
  done < <(printf '%s\n' "$dl_liste" | awk -F'\t' -v p="$dl_plat" '$1 == "sistem" && $2 == p { print $3 }' | awk '!gorulen[$0]++')

  # Windows: baslaticilar. Hata halinde konsolu acik tutan tek sey onlar.
  if [ "$dl_plat" = "windows" ]; then
    local t2
    for t2 in "${IKILILER[@]}"; do
      say=$((say + 1))
      if [ -s "$cikti/BASLAT-$t2.bat" ]; then
        echo "  var     BASLAT-$t2.bat"
      else
        echo "  EKSIK   BASLAT-$t2.bat (cift tiklayan kullanici hatayi goremez)"
        eksik=$((eksik + 1))
      fi
    done
  fi

  # --- Manifest (SURUM.txt + DOSYALAR.txt) ---------------------------------
  # Guncelleyici kullanicinin degistirdigi dosyayi DOSYALAR.txt'deki ozetle
  # taniyor; yanlis bir satir ya bir kullanici dosyasini sessizce ezer ya da
  # temiz bir paketi "bozuk" sayar. Kapi once KENDINI sinar (bozuk paketleri
  # yakaliyor, temizini geciriyor mu), sonra bu paketi olcer: her satirin
  # ozeti, listede olmayan/pakette olmayan dosya, sira, satir sonu, SURUM.txt
  # = ikilideki surum + platform.
  echo "  --- manifest (SURUM.txt + DOSYALAR.txt) ---"
  local oz
  if ! oz="$(python3 "$kok/tools/paket_manifest.py" --oz-sinama 2>&1)"; then
    printf '%s\n' "$oz" >&2
    ci_hata "manifest kapisinin oz-sinamasi KIRMIZI — kapi bozuk paketi yakalayamiyor ya da temizini geciremiyor; olcumune guvenilmez."
  fi
  # Ozet satiri + (varsa) GORUNUR atlamalar.
  printf '%s\n' "$oz" | tr -d '\r' | grep -E 'ATLANDI:|sinama tuttu' | sed 's/^/  /' || true
  if python3 "$kok/tools/paket_manifest.py" denetle "$cikti"; then
    say=$((say + 1))
  else
    echo "  EKSIK   gecerli bir manifest (yukaridaki MANIFEST satirlari)"
    eksik=$((eksik + 1))
  fi

  [ "$say" -gt 0 ] || ci_hata "Turetilen varlik listesi BOS — kapi hicbir sey olcmuyordu."
  if [ "$eksik" -gt 0 ]; then
    ci_hata "Paket eksik: $eksik ogenin karsiligi yok. Bu paket calistirilabilir degil, yayinlanmaz."
  fi
  echo "tamlik kapisi TAMAM: $say oge (varlik + dlopen kutuphanesi/lisansi + baslatici + manifest) + ${#IKILILER[@]} ikili + OKUBENI.md yerinde."
}

# --- Giris -----------------------------------------------------------------
[ $# -ge 1 ] || kullanim
case "$1" in
  --denetle|--check)
    [ $# -eq 2 ] || kullanim
    denetle "$2"
    ;;
  -h|--help|--yardim)
    kullanim
    ;;
  *)
    [ $# -eq 2 ] || kullanim
    paketle "$1" "$2"
    denetle "$2"
    echo "--- paket icerigi ---"
    ( cd "$2" && find . -type f | sed 's|^\./||' | sort | while IFS= read -r f; do
        printf '%10s  %s\n' "$(wc -c < "$f" | tr -d ' ')" "$f"
      done )
    echo "--- toplam ---"
    du -sh "$2" | sed 's/^/  /'
    ;;
esac
