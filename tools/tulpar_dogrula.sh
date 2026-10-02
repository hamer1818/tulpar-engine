#!/usr/bin/env bash
# Tulpar Engine — kurulu `tulpar` + motorun eklenti paketi motor oyunlarini
# gercekten kosturuyor mu? (Linux / macOS; pencere ACMAZ.)
#
#   tools/tulpar_dogrula.sh                    PATH'teki tulpar + yapi/tulpar-ext
#   tools/tulpar_dogrula.sh --tulpar /yol/tulpar --eklenti /yol/tulpar-ext
#   tools/tulpar_dogrula.sh --tam              + taban cizgisi kapilari (dalga,
#                                              aksiyon, kopru testi, 6 ornek)
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
tul="${TULPAR:-}"; ext="$kok/yapi/tulpar-ext"; tam=0
while [ $# -gt 0 ]; do case "$1" in
  --tulpar) [ $# -ge 2 ] || { hata "--tulpar bir yol ister"; exit 2; }; tul="$2"; shift ;;
  --eklenti) [ $# -ge 2 ] || { hata "--eklenti bir dizin ister"; exit 2; }; ext="$2"; shift ;;
  --tam) tam=1 ;;
  -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) hata "bilinmeyen secenek: $1 (--help)"; exit 2 ;;
esac; shift; done

[ -n "$tul" ] || tul="$(command -v tulpar || true)"
[ -n "$tul" ] && [ -x "$tul" ] || { hata "tulpar bulunamadi (PATH, TULPAR ya da --tulpar)"; exit 1; }
tul="$(cd "$(dirname "$tul")" && pwd)/$(basename "$tul")"
[ -f "$ext/tulpar-ext.json" ] || { hata "eklenti paketi yok: $ext/tulpar-ext.json — once motoru derleyin (cmake --build yapi)"; exit 1; }
ext="$(cd "$ext" && pwd)"
bilgi "tulpar: $tul ($("$tul" --version 2>&1 | head -1))"
bilgi "eklenti: $ext"

# Eklenti destegi: `--ext` K303 oncesi bir derleyicide bilinmeyen bir bayraktir.
if ! "$tul" --ext "$ext" version >/dev/null 2>&1; then
  hata "bu tulpar yerel eklentiyi (--ext) tanimiyor ya da paket bozuk: TulparLang'i guncelleyin (tulpar update)"
  "$tul" --ext "$ext" version 2>&1 | head -5 >&2
  exit 1
fi

gun="$(mktemp -d)"
trap 'rm -rf "$gun"' EXIT
fail=0
dus() { hata "$1"; fail=1; }

cd "$kok/tulpar"
export DISPLAY=
unset TULPAR_EXT_PATH

# --- Duman: 3 kare ------------------------------------------------------------
# "kapanis:" satiri motorun kendi kapanis raporu: yalniz eng_init + eng_shutdown
# gercekten kostuysa basilir. Cikis kodu 0 tek basina yetmez.
if TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$ext" examples/engine_ilk_oyun.tpr >"$gun/duman.log" 2>&1 && grep -q "kapanis:" "$gun/duman.log"; then
  iyi "engine_ilk_oyun penceresiz 3 kare: $(grep -m1 'kapanis:' "$gun/duman.log" | sed 's/.*kapanis: //')"
else
  dus "engine_ilk_oyun kosmadi ya da motor kapanis raporu yok"; tail -12 "$gun/duman.log" >&2
fi

# --- Pozitif kontrol: eklentisiz ayni program DUSMELI --------------------------
# tulpar/tulpar.toml [ext] paths eklentiyi verir; o yuzden toml'suz bir dizinde.
mkdir -p "$gun/eklentisiz"
printf 'import "engine";\nfunc main() { motor_ac("x", 64, 64); motor_kapat(); }\nmain();\n' >"$gun/eklentisiz/p.tpr"
if (cd "$gun/eklentisiz" && "$tul" p.tpr) >"$gun/eklentisiz.log" 2>&1; then
  dus "pozitif kontrol: eklenti VERILMEDEN import \"engine\" derlendi — motor eklentiden gelmiyor"
elif grep -q -- "--ext" "$gun/eklentisiz.log"; then
  iyi "pozitif kontrol: eklentisiz import \"engine\" dustu (ipucu --ext'i gosteriyor)"
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
  if TULPAR_ENGINE_HEADLESS=3 "$tul" --ext "$gun/bozuk" examples/engine_ilk_oyun.tpr >"$gun/bozuk_sembol.log" 2>&1; then
    dus "pozitif kontrol: bozuk sembol (teng_dt_YOK) ile oyun LINKLENDI"
  elif grep -q "teng_dt_YOK" "$gun/bozuk_sembol.log"; then
    iyi "pozitif kontrol: bildirimdeki bozuk sembol link hatasinda adiyla (teng_dt_YOK)"
  else
    dus "pozitif kontrol: bozuk sembol linki dustu ama ad gorunmedi"; tail -5 "$gun/bozuk_sembol.log" >&2
  fi
  printf 'import "engine";\nfunc main() { eng_log_level(2); }\nmain();\n' >"$gun/imza.tpr"
  if (cd "$gun" && LC_ALL=C "$tul" typecheck --ext "$gun/bozuk" imza.tpr) >"$gun/imza.log" 2>&1; then
    dus "pozitif kontrol: bozuk imza (eng_log_level str) typecheck'ten gecti"
  elif grep -q "Argument 1 of 'eng_log_level': expected str, got int" "$gun/imza.log"; then
    iyi "pozitif kontrol: bildirimdeki bozuk imza typecheck'te yakalandi (eng_log_level)"
  else
    dus "pozitif kontrol: bozuk imza typecheck'i beklenen iletiyle dusmedi"; tail -4 "$gun/imza.log" >&2
  fi
  if (cd "$gun" && LC_ALL=C "$tul" typecheck --ext "$ext" imza.tpr) >"$gun/imza_ok.log" 2>&1; then
    iyi "kontrol: ayni program saglam bildirimle typecheck'ten temiz"
  else
    dus "kontrol: saglam bildirimle typecheck dustu"; tail -4 "$gun/imza_ok.log" >&2
  fi
fi

# --- Dil sondalari (motor_derleyici.sh'ten tasindi) -----------------------------
sonda() {  # sonda <ad> <beklenen> <aciklama>
  local cevap
  cevap="$(cd "$gun" && "$tul" "$1" 2>&1 | tail -1)"
  if [ "$cevap" = "$2" ]; then iyi "dil sondasi: $3 ($cevap)"
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

if [ "$tam" = 1 ]; then
  # --- Taban cizgileri (docs/KOPRU.md, 2026-10-02 olculdu) ----------------------
  kapi() {  # kapi <ornek> <kare> <beklenen [kapi] satiri>
    TULPAR_ENGINE_HEADLESS="$2" "$tul" --ext "$ext" "examples/$1.tpr" >"$gun/$1.log" 2>&1; local rc=$?
    local satir; satir="$(grep -m1 '^\[kapi\] kare=' "$gun/$1.log")"
    if [ $rc -eq 0 ] && [ "$satir" = "$3" ]; then iyi "$1 ($2 kare): $satir"
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
  grep -q '^\[kapi\] TAMAM' "$gun/engine_aksiyon.log" && iyi "engine_aksiyon: [kapi] TAMAM" || dus "engine_aksiyon: [kapi] TAMAM yok"
  grep -m1 '^\[kapi\] bellek' "$gun/engine_aksiyon.log" | sed 's/^/    /'
  if [ "$llvmpipe" = 1 ]; then
    TULPAR_AKSIYON_BELLEK_ILK=2400 TULPAR_ENGINE_HEADLESS=4600 "$tul" --ext "$ext" examples/engine_aksiyon.tpr >"$gun/aksiyon_bellek.log" 2>&1; rc=$?
    if [ $rc -eq 0 ] && grep -q '^\[kapi\] TAMAM' "$gun/aksiyon_bellek.log" && ! grep -q '^\[kapi\] ATLANDI: bellek' "$gun/aksiyon_bellek.log"; then
      iyi "engine_aksiyon bellek (llvmpipe, 4600 kare): $(grep -m1 '^\[kapi\] bellek' "$gun/aksiyon_bellek.log" | sed 's/^\[kapi\] bellek: //')"
    else
      dus "engine_aksiyon bellek (llvmpipe, 4600 kare): rc=$rc"; grep '^\[kapi\]' "$gun/aksiyon_bellek.log" | tail -6 >&2
    fi
    unset TULPAR_AKSIYON_BELLEK_ILK
  fi
  "$tul" --ext "$ext" tests/engine_bridge.test.tpr >"$gun/kopru.log" 2>&1; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/kopru.log")"
  if [ $rc -eq 0 ] && echo "$ozet" | grep -q 'Fail: 0' && [ "$(echo "$ozet" | sed -n 's/^Tests: \([0-9]*\).*/\1/p')" = "$(echo "$ozet" | sed -n 's/.*Pass: \([0-9]*\).*/\1/p')" ]; then
    iyi "engine_bridge.test.tpr: $ozet"
  else
    dus "engine_bridge.test.tpr: rc=$rc '$ozet'"; tail -8 "$gun/kopru.log" >&2
  fi
  for o in engine_ilk_oyun engine_arena engine_karakter engine_kanca_olcumu engine_taban_bellek engine_betik_dagitimi; do
    TULPAR_ENGINE_HEADLESS=60 "$tul" --ext "$ext" "examples/$o.tpr" >"$gun/$o.log" 2>&1; rc=$?
    if [ $rc -eq 0 ] && grep -q "kapanis:" "$gun/$o.log"; then iyi "$o (60 kare): rc=0"
    else dus "$o (60 kare): rc=$rc"; tail -6 "$gun/$o.log" >&2; fi
  done
fi

if [ $fail -ne 0 ]; then hata "tulpar + motor eklentisi DOGRULANAMADI"; exit 1; fi
iyi "tulpar + motor eklentisi calisiyor"
