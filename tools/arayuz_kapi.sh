#!/usr/bin/env bash
# Arayuz surucusu kapisi (docs/ARAYUZ-SURUCUSU.md): penceresiz editor tests/arayuz/temel.txt
# senaryosunu kosar (menu, hiyerarsi tiklamasi, Delete/Ctrl+Z, metin girisi) — cikis 0 olmali.
# tests/arayuz/hata_satiri.txt (U10 B3): bozuk betik -> Oynat -> "Kodu ac (satir 3)" -> Kod paneli o satirda.
# tests/arayuz/kod.txt (U10 B2): cift tik Kod panelinde acar, panelden nesneye ekler, Ctrl+S yazar.
# tests/arayuz/betik_{yeni,eski}.txt (U10 B1): atanan betik oyunda GERCEKTEN calisir; eski projede editor baglar.
#   Pozitif kontrol: baglanmamis eski projede ayni betik CALISMAZ ("betik kancasi YOK"). tulpar yoksa ATLANDI.
# tests/arayuz/ornek.txt: karsilamadaki Ornek: Bilardo gecici oyun dizinine uretilip acilmali.
# tests/arayuz/kaydet.txt GECICI KOPYA sahneyle: iki Ctrl+S, yol unutulmamali, dosya 7 varlikla yazilmali.
# POZITIF KONTROL: tests/arayuz/pozitif_kontrol.txt (olmayan oge) cikis 1 + HATA satiri vermeli.
#   tools/arayuz_kapi.sh [cikti-dizini]
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
ED="${ED:-$KOK/yapi/engine_editor}"
D="${1:-$(mktemp -d)}"; mkdir -p "$D/temel" "$D/pk" "$D/kaydet" "$D/ornek"
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
# Ornek bilardo kullanicinin ~/TulparOyunlari'na DEGIL gecici dizine uretilsin.
TULPAR_OYUNLAR_DIZINI="$D/ornek/oyunlar" kos ornek.txt "$D/ornek" --karsilama & p4=$!
wait $p1; r1=$?; wait $p2; r2=$?; wait $p3; r3=$?; wait $p4; r4=$?
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
ob="$D/ornek/oyunlar/Ornek_Bilardo"
if [ $r4 = 0 ] && [ -s "$ob/assets/bilardo.sahneb" ] && grep -q 'func oto_nisan' "$ob/oyun.tpr" 2>/dev/null; then
  echo "  ornek senaryosu: Ornek: Bilardo uretildi + acildi (41 varlik), bilardo.sahneb derlendi"
else
  echo "  KIRMIZI ornek senaryosu (cikis $r4):"; grep -E 'HATA|^> ' "$D/ornek/yanit.txt" 2>/dev/null | tail -4 | sed 's/^/      /'
  ls "$ob" "$ob/assets" 2>&1 | head -6 | sed 's/^/      /'; kirmizi=1
fi
if [ $r2 = 1 ] && grep -q 'HATA: "Boyle Bir Menu Ogesi Yok"' "$D/pk/yanit.txt" 2>/dev/null; then
  echo "  pozitif kontrol: olmayan oge dustu (cikis 1)"
else
  echo "  KAPI BOZUK: olmayan oge dusmedi (cikis $r2)"; kirmizi=1
fi
# --- U10 B1: betik baglama (yeni ve eski proje) ---
betik_proje() {  # betik_proje <dizin> : arena projesi + duvara dondur.tpr
  "$ED" --yeni-oyun "$1" --kimlik bk --onek BK --tr "Betik Kapisi" --en "Script Gate" > /dev/null 2>&1 || return 1
  mkdir -p "$1/assets/betikler"
  printf 'func dondur_baslat(id) {\n    logla("BETIK_KAPI_CALISTI " + sahne_adi(id));\n}\n' > "$1/assets/betikler/dondur.tpr"
  python3 - "$1/assets/arena.sahne" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
s=s.replace('nesne "duvar_dogu"\n','nesne "duvar_dogu"\n  betik "betikler/dondur.tpr" etkin\n',1)
open(p,'w').write(s)
PY
}
oyun_kos() {  # oyun_kos <proje> <gunluk>
  ( cd "$1" && TULPAR_ENGINE_DIR="$KOK" bash tools/ortam.sh > /dev/null 2>&1; \
    DISPLAY= WAYLAND_DISPLAY= TULPAR_ENGINE_HEADLESS=40 timeout 200 tulpar --ext "$KOK/yapi/tulpar-ext" oyun.tpr ) > "$2" 2>&1
}
if ! command -v tulpar > /dev/null; then
  echo "  ATLANDI: betik baglama — tulpar kurulu degil (oyun kosturulamaz)"
else
  B="$D/betik"; mkdir -p "$B/ui_yeni" "$B/ui_eski" "$B/ui_kod" "$B/ui_hata"
  betik_proje "$B/yeni" && betik_proje "$B/eski" && betik_proje "$B/kod" && betik_proje "$B/hata" || { echo "  KIRMIZI betik: proje uretilemedi"; kirmizi=1; }
  printf 'func dondur_baslat(id) {\n    logla("x");\n    y = = 3;\n}\n' > "$B/hata/assets/betikler/dondur.tpr"
  ( cd "$B/hata" && TULPAR_ENGINE_DIR="$KOK" bash tools/ortam.sh > /dev/null 2>&1 )
  sed -i '/^import "assets\/betikler";/d' "$B/eski/oyun.tpr"   # eski proje: baglanti satiri YOK
  oyun_kos "$B/eski" "$B/eski_once.log"                          # pozitif kontrol: betik calismamali
  ( kos betik_yeni.txt "$B/ui_yeni" --scene "$B/yeni/assets/arena.sahne" ); ry=$?
  ( kos betik_eski.txt "$B/ui_eski" --scene "$B/eski/assets/arena.sahne" ); re=$?
  ( kos kod.txt "$B/ui_kod" --scene "$B/kod/assets/arena.sahne" ); rk=$?
  ( kos hata_satiri.txt "$B/ui_hata" --scene "$B/hata/assets/arena.sahne" ); rh=$?
  if [ $rh = 0 ]; then echo "  hata satiri: Oynat -> hata penceresi -> Kodu ac -> Kod paneli 3. satirda"
  else echo "  KIRMIZI hata satiri (cikis $rh):"; grep -E 'HATA|> ' "$B/ui_hata/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  oyun_kos "$B/yeni" "$B/yeni.log"; oyun_kos "$B/eski" "$B/eski_sonra.log"
  if [ $rk = 0 ] && grep -q 'KOD_PANELI_YAZDI' "$B/kod/assets/betikler/dondur.tpr"; then
    echo "  kod paneli: cift tik acti, nesneye ekledi, Ctrl+Z sahneyi geri almadi, Ctrl+S yazdi"
  else echo "  KIRMIZI kod paneli (cikis $rk):"; grep -E 'HATA|> ' "$B/ui_kod/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  if ! grep -q 'BETIK_KAPI_CALISTI duvar_dogu' "$B/eski_once.log" && grep -q 'betik kancasi YOK' "$B/eski_once.log"; then
    echo "  betik pozitif kontrol: baglanmamis projede betik calismadi (kanca YOK)"
  else echo "  KAPI BOZUK: baglanmamis projede betik calisti ya da hata yok"; kirmizi=1; fi
  if [ $ry = 0 ] && grep -q 'import "assets/betikler/dondur";' "$B/yeni/assets/betikler.tpr" && grep -q 'BETIK_KAPI_CALISTI duvar_dogu' "$B/yeni.log"; then
    echo "  betik (yeni proje): Derle baglandi, oyunda calisti"
  else echo "  KIRMIZI betik (yeni proje, cikis $ry):"; grep -E 'HATA|kanca' "$B/yeni.log" | head -3 | sed 's/^/      /'; kirmizi=1; fi
  if [ $re = 0 ] && grep -q '^import "assets/betikler";' "$B/eski/oyun.tpr" && grep -q 'BETIK_KAPI_CALISTI duvar_dogu' "$B/eski_sonra.log"; then
    echo "  betik (eski proje): editor sordu, Bagla satiri ekledi, oyunda calisti"
  else echo "  KIRMIZI betik (eski proje, cikis $re):"; grep -E 'HATA|> ' "$B/ui_eski/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
fi
[ $kirmizi = 0 ] && echo "ARAYUZ SURUCUSU: YESIL" || echo "ARAYUZ SURUCUSU: KIRMIZI"
exit $kirmizi
