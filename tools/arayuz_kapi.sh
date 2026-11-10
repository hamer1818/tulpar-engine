#!/usr/bin/env bash
# Arayuz surucusu kapisi (docs/ARAYUZ-SURUCUSU.md): penceresiz editor tests/arayuz/temel.txt
# senaryosunu kosar (menu, hiyerarsi tiklamasi, Delete/Ctrl+Z, metin girisi) — cikis 0 olmali.
# tests/arayuz/kaydet.txt GECICI KOPYA sahneyle: iki Ctrl+S, yol unutulmamali, dosya 7 varlikla yazilmali.
# POZITIF KONTROL: tests/arayuz/pozitif_kontrol.txt (olmayan oge) cikis 1 + HATA satiri vermeli.
#   tools/arayuz_kapi.sh [cikti-dizini]
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
ED="${ED:-$KOK/yapi/engine_editor}"
D="${1:-$(mktemp -d)}"; mkdir -p "$D/temel" "$D/pk" "$D/kaydet"
[ -x "$ED" ] || { echo "HATA: $ED yok"; exit 2; }
kos() {  # kos <senaryo> <dizin> [editor argumanlari...]
  local s="$1" d="$2"; shift 2
  ( cd "$KOK" && DISPLAY= WAYLAND_DISPLAY= TULPAR_ENGINE_HEADLESS=60 timeout 300 "$ED" --size 1600x900 \
      --surucu "$KOK/tests/arayuz/$s" --surucu-dizin "$d" "$@" > "$d/editor.log" 2>&1 )
}
# Kaydet senaryosu depodaki sahneye YAZMASIN: kaynaklariyla birlikte gecici kopya.
cp -r "$KOK/tests/assets" "$D/kaydet/assets"
kos temel.txt "$D/temel" & p1=$!
kos pozitif_kontrol.txt "$D/pk" & p2=$!
kos kaydet.txt "$D/kaydet" --scene "$D/kaydet/assets/editor.sahne" & p3=$!
wait $p1; r1=$?; wait $p2; r2=$?; wait $p3; r3=$?
kirmizi=0
n=$(grep -c '^\.son .* tamam' "$D/temel/yanit.txt" 2>/dev/null || echo 0)
if [ $r1 = 0 ] && [ -s "$D/temel/menu.png" ] && [ -s "$D/temel/secili.png" ]; then
  echo "  temel senaryo: $n komut tamam, ekranlar $D/temel/{menu,secili}.png"
else
  echo "  KIRMIZI temel senaryo (cikis $r1):"; grep -E 'HATA|^> ' "$D/temel/yanit.txt" 2>/dev/null | tail -4 | sed 's/^/      /'
  tail -5 "$D/temel/editor.log" | sed 's/^/      /'; kirmizi=1
fi
nk=$(grep -c '^nesne ' "$D/kaydet/assets/editor.sahne" 2>/dev/null || echo 0)
if [ $r3 = 0 ] && [ "$nk" = 7 ]; then
  echo "  kaydet senaryosu: iki Ctrl+S, yol korundu, dosyada $nk varlik"
else
  echo "  KIRMIZI kaydet senaryosu (cikis $r3, dosyada $nk varlik):"; grep -E 'HATA|^> ' "$D/kaydet/yanit.txt" 2>/dev/null | tail -4 | sed 's/^/      /'
  kirmizi=1
fi
if [ $r2 = 1 ] && grep -q 'HATA: "Boyle Bir Menu Ogesi Yok"' "$D/pk/yanit.txt" 2>/dev/null; then
  echo "  pozitif kontrol: olmayan oge dustu (cikis 1)"
else
  echo "  KAPI BOZUK: olmayan oge dusmedi (cikis $r2)"; kirmizi=1
fi
[ $kirmizi = 0 ] && echo "ARAYUZ SURUCUSU: YESIL" || echo "ARAYUZ SURUCUSU: KIRMIZI"
exit $kirmizi
