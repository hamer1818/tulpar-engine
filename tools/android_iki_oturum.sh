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
# Gerekenler: adb'de tek cihaz/emulator; PATH'te tulpar (>= K303);
# yapi/tulpar-ext/android/<abi>/ (tools/build_bridge_android.sh) ve
# TULPAR_ANDROID_LIB_DIR (TulparLang'in android/build_tame_android.sh
# ciktisi, android/dist). Olculdu 2026-10-06: emulator API 31 x86_64,
# -gpu swiftshader_indirect — 3/3 ayni PID, temiz. Fiziksel cihazda
# (Mali) olculmedi.
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR=${1:-3}
PKG=dev.tulparlang.android_iki_oturum
ACT=$PKG/android.app.NativeActivity
EXT="${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext}"
command -v tulpar >/dev/null || { echo "HATA: PATH'te tulpar yok"; exit 1; }
[ "$(adb devices | grep -c 'device$')" -eq 1 ] || { echo "HATA: adb'de tam bir cihaz olmali"; adb devices; exit 1; }
GUN=$(mktemp -d)
trap 'rm -rf "$GUN"' EXIT
cp "$ROOT/tulpar/tests/android_iki_oturum.tpr" "$GUN/"
(cd "$GUN" && TULPAR_AOT_NOCACHE=1 tulpar --ext "$EXT" build --target=android android_iki_oturum.tpr android_iki_oturum --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
adb install -r "$GUN/android_iki_oturum.apk" >/dev/null || { echo "HATA: adb install"; exit 1; }
adb shell am force-stop "$PKG"
adb logcat -c
pids=""
for t in $(seq 1 "$TUR"); do
  adb shell am start -W -n "$ACT" >/dev/null
  for i in $(seq 1 90); do
    [ "$(adb logcat -d -s tulpar:I | grep -c 'android: bitti')" -ge "$t" ] && break
    adb logcat -d -b crash | grep -q "Fatal signal" && break
    sleep 1
  done
  sleep 2
  pids="$pids $(adb shell pidof "$PKG" | tr -d '\r')"
done
LOG=$(adb logcat -d -v pid -s tulpar:I)
grep -E "bilgi kurulum|\[oturum\]|bitti" <<< "$LOG" | sed 's/^/  /'
HATA=0
[ "$(adb logcat -d -b crash | grep -c 'Fatal signal')" -eq 0 ] || { echo "  COKME:"; adb logcat -d -b crash | head -20; HATA=1; }
uniq_pid=$(tr ' ' '\n' <<< "$pids" | grep -v '^$' | sort -u | wc -l)
[ "$uniq_pid" -eq 1 ] || { echo "  turlar AYNI surecte degil (PID:$pids) — olcum bu yolu sinamadi"; HATA=1; }
n_temiz=$(grep -c "basta varlik=0 kare=0 .*onceki canli=false .*hata=0" <<< "$LOG")
[ "$n_temiz" -eq "$TUR" ] || { echo "  temiz oturum $n_temiz / $TUR"; HATA=1; }
n_k0=$(grep -c "k0 bilgi kurulum" <<< "$LOG")
[ "$n_k0" -eq "$TUR" ] || { echo "  kurulum satiri k0: $n_k0 / $TUR (bayat log kare oneki)"; HATA=1; }
if [ "$HATA" -ne 0 ]; then echo "android iki oturum: DUSTU"; exit 1; fi
echo "android iki oturum: GECTI ($TUR tur, tek PID$pids, her oturum temiz)"
