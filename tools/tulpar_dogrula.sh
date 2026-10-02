#!/usr/bin/env bash
# Tulpar Engine — kurulu `tulpar` + motorun eklenti paketi motor oyunlarini
# gercekten kosturuyor mu? (Linux / macOS / Windows MSYS2; pencere ACMAZ.)
#
#   tools/tulpar_dogrula.sh                    PATH'teki tulpar + yapi/tulpar-ext
#   tools/tulpar_dogrula.sh --tulpar /yol/tulpar --eklenti /yol/tulpar-ext
#   tools/tulpar_dogrula.sh --tam              + taban cizgisi kapilari (dalga,
#                                              aksiyon, kopru testi, 6 ornek)
#   tools/tulpar_dogrula.sh --gpusuz-izinli    motor kurulamazsa (Vulkan/GPU
#                                              yok) GPU isteyen kapilar DUSMEZ:
#                                              "ATLANDI: <sebep>" basilir ve
#                                              ozette SAYILIR (Windows CI)
#
# Son satir her zaman ozettir:
#   tulpar dogrulama: N gecti, M dustu, K atlandi
# K'yi OKU: GPU'suz makinede kapi satirlari, kopru testi ve ornek oyunlarin
# kare dongusu HIC kosmamistir — o programlar yalniz derlenmis, linklenmis ve
# kurulumun dustugu yere kadar kosmustur. GPU'suz da gercekten olculen:
# derleme + link + pozitif kontroller + dil sondalari + GPU'SUZ kopru suite'i
# (tulpar/tests/engine_gpusuz.test.tpr: kurulumsuz teng_* cagrilari, alti
# double'in gidip gelmesi, dusen kurulumun sozlesmesi).
#
# NASIL BAGLANIYOR (K303, 2026-10-02): TulparLang derleyicisinin genel "yerel
# eklenti" noktasi var. Motor derlemesi yapi/tulpar-ext/ altina bir PAKET
# koyar (tulpar-ext.json bildirimi + engine.tpr + lib/libengine_*.a);
# `tulpar --ext yapi/tulpar-ext oyun.tpr` onu kullanir — derleyici yeniden
# DERLENMEZ, motora ozgu hicbir sey bilmez. Bu betik o zinciri sinar.
#
# Bu betik tools/motor_derleyici.sh'in YERINE geldi: o, derleyiciden cikarilan
# kopruyu 13 dosyaya TERS YAMA ile geri takip ayri bir derleyici kuruyordu ve
# iki gunde iki kez kirildi. Dil sondalari (4 adet) buraya tasindi: motorun
# Tulpar kitapligi ve ornekleri o dil ozelliklerine dayaniyor; eski bir
# derleyici HATA VERMEDEN yanlis calistirir.
#
# Pozitif kontrol: ayni program eklenti VERILMEDEN derlenmeli ve DUSMELI
# (import "engine" cozulmez) — dusmuyorsa motor eklentiden degil baska bir
# yoldan (eski, motoru icine gommus bir derleyici) geliyor demektir.
set -uo pipefail

K='\033[0;31m'; Y='\033[0;32m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}✓${N} %s\n" "$*"; }
hata()  { printf "${K}✗${N} %s\n" "$*" >&2; }

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tul="${TULPAR:-}"; ext="$kok/yapi/tulpar-ext"; tam=0; gpusuz_izinli=0
while [ $# -gt 0 ]; do case "$1" in
  --tulpar) [ $# -ge 2 ] || { hata "--tulpar bir yol ister"; exit 2; }; tul="$2"; shift ;;
  --eklenti) [ $# -ge 2 ] || { hata "--eklenti bir dizin ister"; exit 2; }; ext="$2"; shift ;;
  --tam) tam=1 ;;
  --gpusuz-izinli) gpusuz_izinli=1 ;;
  -h|--help) sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) hata "bilinmeyen secenek: $1 (--help)"; exit 2 ;;
esac; shift; done

# Platform: bildirimin link.<platform> anahtari (eksik arsiv kontrolu).
case "$(uname -s)" in
  Linux) plat=linux ;;
  Darwin) plat=macos ;;
  MINGW*|MSYS*|CYGWIN*) plat=windows ;;
  *) plat="$(uname -s)" ;;
esac

[ -n "$tul" ] || tul="$(command -v tulpar || true)"
[ -n "$tul" ] && [ -x "$tul" ] || { hata "tulpar bulunamadi (PATH, TULPAR ya da --tulpar)"; exit 1; }
tul="$(cd "$(dirname "$tul")" && pwd)/$(basename "$tul")"
[ -f "$ext/tulpar-ext.json" ] || { hata "eklenti paketi yok: $ext/tulpar-ext.json — once motoru derleyin (cmake --build yapi)"; exit 1; }
ext="$(cd "$ext" && pwd)"
bilgi "tulpar: $tul ($("$tul" --version 2>&1 | tr -d '\r' | head -1))"
bilgi "eklenti: $ext (link.$plat)"

# Eklenti destegi: `--ext` K303 oncesi bir derleyicide bilinmeyen bir bayraktir.
if ! "$tul" --ext "$ext" version >/dev/null 2>&1; then
  hata "bu tulpar yerel eklentiyi (--ext) tanimiyor ya da paket bozuk: TulparLang'i guncelleyin (tulpar update)"
  "$tul" --ext "$ext" version 2>&1 | head -5 >&2
  exit 1
fi

gun="$(mktemp -d)"
trap 'rm -rf "$gun"' EXIT
fail=0; n_gecti=0; n_dustu=0; n_atlandi=0
dus()  { hata "$1"; fail=1; n_dustu=$((n_dustu + 1)); }
gec()  { iyi "$1"; n_gecti=$((n_gecti + 1)); }
atla() { printf -- "${M}-${N} ATLANDI: %s\n" "$1"; n_atlandi=$((n_atlandi + 1)); }
# kos <gunluk> <komut...>: ciktiyi gunluge yazar, CR'leri atar. Windows'ta
# Tulpar ikilisinin stdout'u metin kipinde CRLF; `[ "$satir" = ... ]` ve
# `grep '^...'` karsilastirmalari aksi halde SESSIZCE tutmaz. Cikis kodu
# komutunkidir.
kos() {
  local g="$1"; shift
  "$@" >"$g.ham" 2>&1; local rc=$?
  tr -d '\r' <"$g.ham" >"$g"; rm -f "$g.ham"
  return $rc
}

cd "$kok/tulpar"
export DISPLAY=
unset TULPAR_EXT_PATH

# --- Duman: 3 kare ------------------------------------------------------------
# "kapanis:" satiri motorun kendi kapanis raporu: yalniz eng_init + eng_shutdown
# gercekten kostuysa basilir. Cikis kodu 0 tek basina yetmez.
# GPU'SUZ MAKINE: program derlenir, linklenir, kosar ve kurulum "motor
# acilamadi: <sebep>" ile duser. --gpusuz-izinli yoksa bu DUSTU'dur (Linux /
# macOS CI'da surucu kurulu, atlama orada bir hata); varsa GPU isteyen
# kapilar asagida ATLANDI olarak sayilir.
gpu=1; gpu_sebep=""
kos "$gun/duman.log" env TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$ext" examples/engine_ilk_oyun.tpr; rc=$?
if [ $rc -eq 0 ] && grep -q "kapanis:" "$gun/duman.log"; then
  gec "engine_ilk_oyun penceresiz 3 kare: $(grep -m1 'kapanis:' "$gun/duman.log" | sed 's/.*kapanis: //')"
elif [ $rc -eq 0 ] && grep -q "^motor acilamadi: " "$gun/duman.log"; then
  gpu=0; gpu_sebep="$(grep -m1 '^motor acilamadi: ' "$gun/duman.log" | sed 's/^motor acilamadi: //')"
  if [ "$gpusuz_izinli" = 1 ]; then
    gec "engine_ilk_oyun derlendi + linklendi + kostu (rc=0), kurulum dustu: $gpu_sebep"
    grep '^\[engine_bridge\]' "$gun/duman.log" | sed 's/^/    /'
    atla "engine_ilk_oyun kare dongusu (3 kare) — motor kurulamadi: $gpu_sebep"
  else
    dus "engine_ilk_oyun: motor kurulamadi ($gpu_sebep) — GPU'suz makinede --gpusuz-izinli"
  fi
else
  dus "engine_ilk_oyun kosmadi ya da motor kapanis raporu yok (rc=$rc)"; tail -12 "$gun/duman.log" >&2
  # Link dustuyse tanimsiz adlar (link.<platform> neyi eksik birakti) tekillestirilmis.
  if grep -qE 'undefined reference to|Undefined symbols' "$gun/duman.log"; then
    echo "  tanimsiz semboller (tekil, ilk 25):" >&2
    grep -oE "undefined reference to .[^']*" "$gun/duman.log" | sed 's/undefined reference to .//' | sort -u | head -25 | sed 's/^/    /' >&2
  fi
fi

# --- Pozitif kontrol: eklentisiz ayni program DUSMELI --------------------------
# tulpar/tulpar.toml [ext] paths eklentiyi verir; o yuzden toml'suz bir dizinde.
mkdir -p "$gun/eklentisiz"
printf 'import "engine";\nfunc main() { motor_ac("x", 64, 64); motor_kapat(); }\nmain();\n' >"$gun/eklentisiz/p.tpr"
if (cd "$gun/eklentisiz" && kos "$gun/eklentisiz.log" "$tul" p.tpr); then
  dus "pozitif kontrol: eklenti VERILMEDEN import \"engine\" derlendi — motor eklentiden gelmiyor"
elif grep -q -- "--ext" "$gun/eklentisiz.log"; then
  gec "pozitif kontrol: eklentisiz import \"engine\" dustu (ipucu --ext'i gosteriyor)"
else
  dus "pozitif kontrol: eklentisiz derleme dustu ama --ext ipucu yok"; head -5 "$gun/eklentisiz.log" >&2
fi

# --- Pozitif kontrol: bildirim bozulunca derleyici ADIYLA soyler ----------------
# Derleyici statik arsivde tip goremez; bildirim yalan soylerse ya link (sembol)
# ya typecheck (imza) yakalamali. Paketin bir kopyasi bozulur, asil paket degil.
cp -R "$ext" "$gun/bozuk"
sed -i.bak -e 's/"symbol": "teng_dt"/"symbol": "teng_dt_YOK"/' \
           -e 's/"name": "eng_log_level", "symbol": "teng_log_level", "params": \["level: i32"\]/"name": "eng_log_level", "symbol": "teng_log_level", "params": ["level: str"]/' \
           "$gun/bozuk/tulpar-ext.json"
if ! grep -q '"teng_dt_YOK"' "$gun/bozuk/tulpar-ext.json" || ! grep -q '"level: str"' "$gun/bozuk/tulpar-ext.json"; then
  dus "pozitif kontrol kurulamadi: bildirimde teng_dt / eng_log_level satiri beklenen bicimde degil"
else
  if kos "$gun/bozuk_sembol.log" env TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$gun/bozuk" examples/engine_ilk_oyun.tpr; then
    dus "pozitif kontrol: bozuk sembol (teng_dt_YOK) ile oyun LINKLENDI"
  elif grep -q "teng_dt_YOK" "$gun/bozuk_sembol.log"; then
    gec "pozitif kontrol: bildirimdeki bozuk sembol link hatasinda adiyla (teng_dt_YOK)"
  else
    dus "pozitif kontrol: bozuk sembol linki dustu ama ad gorunmedi"; tail -5 "$gun/bozuk_sembol.log" >&2
  fi
  printf 'import "engine";\nfunc main() { eng_log_level(2); }\nmain();\n' >"$gun/imza.tpr"
  if (cd "$gun" && kos "$gun/imza.log" env LC_ALL=C "$tul" typecheck --ext "$gun/bozuk" imza.tpr); then
    dus "pozitif kontrol: bozuk imza (eng_log_level str) typecheck'ten gecti"
  elif grep -q "Argument 1 of 'eng_log_level': expected str, got int" "$gun/imza.log"; then
    gec "pozitif kontrol: bildirimdeki bozuk imza typecheck'te yakalandi (eng_log_level)"
  else
    dus "pozitif kontrol: bozuk imza typecheck'i beklenen iletiyle dusmedi"; tail -4 "$gun/imza.log" >&2
  fi
  if (cd "$gun" && kos "$gun/imza_ok.log" env LC_ALL=C "$tul" typecheck --ext "$ext" imza.tpr); then
    gec "kontrol: ayni program saglam bildirimle typecheck'ten temiz"
  else
    dus "kontrol: saglam bildirimle typecheck dustu"; tail -4 "$gun/imza_ok.log" >&2
  fi
fi

# --- Pozitif kontrol: link.<platform>'dan bir arsiv eksikse link ADIYLA duser ---
# Bu platformun link bolumu (link.linux / link.macos / link.windows) gercekten
# KULLANILIYOR mu, eksik bir kitapligi sessizce yutmuyor mu? Paketin bir
# kopyasinda yalniz BU platformun satirindan engine_core cikarilir; link
# motorun kendi sembolleriyle (tulpar::engine::...) dusmeli.
cp -R "$ext" "$gun/eksik"
sed -i.bak -e "/\"$plat\": {/s/\"engine_core\", //" "$gun/eksik/tulpar-ext.json"
if ! grep -q "\"$plat\": {" "$gun/eksik/tulpar-ext.json"; then
  dus "pozitif kontrol kurulamadi: bildirimde link.$plat yok"
elif grep "\"$plat\": {" "$gun/eksik/tulpar-ext.json" | grep -q '"engine_core"'; then
  dus "pozitif kontrol kurulamadi: link.$plat satirindan engine_core cikarilamadi"
elif kos "$gun/eksik.log" env TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$gun/eksik" examples/engine_ilk_oyun.tpr; then
  dus "pozitif kontrol: link.$plat'ta engine_core YOKKEN oyun linklendi — bolum kullanilmiyor"
elif grep -E 'undefined reference to|referenced from' "$gun/eksik.log" | grep -q "tulpar::engine::"; then
  # GNU ld: undefined reference to `tulpar::engine::X(...)'; ld64: "tulpar::engine::X(...)", referenced from
  ad="$(grep -m1 -E "undefined reference to .tulpar::engine::|^ *\"tulpar::engine::" "$gun/eksik.log" | grep -oE 'tulpar::engine::[A-Za-z0-9_:]+' | head -1)"
  gec "pozitif kontrol: link.$plat'tan engine_core cikinca link adiyla dustu (tanimsiz: $ad)"
else
  dus "pozitif kontrol: eksik arsivle link dustu ama motor sembolu gorunmedi"; tail -5 "$gun/eksik.log" >&2
fi

# --- GPU'suz kopru suite'i ------------------------------------------------------
# GPU'suz makinede kopru hakkinda GERCEKTEN olculen sey bu: kurulumsuz teng_*
# cagrilari ve dusen kurulumun sozlesmesi. GPU varsa yukleyici kapatilir
# (TULPAR_ENGINE_NO_VULKAN=1) ki ayni yol kossun; GPU yoksa dokunulmaz — o
# makinenin GERCEK hata yolu olculur ([bilgi] satirlari sebebi basar).
if [ "$gpu" = 1 ]; then novk=1; else novk="${TULPAR_ENGINE_NO_VULKAN:-}"; fi
kos "$gun/gpusuz.log" env TULPAR_ENGINE_NO_VULKAN="$novk" "$tul" --ext "$ext" tests/engine_gpusuz.test.tpr; rc=$?
ozet="$(grep -m1 '^Tests:' "$gun/gpusuz.log")"
toplam="$(echo "$ozet" | sed -n 's/^Tests: \([0-9]*\).*/\1/p')"
if [ $rc -eq 0 ] && echo "$ozet" | grep -q 'Fail: 0' && [ -n "$toplam" ] && [ "$toplam" -gt 0 ] \
   && [ "$toplam" = "$(echo "$ozet" | sed -n 's/.*Pass: \([0-9]*\).*/\1/p')" ]; then
  gec "engine_gpusuz.test.tpr: $ozet"
  grep '\[bilgi\]' "$gun/gpusuz.log" | sed 's/^ */    /'
else
  dus "engine_gpusuz.test.tpr: rc=$rc '$ozet'"; tail -10 "$gun/gpusuz.log" >&2
fi

# --- Dil sondalari (motor_derleyici.sh'ten tasindi) -----------------------------
sonda() {  # sonda <ad> <beklenen> <aciklama>
  local cevap
  cevap="$(cd "$gun" && "$tul" "$1" 2>&1 | tr -d '\r' | tail -1)"
  if [ "$cevap" = "$2" ]; then gec "dil sondasi: $3 ($cevap)"
  else dus "dil sondasi: $3 — '$cevap', beklenen '$2' (TulparLang'i guncelleyin)"; fi
}
# 1) struct alanina bilesik atama (`dusmanlar[i].can -= 50`) — TulparLang
#    #344 oncesi SESSIZCE hicbir sey yapmiyordu: dusmanlar olmez.
printf 'struct D { int can; }\nD[] d = [];\nfunc main() { push(d, { can: 100 }); d[0].can -= 30; print("sonda " + toString(d[0].can)); }\nmain();\n' >"$gun/s1.tpr"
sonda s1.tpr "sonda 70" "struct alanina bilesik atama"
# 2) import edilen modulun struct'lari (engine.tpr'nin Vec3'u) — #345 oncesi
#    hic kaydedilmiyordu: her v3() malloc'lanan bir nesneydi.
printf 'struct M { int can; }\nM[] ms = [];\nfunc m_ekle() { push(ms, { can: 5 }); ms[0].can -= 2; }\nfunc m_can(): int { return ms[0].can; }\n' >"$gun/sonda_modul.tpr"
printf 'import "sonda_modul.tpr";\nfunc main() { m_ekle(); print("modul " + toString(m_can())); }\nmain();\n' >"$gun/s2.tpr"
sonda s2.tpr "modul 3" "import edilen struct kutusuz"
# 3) kare bellegi (Tuzaklar 8ch): global dizgiye `+=` geri sarimdan sonra
#    kalici mi — #347 oncesi deger cop ("<object>") olurdu.
printf 'str g = "";\nfunc main() {\n    for (int k = 0; k < 3; k++) {\n        var wm = arena_save();\n        g += "k" + toString(k);\n        arena_drop(wm);\n        var wz = arena_save();\n        array cop = [];\n        for (int j = 0; j < 3000; j++) { push(cop, "COPCOPCOP" + toString(j)); }\n        arena_drop(wz);\n    }\n    print("arena " + g);\n}\nmain();\n' >"$gun/s3.tpr"
sonda s3.tpr "arena k0k1k2" "kare belleginde global '+=' kalici"
# 4) 8ch'nin ikinci kalibi: tipli struct dizisi global'ini kare icinde
#    yeniden kurmak (bolum gecisi) — #347 oncesi global olu bellege bakar.
printf 'struct P { int x; }\nP[] g = [];\nfunc mk(int x): P { P p; p.x = x; return p; }\nfunc main() {\n    for (int k = 0; k < 3; k++) {\n        var wm = arena_save();\n        g = [];\n        push(g, mk(10 + k));\n        push(g, mk(20 + k));\n        arena_drop(wm);\n        var wz = arena_save();\n        array cop = [];\n        for (int j = 0; j < 3000; j++) { push(cop, "COPCOPCOP" + toString(j)); }\n        arena_drop(wz);\n    }\n    print("dizi " + toString(len(g)) + " " + toString(g[0].x) + " " + toString(g[1].x));\n}\nmain();\n' >"$gun/s4.tpr"
sonda s4.tpr "dizi 2 12 22" "kare icinde yeniden kurulan struct dizisi kalici"

# GPU'suz kipte GPU isteyen bir program: derlenip linklenip kurulumun dustugu
# yere kadar kosmasi OLCULUR (gecti), kare dongusu ATLANDI sayilir.
gpusuz_kos() {  # gpusuz_kos <ornek> <ne olculmedi>
  kos "$gun/$1.log" env TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$ext" "examples/$1.tpr"; local rc=$?
  if [ $rc -eq 0 ] && grep -q '^motor acilamadi: ' "$gun/$1.log"; then
    gec "$1 derlendi + linklendi + kostu (rc=0), kurulum dustu"
    atla "$1: $2 — motor kurulamadi: $gpu_sebep"
  else
    dus "$1 (GPU'suz): rc=$rc, 'motor acilamadi' satiri yok"; tail -8 "$gun/$1.log" >&2
  fi
}

if [ "$tam" = 1 ] && [ "$gpu" = 0 ]; then
  # Duman zaten --gpusuz-izinli yoksa dustu; burada yalniz izinli kip kalir.
  for o in engine_dalga engine_aksiyon; do gpusuz_kos "$o" "[kapi] satiri (taban cizgisi)"; done
  kos "$gun/kopru.log" "$tul" --ext "$ext" tests/engine_bridge.test.tpr; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/kopru.log")"
  if [ $rc -eq 0 ] && grep -q '^ *ATLANDI: motor kurulamadi' "$gun/kopru.log"; then
    gec "engine_bridge.test.tpr derlendi + linklendi + kostu (rc=0), ozet '$ozet'"
    atla "engine_bridge.test.tpr: 27 test — $(grep -m1 '^ *ATLANDI: motor kurulamadi' "$gun/kopru.log" | sed 's/^ *ATLANDI: //')"
  else
    dus "engine_bridge.test.tpr (GPU'suz): rc=$rc '$ozet', kurulum atlama satiri yok"; tail -8 "$gun/kopru.log" >&2
  fi
  for o in engine_arena engine_karakter engine_kanca_olcumu engine_taban_bellek engine_betik_dagitimi; do
    gpusuz_kos "$o" "60 karelik kosum"
  done
elif [ "$tam" = 1 ]; then
  # --- Taban cizgileri (docs/KOPRU.md, 2026-10-02 olculdu) ----------------------
  kapi() {  # kapi <ornek> <kare> <beklenen [kapi] satiri>
    kos "$gun/$1.log" env TULPAR_ENGINE_HEADLESS="$2" "$tul" --ext "$ext" "examples/$1.tpr"; local rc=$?
    local satir; satir="$(grep -m1 '^\[kapi\] kare=' "$gun/$1.log")"
    if [ $rc -eq 0 ] && [ "$satir" = "$3" ]; then gec "$1 ($2 kare): $satir"
    else dus "$1: rc=$rc, kapi satiri '$satir' (beklenen '$3')"; tail -8 "$gun/$1.log" >&2; fi
  }
  kapi engine_dalga 2400 "[kapi] kare=972 dalga=4 dogan=28 tuzakta=25 carpan=3 kalan=0 bagli=4 can=7 hata=0"
  # Yazilim rasterlestiricisi (llvmpipe): aksiyonun RSS bellek kapisi suruc
  # isinmasini da olcer — CI'da (mesa 25.2 lavapipe) [heap] ilk ~k1100'e kadar
  # +31 MB buyuyup sonra DUZ (olculdu 2026-10-02, smaps). Orada iki kosum:
  # tam kapi satiri 3200 karede (bellek penceresi ATLANDI diye basilir), bellek
  # egimi ayri bir 4600 karelik kosumda k2400'den sonra AYNI sinirla.
  llvmpipe=0
  grep -q "GPU: llvmpipe" "$gun/duman.log" && llvmpipe=1
  if [ "$llvmpipe" = 1 ]; then
    bilgi "GPU llvmpipe: aksiyonun bellek egimi ayri kosumda, surucu isinmasindan sonra (k2400..4600)"
    export TULPAR_AKSIYON_BELLEK_ILK=2000
  fi
  kapi engine_aksiyon 3200 "[kapi] kare=3200 bolum=2 gecis=1 oldurulen=10 kalan_dusman=0 can=8 skor=1000 durum=3 navmesh=true dongu_hatasi=0 kurulum_hatasi=0 uyari=0"
  grep -q '^\[kapi\] TAMAM' "$gun/engine_aksiyon.log" && gec "engine_aksiyon: [kapi] TAMAM" || dus "engine_aksiyon: [kapi] TAMAM yok"
  grep -m1 '^\[kapi\] bellek' "$gun/engine_aksiyon.log" | sed 's/^/    /'
  if [ "$llvmpipe" = 1 ]; then
    kos "$gun/aksiyon_bellek.log" env TULPAR_AKSIYON_BELLEK_ILK=2400 TULPAR_ENGINE_HEADLESS=4600 "$tul" --ext "$ext" examples/engine_aksiyon.tpr; rc=$?
    if [ $rc -eq 0 ] && grep -q '^\[kapi\] TAMAM' "$gun/aksiyon_bellek.log" && ! grep -q '^\[kapi\] ATLANDI: bellek' "$gun/aksiyon_bellek.log"; then
      gec "engine_aksiyon bellek (llvmpipe, 4600 kare): $(grep -m1 '^\[kapi\] bellek' "$gun/aksiyon_bellek.log" | sed 's/^\[kapi\] bellek: //')"
    else
      dus "engine_aksiyon bellek (llvmpipe, 4600 kare): rc=$rc"; grep '^\[kapi\]' "$gun/aksiyon_bellek.log" | tail -6 >&2
    fi
    unset TULPAR_AKSIYON_BELLEK_ILK
  fi
  kos "$gun/kopru.log" "$tul" --ext "$ext" tests/engine_bridge.test.tpr; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/kopru.log")"
  if [ $rc -eq 0 ] && echo "$ozet" | grep -q 'Fail: 0' && [ "$(echo "$ozet" | sed -n 's/^Tests: \([0-9]*\).*/\1/p')" = "$(echo "$ozet" | sed -n 's/.*Pass: \([0-9]*\).*/\1/p')" ]; then
    gec "engine_bridge.test.tpr: $ozet"
  else
    dus "engine_bridge.test.tpr: rc=$rc '$ozet'"; tail -8 "$gun/kopru.log" >&2
  fi
  for o in engine_ilk_oyun engine_arena engine_karakter engine_kanca_olcumu engine_taban_bellek engine_betik_dagitimi; do
    kos "$gun/$o.log" env TULPAR_ENGINE_HEADLESS=60 "$tul" --ext "$ext" "examples/$o.tpr"; rc=$?
    if [ $rc -eq 0 ] && grep -q "kapanis:" "$gun/$o.log"; then gec "$o (60 kare): rc=0"
    else dus "$o (60 kare): rc=$rc"; tail -6 "$gun/$o.log" >&2; fi
  done
fi

echo "tulpar dogrulama: $n_gecti gecti, $n_dustu dustu, $n_atlandi atlandi"
if [ $fail -ne 0 ]; then hata "tulpar + motor eklentisi DOGRULANAMADI"; exit 1; fi
if [ $n_atlandi -gt 0 ]; then iyi "tulpar + motor eklentisi linkleniyor ve GPU'suz kopru calisiyor — $n_atlandi GPU kapisi ATLANDI (olculmedi)"
else iyi "tulpar + motor eklentisi calisiyor"; fi
