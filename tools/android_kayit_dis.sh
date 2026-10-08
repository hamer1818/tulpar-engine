#!/bin/bash
# ============================================================
# Android: kayit dosyasi adb ile okunup yazilabiliyor mu (Geri bildirim #19).
#
# Kayit varsayilan olarak uygulamanin IC dizininde; adb okuyamaz ve Huawei'de
# `run-as` calismiyor (Tuzaklar 8n). Tani secenegi: `setprop debug.tulpar.kayit dis`
# + HATA AYIKLANABILIR APK -> kayit /sdcard/Android/data/<paket>/files/
# tulpar_kayit.txt (bridge/android_host.cpp save_mirror_external).
#
# Akis: debuggable APK (TULPAR_ANDROID_DEBUGGABLE=1; TulparLang bunu tanimali)
# kur -> KONTROL: ozellik kapali baslat (ic kayit, sayac 0 -> 1; dis dosya
# olusmamali — secenek varsayilani degistirmiyor) -> ozellik ac -> baslat (ic
# kayit disa kopyalanir, oyun 1 okur, 2 yazar) -> `adb shell cat` (kd_sayac=2)
# -> kd_sayac=41 ile `adb push` -> baslat -> oyun 41 okur, 42 yazar. GECER: dis
# yol kullanildi, okumalar beklenen, hata 0.
# Ozellik betik sonunda geri alinir (setprop debug.tulpar.kayit "").
#
#   tools/android_kayit_dis.sh
#   TULPAR_BIN=<tulpar> tools/android_kayit_dis.sh     # debuggable bilen derleyici
#
# Hata ayiklanamaz APK uretilirse (eski TulparLang) betik bunu soyler ve
# YAYIN kipini olcer: ozellik acikken bile kayit ic dizinde kalmali.
# Olculdu 2026-10-08, Huawei P20 Pro (CLT-L09, Android 10) — ayrinti PR'da.
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=dev.tulparlang.android_kayit_dis
ACT=$PKG/android.app.NativeActivity
EXT="${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext}"
TP="${TULPAR_BIN:-tulpar}"
command -v "$TP" >/dev/null || { echo "HATA: tulpar yok ($TP)"; exit 1; }
if [ -n "${ANDROID_SERIAL:-}" ]; then
  adb devices | grep -q "^$ANDROID_SERIAL[[:space:]]*device$" \
    || { echo "HATA: ANDROID_SERIAL=$ANDROID_SERIAL bagli degil"; adb devices; exit 1; }
else
  [ "$(adb devices | grep -c 'device$')" -eq 1 ] \
    || { echo "HATA: adb'de tam bir cihaz olmali (birden coksa ANDROID_SERIAL=<seri> verin)"; adb devices; exit 1; }
fi
GUN=$(mktemp -d)
trap 'adb shell setprop debug.tulpar.kayit "\"\"" >/dev/null 2>&1; rm -rf "$GUN"' EXIT
cp "$ROOT/tulpar/tests/android_kayit_dis.tpr" "$GUN/"
(cd "$GUN" && TULPAR_ANDROID_DEBUGGABLE=1 TULPAR_AOT_NOCACHE=1 "$TP" --ext "$EXT" build --target=android android_kayit_dis.tpr android_kayit_dis --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
DEBUGGABLE=0
grep -q 'android:debuggable="true"' "$GUN/android_kayit_dis_apk/AndroidManifest.xml" && DEBUGGABLE=1
echo "APK hata ayiklanabilir: $([ $DEBUGGABLE -eq 1 ] && echo evet || echo 'HAYIR (derleyici TULPAR_ANDROID_DEBUGGABLE tanimiyor) -> yayin kipi olculur')"
DIS=/sdcard/Android/data/$PKG/files/tulpar_kayit.txt
kos() { # $1 = log dosyasi
  adb logcat -c
  adb logcat -v time -s tulpar:I > "$1" 2>/dev/null &
  local lp=$!
  adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
  adb shell am start -n "$ACT" >/dev/null 2>&1
  for i in $(seq 1 40); do grep -q "android: bitti" "$1" && break; sleep 1; done
  sleep 1
  kill $lp 2>/dev/null
  adb shell am force-stop "$PKG"
  tr -d '\r' < "$1" | grep -E "\[kd\]|kayit DIS|debug.tulpar.kayit" | sed 's/^.*\[engine_bridge\] /  /;s/^.*\[kd\]/  [kd]/'
}
HATA=0
adb uninstall "$PKG" >/dev/null 2>&1
adb install -r "$GUN/android_kayit_dis.apk" >/dev/null || { echo "HATA: adb install"; exit 1; }
# --- KONTROL: ozellik kapali -> ic dizin, dis dosya yok -----------------------
adb shell setprop debug.tulpar.kayit '""' >/dev/null 2>&1
echo "== ozellik kapali"
kos "$GUN/k0.log"
grep -q "\[kd\] yol tulpar_kayit.txt okunan 0" "$GUN/k0.log" || { echo "  KONTROL: varsayilan ic yol kullanilmadi"; HATA=1; }
adb shell "test -f $DIS" && { echo "  KONTROL: ozellik kapaliyken dis kayit olustu"; HATA=1; }
# --- ozellik acik -------------------------------------------------------------
adb shell setprop debug.tulpar.kayit dis
echo "== ozellik acik (debug.tulpar.kayit=dis)"
kos "$GUN/k1.log"
if [ "$DEBUGGABLE" -eq 1 ]; then
  grep -q "\[kd\] yol $DIS okunan 1" "$GUN/k1.log" || grep -q "\[kd\] yol /storage/emulated/0/Android/data/$PKG/files/tulpar_kayit.txt okunan 1" "$GUN/k1.log" \
    || { echo "  dis yol kullanilmadi ya da ic kayit kopyalanmadi (okunan 1 bekleniyordu)"; HATA=1; }
  icerik=$(adb shell cat "$DIS" 2>/dev/null | tr -d '\r')
  echo "  adb ile okundu: $(tr '\n' ' ' <<< "$icerik")"
  grep -q "^kd_sayac=2$" <<< "$icerik" || { echo "  dis kayitta kd_sayac=2 yok"; HATA=1; }
  printf 'kd_sayac=41\n' > "$GUN/yeni.txt"
  adb push "$GUN/yeni.txt" "$DIS" >/dev/null 2>&1 || { echo "  adb push basarisiz"; HATA=1; }
  echo "== adb push kd_sayac=41 sonrasi"
  kos "$GUN/k2.log"
  grep -q "okunan 41" "$GUN/k2.log" || { echo "  oyun adb ile yazilan degeri okumadi"; HATA=1; }
  adb shell cat "$DIS" 2>/dev/null | tr -d '\r' | grep -q "^kd_sayac=42$" || { echo "  dis kayit 42 olmadi"; HATA=1; }
else
  grep -q "YOK SAYILDI" "$GUN/k1.log" || { echo "  yayin APK'sinda ozellik yok sayilmadi"; HATA=1; }
  adb shell "test -f $DIS" && { echo "  yayin APK'sinda dis kayit olustu"; HATA=1; }
fi
grep -q "hata=0" "$GUN/k1.log" || { echo "  oyun hatali bitti"; HATA=1; }
adb uninstall "$PKG" >/dev/null 2>&1
if [ "$HATA" -ne 0 ]; then echo "android kayit dis: DUSTU"; exit 1; fi
echo "android kayit dis: GECTI"
