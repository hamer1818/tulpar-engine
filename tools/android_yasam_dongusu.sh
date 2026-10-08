#!/bin/bash
# ============================================================
# Android yasam dongusu kapisi (Geri bildirim #18, #8, #9, #17). Fiziksel cihazda / emulatorde.
#
# tulpar/tests/android_yasam_dongusu.tpr'yi APK olarak kurar, pencereli baslatir
# ve logcat'i AKIS olarak toplar (EMUI tamponu 256 KiB; `logcat -d` satir kaybeder).
# Akis: baslat -> 4 sn -> bellek baskisi (`am send-trim-memory`) -> ANA EKRAN ->
# 3 sn -> geri getir -> 3 sn -> GERI -> 3 sn -> GERI (oyun ikinci basista
# kendisi cikar). GECER:
#   #18 hic `android cmd ?` satiri yok (eskiden her acilista 4-5 adsiz satir:
#       INPUT_CHANGED, WINDOW_REDRAW_NEEDED, CONTENT_RECT_CHANGED, SAVE_STATE);
#       yasam dongusunun cekirdegi goruldu (START, RESUME, INIT_WINDOW);
#   #8  ilk GERI'den sonra surec CANLI ve oyun "[yd] geri 1" basti (eskiden
#       sistem etkinligi kapatiyordu: DESTROY, savas kaybolur); ikinci GERI'de
#       oyun kendi karariyla cikti ("[yd] geri 2", "android: bitti");
#   #9  ana ekrandayken oyunun AAudio oyuncusu `started` DEGIL (`dumpsys audio`;
#       eskiden arka planda da `state:started` kaliyordu), oyun "[yd] arka_planda
#       true" gordu; geri getirince oyuncu yine `started`, "arka_planda false";
#   #17 "[yd] ekran WxH guvenli L T R B": varsayilan temada bosluklar 0 (sistem
#       pencereyi centikten uzak tutar). TULPAR_YD_CENTIK=1 (derleyici
#       TULPAR_ANDROID_CUTOUT=short_edges tanimali): pencere ekranin UZUN kenari
#       kadar (`wm size`) ve centikli cihazda sol+sag bosluk > 0;
#   oyun hatasiz bitti, cokme yok.
# Bilgi (kapi degil): LOW_MEMORY geldiyse "DUSUK BELLEK uyarisi #N (RSS ..)" ve
# kapanistaki toplam; `send-trim-memory` onLowMemory'ye donusmez — not basilir.
#
#   tools/android_yasam_dongusu.sh
#   TULPAR_YD_ESC=1 tools/android_yasam_dongusu.sh   # oyun geri'yi tus_basildi("ESC") ile sorar
#   TULPAR_YD_DINLEMEZ=1 tools/android_yasam_dongusu.sh  # geri'yi hic sormayan oyun: sistem kapatir
#   TULPAR_YD_CENTIK=1 TULPAR_BIN=<tulpar> tools/android_yasam_dongusu.sh  # pencere centige uzanir
#   TULPAR_EXT_DIR=<eski paket> TULPAR_YD_ESKI=1 tools/android_yasam_dongusu.sh   # pozitif kontrol
# (TULPAR_YD_ESKI: yeni API'siz kip — geri ESC ile sorulur, arka_planda() yok
# ve guvenli_*() 0 sayilir; eski paketin engine.tpr'siyle de derlenir.)
#
# Gerekenler: android_iki_oturum.sh ile ayni (tulpar >= K303, yapi/tulpar-ext/
# android/<abi>/, TULPAR_ANDROID_LIB_DIR; birden cok cihazda ANDROID_SERIAL).
# Olculdu 2026-10-08, Huawei P20 Pro (CLT-L09, Mali-G72, Android 10): GECTI
# (geri_basildi ve ESC kipi); pozitif kontrol (main'in arsivi) DUSTU — PR'larda.
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=dev.tulparlang.android_yasam_dongusu
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
echo "cihaz: $(adb shell getprop ro.product.model | tr -d '\r') / Android $(adb shell getprop ro.build.version.release | tr -d '\r') / $(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
GUN=$(mktemp -d)
trap 'rm -rf "$GUN"' EXIT
cp "$ROOT/tulpar/tests/android_yasam_dongusu.tpr" "$GUN/"
DINLER=1
ESKI=0
if [ -n "${TULPAR_YD_ESKI:-}" ]; then
  sed -i 's/geri_basildi()/tus_basildi("ESC")/; s/arka_planda()/false/; s/guvenli_[a-z]*()/0/g' "$GUN/android_yasam_dongusu.tpr"
  ESKI=1
  echo "kip: yeni API'siz (geri = tus_basildi(\"ESC\"), arka_planda yok)"
elif [ -n "${TULPAR_YD_ESC:-}" ]; then
  sed -i 's/geri_basildi()/tus_basildi("ESC")/' "$GUN/android_yasam_dongusu.tpr"
  echo "kip: geri tusu tus_basildi(\"ESC\") ile soruluyor"
elif [ -n "${TULPAR_YD_DINLEMEZ:-}" ]; then
  # Geri tusunu HIC sormayan (eski) oyun: sistem etkinligi yine kapatmali.
  sed -i 's/geri_basildi()/false/' "$GUN/android_yasam_dongusu.tpr"
  DINLER=0
  echo "kip: oyun geri tusunu sormuyor (eski davranis beklenir: sistem kapatir)"
fi
CENTIK=0
[ -n "${TULPAR_YD_CENTIK:-}" ] && { CENTIK=1; export TULPAR_ANDROID_CUTOUT=short_edges; echo "kip: pencere centige uzanir (TULPAR_ANDROID_CUTOUT=short_edges)"; }
(cd "$GUN" && TULPAR_AOT_NOCACHE=1 "$TP" --ext "$EXT" build --target=android android_yasam_dongusu.tpr android_yasam_dongusu --apk) > "$GUN/derle.log" 2>&1 \
  || { echo "HATA: APK kurulamadi"; tail -15 "$GUN/derle.log"; exit 1; }
if [ "$CENTIK" -eq 1 ] && ! grep -q "windowLayoutInDisplayCutoutMode" "$GUN"/android_yasam_dongusu_apk/res/values-v28/styles.xml 2>/dev/null; then
  echo "HATA: derleyici TULPAR_ANDROID_CUTOUT'u tanimiyor (res/values-v28 yok) — centik olculemez"; exit 1
fi
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
[ -n "$PID" ] || { echo "HATA: surec baslamadi (pidof bos)"; exit 1; }
# Iki bicim: standart `-v pid` ("I( 1234) ...") ve EMUI'nin onu yok sayip bastigi
# threadtime ("10-07 21:27:39.148 +0000  1234  1250 I tulpar: ...").
suz() { tr -d '\r' < "$GUN/tulpar.log" | grep -E "^[A-Z]\( *$PID\)|^[0-9-]+ [0-9:.]+ +(\+[0-9]+ +)?$PID "; }
# "Canli" = etkinlik ON PLANDA (mResumedActivity). Yalniz PID'e bakmak yetmez:
# geri tusu NativeActivity'yi bitirdiginde surec bir sure yasamaya devam eder
# (olculdu: pozitif kontrolde PID duruyordu, etkinlik DESTROY olmustu).
canli() { adb shell dumpsys activity activities | grep -m1 "mResumedActivity" | grep -q "$PKG/"; }
# Bu surecin AAudio oyuncusunun durumu (dumpsys audio "players:"): started / stopped / paused / yok.
ses_durumu() {
  adb shell dumpsys audio | tr -d '\r' | grep -E "type:AAudio -- u/pid:[0-9]+/$PID -- state:" | tail -1 | sed 's/.*state:\([a-z]*\).*/\1/'
}
HATA=0
sleep 4
adb shell am send-trim-memory "$PKG" COMPLETE >/dev/null 2>&1
sleep 1
# --- #17: ekran ve guvenli alan -----------------------------------------------
UZUN=$(adb shell wm size | tr -d '\r' | sed -n 's/.*: *\([0-9]*\)x\([0-9]*\).*/\1 \2/p' | tail -1 | awk '{print ($1 > $2) ? $1 : $2}')
read -r EW EH GL GT GR GB <<< "$(suz | sed -n 's/.*\[yd\] ekran \([0-9]*\)x\([0-9]*\) guvenli \([0-9]*\) \([0-9]*\) \([0-9]*\) \([0-9]*\).*/\1 \2 \3 \4 \5 \6/p' | tail -1)"
echo "  ekran ${EW:-?}x${EH:-?} (fiziksel uzun kenar ${UZUN:-?}), guvenli sol ${GL:-?} ust ${GT:-?} sag ${GR:-?} alt ${GB:-?}"
if [ -z "${EW:-}" ]; then echo "  '[yd] ekran' satiri yok — #17 olculmedi"; HATA=1
elif [ "$ESKI" -eq 0 ]; then
  if [ "$CENTIK" -eq 1 ]; then
    [ "$EW" = "$UZUN" ] || { echo "  CENTIK: pencere uzun kenari ($EW) ekranin uzun kenari ($UZUN) degil — centik alani kullanilmiyor"; HATA=1; }
    [ $((GL + GR)) -gt 0 ] || echo "  not: sol+sag bosluk 0 (cihazda centik yok mu?) — guvenli alan olculmedi"
  else
    [ $((GL + GT + GR + GB)) -eq 0 ] || { echo "  varsayilan temada bosluk 0 olmaliydi"; HATA=1; }
  fi
fi
# --- #9: ana ekran — ses akisi durmali; geri getirince surmeli ---------------
s0=$(ses_durumu)
echo "  ses (on planda): ${s0:-yok}"
[ "$s0" = started ] || { echo "  ses akisi on planda 'started' degil — #9 olculemedi"; HATA=1; }
adb shell input keyevent KEYCODE_HOME
sleep 3
s1=$(ses_durumu)
echo "  ses (ana ekranda): ${s1:-yok}"
[ "$s1" != started ] || { echo "  ARKA PLAN: ses akisi hala 'started' (motor durdurmadi)"; HATA=1; }
[ "$ESKI" -eq 1 ] || suz | grep -q "\[yd\] arka_planda true" || { echo "  ARKA PLAN: oyun arka_planda() true gormedi"; HATA=1; }
adb shell am start -n "$ACT" >/dev/null 2>&1 # gorev one gelir ("Activity not started" uyarisi beklenir)
sleep 3
s2=$(ses_durumu)
echo "  ses (geri gelince): ${s2:-yok}"
[ "$s2" = started ] || { echo "  ON PLAN: ses akisi yeniden 'started' degil"; HATA=1; }
[ "$ESKI" -eq 1 ] || suz | grep -q "\[yd\] arka_planda false" || { echo "  ON PLAN: oyun arka_planda() false gormedi"; HATA=1; }
# --- #8: birinci GERI — etkinlik kapanmamali, oyun gormeli -------------------
adb shell input keyevent KEYCODE_BACK
sleep 3
if [ "$DINLER" -eq 1 ]; then
  if canli; then echo "  GERI 1: etkinlik on planda"; else echo "  GERI 1: etkinlik on planda DEGIL (sistem kapatti)"; HATA=1; fi
  suz | grep -q "\[yd\] geri 1 " || { echo "  GERI 1: oyun basisi gormedi ('[yd] geri 1' yok)"; HATA=1; }
  # --- ikinci GERI: oyun kendi karariyla cikar -------------------------------
  adb shell input keyevent KEYCODE_BACK
else
  if canli; then echo "  GERI 1: etkinlik HALA on planda (sormayan oyunda sistem kapatmaliydi)"; HATA=1; else echo "  GERI 1: sistem etkinligi kapatti (beklenen)"; fi
fi
for i in $(seq 1 30); do
  grep -q "android: bitti" "$GUN/tulpar.log" && break
  adb logcat -d -b crash | grep -q "Fatal signal" && break
  sleep 1
done
sleep 1
kill $LOGCAT_PID 2>/dev/null
adb shell am force-stop "$PKG"
adb uninstall "$PKG" >/dev/null 2>&1
LOG=$(suz)
grep -E "android cmd|DUSUK BELLEK|host: kapatildi|geri tusu|ses cihazi|kapanis \(yasam|guvenli alan|\[yd\] (geri|bitis|arka|ses|ekran)" <<< "$LOG" | sed 's/^.*\[engine_bridge\] /  /;s/^.*\[yd\]/  [yd]/'
[ "$(adb logcat -d -b crash | grep -c 'Fatal signal')" -eq 0 ] || { echo "  COKME:"; adb logcat -d -b crash | head -20; HATA=1; }
n_adsiz=$(grep -c "android cmd ?" <<< "$LOG")
[ "$n_adsiz" -eq 0 ] || { echo "  adsiz komut satiri: $n_adsiz (her glue komutu adli olmali)"; HATA=1; }
for c in START RESUME INIT_WINDOW; do
  grep -q "android cmd $c\$" <<< "$LOG" || { echo "  '$c' gorulmedi — yasam dongusu olculmedi"; HATA=1; }
done
if [ "$DINLER" -eq 1 ]; then
  grep -q "\[yd\] geri 2 " <<< "$LOG" || { echo "  GERI 2: oyun ikinci basisi gormedi"; HATA=1; }
  grep -q "\[yd\] bitis kare=.* geri=2 hata=0" <<< "$LOG" || { echo "  oyun kendi karariyla (geri=2) hatasiz bitmedi"; HATA=1; }
else
  grep -q "\[yd\] bitis kare=.* geri=0 hata=0" <<< "$LOG" || { echo "  oyun hatasiz bitmedi"; HATA=1; }
fi
grep -q "DUSUK BELLEK" <<< "$LOG" || echo "  not: LOW_MEMORY gelmedi (send-trim-memory onLowMemory'ye donusmedi) — sayac olculmedi"
if [ "$HATA" -ne 0 ]; then echo "android yasam dongusu: DUSTU"; exit 1; fi
echo "android yasam dongusu: GECTI"
