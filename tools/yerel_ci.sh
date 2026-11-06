#!/usr/bin/env bash
# YEREL CI (2026-11-04) — GitHub'a gondermeden ONCE, Linux CI isinin (ci.yml
# "linux" + "linux-savas") yaptigini bu makinede yapar. Amac: her degisiklikte
# CI beklemek yerine yerelde dogrula, GitHub'a yalniz kilometre tasinda git.
#
#   tools/yerel_ci.sh                 tam tur (katman, derleme, testler + CI'in
#                                     ozet kurallari + iki pozitif kontrol, clang
#                                     sozdizimi, Tulpar koprusu --tam)
#   tools/yerel_ci.sh --hizli         derleme + engine_tests + kurallar + Faz 8
#                                     shader/yerlesim kapilari (kopru yok)
#   tools/yerel_ci.sh --lavapipe      engine_tests'i CI'in surucusuyle de kostur
#                                     (mesa lavapipe; paket: vulkan-swrast)
#   tools/yerel_ci.sh --filtre rhi    yalniz o testler (alt dize)
#
# CI'dan FARKI (bilerek, ozette yazilir):
#   * macOS (MoltenVK, AArch64 fiber assembly'si) ve Windows (MSYS2 +
#     SwiftShader) yerelde YOK — onlari yalniz GitHub olcer. Yerine clang
#     sozdizimi denetimi (macOS'un derleyicisi clang: GCC'nin gecirdigi
#     sablon hatalarini yakalar, Tuzaklar/clang_syntax_check.sh).
#   * Varsayilan surucu RTX (NVIDIA); CI lavapipe. --lavapipe ikisini de olcer.
#   * tulpar: CI yayinlanmis SON surumu indirir; burada PATH'teki tulpar
#     (`tulpar update` ile ayni surum olmali — surum basilir).
# Cikis: 0 = hepsi yesil; degilse ilk kirmizi asamanin adi basilir.
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
cd "$KOK"
HIZLI=0; LVP=0; FILTRE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --hizli) HIZLI=1 ;;
    --lavapipe) LVP=1 ;;
    --filtre) FILTRE="${2:-}"; shift ;;
    -h|--help) sed -n 2,23p "$0"; exit 0 ;;
    *) echo "bilinmeyen secenek: $1" >&2; exit 2 ;;
  esac
  shift
done
IS="${TMPDIR:-/tmp}/yerel_ci.$$"
mkdir -p "$IS"
trap 'rm -rf "$IS"' EXIT
OZET=()
kirmizi=""
asama() { # ad, sure, sonuc
  OZET+=("$(printf '%-34s %6ss  %s' "$1" "$2" "$3")")
  case "$3" in HATA*) [ -z "$kirmizi" ] && kirmizi="$1" ;; esac
}
zaman() { date +%s; }

# CI'in "Testler + kapi" adimindaki kurallarin AYNISI (ci.yml). $1 log, $2 etiket.
test_kurallari() {
  local log="$1" ozet gecen kalan atlanan kosan kayitli
  ozet="$(grep -E '^engine tests: [0-9]+ passed' "$log" | tail -1 || true)"
  [ -n "$ozet" ] || { echo "HATA ozet satiri yok (cokme/asilma)"; return; }
  gecen=$(sed -E 's/^engine tests: ([0-9]+) passed.*/\1/' <<< "$ozet")
  kalan=$(sed -E 's/.*, ([0-9]+) failed.*/\1/' <<< "$ozet")
  atlanan=$(sed -E 's/.*, ([0-9]+) atlandi.*/\1/' <<< "$ozet")
  kosan=$(sed -E 's/.*\(([0-9]+)\/[0-9]+ kosuldu\).*/\1/' <<< "$ozet")
  kayitli=$(sed -E 's/.*\([0-9]+\/([0-9]+) kosuldu\).*/\1/' <<< "$ozet")
  if grep -q 'KAYIT TASMASI' "$log"; then echo "HATA kayit tasmasi (Registry::kMax)"; return; fi
  if [ -z "$FILTRE" ] && [ "$kosan" != "$kayitli" ]; then echo "HATA kayitli $kayitli, kosan $kosan"; return; fi
  if [ "$kalan" != 0 ] || [ "$gecen" -le 0 ]; then echo "HATA $kalan kirmizi ($gecen gecti)"; return; fi
  if grep -E 'ATLANDI:.*Vulkan' "$log" | grep -qv tukendi; then echo "HATA Vulkan kapilari atlandi (surucu/loader?)"; return; fi
  if grep -qE 'ATLANDI:.*VK_LAYER_KHRONOS_validation' "$log"; then echo "HATA dogrulama katmani kapilari atlandi"; return; fi
  echo "OK $gecen gecti, $atlanan atlandi ($kosan/$kayitli)"
}

# 1) Katman
t=$(zaman); r="OK"; python3 tools/layer_check.py . > "$IS/katman.log" 2>&1 || r="HATA $(tail -1 "$IS/katman.log")"
asama "katman denetimi" $(( $(zaman) - t )) "$r"

# 2) Derleme (kapilar CMake on kosulu: shader/sahne/ikon/baglama)
t=$(zaman); r="OK"
if [ ! -f yapi/build.ninja ] && [ ! -f yapi/Makefile ]; then cmake -S . -B yapi -G Ninja -DCMAKE_BUILD_TYPE=Release > "$IS/cfg.log" 2>&1 || r="HATA yapilandirma"; fi
if [ "$r" = OK ]; then
  nice -n 10 cmake --build yapi -j"$(nproc)" > "$IS/derle.log" 2>&1 || r="HATA $(grep -m1 -E 'error|FAILED' "$IS/derle.log")"
fi
asama "derleme (yapi/)" $(( $(zaman) - t )) "$r"
if [ "$r" != OK ]; then
  printf '%s\n' "${OZET[@]}"; tail -30 "$IS/derle.log"; echo "YEREL CI: KIRMIZI ($kirmizi)"; exit 1
fi

# 3) engine_tests (RTX) + CI kurallari
t=$(zaman)
DISPLAY= ./yapi/engine_tests ${FILTRE:+"$FILTRE"} > "$IS/test.log" 2>&1
asama "engine_tests (yerel surucu)" $(( $(zaman) - t )) "$(test_kurallari "$IS/test.log")"
cp "$IS/test.log" yapi/yerel_ci_test.log

# 4) Iki pozitif kontrol (ci.yml ile ayni ifadeler)
t=$(zaman)
TULPAR_ENGINE_NO_VULKAN=1 DISPLAY= ./yapi/engine_tests rhi_ > "$IS/pk1.log" 2>&1 || true
n1=$(grep -E 'ATLANDI:.*Vulkan' "$IS/pk1.log" | grep -cv tukendi || true)
bos="$IS/bos_katman"; mkdir -p "$bos"
VK_LAYER_PATH="$bos" VK_ADD_LAYER_PATH="$bos" DISPLAY= ./yapi/engine_tests mali_best_practices > "$IS/pk2.log" 2>&1 || true
VK_LAYER_PATH="$bos" VK_ADD_LAYER_PATH="$bos" DISPLAY= ./yapi/engine_tests validation_layer >> "$IS/pk2.log" 2>&1 || true
n2=$(grep -cE 'ATLANDI:.*VK_LAYER_KHRONOS_validation' "$IS/pk2.log" || true)
r="OK Vulkan atlama $n1 satir, katman atlama $n2 satir (>=5)"
[ "$n1" -gt 0 ] && [ "$n2" -ge 5 ] || r="HATA pozitif kontrol: vulkan $n1, katman $n2"
asama "atlama pozitif kontrolleri" $(( $(zaman) - t )) "$r"

# 5) Lavapipe (istege bagli): CI Linux isinin surucusu
if [ "$LVP" = 1 ]; then
  t=$(zaman)
  ICD="$(ls /usr/share/vulkan/icd.d/lvp_icd*.json 2>/dev/null | grep -v i686 | head -1)"
  if [ -z "$ICD" ]; then
    asama "engine_tests (lavapipe)" 0 "HATA lavapipe yok (sudo pacman -S vulkan-swrast)"
  else
    VK_DRIVER_FILES="$ICD" VK_ICD_FILENAMES="$ICD" DISPLAY= ./yapi/engine_tests ${FILTRE:+"$FILTRE"} > "$IS/lvp.log" 2>&1
    cp "$IS/lvp.log" yapi/yerel_ci_lavapipe.log
    asama "engine_tests (lavapipe)" $(( $(zaman) - t )) "$(test_kurallari "$IS/lvp.log")"
  fi
fi

# 5b) Faz 8 shader kapilari + CPU-GPU yerlesim (2026-11-05). Uc denetim de
# "elle kosulur, otomasyonda degil" idi ve CURUDU: 2026-09-27'de 18/19 bayt
# ayni olan kapi 2026-11-05'te 14/19'du (4 kaynak GLSL degismis, .tprs
# tasinmamis) ve rc=0 + "ATLANDI" ile yesil gorunuyordu. Burada ATLANDI da
# KIRMIZI: GLSL degistiren, .tprs'i de tasir (ya da bunu bilerek soyler).
# glslc gerekir (CI'da yok — bu yuzden yalniz yerel CI'da).
t=$(zaman); r="OK"
if ! command -v glslc >/dev/null; then
  r="HATA glslc yok (shaderc) — Faz 8 bayt kapisi olcemez"
else
  python3 tools/faz8_shader_audit.py > "$IS/faz8.log" 2>&1; rc8=$?
  s8="$(grep -E '^Faz 8 shader denetimi:' "$IS/faz8.log" | tail -1)"
  b8="$(grep -oE '[0-9]+/[0-9]+ dosya referans' "$IS/faz8.log" | head -1 | cut -d' ' -f1)"
  TULPAR_BIN="$(command -v tulpar || true)" TPRS_ZORUNLU=1 \
    python3 tools/tpr_shader.py --tulpar-parse > "$IS/tprs.log" 2>&1; rcp=$?
  sp="$(grep -E 'dosya bugunku Tulpar' "$IS/tprs.log" | tail -1 | cut -d' ' -f1)"
  python3 tools/layout_audit.py > "$IS/yerlesim.log" 2>&1; rcl=$?
  cat "$IS/faz8.log" "$IS/tprs.log" "$IS/yerlesim.log" > yapi/yerel_ci_faz8.log
  if [ "$rc8" != 0 ] || [ -z "$s8" ]; then r="HATA faz8: ${s8:-ozet yok}"
  elif ! grep -qE ', 0 atlandi$' <<< "$s8"; then r="HATA faz8 ATLANDI: $(grep -m1 ATLANDI "$IS/faz8.log" | sed 's/^ *//')"
  elif [ "$rcp" != 0 ]; then r="HATA tulpar gramer denetimi (rc $rcp)"
  elif [ "$rcl" != 0 ]; then r="HATA yerlesim denetimi (rc $rcl)"
  else r="OK bayt ayni $b8, gramer temiz $sp, yerlesim temiz"; fi
fi
asama "Faz 8 shader + yerlesim" $(( $(zaman) - t )) "$r"

# 5b2) Editor UX kapisi (docs/EDITOR-UYGULAMA-PLANI.md U1): dort cozunurlukte kesik metin 0,
# Stats gorunumden tasmaz, 1920x1080'de 3B gorunum orani >= 0.45. Pozitif kontrol C++ testinde
# (editor_ux_imgui_kancasi_kesik_metni_gorur: ImGui yamasi canli mi).
t=$(zaman); r="OK"
UX_DIZIN="$IS/ux" bash tools/editor_ux.sh --kapi > "$IS/ux.log" 2>&1 || r="HATA $(grep -m1 KIRMIZI "$IS/ux.log")"
cp "$IS/ux.log" yapi/yerel_ci_ux.log
[ "$r" = OK ] && r="OK $(awk '/^1920x1080/{print "1920x1080 kesik "$2", gorunum "$4}' "$IS/ux.log")"
asama "Editor UX (kesik/tasma/gorunum)" $(( $(zaman) - t )) "$r"

# 5c) Oyun fabrikasi (docs/FABRIKA.md asama 2): sablondan uretilen oyun ilk gun dogrulamadan
# gecmeli; POZITIF KONTROL YENI_OYUN_BOZ=1 (sablona bozuk satir) KIRMIZI donmeli. Ikisi paralel.
t=$(zaman); r="OK"
fab="$IS/fabrika"; mkdir -p "$fab"
bash tools/yeni_oyun.sh "$fab/iyi" --kimlik ci_oyun --onek CIO --tr "CI Oyunu" --en "CI Game" > "$IS/fab_iyi.log" 2>&1 & p1=$!
YENI_OYUN_BOZ=1 bash tools/yeni_oyun.sh "$fab/boz" --kimlik ci_boz --onek CIB --tr "CI Bozuk" --en "CI Broken" > "$IS/fab_boz.log" 2>&1 & p2=$!
wait $p1; rf1=$?; wait $p2; rf2=$?
cat "$IS/fab_iyi.log" "$IS/fab_boz.log" > yapi/yerel_ci_fabrika.log
if [ $rf1 != 0 ]; then r="HATA yeni_oyun: $(grep -m1 -E '✗|HATA|KIRMIZI' "$IS/fab_iyi.log")"
elif [ $rf2 = 0 ]; then r="HATA pozitif kontrol: bozuk sablon YESIL gecti"
else
  # Uretilen oyunun Android APK'si (x86_64 debug) derlenip APK'nin KENDISINDEN dogrulanir
  # (paket, targetSdk, ABI, izin kumesi, ikon, varlik listesi). Cihaz kosumu (tools/android_dene.sh)
  # cihaz ister; burada yok.
  # Kopru Android arsivleri cmake --build ile YENILENMEZ (ayri NDK yapisi): yeni builtin eklenince
  # APK linki "undefined symbol teng_..." ile duser (2026-11-06, U9). Once tazelenir (ccache ile ~10 s).
  bash tools/build_bridge_android.sh > "$IS/fab_kopru_android.log" 2>&1 || echo "UYARI: kopru Android arsivleri derlenemedi" >> "$IS/fab_apk.log"
  ( cd "$fab/iyi" && CIKTI="$fab/apk" DEBUG=1 ./apk_derle.sh x86_64 ) >> "$IS/fab_apk.log" 2>&1; ra=$?
  cat "$IS/fab_apk.log" >> yapi/yerel_ci_fabrika.log
  if [ $ra != 0 ]; then r="HATA APK: $(grep -m1 -E 'HATA' "$IS/fab_apk.log")"
  else r="OK $(grep -m1 'dogrulama:' "$IS/fab_iyi.log"); APK $(du -h "$fab"/apk/*.apk | cut -f1) dogrulandi; bozuk sablon kirmizi"; fi
fi
asama "Fabrika: yeni_oyun (arena)" $(( $(zaman) - t )) "$r"

# 5d) Dagitim paketi (tools/package.sh: paketle + tamlik + eklenti kosusu + kurulu duzende Yeni oyun).
# CI'da her PR'da kosuyordu ama yerelde YOKTU: U3a'dan (2026-11-05) beri varlik turetmesi duruyordu
# ve ancak 2026-11-06'da elle kosunca goruldu.
t=$(zaman); r="OK"
bash tools/package.sh yapi "$IS/paket" > "$IS/paket.log" 2>&1 || r="HATA $(grep -m1 -E 'HATA|EKSIK|FABRIKA|KOSU' "$IS/paket.log")"
cp "$IS/paket.log" yapi/yerel_ci_paket.log
[ "$r" = OK ] && r="OK $(du -sh "$IS/paket" | cut -f1); $(grep -c '^  var ' "$IS/paket.log") dosya var; fabrika + eklenti kosusu + pozitif kontroller"
asama "Paket (package.sh)" $(( $(zaman) - t )) "$r"

# 5e) Linux AppImage (EDITOR-UYGULAMA-PLANI U7b): paketten tek dosya; ici SALT OKUNUR acilip
# kaynaksiz kosar, masaustu kaydi, POZITIF KONTROL sahnesiz AppImage duser. Paket yoksa kosmaz.
t=$(zaman); r="OK"
if [ -x "$IS/paket/engine_editor" ]; then
  bash tools/kurulum/appimage.sh "$IS/paket" "$IS/Tulpar_Editor-x86_64.AppImage" > "$IS/appimage.log" 2>&1 || \
    r="HATA $(grep -m1 -E 'KIRMIZI|HATA|BOZUK' "$IS/appimage.log")"
  [ "$r" = OK ] && r="OK $(du -h "$IS/Tulpar_Editor-x86_64.AppImage" | cut -f1); salt okunur kosum + masaustu kaydi + pozitif kontrol"
else
  r="HATA paket yok (5d kirmizi)"
fi
cp "$IS/appimage.log" yapi/yerel_ci_appimage.log 2>/dev/null || true
asama "AppImage (Linux kurulum)" $(( $(zaman) - t )) "$r"

if [ "$HIZLI" = 0 ]; then
  # 6) clang sozdizimi (macOS ayaginin derleyici vekili)
  t=$(zaman); r="OK"
  bash tools/clang_syntax_check.sh > "$IS/clang.log" 2>&1 || r="HATA $(grep -m1 '^== ' "$IS/clang.log")"
  grep -q ATLANDI "$IS/clang.log" && r="ATLANDI clang++ yok"
  asama "clang sozdizimi (macOS vekili)" $(( $(zaman) - t )) "$r"

  # 7) Tulpar koprusu --tam (CI: yayinlanmis tulpar + eklenti paketi + savas)
  t=$(zaman)
  if command -v tulpar >/dev/null; then
    tv="$(tulpar --version 2>&1 | head -1)"
    DISPLAY= bash tools/tulpar_dogrula.sh --tam > "$IS/tulpar.log" 2>&1
    s="$(grep -E '^tulpar dogrulama:' "$IS/tulpar.log" | tail -1)"
    cp "$IS/tulpar.log" yapi/yerel_ci_tulpar.log
    if [ -z "$s" ]; then r="HATA ozet yok"
    elif grep -qE ' [1-9][0-9]* dustu' <<< "$s"; then r="HATA $s"
    else r="OK $s ($tv)"; fi
  else
    r="HATA PATH'te tulpar yok"
  fi
  asama "Tulpar koprusu --tam" $(( $(zaman) - t )) "$r"
fi

echo
echo "== YEREL CI ozeti ($(git rev-parse --abbrev-ref HEAD) @ $(git rev-parse --short HEAD)$(git diff --quiet || echo ' + commitlenmemis'))"
printf '%s\n' "${OZET[@]}"
echo "OLCULMEDI (yalniz GitHub): macOS arm64 (MoltenVK, AArch64 asm), Windows (MSYS2 + SwiftShader)$([ "$LVP" = 0 ] && echo ', lavapipe (--lavapipe ile olculur)')"
echo "Gunlukler: yapi/yerel_ci_*.log"
if [ -n "$kirmizi" ]; then echo "YEREL CI: KIRMIZI ($kirmizi)"; exit 1; fi
echo "YEREL CI: YESIL"
