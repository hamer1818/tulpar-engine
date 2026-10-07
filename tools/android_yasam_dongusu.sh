#!/bin/bash
# ============================================================
# Android yasam dongusu kapisi (Geri bildirim #18). Fiziksel cihazda / emulatorde.
#
# tulpar/tests/android_yasam_dongusu.tpr'yi APK olarak kurar, pencereli baslatir,
# kosarken sisteme bellek baskisi bildirir (`am send-trim-memory`) ve logcat'i
# AKIS olarak toplar. GECER:
#   - hic `android cmd ?` satiri yok (glue'nun her komutu adli; eskiden her
#     acilista 4-5 adsiz satir: INPUT_CHANGED, WINDOW_REDRAW_NEEDED,
#     CONTENT_RECT_CHANGED, SAVE_STATE);
#   - yasam dongusunun cekirdegi goruldu (START, RESUME, INIT_WINDOW) — denetim
#     bir sey olcmus olsun;
#   - oyun kendi sonuna kadar kostu, hata 0, cokme yok.
# Bilgi (kapi degil): LOW_MEMORY geldiyse "DUSUK BELLEK uyarisi #N (RSS ..)"
# satiri ve kapanistaki toplam. `send-trim-memory` her surumde onLowMemory'ye
# donusmez — gelmediyse not basilir.
#
#   tools/android_yasam_dongusu.sh
#   TULPAR_EXT_DIR=<eski paket> tools/android_yasam_dongusu.sh   # pozitif kontrol
#
# Gerekenler: android_iki_oturum.sh ile ayni (tulpar >= K303, yapi/tulpar-ext/
# android/<abi>/, TULPAR_ANDROID_LIB_DIR; birden cok cihazda ANDROID_SERIAL).
# Olculdu 2026-10-08, Huawei P20 Pro (CLT-L09, Mali-G72, Android 10): bu dalla
# GECTI; pozitif kontrol (main'in arsivi) KIRMIZI — ayrinti PR'da.
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=dev.tulparlang.android_yasam_dongusu
ACT=$PKG/android.app.NativeActivity
EXT="${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext}"
command -v tulpar >/dev/null || { echo "HATA: PATH'te tulpar yok"; exit 1; }
if [ -n "${ANDROID_SERIAL:-}" ]; then
  adb devices | grep -q "^$ANDROID_SERIAL[[:space:]]*device$" \
    || { echo "HATA: ANDROID_SERIAL=$ANDROID_SERIAL bagli degil"; adb devices; exit 1; }
else
  [ "$(adb devices | grep -c 'device$')" -eq 1 ] \
    || { echo "HATA: adb'de tam bir cihaz olmali (birden coksa ANDROID_SERIAL=<seri> verin)"; adb devices; exit 1; }
fi
echo "cihaz: $(adb shell getprop ro.product.model | tr -d '\r') / Android $(adb shell getprop ro.build.version.release | tr -d '\r') / $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
GUN=$(mktemp -d)
trap 'rm -rf "$GUN"' EXIT
cp "$ROOT/tulpar/tests/android_yasam_dongusu.tpr" "$GUN/"
(cd "$GUN" && TULPAR_AOT_NOCACHE=1 tulpar --ext "$EXT" build --target=android android_yasam_dongusu.tpr android_yasam_dongusu --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
adb install -r "$GUN/android_yasam_dongusu.apk" >/dev/null || { echo "HATA: adb install"; exit 1; }
adb shell am force-stop "$PKG"
# Ekran kapaliyken etkinlik pencere almaz (android_iki_oturum.sh notu).
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -c
adb logcat -v pid -s tulpar:I > "$GUN/tulpar.log" 2>/dev/null &
LOGCAT_PID=$!
trap 'kill $LOGCAT_PID 2>/dev/null; rm -rf "$GUN"' EXIT
adb shell am start -W -n "$ACT" >/dev/null
# Satirlar BU surecin PID'iyle suzulur: cihazda baska bir Tulpar oyunu da
# "tulpar" etiketiyle loglar ve on plana gecisimizde kendi PAUSE/STOP'unu
# basar (olculdu P20 Pro 2026-10-08: kurulu oyunun satirlari karisti).
PID=$(adb shell pidof "$PKG" | tr -d '\r')
sleep 4
adb shell am send-trim-memory "$PKG" COMPLETE >/dev/null 2>&1
for i in $(seq 1 60); do
  grep -q "android: bitti" "$GUN/tulpar.log" && break
  adb logcat -d -b crash | grep -q "Fatal signal" && break
  sleep 1
done
sleep 1
kill $LOGCAT_PID 2>/dev/null
adb shell am force-stop "$PKG"
adb uninstall "$PKG" >/dev/null 2>&1
[ -n "$PID" ] || { echo "HATA: surec baslamadi (pidof bos)"; exit 1; }
# Iki bicim: standart `-v pid` ("I( 1234) ...") ve EMUI'nin onu yok sayip bastigi
# threadtime ("10-07 21:27:39.148 +0000  1234  1250 I tulpar: ...").
LOG=$(tr -d '\r' < "$GUN/tulpar.log" | grep -E "^[A-Z]\( *$PID\)|^[0-9-]+ [0-9:.]+ +(\+[0-9]+ +)?$PID ")
grep -E "android cmd|DUSUK BELLEK|host: kapatildi|\[yd\] bitis" <<< "$LOG" | sed 's/^.*\[engine_bridge\] /  /;s/^.*\[yd\]/  [yd]/'
HATA=0
[ "$(adb logcat -d -b crash | grep -c 'Fatal signal')" -eq 0 ] || { echo "  COKME:"; adb logcat -d -b crash | head -20; HATA=1; }
n_adsiz=$(grep -c "android cmd ?" <<< "$LOG")
[ "$n_adsiz" -eq 0 ] || { echo "  adsiz komut satiri: $n_adsiz (her glue komutu adli olmali)"; HATA=1; }
for c in START RESUME INIT_WINDOW; do
  grep -q "android cmd $c\$" <<< "$LOG" || { echo "  '$c' gorulmedi — yasam dongusu olculmedi"; HATA=1; }
done
grep -q "\[yd\] bitis kare=.* hata=0" <<< "$LOG" || { echo "  oyun sonuna kadar kosmadi ya da hata var"; HATA=1; }
grep -q "DUSUK BELLEK" <<< "$LOG" || echo "  not: LOW_MEMORY gelmedi (send-trim-memory bu surumde onLowMemory'ye donusmedi) — sayac olculmedi"
if [ "$HATA" -ne 0 ]; then echo "android yasam dongusu: DUSTU"; exit 1; fi
echo "android yasam dongusu: GECTI"
