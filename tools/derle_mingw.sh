#!/usr/bin/env bash
# Tulpar Engine — Windows derlemesinin MSYS2/MINGW64 tarafi.
# derle.bat tarafindan cagrilir; dogrudan da calistirilabilir (MINGW64 kabugunda).
#
# NEDEN AYRI DOSYA: mantigi cmd ile bash arasinda BOLMEK istemedik. .bat yalniz
# "MSYS2 var mi, yoksa kur" sorusunu cozer; paket denetimi, kurulum ve derleme
# — yani asil karar veren kisim — burada, tek yerde durur.
set -uo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$kok"

K='\033[0;31m'; Y='\033[0;32m'; S='\033[0;33m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; S=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}+${N} %s\n" "$*"; }
uyar()  { printf "${S}!${N} %s\n" "$*"; }
hata()  { printf "${K}x${N} %s\n" "$*" >&2; }

otomatik=0; sadece_denetle=0; temiz=0
for a in "$@"; do case "$a" in
  --otomatik|--evet|-y) otomatik=1 ;;
  --sadece-denetle|--denetle) sadece_denetle=1 ;;
  --temiz) temiz=1 ;;
  -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) hata "bilinmeyen secenek: $a"; exit 2 ;;
esac; done

# MINGW64 kabugunda miyiz? MSYS kabugunda derlenen ikili MSYS2'nin POSIX
# katmanina baglanir ve MSYS2 disinda CALISMAZ — sessizce yanlis ikili uretmek
# yerine burada duruyoruz.
if [ "${MSYSTEM:-}" != "MINGW64" ]; then
  hata "MINGW64 kabugu gerekli (su an: ${MSYSTEM:-yok})."
  echo  "  Baslat menusunden 'MSYS2 MINGW64' ile acin, ya da derle.bat kullanin."
  exit 1
fi

# --- Bagimliliklar ----------------------------------------------------------
# Bicim: "komut|zorunlu|aciklama|pacman paketi"
# Liste CI'nin gercekten kurdugu paketlerden alindi (.github/workflows/ci.yml),
# tahminden degil. glslc CI'da da kurulu DEGIL: shader BAYT kapisi atlanir,
# ozet kapisi yine kosar — bu yuzden istege bagli.
DEPS='
cmake|1|derleme sistemi (>= 3.14)|mingw-w64-x86_64-cmake
ninja|1|hizli derleyici surucusu|mingw-w64-x86_64-ninja
g++|1|C++17 derleyicisi|mingw-w64-x86_64-gcc
python|1|kapi betikleri (katman/shader/sahne denetimi)|mingw-w64-x86_64-python
ccache|0|yeniden derlemeyi ~10x hizlandirir|mingw-w64-x86_64-ccache
glslc|0|shader BAYT kapisi (yoksa ozet kapisi yine kosar)|mingw-w64-x86_64-shaderc
'

echo
bilgi "Tulpar Engine — bagimlilik denetimi (MINGW64)"
echo

eksik_zorunlu=(); kur_listesi=()
while IFS='|' read -r komut zorunlu aciklama pkg; do
  [ -n "${komut:-}" ] || continue
  if command -v "$komut" >/dev/null 2>&1; then
    printf "  ${Y}+${N} %-8s %s\n" "$komut" "$aciklama"
  elif [ "$zorunlu" = 1 ]; then
    printf "  ${K}x${N} %-8s %s  ${K}(ZORUNLU)${N}\n" "$komut" "$aciklama"
    eksik_zorunlu+=("$komut"); kur_listesi+=("$pkg")
  else
    printf "  ${S}.${N} %-8s %s  (istege bagli)\n" "$komut" "$aciklama"
    kur_listesi+=("$pkg")
  fi
done <<< "$DEPS"

# glfw3.dll komut degil, kutuphane: `command -v` ile GORUNMEZ. Ayri denetlenir,
# yoksa editor derlenir ama pencere acamaz.
if [ -f /mingw64/bin/glfw3.dll ]; then
  printf "  ${Y}+${N} %-8s %s\n" "glfw" "pencere acma (masaustu kip)"
else
  printf "  ${S}.${N} %-8s %s  (calistirmak icin)\n" "glfw" "pencere acma (masaustu kip)"
  kur_listesi+=("mingw-w64-x86_64-glfw")
fi

# vulkan-1.dll EKRAN KARTI SURUCUSU ile gelir, pacman paketiyle degil. Kurulum
# onerisi vermek yaniltici olurdu; yalnizca durumu bildiriyoruz.
if [ -f "/c/Windows/System32/vulkan-1.dll" ] || [ -f "${SYSTEMROOT:-/c/Windows}/System32/vulkan-1.dll" ]; then
  printf "  ${Y}+${N} %-8s %s\n" "vulkan" "Vulkan yukleyicisi (surucuden)"
else
  printf "  ${S}.${N} %-8s %s\n" "vulkan" "vulkan-1.dll yok — EKRAN KARTI SURUCUSUNU guncelleyin"
fi
echo

# --- Eksikse: anlat, sor, gerekirse kur -------------------------------------
if [ ${#kur_listesi[@]} -gt 0 ]; then
  uyar "Eksik paketler icin kurulum komutu:"
  printf "\n    pacman -S --needed --noconfirm %s\n\n" "${kur_listesi[*]}"
  kur=0
  if [ "$otomatik" = 1 ]; then kur=1
  elif [ "$sadece_denetle" = 1 ]; then kur=0
  elif [ -t 0 ]; then
    read -r -p "Otomatik kurayim mi? [e/H] " c
    case "$c" in e|E|y|Y|evet|EVET) kur=1 ;; *) kur=0 ;; esac
  else
    uyar "Etkilesimli terminal yok — kurulum yapilmadi (--otomatik ile zorlayabilirsiniz)."
  fi
  if [ "$kur" = 1 ]; then
    bilgi "kuruluyor: ${kur_listesi[*]}"
    # shellcheck disable=SC2086
    if ! pacman -S --needed --noconfirm ${kur_listesi[*]}; then
      hata "Kurulum basarisiz. Komutu elle calistirip tekrar deneyin."; exit 1
    fi
    iyi "kurulum tamam"
  fi
fi

kalan=()
for k in "${eksik_zorunlu[@]-}"; do
  [ -n "$k" ] && ! command -v "$k" >/dev/null 2>&1 && kalan+=("$k")
done
if [ ${#kalan[@]} -gt 0 ]; then hata "Zorunlu araclar hala eksik: ${kalan[*]}"; exit 1; fi
[ "$sadece_denetle" = 1 ] && { iyi "denetim bitti (--denetle)"; exit 0; }

# --- Derleme ----------------------------------------------------------------
yapi="yapi"
[ "$temiz" = 1 ] && { bilgi "temiz derleme: $yapi siliniyor"; rm -rf "$yapi"; }
cc_bayrak=()
command -v ccache >/dev/null 2>&1 && cc_bayrak=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
is="$(nproc 2>/dev/null || echo 4)"

bilgi "yapilandiriliyor ($yapi, Release)"
if ! cmake -S . -B "$yapi" -G Ninja -DCMAKE_BUILD_TYPE=Release "${cc_bayrak[@]}"; then
  hata "cmake yapilandirmasi dustu."; exit 1
fi
bilgi "derleniyor (-j$is)"
if ! cmake --build "$yapi" -j"$is"; then
  hata "Derleme dustu. Yukaridaki ilk 'error:' satiri sebebi soyler."; exit 1
fi

echo; iyi "derleme tamam"
echo "  ikililer: $kok/$yapi/"
for x in engine_editor engine_demo engine_tests engine_sahnec engine_texpack engine_clodbake; do
  [ -f "$yapi/$x.exe" ] && printf "    %-16s %s\n" "$x.exe" "$(du -h "$yapi/$x.exe" | cut -f1)"
done
echo
bilgi "Editoru ac: editor.bat (cift tiklanabilir) ya da bu MINGW64 kabugundan ./editor.sh [x.sahne]"
echo "  Testleri kostur:   ./$yapi/engine_tests.exe"
echo "  Cift tiklanabilir, DLL'leri yanina alan paket icin: tools/package.sh"
