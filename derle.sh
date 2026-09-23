#!/usr/bin/env bash
# Tulpar Engine — tek komutluk derleme betigi (Linux / macOS).
#
#   ./derle.sh                 bagimliliklari denetle, eksikse SOR, sonra derle
#   ./derle.sh --otomatik      sormadan kur (CI / betik icinden)
#   ./derle.sh --sadece-denetle  yalniz rapor, kurma, derleme
#   ./derle.sh --temiz         yapi dizinini sifirdan kur
#
# NEDEN VAR: "nasil derlerim" sorusunun cevabi README'de dagilmis durumdaydi ve
# eksik bir paket CMake'in ortasinda anlasilmaz bir hatayla cikiyordu. Bu betik
# eksigi ONCE ve ADIYLA soyler, kurulum komutunu yazar, istersen kendi kurar.
set -uo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$kok"

K='\033[0;31m'; Y='\033[0;32m'; S='\033[0;33m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; S=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}✓${N} %s\n" "$*"; }
uyar()  { printf "${S}!${N} %s\n" "$*"; }
hata()  { printf "${K}✗${N} %s\n" "$*" >&2; }

otomatik=0; sadece_denetle=0; temiz=0
for a in "$@"; do case "$a" in
  --otomatik|--evet|-y) otomatik=1 ;;
  --sadece-denetle|--denetle) sadece_denetle=1 ;;
  --temiz) temiz=1 ;;
  -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) hata "bilinmeyen secenek: $a (--help)"; exit 2 ;;
esac; done

# --- Paket yoneticisini bul -------------------------------------------------
# Kurulum komutu DAGITIMA gore degisir; yanlis komut yazmaktansa "bilmiyorum"
# demek daha iyidir, o yuzden taninmayan sistemde elle kurulum anlatilir.
yonetici=""; kur_on=""
if   command -v pacman  >/dev/null 2>&1; then yonetici=pacman;  kur_on="sudo pacman -S --needed --noconfirm"
elif command -v apt-get >/dev/null 2>&1; then yonetici=apt;     kur_on="sudo apt-get install -y"
elif command -v dnf     >/dev/null 2>&1; then yonetici=dnf;     kur_on="sudo dnf install -y"
elif command -v zypper  >/dev/null 2>&1; then yonetici=zypper;  kur_on="sudo zypper install -y"
elif command -v brew    >/dev/null 2>&1; then yonetici=brew;    kur_on="brew install"
fi

# --- Bagimliliklar ----------------------------------------------------------
# Bicim: "komut|zorunlu mu|ne ise yarar|pacman|apt|dnf|zypper|brew"
# ZORUNLU = derleme icin sart. ISTEGE BAGLI = derleme yine olur, ama eksik
# kalan bir sey olur (hiz, ya da CALISTIRMA icin gereken surucu).
DEPS='
cmake|1|derleme sistemi (>= 3.14)|cmake|cmake|cmake|cmake|cmake
ninja|1|hizli derleyici surucusu|ninja|ninja-build|ninja-build|ninja|ninja
c++|1|C++17 derleyicisi|gcc|g++|gcc-c++|gcc-c++|
python3|1|kapi betikleri (katman/shader/sahne denetimi)|python|python3|python3|python3|python3
ccache|0|yeniden derlemeyi ~10x hizlandirir|ccache|ccache|ccache|ccache|ccache
glslc|0|shader BAYT kapisi (yoksa ozet kapisi yine kosar)|shaderc|glslc|glslc|shaderc|shaderc
'

# Calistirma zamani (derlemeyi engellemez ama editor acilmaz)
CALISMA='
libvulkan|Vulkan yukleyicisi — SURUCU olmadan editor acilmaz|vulkan-icd-loader|libvulkan1|vulkan-loader|libvulkan1|vulkan-loader
libglfw|pencere acma (masaustu kip)|glfw|libglfw3|glfw|libglfw3|glfw
'

paket_adi() { # $1=satir $2=alan indeksi(4..8)
  echo "$1" | cut -d'|' -f"$2"
}
alan_no() { case "$yonetici" in pacman) echo 4;; apt) echo 5;; dnf) echo 6;; zypper) echo 7;; brew) echo 8;; *) echo 0;; esac; }

kutuphane_var() {
  # DIKKAT: yollari TEK bir `ls`e vermek YANLIS olcum uretir — biri yoksa
  # (ornegin Linux'ta /opt/homebrew) `ls` digerlerini bulsa bile HATA doner ve
  # kurulu kutuphane "eksik" gorunur. Olculdu 2026-09-20; her yol AYRI denenir.
  local ad="$1" d
  if command -v ldconfig >/dev/null 2>&1; then
    if ldconfig -p 2>/dev/null | grep -q -- "$ad"; then return 0; fi
  fi
  for d in /usr/lib /usr/lib64 /lib /lib64 /usr/local/lib /opt/homebrew/lib \
           /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu; do
    [ -d "$d" ] || continue
    # `compgen -G` glob'u GENISLETIR ama bulunamayinca sessizce 1 doner.
    compgen -G "$d/$ad*" >/dev/null 2>&1 && return 0
  done
  return 1
}

echo
bilgi "Tulpar Engine — bagimlilik denetimi"
[ -n "$yonetici" ] && iyi "paket yoneticisi: $yonetici" || uyar "paket yoneticisi taninmadi — kurulum komutlari elle verilecek"
echo

eksik_zorunlu=(); eksik_istege=(); eksik_calisma=(); kur_listesi=()
an=$(alan_no)

while IFS= read -r satir; do
  [ -n "$satir" ] || continue
  komut="$(echo "$satir" | cut -d'|' -f1)"
  zorunlu="$(echo "$satir" | cut -d'|' -f2)"
  aciklama="$(echo "$satir" | cut -d'|' -f3)"
  pkg=""; [ "$an" != 0 ] && pkg="$(paket_adi "$satir" "$an")"
  if command -v "$komut" >/dev/null 2>&1; then
    printf "  ${Y}✓${N} %-10s %s\n" "$komut" "$aciklama"
  elif [ "$zorunlu" = 1 ]; then
    printf "  ${K}✗${N} %-10s %s  ${K}(ZORUNLU)${N}\n" "$komut" "$aciklama"
    eksik_zorunlu+=("$komut"); [ -n "$pkg" ] && kur_listesi+=("$pkg")
  else
    printf "  ${S}·${N} %-10s %s  (istege bagli)\n" "$komut" "$aciklama"
    eksik_istege+=("$komut"); [ -n "$pkg" ] && kur_listesi+=("$pkg")
  fi
done <<< "$DEPS"

while IFS= read -r satir; do
  [ -n "$satir" ] || continue
  ad="$(echo "$satir" | cut -d'|' -f1)"
  aciklama="$(echo "$satir" | cut -d'|' -f2)"
  pkg=""; [ "$an" != 0 ] && pkg="$(echo "$satir" | cut -d'|' -f$((an-1)))"
  if kutuphane_var "$ad"; then printf "  ${Y}✓${N} %-10s %s\n" "$ad" "$aciklama"
  else printf "  ${S}·${N} %-10s %s  (calistirmak icin)\n" "$ad" "$aciklama"
       eksik_calisma+=("$ad"); [ -n "$pkg" ] && kur_listesi+=("$pkg"); fi
done <<< "$CALISMA"
echo

# --- Eksikse: anlat, sor, gerekirse kur -------------------------------------
if [ ${#kur_listesi[@]} -gt 0 ]; then
  if [ -z "$yonetici" ]; then
    hata "Eksik var ama paket yoneticisi taninmadi."
    echo "  Su araclari elle kurun: ${eksik_zorunlu[*]-} ${eksik_istege[*]-} ${eksik_calisma[*]-}"
  else
    uyar "Eksik paketler icin kurulum komutu:"
    printf "\n    %s %s\n\n" "$kur_on" "${kur_listesi[*]}"
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
      if ! $kur_on ${kur_listesi[*]}; then
        hata "Kurulum basarisiz. Komutu elle calistirip tekrar deneyin."; exit 1
      fi
      iyi "kurulum tamam"
    fi
  fi
fi

# ZORUNLU eksik hala duruyorsa derlemeye girmenin anlami yok: CMake'in
# ortasinda anlasilmaz bir hata yerine BURADA ve adiyla duruyoruz.
kalan=()
for k in "${eksik_zorunlu[@]-}"; do [ -n "$k" ] && ! command -v "$k" >/dev/null 2>&1 && kalan+=("$k"); done
if [ ${#kalan[@]} -gt 0 ]; then hata "Zorunlu araclar hala eksik: ${kalan[*]}"; exit 1; fi
[ "$sadece_denetle" = 1 ] && { iyi "denetim bitti (--sadece-denetle)"; exit 0; }

# --- Derleme ----------------------------------------------------------------
yapi="yapi"
[ "$temiz" = 1 ] && { bilgi "temiz derleme: $yapi siliniyor"; rm -rf "$yapi"; }
cc_bayrak=()
command -v ccache >/dev/null 2>&1 && cc_bayrak=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
is=$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )

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
  [ -x "$yapi/$x" ] && printf "    %-16s %s\n" "$x" "$(du -h "$yapi/$x" | cut -f1)"
done
echo
echo "  Editoru ac:        ./editor.sh [x.sahne]"
echo "  Testleri kostur:   DISPLAY= ./$yapi/engine_tests"
echo "  Penceresiz kanit:  ./$yapi/engine_editor --headless 30 --out kare.ppm"
if [ ${#eksik_calisma[@]} -gt 0 ]; then
  echo; uyar "Calistirmak icin eksik: ${eksik_calisma[*]} — derleme bundan etkilenmedi,"
  echo "  ama editor pencere acamayabilir ya da Vulkan cihazi bulamayabilir."
fi
