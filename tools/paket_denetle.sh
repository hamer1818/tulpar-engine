#!/usr/bin/env bash
# ===========================================================================
# Paket yerlesimi + (Windows'ta) MinGW DLL kapisi.
#
#   tools/paket_denetle.sh <yapi-dizini> <paket-koku>
#
# NEDEN AYRI DOSYA: bu olcum onceden YALNIZ .github/workflows/release.yml
# icinde, satir arasinda duruyordu. Sonucu: PR'lar yesil kaliyor, hata ancak
# ETIKET atilinca goruluyordu — v0.1.1 ve v0.1.2 surum kosumlari tam bu
# yuzden dustu. Kapi artik tek yerde; CI de surum de ayni dosyayi cagiriyor,
# yani PR'da yesilse surumde de yesildir.
# ===========================================================================
set -uo pipefail

yapi="${1:?kullanim: paket_denetle.sh <yapi-dizini> <paket-koku>}"
kok="${2:?kullanim: paket_denetle.sh <yapi-dizini> <paket-koku>}"
eksik=0

# GitHub Actions'ta "::error::" satiri isi kirmizi yapar; disarida duz yazi.
hata() { if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error::$*"; else echo "HATA: $*" >&2; fi; eksik=1; }

# Windows mi? .exe uzantisi ve DLL kapisi yalniz orada gecerli.
win=0
case "${OSTYPE:-}${MSYSTEM:-}" in *msys*|*MINGW*|*cygwin*) win=1;; esac
[ -f "$yapi/engine_editor.exe" ] && win=1
ek=""; [ "$win" = 1 ] && ek=".exe"

for t in engine_demo engine_editor engine_sahnec engine_texpack engine_clodbake; do
  [ -f "$kok/$t$ek" ] || hata "Pakette $t$ek yok."
done
ls "$kok"/assets/fonts/*.ttf >/dev/null 2>&1 || hata "Pakette assets/fonts/*.ttf yok."
[ -d "$kok/tests/assets" ] || hata "Pakette tests/assets/ yok."
[ -f "$kok/OKUBENI.md" ]   || hata "Pakette OKUBENI.md yok."
# Guncelleyicinin manifesti. Icerigini tools/paket_manifest.py olcer (package.sh
# --denetle); burada yalniz VARLIK: dosyasiz paket guncelleyicide Disabled olur.
[ -s "$kok/SURUM.txt" ]    || hata "Pakette SURUM.txt yok."
[ -s "$kok/DOSYALAR.txt" ] || hata "Pakette DOSYALAR.txt yok."

if [ "$win" = 1 ]; then
  # GEREKSINIM **YAPI AGACINDAN** TURETILIR, PAKETTEN DEGIL.
  #
  # Iki yanlis surum denendi, ikisi de olculdu:
  #  1) Pakette "/mingw64" onekiyle suzmek: DLL'ler .exe'nin YANINA
  #     kopyalandigi icin ldd onlari paket dizininden cozuyor, hicbir yol
  #     /mingw64 ile baslamiyor -> tam ve dogru bir paket "olcum yapilamadi"
  #     diye KIRMIZI donuyordu (v0.1.1 kosumu).
  #  2) Ada gore olcup sistem DLL'lerini ELLE yazilmis listeyle elemek: liste
  #     eksikti ve combase / KERNELBASE / msvcp_win / ucrtbase / win32u /
  #     wintypes pakette araniyordu. Bunlar Windows'un KENDI DLL'leri; pakete
  #     konmamalari DOGRU. Yine kirmizi dondu (v0.1.2 kosumu). Liste uzatmak
  #     cozum degil: runner imaji degistikce yeniden kirilir.
  #
  # Dogru ayrim KAYNAK YOLUDUR, ama YAPI agacinda sorulmali: orada ldd her
  # DLL'i gercek yerinden cozer (MinGW -> /mingw64/bin, sistem -> /c/Windows).
  # Pakette sormak bu bilgiyi yok eder.
  gerekli="$(for exe in "$yapi"/*.exe; do
               ldd "$exe" 2>/dev/null | awk '/=>/ && $3 ~ /^\/mingw64\// {print $1}'
             done | sort -u)"
  echo "Pakete girmesi gereken MinGW DLL'leri:"
  echo "${gerekli:-(hicbiri)}" | sed 's/^/  /'

  # POZITIF KONTROL: ikililer C++ calisma zamanina DINAMIK bagli, yani bu
  # liste asla bos olamaz. Bossa olcum yapilamamistir (ldd yok, yapi dizini
  # bos, yol yanlis...) ve sessizce yesil gecmek YASAK.
  if [ -z "$gerekli" ]; then
    hata "$yapi/*.exe icin /mingw64 altindan cozulen HIC DLL yok — olcum yapilamadi (ldd calisti mi? $yapi dolu mu?)."
  else
    while IFS= read -r ad; do
      [ -n "$ad" ] || continue
      [ -f "$kok/$(basename "$ad")" ] || hata "Pakette ithal edilen $(basename "$ad") yok — bu paket MSYS2 disinda calismaz."
    done <<< "$gerekli"
  fi
fi

if [ "$eksik" -ne 0 ]; then
  echo "Paket agaci:"
  find "$kok" -maxdepth 2 | sort
  exit 1
fi
echo "Paket yerlesimi tamam."
