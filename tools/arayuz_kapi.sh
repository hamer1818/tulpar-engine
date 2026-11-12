#!/usr/bin/env bash
# Arayuz surucusu kapisi (docs/ARAYUZ-SURUCUSU.md): penceresiz editor tests/arayuz/temel.txt
# senaryosunu kosar (menu, hiyerarsi tiklamasi, Delete/Ctrl+Z, metin girisi) — cikis 0 olmali.
# tests/arayuz/sade_dil.txt (U11 A1): Basit kipte Konsol sade (motor satiri, uzun yol, fps yok); Gelismis'te
#   gorunur (pozitif kontrol). tulpar yoksa ATLANDI.
# tests/arayuz/hazirlar.txt (U10 B4b): Dondur + Salla hazirlari oyunda hareket ettirir (sonda _bitir kancasi);
#   pozitif kontrol: hiz/yukseklik 0 -> yerinde. tulpar yoksa ATLANDI.
# tests/arayuz/kaynaklar.txt (U11 B5): bos Kaynaklar eylemli, Model ice aktar kopyalar; kontrol: once karo yok.
# tests/arayuz/ilk_adimlar{,_gizli}.txt (U11 B4): kart sablonu anlatir, adimlar isaretlenir, gizle kalici.
# tests/arayuz/izgara.txt (U11 C4): Izgara dugmesi gercekten cizer; kontrol: kapaliyken goruntu duz.
# tests/arayuz/sahne_ozeti.txt (U11 C5): secimsiz Ozellikler sahne ozeti; kontrol: ayarlar Dunya sekmesinde.
# tests/arayuz/ekle_menu.txt (U11 B2): Basit kipte sade "+"/Ekle, secim varken ekleme; kontrol: Gelismis tam liste.
# tests/arayuz/tercihler{,_kalici}.txt (U11 B1): yazi boyu kalici (menu ogesi boyu buyur), otomatik kayit, duzen
#   sifirla; kontrol: varsayilan boyla ilk kosum + aralik dolmadan "otomatik kaydedildi" yok.
# tests/arayuz/karsilama.txt (U11 A3): sablon karti + Ingilizce ad bos -> Turkce ad; kontrol: ad yokken uyari satiri.
# tests/arayuz/ana_betik.txt (U10 B7): Oynat > Ana betigi ac -> oyun.tpr Kod panelinde, "nesneye ekle" yok (kontrol:
#   siradan betikte var).
# tests/arayuz/betik_karti.txt (U10 B5): kart kancalari isaretler, bagli degil -> Bagla -> bagli; Kod panelinde
#   _guncelle eklenince "her karede" isaretlenir. Pozitif kontrol senaryonun icinde: ayni satir once cizgili.
# tests/arayuz/yeni_betik.txt (U10 B4): Davranis ekle -> hazir + Turkce ad -> olusan betik oyunda calisir.
# tests/arayuz/hata_satiri.txt (U10 B3): bozuk betik -> Oynat -> "Kodu ac (satir 3)" -> Kod paneli o satirda.
# tests/arayuz/ana_kod_nesne.txt: oyun.tpr nesneyi adiyla aniyorsa Ozellikler karti; anmiyorsa yok (kontrol).
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
# --- U11 B5: bos Kaynaklar + model ice aktar (tulpar gerekmez) ---
KY="$D/kaynak"; mkdir -p "$KY/ui"
"$ED" --yeni-oyun "$KY/p" --kimlik kay --onek KY --tr "Kaynak" --en "Asset" > /dev/null 2>&1
TULPAR_DOSYA_SECICI="sabit:$KOK/tests/assets/checker_cube.gltf" kos kaynaklar.txt "$KY/ui" --scene "$KY/p/assets/arena.sahne" --kip basit; rky=$?
if [ $rky = 0 ] && cmp -s "$KOK/tests/assets/checker_cube.gltf" "$KY/p/assets/checker_cube.gltf"; then
  echo "  kaynaklar: bos durum eylemli, Model ice aktar -> checker_cube.gltf proje klasorunde ve listede"
else echo "  KIRMIZI kaynaklar (cikis $rky):"; grep -E 'HATA|> ' "$KY/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 B4: Ilk adimlar karti (yeni fabrika projesi; tulpar gerekmez) ---
IA="$D/ilk"; mkdir -p "$IA/ui1" "$IA/ui2"
"$ED" --yeni-oyun "$IA/p" --kimlik ilk --onek IL --tr "Ilk Adim" --en "First Step" > /dev/null 2>&1
TULPAR_EDITOR_AYAR_DIZINI="$IA/ayar" kos ilk_adimlar.txt "$IA/ui1" --scene "$IA/p/assets/arena.sahne" --kip basit; ria1=$?
TULPAR_EDITOR_AYAR_DIZINI="$IA/ayar" kos ilk_adimlar_gizli.txt "$IA/ui2" --scene "$IA/p/assets/arena.sahne" --kip basit; ria2=$?
if [ $ria1 = 0 ] && [ $ria2 = 0 ] && grep -qE "^26 .*/p$" "$IA/ayar/ilk_adimlar.txt" 2>/dev/null; then
  echo "  ilk adimlar: kart sablonu anlatti, nesne ekle + kaydet isaretlendi, gizle kalici (bitler 26)"
else echo "  KIRMIZI ilk adimlar (cikis $ria1/$ria2):"; grep -hE 'HATA|> ' "$IA"/ui*/yanit.txt 2>/dev/null | tail -3 | sed 's/^/      /'; cat "$IA/ayar/ilk_adimlar.txt" 2>&1 | head -2 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 C4: zemin izgarasi gercekten cizilir (tulpar gerekmez) ---
IZ="$D/izgara"; mkdir -p "$IZ/ui"; cp -r "$KOK/tests/assets" "$IZ/assets"
kos izgara.txt "$IZ/ui" --scene "$IZ/assets/editor.sahne" --kip basit; riz=$?
izs=$(python3 - "$IZ/ui/izgara_acik.png" "$IZ/ui/izgara_kapali.png" <<'PYI' 2>/dev/null
import sys
from PIL import Image, ImageStat
v=[]
for f in sys.argv[1:3]:
    im=Image.open(f).convert('L'); w,h=im.size
    v.append(ImageStat.Stat(im.crop((int(w*0.22),int(h*0.2),int(w*0.75),int(h*0.9)))).stddev[0])
print('%.1f %.1f' % (v[0], v[1]))
PYI
)
ia=${izs%% *}; ik=${izs##* }
if [ $riz = 0 ] && [ -n "$izs" ] && awk "BEGIN{exit !($ia > $ik + 4.0)}"; then
  echo "  izgara: acikken gorunum dokulu (sapma $ia), kapaliyken duz ($ik)"
else echo "  KIRMIZI izgara (cikis $riz, sapma acik/kapali: ${izs:-olculemedi})"; grep -E 'HATA|> ' "$IZ/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 C5: secimsiz Ozellikler = sahne ozeti; gizmolar Gorunum menusunde (tulpar gerekmez) ---
SO="$D/ozet"; mkdir -p "$SO/ui"; cp -r "$KOK/tests/assets" "$SO/assets"
kos sahne_ozeti.txt "$SO/ui" --scene "$SO/assets/editor.sahne" --kip basit; rso=$?
if [ $rso = 0 ]; then echo "  sahne ozeti: secimsiz Ozellikler dunya ayarlarini tekrarlamiyor; Dunya sekmesinde var (kontrol); gizmolar Gorunum'de"
else echo "  KIRMIZI sahne ozeti (cikis $rso):"; grep -E 'HATA|> ' "$SO/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 B2: sade "+" / ana menu "Ekle" (tulpar gerekmez) ---
EK="$D/ekle"; mkdir -p "$EK/ui"; cp -r "$KOK/tests/assets" "$EK/assets"
kos ekle_menu.txt "$EK/ui" --scene "$EK/assets/editor.sahne" --kip basit; rek=$?
if [ $rek = 0 ]; then echo "  ekle menusu: Basit kipte sade agac, secim varken Ekle > Sekiller > Kup eklendi; Gelismis'te tam liste (kontrol)"
else echo "  KIRMIZI ekle menusu (cikis $rek):"; grep -E 'HATA|> ' "$EK/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 B1: Tercihler (yazi boyu kalici, otomatik kayit, duzen sifirla; tulpar gerekmez) ---
TR="$D/tercih"; mkdir -p "$TR/ui1" "$TR/ui2" "$TR/ui0" "$TR/ayar"
cp -r "$KOK/tests/assets" "$TR/assets"
TULPAR_EDITOR_AYAR_DIZINI="$TR/ayar0" kos tercihler_kalici.txt "$TR/ui0" --scene "$TR/assets/editor.sahne"; r0=$?  # kontrol: varsayilan boy
TULPAR_EDITOR_AYAR_DIZINI="$TR/ayar" TULPAR_EDITOR_OTO_KAYIT_SN=2 kos tercihler.txt "$TR/ui1" --scene "$TR/assets/editor.sahne"; r1=$?
TULPAR_EDITOR_AYAR_DIZINI="$TR/ayar" kos tercihler_kalici.txt "$TR/ui2" --scene "$TR/assets/editor.sahne"; r2=$?
boy() { grep -m1 '^"Dosya" @' "$1" | sed -E 's/.* ([0-9]+)x([0-9]+)\).*/\2/'; }
b0=$(boy "$TR/ui0/yanit.txt"); b2=$(boy "$TR/ui2/yanit.txt"); nk=$(grep -c '^nesne ' "$TR/assets/editor.sahne")
# U11 C6: tema kalici — ikinci acilista panel zemini ACIK; kontrol kosusunda (varsayilan) KOYU.
tz=$(python3 - "$TR/ui0/tema.png" "$TR/ui2/tema.png" <<'PYT' 2>/dev/null
import sys
from PIL import Image
v=[]
for f in sys.argv[1:3]:
    im=Image.open(f).convert('L'); w,h=im.size; v.append(im.getpixel((int(w*0.08), int(h*0.45))))
print(v[0], v[1])
PYT
)
t0=${tz%% *}; t2=${tz##* }
if [ $r0 = 0 ] && [ $r1 = 0 ] && [ $r2 = 0 ] && grep -q '^yazi = 115$' "$TR/ayar/tercihler.txt" && grep -q '^oto_kayit_dk = 1$' "$TR/ayar/tercihler.txt" \
   && [ "$nk" = 9 ] && [ -n "$b0" ] && [ -n "$b2" ] && [ "$b2" -gt "$b0" ] && grep -q '^tema = 1$' "$TR/ayar/tercihler.txt" \
   && [ -n "$tz" ] && [ "$t2" -gt 150 ] && [ "$t0" -lt 60 ]; then
  echo "  tercihler: Buyuk + 1 dk + Acik tema yazildi, kup otomatik kaydedildi (9 varlik), menu boyu $b0 -> $b2 px, panel zemini $t0 -> $t2 (kalici), duzen sifirlandi"
else echo "  KIRMIZI tercihler (cikis $r0/$r1/$r2, varlik $nk, boy $b0 -> $b2):"; grep -hE 'HATA|> ' "$TR"/ui*/yanit.txt 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U11 A3: karsilama formu (sablon karti, Ingilizce ad istege bagli; tulpar gerekmez) ---
KR="$D/karsilama"; mkdir -p "$KR/ui"
TULPAR_OYUNLAR_DIZINI="$KR/oyunlar" kos karsilama.txt "$KR/ui" --karsilama --kip basit; rkr=$?
kp="$KR/oyunlar/kart_deneme"
if [ $rkr = 0 ] && grep -q 'func oto_nisan' "$kp/oyun.tpr" 2>/dev/null && head -1 "$kp/oyun.tpr" | grep -q '^// Kart Deneme — Kart Deneme$'; then
  echo "  karsilama: Bilardo karti secildi, Ingilizce ad bos -> Turkce ad kullanildi, proje uretildi"
else echo "  KIRMIZI karsilama (cikis $rkr):"; grep -E 'HATA|> ' "$KR/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; head -1 "$kp/oyun.tpr" 2>&1 | sed 's/^/      /'; kirmizi=1; fi
# --- U10 B7: Oynat > Ana betigi ac (tulpar gerekmez) ---
A="$D/ana"; mkdir -p "$A/ui"
if betik_proje "$A/p"; then
  ( kos ana_betik.txt "$A/ui" --scene "$A/p/assets/arena.sahne" ); rab=$?
  if [ $rab = 0 ]; then echo "  ana betik: Oynat menusu oyun.tpr'yi Kod panelinde acti, nesneye ekle onermedi; siradan betikte onerdi"
  else echo "  KIRMIZI ana betik (cikis $rab):"; grep -E 'HATA|> ' "$A/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
else echo "  KIRMIZI ana betik: proje uretilemedi"; kirmizi=1; fi
# --- Ana kodun adla yonettigi nesne: Ozellikler'de bilgi karti (tulpar gerekmez) ---
AK="$D/anakod"; mkdir -p "$AK/ui"
"$ED" --yeni-oyun "$AK/p" --kimlik ak --onek AK --tr "Ana Kod" --en "Main Code" > /dev/null 2>&1
( kos ana_kod_nesne.txt "$AK/ui" --scene "$AK/p/assets/arena.sahne" ); rak=$?
if [ $rak = 0 ]; then echo "  ana kod nesnesi: oyuncu/parilti_1 secilince bilgi karti + oyun.tpr acilir; duvar_bati'da kart yok (kontrol)"
else echo "  KIRMIZI ana kod nesnesi (cikis $rak):"; grep -E 'HATA|> ' "$AK/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
# --- U10 B5: Betik karti (tulpar gerekmez: yalniz editor) ---
K="$D/kart"; mkdir -p "$K/ui"
if betik_proje "$K/p"; then
  sed -i '/^import "assets\/betikler";/d' "$K/p/oyun.tpr"     # bagli DEGIL baslasin; kart Bagla'yi gostermeli
  ( kos betik_karti.txt "$K/ui" --scene "$K/p/assets/arena.sahne" ); rkk=$?
  if [ $rkk = 0 ] && grep -q '^import "assets/betikler";' "$K/p/oyun.tpr" && grep -q 'func dondur_guncelle(id, dt)' "$K/p/assets/betikler/dondur.tpr"; then
    echo "  betik karti: baslarken isaretli, bagli degil -> Bagla -> bagli; _guncelle eklenince her karede isaretlendi"
  else echo "  KIRMIZI betik karti (cikis $rkk):"; grep -E 'HATA|> ' "$K/ui/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
else echo "  KIRMIZI betik karti: proje uretilemedi"; kirmizi=1; fi
oyun_kos() {  # oyun_kos <proje> <gunluk>
  ( cd "$1" && TULPAR_ENGINE_DIR="$KOK" bash tools/ortam.sh > /dev/null 2>&1; \
    DISPLAY= WAYLAND_DISPLAY= TULPAR_ENGINE_HEADLESS=40 timeout 200 tulpar --ext "$KOK/yapi/tulpar-ext" oyun.tpr ) > "$2" 2>&1
}
if ! command -v tulpar > /dev/null; then
  echo "  ATLANDI: betik baglama — tulpar kurulu degil (oyun kosturulamaz)"
else
  B="$D/betik"; mkdir -p "$B/ui_yeni" "$B/ui_eski" "$B/ui_kod" "$B/ui_hata" "$B/ui_yb"
  "$ED" --yeni-oyun "$B/yb" --kimlik yb --onek YB --tr "Yeni Betik" --en "New Script" > /dev/null 2>&1
  betik_proje "$B/yeni" && betik_proje "$B/eski" && betik_proje "$B/kod" && betik_proje "$B/hata" || { echo "  KIRMIZI betik: proje uretilemedi"; kirmizi=1; }
  printf 'func dondur_baslat(id) {\n    logla("x");\n    y = = 3;\n}\n' > "$B/hata/assets/betikler/dondur.tpr"
  ( cd "$B/hata" && TULPAR_ENGINE_DIR="$KOK" bash tools/ortam.sh > /dev/null 2>&1 )
  sed -i '/^import "assets\/betikler";/d' "$B/eski/oyun.tpr"   # eski proje: baglanti satiri YOK
  oyun_kos "$B/eski" "$B/eski_once.log"                          # pozitif kontrol: betik calismamali
  ( kos betik_yeni.txt "$B/ui_yeni" --scene "$B/yeni/assets/arena.sahne" ); ry=$?
  ( kos betik_eski.txt "$B/ui_eski" --scene "$B/eski/assets/arena.sahne" ); re=$?
  ( kos kod.txt "$B/ui_kod" --scene "$B/kod/assets/arena.sahne" ); rk=$?
  ( kos hata_satiri.txt "$B/ui_hata" --scene "$B/hata/assets/arena.sahne" ); rh=$?
  ( kos yeni_betik.txt "$B/ui_yb" --scene "$B/yb/assets/arena.sahne" ); rb=$?
  oyun_kos "$B/yb" "$B/yb.log"
  # --- U11 A1: Basit kipte sade dil (oynat/durdur sonrasi Konsol + goruntu) ---
  mkdir -p "$B/ui_sd"
  "$ED" --yeni-oyun "$B/sd" --kimlik sd --onek SD --tr "Sade Dil" --en "Plain Words" > /dev/null 2>&1
  ( kos sade_dil.txt "$B/ui_sd" --scene "$B/sd/assets/arena.sahne" --kip basit ); rsd=$?
  if [ $rsd = 0 ]; then echo "  sade dil: Basit kipte Konsol'da motor satiri / uzun yol / fps yok; Gelismis'te var (kontrol)"
  else echo "  KIRMIZI sade dil (cikis $rsd):"; grep -E 'HATA|> ' "$B/ui_sd/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  # --- U10 B4b: Dondur + Salla hazirlari oyunda GERCEKTEN hareket ettirir ---
  mkdir -p "$B/ui_hz"
  "$ED" --yeni-oyun "$B/hz" --kimlik hz --onek HZ --tr "Hazirlar" --en "Presets" > /dev/null 2>&1
  ( kos hazirlar.txt "$B/ui_hz" --scene "$B/hz/assets/arena.sahne" ); rhz=$?
  hb="$B/hz/assets/betikler"
  # Olcum kancasi (_bitir; hazirlarda yok): donen duvarin eski ucuna isin, sallanan duvarin yuksekligi.
  sonda() {
    printf '\nfunc dondur_kapi_bitir(id) {\n    float d = isin(sahne_ilk_x(id) + 7.0, 6.0, sahne_ilk_z(id), 0.0, -1.0, 0.0, 20.0, 0);\n    if (d >= 0.0 && isin_sahne() == id) { logla("KAPI_DONDUR uc_dolu"); } else { logla("KAPI_DONDUR uc_bos"); }\n}\n' >> "$hb/dondur_kapi.tpr"
    printf '\nfunc salla_kapi_bitir(id) {\n    float f = sahne_y(id) - sahne_ilk_y(id);\n    if (f * f > 0.0025) { logla("KAPI_SALLA oynadi"); } else { logla("KAPI_SALLA yerinde"); }\n}\n' >> "$hb/salla_kapi.tpr"
  }
  if [ $rhz = 0 ] && [ -f "$hb/dondur_kapi.tpr" ] && [ -f "$hb/salla_kapi.tpr" ]; then
    sonda
    oyun_kos "$B/hz" "$B/hz.log"
    # Pozitif kontrol: hiz / yukseklik 0 -> ayni sonda "yerinde" demeli.
    sed -i 's/^float dondur_kapi_HIZ = 90.0;/float dondur_kapi_HIZ = 0.0;/; ' "$hb/dondur_kapi.tpr"
    sed -i 's/^float salla_kapi_YUKSEKLIK = 0.5;/float salla_kapi_YUKSEKLIK = 0.0;/' "$hb/salla_kapi.tpr"
    oyun_kos "$B/hz" "$B/hz_pk.log"
  fi
  if [ $rhz = 0 ] && grep -q 'KAPI_DONDUR uc_bos' "$B/hz.log" && grep -q 'KAPI_SALLA oynadi' "$B/hz.log" && grep -q 'kapanis: .* hata 0,' "$B/hz.log" \
     && grep -q 'KAPI_DONDUR uc_dolu' "$B/hz_pk.log" && grep -q 'KAPI_SALLA yerinde' "$B/hz_pk.log"; then
    echo "  hazirlar: Dondur duvari cevirdi, Salla oynatti (hata 0); pozitif kontrol: hiz/yukseklik 0 -> ikisi de yerinde"
  else echo "  KIRMIZI hazirlar (cikis $rhz):"; grep -E 'HATA|> ' "$B/ui_hz/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'
    grep -hE 'KAPI_|hata:| hata [1-9]|kanca' "$B/hz.log" "$B/hz_pk.log" 2>/dev/null | head -6 | sed 's/^/      /'; kirmizi=1; fi
  if [ $rb = 0 ] && grep -q 'betik "betikler/deneme_davranis.tpr" etkin' "$B/yb/assets/arena.sahne" && grep -q 'deneme_davranis başladı: duvar_guney' "$B/yb.log"; then
    echo "  yeni betik: Davranis ekle -> Bos -> 'Deneme Davranis' -> deneme_davranis.tpr oyunda calisti"
  else echo "  KIRMIZI yeni betik (cikis $rb):"; grep -E 'HATA|> ' "$B/ui_yb/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; grep -E 'hata:|kanca' "$B/yb.log" | head -2 | sed 's/^/      /'; kirmizi=1; fi
  if [ $rh = 0 ]; then echo "  hata satiri: Oynat -> hata penceresi -> Kodu ac -> Kod paneli 3. satirda"
  else echo "  KIRMIZI hata satiri (cikis $rh):"; grep -E 'HATA|> ' "$B/ui_hata/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  oyun_kos "$B/yeni" "$B/yeni.log"; oyun_kos "$B/eski" "$B/eski_sonra.log"
  if [ $rk = 0 ] && grep -q 'KOD_PANELI_YAZDI' "$B/kod/assets/betikler/dondur.tpr"; then
    echo "  kod paneli: cift tik acti, nesneye ekledi, Ctrl+Z sahneyi geri almadi, Ctrl+S yazdi"
  else echo "  KIRMIZI kod paneli (cikis $rk):"; grep -E 'HATA|> ' "$B/ui_kod/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  # Kod paneli Tulpar'a gore yazar (girinti, Tab, }): kaydedilen dosyada dizilimi ara.
  mkdir -p "$B/ui_ky"
  if betik_proje "$B/ky"; then
    ( kos kod_yazim.txt "$B/ui_ky" --scene "$B/ky/assets/arena.sahne" ); rky2=$?
    if [ $rky2 = 0 ] && python3 - "$B/ky/assets/betikler/dondur.tpr" <<'PY2'
import sys
m = open(sys.argv[1], encoding='utf-8').read()
sys.exit(0 if 'func dondur_baslat(id) {\n    a\n    b\n}\n    t' in m else 1)
PY2
    then echo "  kod yazimi: Enter girintiyi korudu ({ sonrasi +4), } geri aldi, Tab 4 bosluk"
    else echo "  KIRMIZI kod yazimi (cikis $rky2):"; grep -E 'HATA|> ' "$B/ui_ky/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; sed -n 1,8p "$B/ky/assets/betikler/dondur.tpr" | cat -A | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod yazimi: proje uretilemedi"; kirmizi=1; fi
  # Oneri listesi + parantez kapatma (motor kutuphanesinden oneri; Tab acar; kapanisin ustunden atlanir).
  mkdir -p "$B/ui_kz"
  if betik_proje "$B/kz"; then
    ( kos kod_zeka.txt "$B/ui_kz" --scene "$B/kz/assets/arena.sahne" ); rkz=$?
    if [ $rkz = 0 ] && python3 - "$B/kz/assets/betikler/dondur.tpr" <<'PY3'
import sys
m = open(sys.argv[1], encoding='utf-8').read()
sys.exit(0 if 'func dondur_baslat(id) {\n    kure(0);\n    logla("x");\n' in m else 1)
PY3
    then echo "  kod onerisi: 'kur' -> kure() oneri listesi, Tab kabul + parantez, logla(\"x\") kapanislar kendiliginden, kapanista atlandi"
    else echo "  KIRMIZI kod onerisi (cikis $rkz):"; grep -E 'HATA|> ' "$B/ui_kz/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; sed -n 1,6p "$B/kz/assets/betikler/dondur.tpr" | cat -A | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod onerisi: proje uretilemedi"; kirmizi=1; fi
  # Oneri etkilesimi: klavye gezinme + Enter, fareyle satir secme, tek harfte Ctrl+Bosluk, Esc.
  mkdir -p "$B/ui_ke"
  if betik_proje "$B/ke"; then
    printf 'func qa_bir() {\n}\nfunc qa_iki(zzz_iki) {\n}\nfunc qa_uc() {\n}\nfunc dondur_baslat(id) {\n    logla("BETIK_KAPI_CALISTI " + sahne_adi(id));\n}\n' > "$B/ke/assets/betikler/dondur.tpr"
    ( kos kod_oneri_etkilesim.txt "$B/ui_ke" --scene "$B/ke/assets/arena.sahne" ); rke=$?
    if [ $rke = 0 ] && python3 - "$B/ke/assets/betikler/dondur.tpr" <<'PY4'
import sys
m = open(sys.argv[1], encoding='utf-8').read()
# 1) Asagi+Enter: 2. oneri qa_bir ; 2) fare: 3. satir qa_iki ; 3) Esc: tek harf y yerinde
ok = m.rstrip('\n').endswith('qa_bir()\nqa_iki()\ny')
sys.exit(0 if ok else 1)
PY4
    then echo "  kod oneri etkilesimi: Asagi+Enter ikinciyi, fare tiklamasi ucuncuyu yazdi, Ctrl+Bosluk tek harfte acti, Esc kapatti"
    else echo "  KIRMIZI kod oneri etkilesimi (cikis $rke):"; grep -E 'HATA|> ' "$B/ui_ke/yanit.txt" 2>/dev/null | tail -4 | sed 's/^/      /'; tail -6 "$B/ke/assets/betikler/dondur.tpr" | cat -A | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod oneri etkilesimi: proje uretilemedi"; kirmizi=1; fi
  # Kaynaklar: betik varken rol satiri + karo ipucu (betiksiz projede satir yok: kaynaklar.txt).
  mkdir -p "$B/ui_kb"
  if betik_proje "$B/kb"; then
    ( kos kaynaklar_betik.txt "$B/ui_kb" --scene "$B/kb/assets/arena.sahne" ); rkb=$?
    if [ $rkb = 0 ]; then echo "  kaynaklar betik: rol satiri ve karo ipucu (cift tik: kodunu ac, surukle: ata) gorundu"
    else echo "  KIRMIZI kaynaklar betik (cikis $rkb):"; grep -E 'HATA|> ' "$B/ui_kb/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kaynaklar betik: proje uretilemedi"; kirmizi=1; fi
  # Kod paneli SEKME olarak acilir (kenetli): Kod penceresinin dikey araligi Ozellikler'inkiyle ayni; Oynat'ta gizlenir.
  mkdir -p "$B/ui_ks"
  if betik_proje "$B/ks"; then
    ( kos kod_sekme.txt "$B/ui_ks" --scene "$B/ks/assets/arena.sahne" ); rks=$?
    if [ $rks = 0 ] && python3 - "$B/ui_ks/yanit.txt" <<'PY5'
import re, sys
t = open(sys.argv[1], encoding='utf-8').read()
def pen(ad):
    m = re.search(r'@ ?[^()\n]*' + re.escape(ad) + r' \((\d+),(\d+) (\d+)x(\d+)\)', t)  # adin basinda ikon karakteri olabilir
    return tuple(int(x) for x in m.groups()) if m else None
kod, oz = pen('dondur.tpr###kod_paneli'), pen('Özellikler###Ozellikler')
ok = bool(kod and oz and abs(kod[1] - oz[1]) <= 2 and abs(kod[3] - oz[3]) <= 2 and kod[2] > 600)
sys.exit(0 if ok else 1)
PY5
    then echo "  kod sekmesi: Gorunum'un yanina kenetlendi (Ozellikler ile ayni dikey aralik), Oynat'ta gizlendi"
    else echo "  KIRMIZI kod sekmesi (cikis $rks):"; grep -E 'HATA|> ' "$B/ui_ks/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; grep -E "kod_paneli\" @|Ozellikler###Ozellikler\" @" "$B/ui_ks/yanit.txt" | head -3 | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod sekmesi: proje uretilemedi"; kirmizi=1; fi
  # Ayri pencerede ac: sekme kapanir, kod BAGIMSIZ surecte acilir; ana editor kapandiktan sonra cocuk yasar (kare dosyasi olusur).
  mkdir -p "$B/ui_ka"
  if betik_proje "$B/ka"; then
    KARE="$B/ka/ayri_kare.ppm"; rm -f "$KARE"
    ( export TULPAR_KOD_PENCERESI_EK="--headless 2500 --out $KARE"; kos kod_ayir.txt "$B/ui_ka" --scene "$B/ka/assets/arena.sahne" ); rka=$?
    var_hemen=0; [ -f "$KARE" ] && var_hemen=1     # ana editor KAPANDIKTAN hemen sonra: cocuk daha bitirmemis olmali (2500 kare)
    for i in $(seq 1 120); do [ -f "$KARE" ] && break; sleep 0.5; done
    if [ $rka = 0 ] && [ $var_hemen = 0 ] && [ -s "$KARE" ]; then
      echo "  kod ayirma: sekme kapandi, bagimsiz surec editor kapandiktan SONRA da yasadi ve karesini yazdi"
    else echo "  KIRMIZI kod ayirma (cikis $rka, kare hemen=$var_hemen, kare var=$([ -s "$KARE" ] && echo evet || echo hayir)):"; grep -E 'HATA|> ' "$B/ui_ka/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod ayirma: proje uretilemedi"; kirmizi=1; fi
  # Bagimsiz kod penceresi (--kod): tam pencere kod paneli, yaz + Ctrl+S dosyaya.
  mkdir -p "$B/ui_kp" "$B/kp"
  printf 'func dondur_baslat(id) {\n    logla("selam");\n}\n' > "$B/kp/dondur.tpr"
  ( cd "$KOK" && DISPLAY= WAYLAND_DISPLAY= timeout 200 "$ED" --kod "$B/kp/dondur.tpr" --size 1100x800 --surucu "$KOK/tests/arayuz/kod_pencere.txt" --surucu-dizin "$B/ui_kp" > "$B/ui_kp/editor.log" 2>&1 ); rkp=$?
  if [ $rkp = 0 ] && tail -c 20 "$B/kp/dondur.tpr" | grep -q 'x=1;'; then echo "  kod penceresi: --kod tam pencerede acildi (sahne/panel yok), yazi Ctrl+S ile dosyaya gitti"
  else echo "  KIRMIZI kod penceresi (cikis $rkp):"; grep -E 'HATA|> ' "$B/ui_kp/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  # Geri birlestirme: (a) editor ACIKKEN istek dosyasi sekme olarak acilir ve tuketilir; (b) editor yokken pencere acik kalir.
  mkdir -p "$B/ui_kg" "$B/ui_ky2" "$B/kg_ayar"
  if betik_proje "$B/kg"; then
    mkdir -p "$B/kg_ayar/kod_istekleri"
    printf '%s\n%s\n' "$(date +%s)" "$B/kg/assets/betikler/dondur.tpr" > "$B/kg_ayar/kod_istekleri/istek_9_1.txt"
    printf '%s\n%s\n' "$(( $(date +%s) - 3600 ))" "/yok/bayat.tpr" > "$B/kg_ayar/kod_istekleri/istek_9_2.txt"       # bayat istek: yok sayilip silinmeli
    ( export TULPAR_EDITOR_AYAR_DIZINI="$B/kg_ayar"; kos kod_geri_al.txt "$B/ui_kg" --scene "$B/kg/assets/arena.sahne" ); rkg=$?
    if [ $rkg = 0 ] && [ ! -e "$B/kg_ayar/kod_istekleri/istek_9_1.txt" ] && [ ! -e "$B/kg_ayar/kod_istekleri/istek_9_2.txt" ]; then
      echo "  kod geri al (editor acik): istek sekme olarak acildi ve tuketildi; bayat istek silindi"
    else echo "  KIRMIZI kod geri al (cikis $rkg):"; grep -E 'HATA|> ' "$B/ui_kg/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; ls "$B/kg_ayar/kod_istekleri" | sed 's/^/      kalan: /'; kirmizi=1; fi
    printf 'func dondur_baslat(id) {\n    logla("selam");\n}\n' > "$B/ky2.tpr"
    ( export TULPAR_EDITOR_AYAR_DIZINI="$B/ky2_ayar"; cd "$KOK" && DISPLAY= WAYLAND_DISPLAY= timeout 200 "$ED" --kod "$B/ky2.tpr" --size 1100x800 --surucu "$KOK/tests/arayuz/kod_geri_al_yok.txt" --surucu-dizin "$B/ui_ky2" > "$B/ui_ky2/editor.log" 2>&1 ); rky=$?
    if [ $rky = 0 ] && [ -z "$(ls "$B/ky2_ayar/kod_istekleri" 2>/dev/null)" ]; then echo "  kod geri al (editor yok): pencere acik kaldi, uyardi, istegi geri aldi"
    else echo "  KIRMIZI kod geri al, editor yok (cikis $rky):"; grep -E 'HATA|> ' "$B/ui_ky2/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod geri al: proje uretilemedi"; kirmizi=1; fi
  # Parametre ipucu: kure( icinde imza + parametre adlari; parantezden cikinca kaybolur.
  mkdir -p "$B/ui_ki"
  if betik_proje "$B/ki"; then
    ( kos kod_ipucu.txt "$B/ui_ki" --scene "$B/ki/assets/arena.sahne" ); rki=$?
    if [ $rki = 0 ]; then echo "  kod ipucu: kure( icinde imza ve parametre adlari gorundu, parantezden cikinca kayboldu (kontrol)"
    else echo "  KIRMIZI kod ipucu (cikis $rki):"; grep -E 'HATA|> ' "$B/ui_ki/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
  else echo "  KIRMIZI kod ipucu: proje uretilemedi"; kirmizi=1; fi
  # Canli soz dizimi denetimi (tulpar typecheck gerekir).
  if command -v tulpar > /dev/null; then
    mkdir -p "$B/ui_kh"
    if betik_proje "$B/kh"; then
      ( kos kod_hata.txt "$B/ui_kh" --scene "$B/kh/assets/arena.sahne" ); rkh=$?
      if [ $rkh = 0 ]; then echo "  kod hatasi: ayrisma, tip hatasi ve bilinmeyen islev (LSP) isaretlendi, 'tamam' kayboldu, duzelince geri geldi; betikte logla icin sahte alarm yok"
      else echo "  KIRMIZI kod hatasi (cikis $rkh):"; grep -E 'HATA|> ' "$B/ui_kh/yanit.txt" 2>/dev/null | tail -3 | sed 's/^/      /'; kirmizi=1; fi
    else echo "  KIRMIZI kod hatasi: proje uretilemedi"; kirmizi=1; fi
  else echo "  ATLANDI: kod hatasi — tulpar kurulu degil (typecheck kosturulamaz)"; fi
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
