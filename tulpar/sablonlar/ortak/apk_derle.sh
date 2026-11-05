#!/usr/bin/env bash
# {{OYUN_TR}} — Android APK derleme + statik dogrulama (oyun fabrikasi iskeleti).
#
#   ./apk_derle.sh                 # arm64-v8a, yayin kipi (debug imza: yalniz yerel kurulum)
#   ./apk_derle.sh x86_64          # emulator
#   ./apk_derle.sh ikisi           # iki ABI tek APK'da
#   DEBUG=1 ./apk_derle.sh x86_64  # hata ayiklanabilir (debug.tulpar.env otopilotu, run-as)
#
# Ortam: TULPAR_ENGINE_DIR / TULPAR / EXT (tools/ortam.sh), TULPAR_ANDROID_DIR (derleyicinin
# yanindaki android/; `tulpar update --android`), CIKTI (varsayilan cikti/apk), CUTOUT
# (short_edges | default | never), INTERNET=0|1 (varsayilan 0), SURUM_KODU / SURUM_ADI.
#
# Play'e YUKLEME bu betigin isi DEGIL (kullanici karari 2026-11-05: yeni oyun Play'e ancak
# acikca istenince). Imzali AAB (yukleme anahtari) da o zaman eklenir — Kupler ile Kureler
# apk_derle.sh AAB=1 yolu ornek.
#
# Dogrulama (derlemeden SONRA, APK'nin kendisinden; her biri bir kez sessiz bozulma olarak yasandi):
# targetSdk >= 36 (Play asgarisi), istenen ABI'ler var / istenmeyenler yok, debuggable istenen
# kipte, cutout teması iki yonlu, izin kumesi TAM (fazlasi da eksigi de hata), ikon var, assets/
# listesi oyunun assets/ dizinine ad ad esit (.md haric).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
. tools/ortam.sh
OYUN="$OYUN_KOK"
ABI_SECIM="${1:-arm64-v8a}"
case "$ABI_SECIM" in arm64-v8a) ABIS="arm64-v8a" ;; x86_64) ABIS="x86_64" ;; ikisi) ABIS="arm64-v8a x86_64" ;;
  *) echo "HATA: ABI arm64-v8a | x86_64 | ikisi"; exit 2 ;; esac
KIP=yayin; [ "${DEBUG:-0}" = 1 ] && KIP=debug
CUTOUT="${CUTOUT:-short_edges}"
INTERNET="${INTERNET:-0}"
CIKTI="${CIKTI:-$OYUN/cikti/apk}"
TULPAR_ANDROID_DIR="${TULPAR_ANDROID_DIR:-$(dirname "$TULPAR")/android}"
PAKETLE="$TULPAR_ANDROID_DIR/package_apk.sh"
[ -f "$PAKETLE" ] || { echo "HATA: $PAKETLE yok (tulpar update --android)"; exit 1; }
A_SURUM="$(cat "$TULPAR_ANDROID_DIR/SURUM" 2>/dev/null || true)"
T_SURUM="$("$TULPAR" --version 2>&1 | head -1)"
[ "$A_SURUM" = "$T_SURUM" ] || { echo "HATA: Android araclari '$A_SURUM', derleyici '$T_SURUM' (runtime ABI'si tutmaz)"; exit 1; }
for abi in $ABIS; do
  [ -d "$EXT/android/$abi" ] || { echo "HATA: motorun $abi arsivi yok: $EXT/android/$abi (motorda tools/build_bridge_android.sh)"; exit 1; }
done
export TULPAR_ANDROID_TOOLS="$TULPAR_ANDROID_DIR" TULPAR_ANDROID_LIB_DIR="$TULPAR_ANDROID_DIR/dist"

IS="$(mktemp -d "${TMPDIR:-/tmp}/{{KIMLIK}}_apk.XXXXXX")"; trap 'rm -rf "$IS"' EXIT
echo "derleyici: $T_SURUM; eklenti: $EXT; ABI: $ABI_SECIM; kip: $KIP"
(
  cd "$OYUN"
  export TULPAR_ANDROID_CUTOUT="$CUTOUT" TULPAR_ANDROID_INTERNET="$INTERNET"
  if [ "$KIP" = debug ]; then export TULPAR_ANDROID_DEBUGGABLE=1; else unset TULPAR_ANDROID_DEBUGGABLE; fi
  [ -n "${SURUM_KODU:-}" ] && export TULPAR_ANDROID_VERSION_CODE="$SURUM_KODU"
  [ -n "${SURUM_ADI:-}" ] && export TULPAR_ANDROID_VERSION_NAME="$SURUM_ADI"
  nice -n 19 "$TULPAR" build --ext "$EXT" --target=android oyun.tpr "$IS/oyun"
) || { echo "HATA: tulpar build"; exit 1; }
STAGE="$IS/oyun_apk"
[ -f "$STAGE/AndroidManifest.xml" ] || { echo "HATA: staging yok: $STAGE"; exit 1; }
for abi in arm64-v8a x86_64; do
  case " $ABIS " in *" $abi "*) [ -f "$STAGE/lib/$abi/libtulpargame.so" ] || { echo "HATA: lib/$abi/libtulpargame.so yok"; exit 1; } ;;
                    *) rm -rf "${STAGE:?}/lib/$abi" ;; esac
done
find "$STAGE/assets/assets" -type f -name '*.md' -delete 2>/dev/null || true

mkdir -p "$CIKTI"
APK="$CIKTI/{{KIMLIK}}_${ABI_SECIM}_${KIP}.apk"
bash "$PAKETLE" "$STAGE" "$APK" | grep -v '^  + assets/' || { echo "HATA: paketleme"; exit 1; }

# --- Statik dogrulama ---
SDK="${TULPAR_ANDROID_SDK:-$HOME/Android/Sdk}"
AAPT2=$(ls -d "$SDK"/build-tools/*/aapt2 2>/dev/null | sort -V | tail -1)
hata=0
if [ -z "$AAPT2" ]; then echo "DOGRULAMA HATASI: aapt2 yok ($SDK) — APK olculmeden TAMAM denmez"; exit 1; fi
rozet=$("$AAPT2" dump badging "$APK" 2>/dev/null)
echo "$rozet" | grep -E "^package:|^application-label:|^native-code:|^targetSdkVersion" || true
grep -q "name='{{PAKET}}'" <<< "$rozet" || { echo "DOGRULAMA HATASI: paket adi {{PAKET}} degil"; hata=1; }
hedef=$(sed -n "s/^targetSdkVersion:'\([0-9]*\)'.*/\1/p" <<< "$rozet")
[ -n "$hedef" ] && [ "$hedef" -ge 36 ] || { echo "DOGRULAMA HATASI: targetSdk '${hedef:-yok}' < 36"; hata=1; }
ncode=$(sed -n "s/^native-code: //p" <<< "$rozet")
for abi in arm64-v8a x86_64; do
  case " $ABIS " in
    *" $abi "*) grep -q "'$abi'" <<< "$ncode" || { echo "DOGRULAMA HATASI: native-code'da $abi yok"; hata=1; } ;;
    *)          grep -q "'$abi'" <<< "$ncode" && { echo "DOGRULAMA HATASI: istenmeyen $abi"; hata=1; } ;;
  esac
done
if [ "$KIP" = debug ]; then grep -q '^application-debuggable' <<< "$rozet" || { echo "DOGRULAMA HATASI: debug APK debuggable degil"; hata=1; }
else grep -q '^application-debuggable' <<< "$rozet" && { echo "DOGRULAMA HATASI: yayin APK'si debuggable"; hata=1; }; fi
if "$AAPT2" dump resources "$APK" 2>/dev/null | grep -q "0x01010586=1"; then cv=var; else cv=yok; fi
echo "cutout shortEdges: $cv (istenen $CUTOUT)"
[ "$CUTOUT" = short_edges ] && [ "$cv" = yok ] && { echo "DOGRULAMA HATASI: shortEdges yok"; hata=1; }
[ "$CUTOUT" != short_edges ] && [ "$cv" = var ] && { echo "DOGRULAMA HATASI: $CUTOUT istendi, shortEdges var"; hata=1; }
kume=$("$AAPT2" dump permissions "$APK" 2>/dev/null | sed -n "s/^uses-permission: name='\(.*\)'/\1/p" | sort | tr '\n' ' ')
if [ "$INTERNET" = 1 ]; then bek="android.permission.INTERNET android.permission.VIBRATE "; else bek="android.permission.VIBRATE "; fi
echo "izinler: $kume(beklenen $bek)"
[ "$kume" = "$bek" ] || { echo "DOGRULAMA HATASI: izin kumesi farkli"; hata=1; }
grep -q "^application: .*icon=" <<< "$rozet" && echo "ikon: var" || { echo "DOGRULAMA HATASI: ikon yok"; hata=1; }
python3 - "$OYUN/assets" "$APK" <<'PY' || hata=1
import os, sys, zipfile
kok, apk = sys.argv[1], sys.argv[2]
bek = set()
for d, _, fs in os.walk(kok):
    for f in fs:
        r = os.path.relpath(os.path.join(d, f), kok).replace(os.sep, "/")
        if not r.endswith(".md"):
            bek.add("assets/assets/" + r)
var = {n for n in zipfile.ZipFile(apk).namelist() if n.startswith("assets/assets/") and not n.endswith("/")}
print("varlik: APK'da %d / beklenen %d" % (len(var), len(bek)))
eksik, fazla = sorted(bek - var), sorted(var - bek)
for n in eksik[:5]: print("  eksik: " + n)
for n in fazla[:5]: print("  fazla: " + n)
sys.exit(1 if eksik or fazla else 0)
PY
ls -lh "$APK"
[ "$hata" = 0 ] || { echo "APK DOGRULAMASI KIRMIZI: $APK"; exit 1; }
echo "TAMAM: $APK"
