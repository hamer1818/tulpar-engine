#!/usr/bin/env bash
# Tulpar Engine — kurulu `tulpar` + motorun eklenti paketi motor oyunlarini
# gercekten kosturuyor mu? (Linux / macOS / Windows MSYS2; pencere ACMAZ.)
#
#   tools/tulpar_dogrula.sh                    PATH'teki tulpar + yapi/tulpar-ext
#   tools/tulpar_dogrula.sh --tulpar /yol/tulpar --eklenti /yol/tulpar-ext
#   tools/tulpar_dogrula.sh --tam              + taban cizgisi kapilari (dalga,
#                                              aksiyon, savas, kopru testi,
#                                              6 ornek)
#   tools/tulpar_dogrula.sh --uzun-atla        --tam'in uzun taban cizgisi
#                                              kapilari (dalga 2400, aksiyon 3200,
#                                              savas 9600 kare) KOSMAZ: "ATLANDI: <sebep>"
#                                              basilir ve SAYILIR (Windows CI:
#                                              yazilim ICD'sinde ~24 dk; ayni
#                                              satirlar Linux/macOS'ta olculur)
#   tools/tulpar_dogrula.sh --savas-ayri       --tam'in savas bolumunu (9600 kare +
#                                              uc pozitif kontrol) KOSMAZ; o bolum
#                                              ayri bir CI isinde --yalniz-savas ile
#                                              kosar ve sonucunu zorunlu Linux isi
#                                              tasir (Linux CI, 2026-10-30)
#   tools/tulpar_dogrula.sh --yalniz-savas     yalniz duman + savas bolumu
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
#
# OYUN KAPILARI BURADA DEGIL (2026-10-26): "Kupler ile Kureler" (eskiden
# tulpar/oyunlar/kup_kure_savasi) ayri, OZEL bir depoda; kendi kapilari orada.
# Eski 9600 karelik oyun kapisinin olctugu seyleri artik motorun kendi
# examples/engine_savas.tpr'si olcer (--tam, asagida "Savas").
set -uo pipefail

K='\033[0;31m'; Y='\033[0;32m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}✓${N} %s\n" "$*"; }
hata()  { printf "${K}✗${N} %s\n" "$*" >&2; }

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tul="${TULPAR:-}"; ext="$kok/yapi/tulpar-ext"; tam=0; gpusuz_izinli=0; uzun_atla=0; savas_ayri=0; yalniz_savas=0
while [ $# -gt 0 ]; do case "$1" in
  --tulpar) [ $# -ge 2 ] || { hata "--tulpar bir yol ister"; exit 2; }; tul="$2"; shift ;;
  --eklenti) [ $# -ge 2 ] || { hata "--eklenti bir dizin ister"; exit 2; }; ext="$2"; shift ;;
  --tam) tam=1 ;;
  --gpusuz-izinli) gpusuz_izinli=1 ;;
  --uzun-atla) uzun_atla=1 ;;
  --savas-ayri) savas_ayri=1 ;;
  --yalniz-savas) yalniz_savas=1; tam=1 ;;
  -h|--help) sed -n '2,56p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

# TULPAR_DOGRULA_GUN=<dizin>: gunlukler orada KALIR (CI artefakti / iki kosumun
# [kapi] satirlarini bayt bayt karsilastirmak icin); yoksa gecici, silinir.
if [ -n "${TULPAR_DOGRULA_GUN:-}" ]; then
  gun="$TULPAR_DOGRULA_GUN"; mkdir -p "$gun"
else
  gun="$(mktemp -d)"
  trap 'rm -rf "$gun"' EXIT
fi
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

# --- Savas bolumu (examples/engine_savas.tpr) — fonksiyon: --savas-ayri ile bu
# kosumda ATLANIR (ayri bir CI isi --yalniz-savas ile kosar), --yalniz-savas ile
# YALNIZ o kosar. Govde degismedi; yalniz girinti.
savas_bolumu() {
  # --- Savas: motorun kendi uzun kapisi (examples/engine_savas.tpr) -------------
  # Eski oyun kopyasinin (tulpar/oyunlar/kup_kure_savasi, #94) 9600 karelik
  # kapisinin YERINE (2026-10-26; oyun ayri ozel depoda, motor CI'i onu cekmez).
  # Ayni seyleri olcer: menu ADIYLA tiklanir (3 ekran), kule yuvasi ekrandan
  # (izdusum -> isin) secilir, iki yapay zeka sabit tohumla (9600) savasir:
  # birlik dog/ol, kinematik mermi + kule, dinamik birlik/enkaz/gokdasi,
  # duraklat -> devam (fizik durur: izlenen birlik 0 m kayar), sonuc ekrani.
  # Olculdu 2026-10-26, RTX 5080 (Linux, NVIDIA): savas 5282. karede ZAFER'le
  # biter (carpisma gruplari + donme kilidi ile; gruplardan once 7397); yayinlanmis
  # v3.41.0 VE yerel TulparLang (T1-T6, 2d83bf40) ile bayt bayt ayni (kapi + fizik).
  # fizik satirinda dost_degme=0: mermi kendi tarafinin govdesine degmedi.
  # Fizik satiri da birebir karsilastirilir: karar Tulpar'da dizi sirasiyla,
  # ham carpisma kuyrugu yalniz SAYILIR (sirasi is parcacigina bagli olabilir).
  savas_kos() {  # savas_kos <gunluk> <kare> [AD=deger ...]
    local g="$1" k="$2"; shift 2
    kos "$g" env TULPAR_ENGINE_HEADLESS="$k" "$@" "$tul" --ext "$ext" examples/engine_savas.tpr
  }
  savas_3200="[kapi] kare=3200 sonuc=suruyor savas_kare=3131 zorluk=0 dogan=26/12 olen=11/7 kule=1/1 ozel=0/1 kale=900/785 altin=24/14 dongu_hatasi=0 kurulum_hatasi=0 uyari=0"
  if [ "$uzun_atla" = 1 ]; then
    savas_kos "$gun/savas_kisa.log" 300; rc=$?
    if [ $rc -eq 0 ] && grep -q '^\[kapi\] TAMAM (kisa kosum' "$gun/savas_kisa.log"; then
      gec "engine_savas (300 kare): menu akisi + kurulum + savasin basi TAMAM"
    else
      dus "engine_savas (300 kare): rc=$rc, kisa kapi TAMAM yok"; grep '^\[kapi\]' "$gun/savas_kisa.log" | tail -6 >&2
    fi
    atla "engine_savas (9600 kare) tam savas [kapi] + fizik satiri, bellek kapisi ve iki pozitif kontrolu — uzun, Linux/macOS'ta olculur (--uzun-atla)"
  else
    savas_kos "$gun/savas.log" 9600; rc=$?
    satir="$(grep -m1 '^\[kapi\] kare=' "$gun/savas.log")"
    fizik="$(grep -m1 '^\[kapi\] fizik: ' "$gun/savas.log")"
    beklenen="[kapi] kare=5282 sonuc=zafer savas_kare=5182 zorluk=0 dogan=38/19 olen=16/18 kule=3/1 ozel=0/2 kale=900/0 altin=124/42 dongu_hatasi=0 kurulum_hatasi=0 uyari=0"
    beklenen_fizik="[kapi] fizik: govde_kur=356 govde_sil=319 kinematik=217 mermi=213 isabet=196 enkaz=68 carpisma=596 carpisma_dusen=0 dost_degme=0 canli_varlik=38 govde=38"
    if [ $rc -eq 0 ] && [ "$satir" = "$beklenen" ] && [ "$fizik" = "$beklenen_fizik" ] && grep -q '^\[kapi\] TAMAM — savas' "$gun/savas.log"; then
      gec "engine_savas (9600 kare): $satir"
    else
      dus "engine_savas: rc=$rc, kapi satiri '$satir' (beklenen '$beklenen'), fizik '$fizik' (beklenen '$beklenen_fizik')"; grep '^\[kapi\]' "$gun/savas.log" | tail -10 >&2
    fi
    grep -E '^\[kapi\] (fizik|secim|menu|bellek|ayirma)' "$gun/savas.log" | sed 's/^/    /'
    # POZITIF KONTROL 1 — bellek kapisi: bilinen hizda ENJEKTE sizinti (2 KB/kare,
    # engine_aksiyon kalibi). Kapi KIRMIZI olmali; savas satiri DEGISMEMELI
    # (sizinti oyun durumuna dokunmuyor: kontrolu baska bir sey bozmuyor).
    # Olcu: 600 karelik kayan pencere artislarinin alt ceyregi (engine_savas.tpr
    # 'Kare bellegi kapisi'). Olculdu 2026-10-27, her biri 5 kosum, temiz (9600) /
    # sizintili (3200), KB/1000 kare: RTX 5080 (NVIDIA, Linux) 6..26 / 2086..2146;
    # Ubuntu 24.04 lavapipe (mesa 25.2.8, LLVM 20.1.2 — CI'inki) 4 cekirdek
    # (taskset) -706..-580 / 1460..1740. Eski olcu (200 karelik ardisik aralik)
    # ayni lavapipe'ta sizintida 5/5 -120..-60, CI'da -600 / -2800 -> YAKALAMIYORDU.
    savas_kos "$gun/savas_sizinti.log" 3200 TULPAR_ENGINE_SAVAS_SIZINTI_B=2048; rc=$?
    if [ $rc -ne 0 ] && grep -q '^\[kapi\] BASARISIZ: bellek buyuyor: alt ceyrek' "$gun/savas_sizinti.log" \
       && [ "$(grep -m1 '^\[kapi\] kare=' "$gun/savas_sizinti.log")" = "$savas_3200" ]; then
      gec "pozitif kontrol: savasta enjekte sizinti (2 KB/kare) yakalandi: $(grep -m1 '^\[kapi\] bellek' "$gun/savas_sizinti.log" | sed 's/^\[kapi\] bellek: //; s/, kare bellegi.*//'); savas satiri ayni"
    else
      dus "pozitif kontrol: savasta enjekte sizinti YAKALANMADI ya da savas satiri degisti (rc=$rc)"; grep '^\[kapi\]' "$gun/savas_sizinti.log" | tail -6 >&2
    fi
    # POZITIF KONTROL — dost mermi sayaci (Geri bildirim #25): gruplar yine atanir,
    # tablo ACIK (TULPAR_ENGINE_SAVAS_DOST_SUZGEC=0) -> ayni savasta mermi dost
    # govdeye DEGMELI. Olculdu 2026-10-26 RTX 5080: 3200 karede 14 (suzgecle 0).
    # (Bu kipte savasin kendisi farkli gider; yalniz sayac okunur.)
    savas_kos "$gun/savas_dost.log" 3200 TULPAR_ENGINE_SAVAS_DOST_SUZGEC=0
    dost="$(sed -n 's/^\[kapi\] fizik: .* dost_degme=\([0-9]*\).*/\1/p' "$gun/savas_dost.log" | head -1)"
    if [ -n "$dost" ] && [ "$dost" -gt 0 ]; then
      gec "pozitif kontrol: carpisma tablosu acik -> 3200 karede $dost dost degmesi (suzgecle 0)"
    else
      dus "pozitif kontrol: suzgec kapaliyken dost degmesi '$dost' — sayac bir sey olcmuyor"; grep '^\[kapi\]' "$gun/savas_dost.log" | tail -6 >&2
    fi
    # POZITIF KONTROL 2 — satir savasi olcuyor mu? 1500. karede taraf 1'e +200
    # altin (TULPAR_ENGINE_SAVAS_BOZ): 3200. karedeki satir normal kosumdan
    # (yukaridaki sizinti kosumu ile ayni satir) FARKLI olmali.
    # Bellek karari bu kontrolun konusu DEGIL: BOZ bilincli olarak baska bir
    # savas — canli varlik sayisi degisir ve lavapipe'ta RSS onunla sinirli
    # buyuyebilir (gruplardan once, 2026-10-27 CI lavapipe'i 4 cekirdek: 3200
    # karede kayan pencere alt ceyregi 706 / 806 / 753, ayni BOZ 9600 karede -653;
    # gruplarla -273 / -340 / -493, RTX 5080 6 / 13 / 66). Kosum sonuna varmali ve
    # bellek disinda BASARISIZ olmamali; bellek satiri bilgi olarak basilir.
    savas_kos "$gun/savas_boz.log" 3200 TULPAR_ENGINE_SAVAS_BOZ=1500; rc=$?
    bsatir="$(grep -m1 '^\[kapi\] kare=' "$gun/savas_boz.log")"
    bdiger="$(grep '^\[kapi\] BASARISIZ: ' "$gun/savas_boz.log" | grep -v '^\[kapi\] BASARISIZ: bellek buyuyor: ')"
    if { [ $rc -eq 0 ] || [ $rc -eq 1 ]; } && [ -z "$bdiger" ] && [ -n "$bsatir" ] && [ "$bsatir" != "$savas_3200" ] && grep -q 'BOZ: kare 1500' "$gun/savas_boz.log"; then
      echo "    [bilgi] BOZ kosumu bellek: $(grep -m1 '^\[kapi\] bellek' "$gun/savas_boz.log" | sed 's/^\[kapi\] bellek: //; s/, kare bellegi.*//')"
      gec "pozitif kontrol: TULPAR_ENGINE_SAVAS_BOZ=1500 -> 3200. kare satiri degisti ($(echo "$bsatir" | sed 's/.* \(dogan=[^ ]*\) \(olen=[^ ]*\).*/\1 \2/'), normal: dogan=26/12 olen=11/7)"
    else
      dus "pozitif kontrol: BOZ savas satirini degistirmedi ya da kosmadi (rc=$rc): '$bsatir'"; grep '^\[kapi\]' "$gun/savas_boz.log" | tail -4 >&2
    fi
  fi
}

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

# --yalniz-savas: duman (GPU var mi) + savas bolumu, sonra ozet. Ayri CI isi
# (Linux kopru — savas): lavapipe tek kosuda 4 cekirdegi zaten dolduruyor
# (2026-10-30 olculdu: savas 3200 kare LP_NUM_THREADS=4 36.5 s duvar / 125 s
# kullanici), ayni koşucuda eszamanli kosu kazandirmiyor; ayri koşucu kazandirir.
if [ "$yalniz_savas" = 1 ]; then
  if [ "$gpu" = 1 ]; then savas_bolumu
  else dus "savas bolumu: motor kurulamadi ($gpu_sebep)"; fi
  echo "tulpar dogrulama (yalniz savas): $n_gecti gecti, $n_dustu dustu, $n_atlandi atlandi"
  if [ $fail -ne 0 ]; then hata "savas bolumu DOGRULANAMADI"; exit 1; fi
  iyi "savas bolumu temiz"; exit 0
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
elif grep -E 'undefined reference to|referenced from|undefined symbol: ' "$gun/eksik.log" | grep -q "tulpar::engine::"; then
  # GNU ld: undefined reference to `tulpar::engine::X(...)'; ld64: "tulpar::engine::X(...)", referenced from;
  # ld.lld: undefined symbol: tulpar::engine::X(...) — TulparLang Linux'ta ld.lld'yi
  # PATH'te bulursa onunla baglar (2026-10-06); uc bicim de sayilir.
  ad="$(grep -m1 -E "undefined reference to .tulpar::engine::|^ *\"tulpar::engine::|undefined symbol: tulpar::engine::" "$gun/eksik.log" | grep -oE 'tulpar::engine::[A-Za-z0-9_:]+' | head -1)"
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

# --- Yerel ag: IKI SUREC (N2a, eng_net_*) --------------------------------------
# engine_gpusuz.test.tpr ayni surecte iki soketi olcer; bu kapi iki AYRI tulpar
# surecini 127.0.0.1'de konusturur (ag 1v1'in gercek sekli: iki surec, iki soket):
# 200 ping/pong, kayip 0, rtt yazilir. Motor kurulmaz, GPU gerekmez. POZITIF
# KONTROL: yanitlayici yokken ayni ping 0/5 + rc 1 — kapi yanita bagli olcuyor.
kos "$gun/ag_derle.log" "$tul" build --ext "$ext" examples/engine_ag_ping.tpr "$gun/ag_ping"; rc=$?
ag_ikili=no; { [ -x "$gun/ag_ping" ] || [ -x "$gun/ag_ping.exe" ]; } && ag_ikili=var
if [ $rc -ne 0 ] || [ $ag_ikili = no ]; then
  dus "engine_ag_ping.tpr derlenemedi (rc=$rc)"; tail -8 "$gun/ag_derle.log" >&2
else
  ag_port=$((47400 + $$ % 400))
  TULPAR_AG_ROL=yanit TULPAR_AG_PORT=$ag_port "$gun/ag_ping" >"$gun/ag_yanit.log" 2>&1 &
  ag_pid=$!
  sleep 1
  kos "$gun/ag.log" env TULPAR_AG_ROL=ping TULPAR_AG_PORT=$((ag_port + 1)) TULPAR_AG_KARSI=$ag_port TULPAR_AG_N=200 "$gun/ag_ping"; rc=$?
  wait $ag_pid 2>/dev/null; rc_y=$?
  satir="$(grep -m1 '^\[ag\] ping [0-9]' "$gun/ag.log")"
  if [ $rc -eq 0 ] && [ $rc_y -eq 0 ] && echo "$satir" | grep -q '^\[ag\] ping 200/200, kayip 0'; then
    gec "ag iki surec (loopback): ${satir#\[ag\] }"
  else
    dus "ag iki surec: rc=$rc yanit rc=$rc_y '$satir'"; tail -5 "$gun/ag.log" "$gun/ag_yanit.log" >&2
  fi
  kos "$gun/ag_kontrol.log" env TULPAR_AG_ROL=ping TULPAR_AG_PORT=$((ag_port + 1)) TULPAR_AG_KARSI=$ag_port TULPAR_AG_N=5 \
    TULPAR_AG_ZAMAN_ASIMI_MS=50 "$gun/ag_ping"; rc=$?
  if [ $rc -ne 0 ] && grep -q '^\[ag\] ping 0/5, kayip 5' "$gun/ag_kontrol.log"; then
    gec "pozitif kontrol: yanitlayici yokken ping 0/5, rc=$rc"
  else
    dus "pozitif kontrol: yanitlayici yokken ping kaybi gorulmedi (rc=$rc)"; tail -3 "$gun/ag_kontrol.log" >&2
  fi
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
  for o in engine_dalga engine_aksiyon engine_savas; do gpusuz_kos "$o" "[kapi] satiri (taban cizgisi)"; done
  kos "$gun/kopru.log" "$tul" --ext "$ext" tests/engine_bridge.test.tpr; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/kopru.log")"
  if [ $rc -eq 0 ] && grep -q '^ *ATLANDI: motor kurulamadi' "$gun/kopru.log"; then
    gec "engine_bridge.test.tpr derlendi + linklendi + kostu (rc=0), ozet '$ozet'"
    atla "engine_bridge.test.tpr: 27 test — $(grep -m1 '^ *ATLANDI: motor kurulamadi' "$gun/kopru.log" | sed 's/^ *ATLANDI: //')"
  else
    dus "engine_bridge.test.tpr (GPU'suz): rc=$rc '$ozet', kurulum atlama satiri yok"; tail -8 "$gun/kopru.log" >&2
  fi
  kos "$gun/iki_oturum.log" "$tul" --ext "$ext" tests/engine_iki_oturum.test.tpr; rc=$?
  if [ $rc -eq 0 ] && grep -q '^ *ATLANDI: motor kurulamadi' "$gun/iki_oturum.log"; then
    gec "engine_iki_oturum.test.tpr derlendi + linklendi + kostu (rc=0)"
    atla "engine_iki_oturum.test.tpr: ayni surecte 5 oturum — $(grep -m1 '^ *ATLANDI: motor kurulamadi' "$gun/iki_oturum.log" | sed 's/^ *ATLANDI: //')"
  else
    dus "engine_iki_oturum.test.tpr (GPU'suz): rc=$rc, kurulum atlama satiri yok"; tail -8 "$gun/iki_oturum.log" >&2
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
  if [ "$uzun_atla" = 1 ]; then
    # Bilincli secim (2026-10-05, olculdu CI windows-latest SwiftShader): dalga
    # 2400 kare ~4,3 dk, aksiyon 3200 kare ~20 dk (p50 ~210 ms/kare). Ayni iki
    # satir o tarihte orada Linux'la BAYT BAYT ayni cikti; her kosumda Linux
    # (lavapipe) ve macOS (MoltenVK) olcer. Burada olculmedikleri GORUNUR.
    atla "engine_dalga (2400 kare) [kapi] satiri — uzun taban cizgisi, Linux/macOS'ta olculur (--uzun-atla)"
    atla "engine_aksiyon (3200 kare) [kapi] satiri + bellek kapisi + pozitif kontrolu — uzun taban cizgisi, Linux/macOS'ta olculur (--uzun-atla)"
  else
    kapi engine_dalga 2400 "[kapi] kare=972 dalga=4 dogan=28 tuzakta=25 carpan=3 kalan=0 bagli=4 can=7 hata=0"
    # Aksiyonun RSS bellek kapisi aralik artislarinin ALT CEYREGIYLE olcer (ornek
    # tulpar/examples/engine_aksiyon.tpr, 2026-10-05): llvmpipe'ta (CI mesa 25.2.8)
    # her kosumda bir kez ~27 MB'lik bir surucu BASAMAGI olusuyor ve zamani
    # rastgele (k1200..k6000); uc farki basamak pencereye dusunce kirmiziydi,
    # alt ceyrek basamaga dayanikli (14 normal kosumda 0). Bu yuzden eskiden llvmpipe'a ozgu olan ikinci
    # (4600 karelik, k2400'den sonra) kosum KALKTI: tek kosum, her yerde ayni kapi.
    kapi engine_aksiyon 3200 "[kapi] kare=3200 bolum=2 gecis=1 oldurulen=10 kalan_dusman=0 can=8 skor=1000 durum=3 navmesh=true dongu_hatasi=0 kurulum_hatasi=0 uyari=0"
    grep -q '^\[kapi\] TAMAM' "$gun/engine_aksiyon.log" && gec "engine_aksiyon: [kapi] TAMAM" || dus "engine_aksiyon: [kapi] TAMAM yok"
    grep -m1 '^\[kapi\] bellek' "$gun/engine_aksiyon.log" | sed 's/^/    /'
    # POZITIF KONTROL — bellek kapisi bir sey olcuyor mu? Bilinen hizda ENJEKTE
    # sizinti (2 KB/kare = ~2000 KB/1000 kare, sinirin ~3 kati): kapi KIRMIZI
    # olmali ve sebebi "bellek buyuyor: alt ceyrek" demeli. Olculdu 2026-10-05:
    # llvmpipe (CI) Q1 1140, RTX 5080 Q1 1240 (artan 9/11); 1 KB/kare'yi
    # yiginin bos alani yutuyor (medyan 20) — RSS'in cozunurlugu bu.
    kos "$gun/aksiyon_sizinti.log" env TULPAR_AKSIYON_SIZINTI_B=2048 TULPAR_ENGINE_HEADLESS=3200 "$tul" --ext "$ext" examples/engine_aksiyon.tpr; rc=$?
    if [ $rc -ne 0 ] && grep -q '^\[kapi\] BASARISIZ: bellek buyuyor: alt ceyrek' "$gun/aksiyon_sizinti.log"; then
      gec "pozitif kontrol: enjekte sizinti (2 KB/kare) bellek kapisinda yakalandi: $(grep -m1 '^\[kapi\] bellek' "$gun/aksiyon_sizinti.log" | sed 's/^\[kapi\] bellek: //; s/, kare bellegi.*//')"
    else
      dus "pozitif kontrol: enjekte sizinti (2 KB/kare) bellek kapisinda YAKALANMADI (rc=$rc) — kapi bir sey olcmuyor"; grep '^\[kapi\]' "$gun/aksiyon_sizinti.log" | tail -6 >&2
    fi
  fi
  if [ "$savas_ayri" = 1 ]; then
    echo "    [bilgi] savas kapisi (9600 + uc pozitif kontrol) bu kosumda DEGIL: ayri iste kosuyor (--savas-ayri; CI: 'Linux kopru — savas')"
  else
    savas_bolumu
  fi
  kos "$gun/kopru.log" "$tul" --ext "$ext" tests/engine_bridge.test.tpr; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/kopru.log")"
  if [ $rc -eq 0 ] && echo "$ozet" | grep -q 'Fail: 0' && [ "$(echo "$ozet" | sed -n 's/^Tests: \([0-9]*\).*/\1/p')" = "$(echo "$ozet" | sed -n 's/.*Pass: \([0-9]*\).*/\1/p')" ]; then
    gec "engine_bridge.test.tpr: $ozet"
  else
    dus "engine_bridge.test.tpr: rc=$rc '$ozet'"; tail -8 "$gun/kopru.log" >&2
  fi
  # Ayni surecte motor_ac -> motor_kapat x5 (Tuzaklar 8ct): taze durum, olu
  # eski id, oturum basina RSS/sanal/thread/vk nesne artisi. Duzeltmesiz
  # koprude ikinci kapanis SIGSEGV (rc 139, ozet yok) -> burada DUSTU.
  kos "$gun/iki_oturum.log" "$tul" --ext "$ext" tests/engine_iki_oturum.test.tpr; rc=$?
  ozet="$(grep -m1 '^Tests:' "$gun/iki_oturum.log")"
  toplam="$(echo "$ozet" | sed -n 's/^Tests: \([0-9]*\).*/\1/p')"
  # Tests: 0 = kurulum dustu ve test HIC kosmadi (GPU varken) -> yesil DEGIL.
  if [ $rc -eq 0 ] && echo "$ozet" | grep -q 'Fail: 0' && [ -n "$toplam" ] && [ "$toplam" -gt 0 ] \
     && [ "$toplam" = "$(echo "$ozet" | sed -n 's/.*Pass: \([0-9]*\).*/\1/p')" ]; then
    gec "engine_iki_oturum.test.tpr: $ozet"
    grep '\[bilgi\] 4 ek oturum' "$gun/iki_oturum.log" | sed 's/^ */    /'
  else
    dus "engine_iki_oturum.test.tpr: rc=$rc '$ozet'"; grep -E '\[bilgi\]|FAIL|ATLANDI' "$gun/iki_oturum.log" | tail -10 >&2
  fi
  # engine_taban_bellek /proc/self/smaps dokumlerini CALISMA DIZININE (tulpar/)
  # yazar (elle olcum icin; taban_bellek.py okur). Dogrulama kosusu agacta iz
  # birakmasin (2026-10-02'de iki kosudan sonra tulpar/ altinda 560 KB'lik doku
  # izlenmeden duruyordu): kullanicinin onceden aldigi doku kosudan once
  # kenara alinip sonra GERI KONUYOR (ornek ayni adla ustune yazar), yoksa
  # kosunun urettigi siliniyor.
  smaps_yedek="$gun/smaps_yedek"; mkdir -p "$smaps_yedek"
  for f in smaps_once.txt smaps_k60.txt; do [ -e "$f" ] && mv "$f" "$smaps_yedek/"; done
  for o in engine_ilk_oyun engine_arena engine_karakter engine_kanca_olcumu engine_taban_bellek engine_betik_dagitimi; do
    kos "$gun/$o.log" env TULPAR_ENGINE_HEADLESS=60 "$tul" --ext "$ext" "examples/$o.tpr"; rc=$?
    if [ "$o" = engine_taban_bellek ]; then
      rm -f smaps_once.txt smaps_k60.txt
      for f in "$smaps_yedek"/smaps_*.txt; do [ -e "$f" ] && mv "$f" .; done
    fi
    if [ $rc -eq 0 ] && grep -q "kapanis:" "$gun/$o.log"; then gec "$o (60 kare): rc=0"
    else dus "$o (60 kare): rc=$rc"; tail -6 "$gun/$o.log" >&2; fi
  done
fi

echo "tulpar dogrulama: $n_gecti gecti, $n_dustu dustu, $n_atlandi atlandi"
if [ $fail -ne 0 ]; then hata "tulpar + motor eklentisi DOGRULANAMADI"; exit 1; fi
if [ $n_atlandi -gt 0 ]; then iyi "tulpar + motor eklentisi calisiyor — $n_atlandi kapi ATLANDI (olculmedi; sebepleri yukarida: GPU yok ya da --uzun-atla)"
else iyi "tulpar + motor eklentisi calisiyor"; fi
