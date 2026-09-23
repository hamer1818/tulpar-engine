#!/usr/bin/env bash
# Tulpar Engine — editoru tek komutla ac (Linux / macOS / MSYS2 MINGW64).
#
#   ./editor.sh                            varsayilan sahne (tests/assets/editor.sahne)
#   ./editor.sh yol/x.sahne                verilen sahneyle ac
#   ./editor.sh --derleme-yok              derlemeyi atla, mevcut ikiliyi ac
#   ./editor.sh --headless 30 --out k.ppm  penceresiz dogrulama
#   ./editor.sh --size 1600x900 --validation
#
# Ikili yoksa once derle.sh'yi (Windows'ta tools/derle_mingw.sh) kosturur,
# bagimlilik denetimi dahil. Ikili varsa ARTIMLI derleme yapar: kaynak
# degistiyse editor taze acilir, degismediyse ninja hicbir sey yapmadan doner
# (olculdu 2026-09-23, Linux 16 cekirdek: 165-179 ms, kapilar dahil).
# Windows'tan cift tiklamak icin: editor.bat.
set -uo pipefail

K='\033[0;31m'; Y='\033[0;32m'; S='\033[0;33m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; S=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}✓${N} %s\n" "$*"; }
uyar()  { printf "${S}!${N} %s\n" "$*"; }
hata()  { printf "${K}✗${N} %s\n" "$*" >&2; }

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Kullanicinin verdigi bir yolu MUTLAK yap — depo kokune `cd` etmeden ONCE.
# Yapilmasaydi `../editor.sh benim.sahne` baska bir dizinden cagrildiginda
# yol depo kokune gore cozulur ve yanlis dosya (ya da hic dosya) acilirdi.
# Windows yolu (`C:\...`, `tests\assets\x.sahne`) once POSIX'e cevrilir:
# `dirname` yalniz `/` taniyor, ters bolu ile her yol "." olurdu.
mutlak() {
  local p="$1"
  if [ -n "${MSYSTEM:-}" ] && command -v cygpath >/dev/null 2>&1; then p="$(cygpath -u "$p")"; fi
  case "$p" in /*) printf '%s' "$p"; return 0 ;; esac
  local d; d="$(cd "$(dirname "$p")" 2>/dev/null && pwd)" || return 1
  printf '%s/%s' "$d" "$(basename "$p")"
}

derleme=1; sahne=""; gecir=()
while [ $# -gt 0 ]; do
  case "$1" in
    --derleme-yok) derleme=0 ;;
    --bekleme-yok) ;; # editor.bat'in secenegi; bat onu buraya da iletiyor, burada etkisiz
    -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --headless|--size)
      [ $# -ge 2 ] || { hata "$1 bir deger ister (--help)"; exit 2; }
      gecir+=("$1" "$2"); shift ;;
    --out)
      [ $# -ge 2 ] || { hata "--out bir dosya yolu ister"; exit 2; }
      o="$(mutlak "$2")" || { hata "--out dizini yok: $(dirname "$2")"; exit 2; }
      gecir+=(--out "$o"); shift ;;
    --validation) gecir+=(--validation) ;;
    --scene)
      [ $# -ge 2 ] || { hata "--scene bir .sahne yolu ister"; exit 2; }
      [ -z "$sahne" ] || { hata "iki sahne verildi: $sahne ve $2"; exit 2; }
      sahne="$2"; shift ;;
    -*)
      # Bilinmeyen secenegi editore GECIRMIYORUZ: editor tanimadigi argumani
      # SESSIZCE yok sayar, yani `--headles 30` gibi bir yazim hatasi hata
      # vermeden pencere acardi.
      hata "bilinmeyen secenek: $1 (--help)"; exit 2 ;;
    *)
      [ -z "$sahne" ] || { hata "iki sahne verildi: $sahne ve $1"; exit 2; }
      sahne="$1" ;;
  esac
  shift
done

# Sahne ONCE denetlenir. Editor bulamadigi sahnede hata loglayip BOS sahneyle
# acilir; pencere acildiktan sonra konsolda kaybolan bir satir yerine burada
# ve adiyla duruyoruz.
if [ -n "$sahne" ]; then
  case "$sahne" in *.sahneb)
    hata "editor .sahne (metin) acar; .sahneb derlenmis blob: $sahne"
    echo "  Kaynak dosyayi verin: ${sahne%b}"; exit 1 ;;
  esac
  s="$(mutlak "$sahne")" && [ -f "$s" ] || { hata "sahne bulunamadi: $sahne"; exit 1; }
  sahne="$s"
fi

cd "$kok"

# --- Platform ---------------------------------------------------------------
# MSYS2'de ikili .exe ve derleyici tools/derle_mingw.sh. MINGW64 sart: editor
# glfw3.dll ve libstdc++ gibi DLL'leri /mingw64/bin'den buluyor; MSYS ya da
# UCRT64 kabugunda o dizin PATH'te degil ve editor "DLL bulunamadi" ile duser.
if [ -n "${MSYSTEM:-}" ]; then
  if [ "$MSYSTEM" != "MINGW64" ]; then
    hata "MINGW64 kabugu gerekli (su an: $MSYSTEM)."
    echo "  Baslat menusunden 'MSYS2 MINGW64' ile acin, ya da editor.bat kullanin."
    exit 1
  fi
  ikili="yapi/engine_editor.exe"; derleyici="tools/derle_mingw.sh"
else
  ikili="yapi/engine_editor"; derleyici="./derle.sh"
fi

# --- Derleme ----------------------------------------------------------------
if [ ! -f "$ikili" ]; then
  if [ "$derleme" = 0 ]; then
    hata "$ikili yok ve --derleme-yok verildi. Once: $derleyici"; exit 1
  fi
  bilgi "$ikili yok — ilk derleme ($derleyici, bagimlilik denetimi dahil)"
  if ! bash "$derleyici"; then hata "derleme basarisiz, editor acilmadi"; exit 1; fi
elif [ "$derleme" = 1 ]; then
  bilgi "artimli derleme (kaynak degismediyse aninda biter)"
  # Dusen derlemede eski ikiliyi SESSIZCE acmiyoruz: kullanici yaptigi
  # degisikligi goruyor sanir, oysa onundeki dunku editordur.
  if ! cmake --build yapi; then
    hata "derleme dustu — editor ACILMADI (eski ikili yaniltirdi)."
    echo "  Ilk 'error:' satiri sebebi soyler. Eski ikiliyi yine de acmak icin: $0 --derleme-yok"
    exit 1
  fi
fi

# --- Pencere var mi? ---------------------------------------------------------
# Yalniz Linux: macOS'ta DISPLAY hic tanimli olmaz (Cocoa), Windows'ta anlamsiz.
headless=0
for a in ${gecir[@]+"${gecir[@]}"}; do [ "$a" = --headless ] && headless=1; done
if [ "$headless" = 0 ] && [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
  hata "Ekran yok (DISPLAY ve WAYLAND_DISPLAY bos) — pencere acilamaz."
  echo "  Penceresiz dogrulama: $0 --headless 30 --out kare.ppm"
  exit 1
fi

# --- Ac -----------------------------------------------------------------------
# Sahne `--scene` ile verilir, ciplak arguman olarak DEGIL: editor yalniz
# `--scene` taniyor ve ciplak yolu sessizce yok sayip varsayilan sahneyi
# aciyor (app/editor.cpp). Varsayilan sahneyle ayni olunca fark GORUNMUYOR.
arg=()
[ -n "$sahne" ] && arg=(--scene "$sahne")
iyi "editor: ${sahne:-tests/assets/editor.sahne (varsayilan)}"
# `${x[@]+"${x[@]}"}`: macOS'un /bin/bash'i 3.2 ve orada `set -u` altinda BOS
# dizi "unbound variable" ile duser. Bu yazim bos diziyi hic genisletmiyor.
exec "./$ikili" ${arg[@]+"${arg[@]}"} ${gecir[@]+"${gecir[@]}"}
