#!/usr/bin/env bash
# Editor UX olcumu (docs/EDITOR-UYGULAMA-PLANI.md U0): dort cozunurlukte PARALEL headless editor,
# her biri `--ux-rapor` ile kesik metin / tasan kaplama / 3B gorunum orani basar.
#
#   tools/editor_ux.sh                 # tablo (olcum)
#   tools/editor_ux.sh --kapi          # esiklere gore kirmizi/yesil (U1'den sonra: kesik 0,
#                                      #  kaplama tasmasi 0 kare, 1920x1080'de gorunum >= 0.45)
# Gorunum esigi 0.45 (planin ilk tahmini 0.55 idi): Gorunum panelinin kendi uc arac satiri ~95 px
# yer kapliyor; alt serit kaldirilip 1920x1080'de 0.349 -> 0.486 olculdu (2026-11-05).
#   UX_KARE=60 UX_DIZIN=<dizin>        # kare sayisi / raporlarin kalacagi dizin
# Kapinin pozitif kontrolu C++ testinde: editor_ux_imgui_kancasi_kesik_metni_gorur (ImGui yamasi
# kaybolursa metin gorulmez ve test KIRMIZI).
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
ED="${ED:-$KOK/yapi/engine_editor}"
KARE="${UX_KARE:-60}"
DIZIN="${UX_DIZIN:-$(mktemp -d)}"; mkdir -p "$DIZIN"
KAPI=0; [ "${1:-}" = "--kapi" ] && KAPI=1
[ -x "$ED" ] || { echo "HATA: $ED yok (cmake --build yapi)"; exit 2; }
COZ=(1280x720 1366x768 1920x1080 2560x1440)
for r in "${COZ[@]}"; do
  ( cd "$KOK" && DISPLAY= "$ED" --headless "$KARE" --size "$r" --ux-rapor "$DIZIN/$r.txt" > "$DIZIN/$r.log" 2>&1; echo "rc=$?" >> "$DIZIN/$r.log" ) &
done
# U3 karsilama ekrani: iki kucuk cozunurlukte kesik metin + "Olustur" isleyicisi (fabrika + sahne
# yukleme + derleme) uctan uca; POZITIF KONTROL: dolu dizine olusturma reddedilmeli.
for r in 1280x720 1366x768; do
  ( cd "$KOK" && DISPLAY= "$ED" --headless 20 --size "$r" --karsilama --ux-rapor "$DIZIN/k_$r.txt" > "$DIZIN/k_$r.log" 2>&1 ) &
done
# U5 Basit kip: gorunen metinde teknik terim YOK; Gelismis'te VAR (pozitif kontrol: olcum gercekten
# Stats'i goruyor). Not: durum cubugu ImGui metni degil (dogrudan cizim), kanca onu gormez.
for k in basit gelismis; do
  ( cd "$KOK" && DISPLAY= "$ED" --headless 60 --kip "$k" --ux-rapor "$DIZIN/kip_$k.txt" > "$DIZIN/kip_$k.log" 2>&1 ) &
done
mkdir -p "$DIZIN/dolu" && echo x > "$DIZIN/dolu/var.txt"
( cd "$KOK" && DISPLAY= "$ED" --headless 20 --karsilama-yeni "$DIZIN/yeni_oyun" > "$DIZIN/kyeni.log" 2>&1 ) &
( cd "$KOK" && DISPLAY= "$ED" --headless 20 --karsilama-yeni "$DIZIN/dolu" > "$DIZIN/kdolu.log" 2>&1 ) &
wait
al() { sed -n "s/.* $2=\([0-9.]*\).*/\1/p" <<< "$1"; }
kirmizi=0
printf '%-10s %6s %14s %12s %8s\n' cozunurluk kesik kaplama_tasan gorunum metin
for r in "${COZ[@]}"; do
  s="$(head -1 "$DIZIN/$r.txt" 2>/dev/null)"
  if [ -z "$s" ]; then echo "$r: RAPOR YOK ($(tail -2 "$DIZIN/$r.log" | tr '\n' ' '))"; kirmizi=1; continue; fi
  k=$(al "$s" kesik); t=$(al "$s" kaplama_tasan_kare); g=$(al "$s" gorunum_orani); m=$(al "$s" metin)
  printf '%-10s %6s %14s %12s %8s\n' "$r" "$k" "$t" "$g" "$m"
  if [ "$KAPI" = 1 ]; then
    [ "$k" = 0 ] || { echo "  KIRMIZI $r: $k kesik metin"; grep '^\[ux\] kesik' "$DIZIN/$r.txt" | head -5 | sed 's/^/    /'; kirmizi=1; }
    [ "$t" = 0 ] || { echo "  KIRMIZI $r: kaplama $t karede gorunumden tasti"; kirmizi=1; }
    if [ "$r" = 1920x1080 ] && awk "BEGIN{exit !($g < 0.45)}"; then echo "  KIRMIZI $r: 3B gorunum orani $g < 0.45"; kirmizi=1; fi
  fi
done
YASAK='setpass|arena|tahsis|tick|\(tris\)|verts|piksel|overdraw|batch|k\xC3\xBCme'
yb=$(grep '^\[ux\] metin' "$DIZIN/kip_basit.txt" 2>/dev/null | grep -ciE "$(printf "$YASAK")")
yg=$(grep '^\[ux\] metin' "$DIZIN/kip_gelismis.txt" 2>/dev/null | grep -ciE "$(printf "$YASAK")")
if [ "${yb:-x}" = 0 ] && [ "${yg:-0}" -gt 0 ]; then echo "  Basit kip: teknik terim 0 (Gelismis'te $yg — olcum Stats'i goruyor)"
else echo "  KIRMIZI Basit kip: teknik terim ${yb:-rapor yok} (Gelismis ${yg:-?})"; grep '^\[ux\] metin' "$DIZIN/kip_basit.txt" | grep -iE "$(printf "$YASAK")" | head -3; [ "$KAPI" = 1 ] && kirmizi=1; fi
for r in 1280x720 1366x768; do
  k=$(al "$(head -1 "$DIZIN/k_$r.txt" 2>/dev/null)" kesik)
  if [ "${k:-x}" != 0 ]; then echo "  KIRMIZI karsilama $r: kesik ${k:-rapor yok}"; [ "$KAPI" = 1 ] && kirmizi=1
  else echo "  karsilama $r: kesik 0"; fi
done
if grep -q '^\[engine_editor\] karsilama: yeni oyun .* derleme evet' "$DIZIN/kyeni.log" && [ -f "$DIZIN/yeni_oyun/assets/arena.sahneb" ]; then
  echo "  karsilama Olustur: proje + sahne + .sahneb (deneme_oyunu)"
else echo "  KIRMIZI karsilama Olustur: $(grep -m1 'karsilama' "$DIZIN/kyeni.log")"; [ "$KAPI" = 1 ] && kirmizi=1; fi
if grep -q '^\[engine_editor\] karsilama: HATA .*bo' "$DIZIN/kdolu.log" && ! grep -q 'derleme evet' "$DIZIN/kdolu.log"; then
  echo "  pozitif kontrol: dolu dizine Olustur reddedildi"
else echo "  KIRMIZI pozitif kontrol: dolu dizin reddedilmedi"; [ "$KAPI" = 1 ] && kirmizi=1; fi
# U2: panel duzeni proje (sahne) dizinine degil kullanici ayar dizinine (penceresizde gecici dizin).
ay="$(grep -m1 '^\[engine_editor\] ayar: panel duzeni' "$DIZIN/1280x720.log" | sed 's/.*panel duzeni //')"
if [ -z "$ay" ]; then echo "  ayar yolu: satir yok"; [ "$KAPI" = 1 ] && kirmizi=1
elif [[ "$ay" == "$KOK/tests/"* ]]; then echo "  KIRMIZI ayar yolu proje dizininde: $ay"; [ "$KAPI" = 1 ] && kirmizi=1
else echo "  ayar yolu: $ay (proje disinda)"; fi
echo "raporlar: $DIZIN"
[ "$KAPI" = 1 ] && { [ $kirmizi = 0 ] && echo "EDITOR UX: YESIL" || echo "EDITOR UX: KIRMIZI"; }
exit $kirmizi
