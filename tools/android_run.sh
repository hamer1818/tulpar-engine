#!/bin/bash
# ============================================================
# Tulpar Engine — telefonda kosum (adb). NDK ile derler, NativeActivity APK'sini
# paketler (TulparLang'in package_apk.sh'i), kurar, secilen kipte baslatir ve logu toplar.
#
# Paketleyici: TULPAR_ANDROID_TOOLS (dizin), yoksa kurulu tulpar'in yanindaki android/
# (TulparLang v3.44.0+: `tulpar update --android`). Motor TulparLang'den ayrildiginda
# (2026-09-20) android/ bu depoda kalmadi; betik yine "$ROOT/android/package_apk.sh"
# ariyordu ve `|| true` yuzunden APK'siz devam edip ESKI kurulu APK'yi kosturuyordu.
#
#   tools/android_run.sh tests [filtre]   # engine_tests APK surecinde
#   tools/android_run.sh demo [kare] [--screenshot cikti.png]
#
# Neden APK: adb shell'deki ikili GPU'yu goremiyor (vkEnumeratePhysicalDevices 0,
# SELinux/HAL, Huawei P20 Pro ile olculdu 2026-09-14). Uygulama sureci gorur.
# Cikti: logcat "tulpar" etiketi + files/engine_log.txt (run-as ile cekilir).
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # depo koku
MODE="${1:-tests}"; shift || true
. "$ROOT/tools/android_adb_hazir.sh"
adb_hazir || exit 1   # Geri bildirim #23: kopuk cihazda sonsuz bekleme yok
NDK="${TULPAR_ANDROID_NDK:-}"
if [ -z "$NDK" ]; then
    NDK=$(ls -d "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/android-ndk-* 2>/dev/null | sort -V | head -1)
fi
[ -z "$NDK" ] && { echo "HATA: NDK yok (TULPAR_ANDROID_NDK)"; exit 1; }
ABI="${TULPAR_ANDROID_ABI:-arm64-v8a}"
# ABI basina ayri build dizini (emulator x86_64, telefon arm64-v8a).
if [ "$ABI" = "arm64-v8a" ]; then BUILD="$ROOT/build-android"; else BUILD="$ROOT/build-android-$ABI"; fi
PKG="dev.tulparlang.engine"
ACT="$PKG/android.app.NativeActivity"

echo "[1/5] derleme (NDK: $(basename "$NDK"), $ABI)"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release -DENGINE_TRACY="${TULPAR_TRACY:-OFF}" -DENGINE_SWAPPY="${TULPAR_SWAPPY:-OFF}" >/dev/null 2>&1
cmake --build "$BUILD" -j"${TULPAR_JOBS:-6}" --target tulparengine engine_tests 2>&1 | grep -E "error|warning: unused" | grep -v third_party || true
[ -f "$BUILD/libtulparengine.so" ] || { echo "HATA: libtulparengine.so uretilmedi"; exit 1; }

echo "[2/5] paketleme"
STAGE="$BUILD/tulparengine_apk"
rm -rf "$STAGE"; mkdir -p "$STAGE/lib/$ABI"
cp "$BUILD/libtulparengine.so" "$STAGE/lib/$ABI/"
# Dogrulama katmani (Khronos, BestPractices+Arm = Mali linter): tests kipinde ve
# TULPAR_VALIDATION=1 ile APK'ya girer; yukleyici debuggable uygulamanin lib
# dizininden katman alir (Android 9+). Yoksa test ATLANDI der (sessiz yesil yok).
VVL="$ROOT/third_party/vvl-android/$ABI/libVkLayer_khronos_validation.so"
if [ "$MODE" = tests ] || [ "${TULPAR_VALIDATION:-0}" = 1 ]; then
    if [ -f "$VVL" ]; then cp "$VVL" "$STAGE/lib/$ABI/"; echo "  dogrulama katmani APK'da ($(du -h "$VVL" | cut -f1))"
    else echo "  UYARI: dogrulama katmani yok -> tools/fetch_vvl_android.sh (BestPractices testi ATLANDI olacak)"; fi
fi
cp "$ROOT/platform/android/AndroidManifest.xml" "$STAGE/"
# Swappy: Java simi (SwappyDisplayManager) uygulamanin KENDI sinif yukleyicisinden
# gelmeli; Swappy'nin bellekten yukledigi kopya lib dizinini goremez ve ilk sunum
# asilir (Tuzaklar 8v; telefonda Android 10'da da olculdu). fetch_swappy.sh
# libswappy_static.a icine gomulu dex'i classes.dex olarak cikarir; hasCode=true.
SWAPPY_DEX="$ROOT/third_party/swappy/libs/classes.dex"
if [ "${TULPAR_SWAPPY:-OFF}" = ON ] && [ "${TULPAR_SWAPPY_DEX:-1}" = 1 ] && [ -f "$SWAPPY_DEX" ]; then
    cp "$SWAPPY_DEX" "$STAGE/classes.dex"
    sed -i 's/android:hasCode="false"/android:hasCode="true"/' "$STAGE/AndroidManifest.xml"
    echo "  swappy: classes.dex ($(stat -c %s "$SWAPPY_DEX") bayt) + hasCode=true"
fi
# Yalniz duz dosyalar: tests/assets/shader/ (Faz 8 shader kapilarinin .tprs kaynaklari, #68) bir
# DIZIN; duz `cp` onu atlayip 1 donuyordu ve set -e betigi paketlemeden once sessizce bitiriyordu.
mkdir -p "$STAGE/assets" && find "$ROOT/tests/assets" -maxdepth 1 -type f -exec cp {} "$STAGE/assets/" \; \
    && cp "$ROOT"/assets/fonts/*.ttf "$STAGE/assets/" # APK icine (host cikarir)
PAKETLE="${TULPAR_ANDROID_TOOLS:+$TULPAR_ANDROID_TOOLS/package_apk.sh}"
if [ -z "$PAKETLE" ] && command -v tulpar >/dev/null; then PAKETLE="$(dirname "$(command -v tulpar)")/android/package_apk.sh"; fi
[ -f "${PAKETLE:-}" ] || { echo "HATA: package_apk.sh yok (TULPAR_ANDROID_TOOLS=<dizin> ya da 'tulpar update --android')"; exit 1; }
rm -f "$BUILD/tulparengine.apk"
"$PAKETLE" "$STAGE" "$BUILD/tulparengine.apk" | grep -E "^\s+\+|HATA|apk" || true
[ -s "$BUILD/tulparengine.apk" ] || { echo "HATA: APK uretilmedi ($PAKETLE)"; exit 1; }

echo "[3/5] kurulum"
# Kurulum dusunce DUR: eskiden `| tail -1` dususu yutuyordu ve betik telefondaki ESKI APK'yi kosturup
# onun eski logunu "sonuc" diye basiyordu (2026-11-02: targetSdk 0 -> -26, 157 testlik Eylul kurulumu kostu).
KUR="$(adb install -r "$BUILD/tulparengine.apk" 2>&1)"; echo "$KUR" | tail -1
grep -q '^Success' <<<"$KUR" || { echo "HATA: kurulum basarisiz — eski APK kosturulmayacak"; exit 1; }
adb shell am force-stop "$PKG" || true

FILTER=""; FRAMES="600"; SHOT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --screenshot) SHOT="$2"; shift 2;;
        *) if [ "$MODE" = tests ]; then FILTER="$1"; else FRAMES="$1"; fi; shift;;
    esac
done
# Bos deger adb shell'de kaybolur: tirnakla.
adb shell "setprop debug.tulpar.mode '$MODE'; setprop debug.tulpar.filter '$FILTER'; setprop debug.tulpar.frames '$FRAMES'; setprop debug.tulpar.present '${TULPAR_PRESENT:-fifo}'; setprop debug.tulpar.prerotate '${TULPAR_PREROTATE:-1}'; setprop debug.tulpar.size '${TULPAR_SIZE:-2159x1080}'; setprop debug.tulpar.validation '${TULPAR_VALIDATION:-0}'; setprop debug.tulpar.audio '${TULPAR_AUDIO:-0}'; setprop debug.tulpar.swappy '$([ "${TULPAR_SWAPPY:-OFF}" = ON ] && echo 1 || echo 0)'; setprop debug.tulpar.swappy_family '${TULPAR_SWAPPY_FAMILY:-1}'; setprop debug.tulpar.dtprobe '${TULPAR_DTPROBE:-0}'"

if [ "${TULPAR_TRACY:-OFF}" = ON ]; then adb forward tcp:8086 tcp:8086 >/dev/null && echo "  tracy: adb forward 8086 (masaustunde tracy-capture -a 127.0.0.1)"; fi
echo "[4/5] baslat: $MODE"
# Cikti dosyasi: uygulamanin harici dizini (adb pull ile okunur; logcat halkasi
# Huawei'de dakikalar icinde tasiyor — dosya asil kaynak, logcat yedek).
EXT="/sdcard/Android/data/$PKG/files"
LOGF="$EXT/engine_log.txt"
adb shell "rm -f '$LOGF'" 2>/dev/null || true
adb logcat -G 8M 2>/dev/null || true
adb logcat -c 2>/dev/null || true
adb shell am start -W -n "$ACT" >/dev/null
START=$(date +%s)
if [ -n "$SHOT" ]; then
    sleep 4
    adb exec-out screencap -p > "$SHOT"
    echo "  ekran goruntusu -> $SHOT"
fi
# Bitisi bekle: dosyada "[android] bitti" ya da cokme (en cok 300 s).
while true; do
    if adb shell "cat '$LOGF' 2>/dev/null" | grep -q "\[android\] bitti"; then break; fi
    if adb logcat -d -b crash 2>/dev/null | grep -q "Fatal signal"; then
        adb logcat -d -b crash 2>/dev/null > "$BUILD/engine_crash_${MODE}.txt"
        echo "  COKTU (Fatal signal) -> $BUILD/engine_crash_${MODE}.txt ($(wc -l < "$BUILD/engine_crash_${MODE}.txt") satir):"
        grep -E "Fatal signal|Abort message|backtrace|#[0-9]+ pc|^.* --- --- |tid=|sysTid" "$BUILD/engine_crash_${MODE}.txt" | head -40; break
    fi
    # Surec oldu ama ne "bitti" ne tombstone (Huawei debuggerd'i logcat'e yazmiyor): bekleme.
    if [ $(( $(date +%s) - START )) -gt 6 ] && ! adb shell pidof "$PKG" >/dev/null 2>&1; then echo "  SUREC OLDU (bitti satiri yok; cokme/abort)"; break; fi
    if [ $(( $(date +%s) - START )) -gt 300 ]; then echo "  ZAMAN ASIMI (300 s)"; break; fi
    sleep 2
done

echo "[5/5] log"
OUT="$BUILD/engine_log_${MODE}.txt"
if ! adb pull "$LOGF" "$OUT" >/dev/null 2>&1; then
    echo "  (dosya cekilemedi, logcat yedegi)"
    adb logcat -d -s tulpar:I 2>/dev/null | sed -E 's/^.*tulpar *: //' > "$OUT"
fi
echo "  -> $OUT ($(wc -l < "$OUT") satir); cihaz dizini: $EXT"
grep -E "^engine tests|FAIL|ATLANDI|\[bilgi\]|\[engine_demo\]|\[android\]|\[fatal\]" "$OUT" | head -80
