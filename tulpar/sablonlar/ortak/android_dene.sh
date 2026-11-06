#!/usr/bin/env bash
# {{OYUN_TR}} — Android cihazda/emulatorde otopilot turu (debug APK). Pencere ACMAZ, cihaz gerekir.
#
#   tools/android_dene.sh            # bagli tek cihaz (adb); ABI cihazdan okunur
#   ANDROID_SERIAL=emulator-5554 tools/android_dene.sh
#   EK_ENV="TULPAR_{{ONEK}}_KAPI_BOZ=1" tools/android_dene.sh   # pozitif kontrol: KIRMIZI donmeli
#
# Yapilan: DEBUG=1 ./apk_derle.sh <cihaz ABI'si> -> adb install -> debug.tulpar.env ile
# TULPAR_{{ONEK}}_OTOPILOT=1 -> uygulamayi baslat -> logcat'te "[kapi] TAMAM" bekle (en cok 5 dk)
# -> env'i temizle. Masaustundeki tools/dogrula.sh ile ayni kapi satiri, GERCEK Android yolunda
# (AOT derleme, APK varliklari, AAudio, Vulkan surucusu).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
timeout 5 adb get-state >/dev/null 2>&1 || { echo "HATA: adb cihazi yok (emulator ya da telefon bagla)"; exit 2; }
abi="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
case "$abi" in arm64-v8a|x86_64) ;; *) echo "HATA: desteklenmeyen cihaz ABI'si '$abi'"; exit 2 ;; esac
DEBUG=1 ./apk_derle.sh "$abi" > /tmp/{{KIMLIK}}_apk.log 2>&1 || { tail -20 /tmp/{{KIMLIK}}_apk.log; echo "HATA: APK"; exit 1; }
apk="$(sed -n 's/^TAMAM: //p' /tmp/{{KIMLIK}}_apk.log | tail -1)"
adb install -r "$apk" >/dev/null 2>&1 || { echo "HATA: kurulum"; exit 1; }
adb shell am force-stop {{PAKET}}
# ';' ayiricidir (bridge/android_host.cpp); uzak kabuk ';'yi komut ayiraci sayar -> deger TEK TIRNAKTA.
adb shell setprop debug.tulpar.env "'TULPAR_{{ONEK}}_OTOPILOT=1${EK_ENV:+;$EK_ENV}'"
adb logcat -c
adb shell monkey -p {{PAKET}} -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sonuc=""
for _ in $(seq 1 100); do
  sonuc="$(adb logcat -d -s tulpar | grep -m1 -oE '\[kapi\] (TAMAM|BASARISIZ)')"
  [ -n "$sonuc" ] && break
  adb shell pidof {{PAKET}} >/dev/null 2>&1 || { sleep 2; sonuc="$(adb logcat -d -s tulpar | grep -m1 -oE '\[kapi\] (TAMAM|BASARISIZ)')"; [ -n "$sonuc" ] && break; echo "HATA: uygulama kapandi, kapi satiri yok"; break; }
  sleep 3
done
adb shell setprop debug.tulpar.env "''"
adb logcat -d -s tulpar | grep -E '\[kapi\]|kapanis: [0-9]+ kare' | sed 's/^.*tulpar *: //'
[ "$sonuc" = "[kapi] TAMAM" ] && { echo "ANDROID ($abi): TAMAM"; exit 0; }
echo "ANDROID ($abi): KIRMIZI"; exit 1
