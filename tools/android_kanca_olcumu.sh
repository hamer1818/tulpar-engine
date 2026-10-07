#!/bin/bash
# ============================================================
# Android: betik kancasi dagitiminin CIHAZDAKI maliyeti (Geri bildirim #14).
#
# tulpar/tests/android_kanca_olcumu.tpr'yi APK olarak kurar, pencereli (vsync)
# kosturur ve dort oturumun kapanis satirlarini toplar:
#   "kapanis (betik dagitimi): kare icinde N kanca cagrisi, X ms, cagri basina
#    Y ns (kanca govdeleri dahil); asama K kare, kare basina Z us"
# Bu bir OLCUM, kapi degil: sayilar cihaz verisidir (basilir, iddia edilmez).
# Betigin kendi denetimi: dort oturumun hepsi rapor verdi mi, hata 0 mi, cokme
# yok mu — rapor eksikse olcum OLMADI demektir (kirmizi).
#
#   tools/android_kanca_olcumu.sh
#
# Gerekenler: android_iki_oturum.sh ile ayni (PATH'te tulpar >= K303,
# yapi/tulpar-ext/android/<abi>/, TULPAR_ANDROID_LIB_DIR; birden cok cihazda
# ANDROID_SERIAL). Ekran kapaliysa uyandirilir; kilit ekrani elle acilmali —
# pencere gelmezse kopru headless kipe duser ve olcum vsync'siz olur (not basilir).
#
# Olculdu 2026-10-08, Huawei P20 Pro (CLT-L09, Kirin 970, Mali-G72, Android 10),
# ayni APK icerigi, eski/yeni motor arsivi sirayla ikiser tur (docs/KOPRU.md 7.9):
#   F_oyun_benzeri  eski 2824-3492 ns/cagri (kare basina 85-105 us)
#                   yeni 1060-1506 ns/cagri (kare basina 32-45 us)
#   A_bos200        eski 482-566 ns, yeni 372-551 ns (fark gurultu icinde)
# Masaustu (Ryzen 7 9800X3D, penceresiz): F 36 -> 22 ns, A 5.8-6.2 ns (degismedi).
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=dev.tulparlang.android_kanca_olcumu
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
echo "cihaz: $(adb shell getprop ro.product.model | tr -d '\r') / $(adb shell getprop ro.hardware.egl | tr -d '\r') / Android $(adb shell getprop ro.build.version.release | tr -d '\r') / $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
GUN=$(mktemp -d)
trap 'rm -rf "$GUN"' EXIT
cp "$ROOT/tulpar/tests/android_kanca_olcumu.tpr" "$GUN/"
(cd "$GUN" && TULPAR_AOT_NOCACHE=1 tulpar --ext "$EXT" build --target=android android_kanca_olcumu.tpr android_kanca_olcumu --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
adb install -r "$GUN/android_kanca_olcumu.apk" >/dev/null || { echo "HATA: adb install"; exit 1; }
adb shell am force-stop "$PKG"
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -c
# Log AKIS olarak (android_iki_oturum.sh: EMUI tamponu 256 KiB, `logcat -d` satir kaybeder).
adb logcat -v pid -s tulpar:I > "$GUN/tulpar.log" 2>/dev/null &
LOGCAT_PID=$!
trap 'kill $LOGCAT_PID 2>/dev/null; rm -rf "$GUN"' EXIT
adb shell am start -n "$ACT" >/dev/null
for i in $(seq 1 150); do
  grep -q "HEPSI BITTI" "$GUN/tulpar.log" && break
  adb logcat -d -b crash | grep -q "Fatal signal" && break
  sleep 1
done
sleep 1
kill $LOGCAT_PID 2>/dev/null
adb shell am force-stop "$PKG"
adb uninstall "$PKG" >/dev/null 2>&1
LOG=$(tr -d '\r' < "$GUN/tulpar.log")
grep -E "\[olcu\]|betik dagitimi" <<< "$LOG" | sed 's/^.*\[olcu\]/  [olcu]/;s/^.*kapanis (betik dagitimi): /      /'
HATA=0
[ "$(adb logcat -d -b crash | grep -c 'Fatal signal')" -eq 0 ] || { echo "  COKME:"; adb logcat -d -b crash | head -20; HATA=1; }
n_rapor=$(grep -c "kapanis (betik dagitimi)" <<< "$LOG")
n_temiz=$(grep -c "\[olcu\] .* bitti hata=0" <<< "$LOG")
[ "$n_rapor" -eq 4 ] || { echo "  dagitim raporu $n_rapor / 4 (logcat satir kaybi ya da oturum kurulamadi) — olcum TAM DEGIL"; HATA=1; }
[ "$n_temiz" -eq 4 ] || { echo "  hatasiz oturum $n_temiz / 4"; HATA=1; }
n_penceresiz=$(grep -c "pencere acilamadi" <<< "$LOG")
[ "$n_penceresiz" -eq 0 ] || echo "  not: $n_penceresiz oturum pencere alamadi (ekran kapali/kilitli?) -> vsync'siz olculdu"
if [ "$HATA" -ne 0 ]; then echo "android kanca olcumu: OLCULEMEDI"; exit 1; fi
echo "android kanca olcumu: 4 oturum olculdu (sayilar cihaz verisi)"
