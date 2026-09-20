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
#           *.dll                  (yalniz Windows; ldd ciktisindan bulunur)
#           assets/fonts/<ttf + lisanslar>
#           tests/assets/<demo ve editorun yukledigi varliklar>
#           OKUBENI.md
#
# NEDEN VARLIKLAR DA PAKETTE (olculdu): ikililer varlik yollarini
# "<exe dizini>/<goreli yol>" -> "<calisma dizini>/<goreli yol>" ->
# ENGINE_SOURCE_DIR sirasiyla ariyor. ENGINE_SOURCE_DIR DERLEME ZAMANI bir
# yol: paket yalniz ikili iceriyorsa, derleyen makinenin kaynak agaci olmayan
# her yerde engine_editor sahneyi bulamaz ve 1 ile cikar. Varliklar buraya,
# exe'nin YANINA, birinci adimin bulacagi yerlesimle konur.
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

# --- Paketleme -------------------------------------------------------------
paketle() {
  local yapi="$1" cikti="$2"
  [ -d "$yapi" ] || hata "yapi dizini yok: $yapi"

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

  # 3) Windows: MinGW calisma zamani DLL'leri. Liste ELLE YAZILMAZ — ldd
  # ciktisindan suzulur, boylece arac zinciri degisince kendiliginden guncel
  # kalir. MSYS2 disinda bu DLL'ler olmadan .exe hic acilmaz.
  if ls "$cikti"/*.exe >/dev/null 2>&1; then
    command -v ldd >/dev/null 2>&1 || ci_hata "ldd yok — Windows paketine DLL konamaz."
    local onek="${MSYSTEM_PREFIX:-/mingw64}"
    local exe dll
    for exe in "$cikti"/*.exe; do
      ldd "$exe" | awk '/=>/ {print $3}'
    done | sort -u | grep -i "^$onek/" | while IFS= read -r dll; do
      if [ -f "$dll" ]; then cp -n "$dll" "$cikti/"; fi
    done || true
    # Kapi: hic DLL kopyalanmadiysa paket calismaz bir .exe yigini demektir.
    ls "$cikti"/*.dll >/dev/null 2>&1 || \
      ci_hata "Hicbir MinGW DLL'i kopyalanmadi ($onek) — bu paket MSYS2 disinda calismaz."
  fi

  # 4) OKUBENI
  [ -f "$OKUBENI_KAYNAK" ] || ci_hata "OKUBENI kaynagi yok: $OKUBENI_KAYNAK"
  cp -f "$OKUBENI_KAYNAK" "$cikti/OKUBENI.md"

  # 5) Semboller: engine_editor striplenmemis ~15 MB. Yalniz IKILILER
  # striplenir (varliklarin uzerinden gecmek anlamsiz).
  for t in "${IKILILER[@]}"; do
    [ -f "$cikti/$t$EXE" ] && strip "$cikti/$t$EXE" 2>/dev/null || true
  done
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

  [ "$say" -gt 0 ] || ci_hata "Turetilen varlik listesi BOS — kapi hicbir sey olcmuyordu."
  if [ "$eksik" -gt 0 ]; then
    ci_hata "Paket eksik: $eksik ogenin karsiligi yok. Bu paket calistirilabilir degil, yayinlanmaz."
  fi
  echo "tamlik kapisi TAMAM: $say varlik + ${#IKILILER[@]} ikili + OKUBENI.md yerinde."
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
