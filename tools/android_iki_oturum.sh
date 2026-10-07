#!/bin/bash
# ============================================================
# Android: AYNI SURECTE ikinci (ve N.) motor oturumu (Tuzaklar 8ct).
#
# Surec canliyken etkinlik yeniden yaratilinca NativeActivity glue
# android_main'i yeni bir thread'de YENIDEN cagirir, oyunun main()'i yeniden
# kosar ve ikinci teng_init ayni surecte kurulur. Bu betik bir Tulpar oyununu
# (tulpar/tests/android_iki_oturum.tpr: motor_ac -> 60 kare -> motor_kapat)
# APK olarak kurar, `am start` ile N kez baslatir ve her turun PID'ini ve
# "[oturum]" satirini toplar. GECER: butun turlar AYNI PID, her oturum
# "basta varlik=0 kare=0", "onceki canli=false", "hata=0", kurulum satiri `k0`.
#
#   tools/android_iki_oturum.sh [tur]          # varsayilan 3
#
# Gerekenler: adb'de tek cihaz/emulator (ya da ANDROID_SERIAL); PATH'te tulpar (>= K303);
# yapi/tulpar-ext/android/<abi>/ (tools/build_bridge_android.sh) ve
# TULPAR_ANDROID_LIB_DIR (TulparLang'in android/build_tame_android.sh
# ciktisi, android/dist). Olculdu 2026-10-06: emulator API 31 x86_64,
# -gpu swiftshader_indirect — 3/3 ayni PID, temiz. Fiziksel cihaz
# (Huawei P20 Pro CLT-L09, Mali-G72, Android 10, arm64-v8a; 2026-10-07):
# 5/5 ve 3/3 ayni PID, her oturum temiz, kurulum satirlari k0, p50 16,7 ms
# (60 Hz vsync); oturum basina RSS 157,6 -> 160,1 -> 161,4 -> 161,7 -> 163,0 MB
# (surucu/yigin; olculur, iddia edilmez). Birden cok cihazda ANDROID_SERIAL.
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR=${1:-3}
PKG=dev.tulparlang.android_iki_oturum
ACT=$PKG/android.app.NativeActivity
EXT="${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext}"
command -v tulpar >/dev/null || { echo "HATA: PATH'te tulpar yok"; exit 1; }
# Birden cok cihaz/emulator bagliysa ANDROID_SERIAL ile secilir (adb'nin
# kendi degiskeni: asagidaki her `adb` cagrisi onu hedefler). 2026-10-07'de
# kullanicinin emulatoru acikken telefon baglandi ve "tam bir cihaz" denetimi
# olcumu baslatmadan durduruyordu.
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
cp "$ROOT/tulpar/tests/android_iki_oturum.tpr" "$GUN/"
(cd "$GUN" && TULPAR_AOT_NOCACHE=1 tulpar --ext "$EXT" build --target=android android_iki_oturum.tpr android_iki_oturum --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
adb install -r "$GUN/android_iki_oturum.apk" >/dev/null || { echo "HATA: adb install"; exit 1; }
adb shell am force-stop "$PKG"
# Ekran kapaliyken etkinlik pencere ALMAZ: kopru 10 sn ANativeWindow bekleyip
# headless kipe duser (olculdu Huawei P20 Pro, 2026-10-07: her oturum
# "UYARI pencere acilamadi ... 10 s zaman asimi", oturum 1 sn yerine 11 sn).
# Pencereli yolu olcmek icin ekran uyandirilir; kilit ekrani elle acilmali.
adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
adb logcat -c
# Log AKIS olarak dosyaya alinir; sonda `logcat -d` ile tampondan okunmaz.
# Huawei P20 Pro'da (EMUI, Android 10; olculdu 2026-10-07) main tamponu
# 256 KiB ve sistem cok konusuyor: 5 turluk kosumun sonunda tampon donmus,
# tulpar satirlarinin HEPSI dusmustu ("temiz oturum 0/5" — kod hatasi degil).
adb logcat -v pid -s tulpar:I > "$GUN/tulpar.log" 2>/dev/null &
LOGCAT_PID=$!
trap 'kill $LOGCAT_PID 2>/dev/null; rm -rf "$GUN"' EXIT
pids=""
for t in $(seq 1 "$TUR"); do
  adb shell am start -W -n "$ACT" >/dev/null
  for i in $(seq 1 90); do
    [ "$(grep -c 'android: bitti' "$GUN/tulpar.log")" -ge "$t" ] && break
    adb logcat -d -b crash | grep -q "Fatal signal" && break
    sleep 1
  done
  sleep 2
  pids="$pids $(adb shell pidof "$PKG" | tr -d '\r')"
done
sleep 1
kill $LOGCAT_PID 2>/dev/null
LOG=$(tr -d '\r' < "$GUN/tulpar.log")
grep -E "bilgi kurulum|\[oturum\]|bitti" <<< "$LOG" | sed 's/^/  /'
HATA=0
[ "$(adb logcat -d -b crash | grep -c 'Fatal signal')" -eq 0 ] || { echo "  COKME:"; adb logcat -d -b crash | head -20; HATA=1; }
uniq_pid=$(tr ' ' '\n' <<< "$pids" | grep -v '^$' | sort -u | wc -l)
[ "$uniq_pid" -eq 1 ] || { echo "  turlar AYNI surecte degil (PID:$pids) — olcum bu yolu sinamadi"; HATA=1; }
n_temiz=$(grep -c "basta varlik=0 kare=0 .*onceki canli=false .*hata=0" <<< "$LOG")
[ "$n_temiz" -eq "$TUR" ] || { echo "  temiz oturum $n_temiz / $TUR"; HATA=1; }
# Bayat log kare oneki (8ct): ikinci oturumun kurulum satiri eski karenin
# numarasini (k60) tasiyordu. Denetim BAYAT satir SAYAR (k0 olmayan kurulum
# satiri = kirmizi); eksik satir kirmizi degil, not olarak basilir — logcat
# satir dusurebilir (asagidaki akis notuna bak). En az bir k0 satiri
# gorulmeli ki denetim bir sey olcmus olsun.
n_k0=$(grep -c "k0 bilgi kurulum" <<< "$LOG")
n_bayat=$(grep -cE "k[1-9][0-9]* bilgi kurulum" <<< "$LOG")
if [ "$n_bayat" -ne 0 ]; then echo "  bayat log kare oneki: $n_bayat kurulum satiri k0 degil"; HATA=1
elif [ "$n_k0" -eq 0 ]; then echo "  kurulum satiri hic gorulmedi — kare oneki denetimi olcmedi"; HATA=1
elif [ "$n_k0" -lt "$TUR" ]; then echo "  not: $((TUR - n_k0)) kurulum satiri logcat'te yok (cihaz log kaybi; gorulen $n_k0 satirin hepsi k0)"; fi
n_penceresiz=$(grep -c "pencere acilamadi" <<< "$LOG")
[ "$n_penceresiz" -eq 0 ] || echo "  not: $n_penceresiz oturum pencere alamadi (ekran kapali/kilitli?) -> headless kipte olculdu, pencereli yol OLCULMEDI"
if [ "$HATA" -ne 0 ]; then echo "android iki oturum: DUSTU"; exit 1; fi
echo "android iki oturum: GECTI ($TUR tur, tek PID$pids, her oturum temiz)"
