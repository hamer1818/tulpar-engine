#!/usr/bin/env bash
# OYUN FABRIKASI — yeni oyun deposu iskeleti (docs/FABRIKA.md asama 2).
#
#   tools/yeni_oyun.sh <dizin> --kimlik <kimlik> --onek <ONEK> --tr "Ad" --en "Name"
#                      [--paket dev.tulparlang.<kimlik>] [--tur arena] [--yon landscape|portrait]
#                      [--git]   (git init + ilk commit)
#
#   kimlik  dosya/anahtar adi (kucuk harf, rakam, _): kayit "tulpar_<kimlik>_kayit.txt",
#           ayar anahtarlari "<kimlik>_efekt" ...
#   ONEK    ortam degiskeni onegi (BUYUK harf): TULPAR_<ONEK>_OTOPILOT / _DIL / _ACILIS ...
#
# Uretilen depo: oyun.tpr (sablon), apk_derle.sh (Android APK + statik dogrulama), tulpar.toml ([ext] .motor/tulpar-ext + [android]),
# tools/ortam.sh (.motor -> motorun ikili dizini, yapi/), tools/dogrula.sh (penceresiz kapi + pozitif
# kontrol), assets/resimler/{acilis,arayuz}.png (kit atlaslari), ikon.png, .gitignore, OKUBENI.md.
# Sonunda ISKELETIN KENDISI dogrulanir: tools/dogrula.sh kosar; kirmiziysa betik kirmizi doner
# (uretilen oyun ilk gun calismiyorsa fabrika bozuktur).
#
# Pozitif kontrol (betigin kendi kapisi): YENI_OYUN_BOZ=1 sablona hatali bir satir ekler ->
# uretilen depo dogrulamada DUSMELI ve betik 1 donmeli.
set -uo pipefail
MOTOR="$(cd "$(dirname "$0")/.." && pwd)"
hata() { echo "HATA: $*" >&2; exit 2; }

DIZIN=""; KIMLIK=""; ONEK=""; TR=""; EN=""; PAKET=""; TUR="arena"; YON="landscape"; GIT=0
while [ $# -gt 0 ]; do
  case "$1" in
    --kimlik) KIMLIK="$2"; shift ;;
    --onek) ONEK="$2"; shift ;;
    --tr) TR="$2"; shift ;;
    --en) EN="$2"; shift ;;
    --paket) PAKET="$2"; shift ;;
    --tur) TUR="$2"; shift ;;
    --yon) YON="$2"; shift ;;
    --git) GIT=1 ;;
    -h|--help) sed -n 2,20p "$0"; exit 0 ;;
    -*) hata "bilinmeyen secenek $1" ;;
    *) [ -z "$DIZIN" ] && DIZIN="$1" || hata "fazla arguman: $1" ;;
  esac
  shift
done
[ -n "$DIZIN" ] || hata "dizin verilmedi (tools/yeni_oyun.sh --help)"
# Uretim TEK KAYNAKTA: editorun fabrika cekirdegi (app/editor_fabrika.cpp, EDITOR-UYGULAMA-PLANI U3).
# Alan denetimi (yerel ayardan bagimsiz, Tuzaklar 8dq), yer tutucular, kit varliklari ve ikon orada;
# editorun "Yeni oyun" formu da ayni fonksiyonu cagirir. Bu betik yalniz .motor baglantisi,
# dogrulama ve git ekler.
ED="${ED:-$MOTOR/yapi/engine_editor}"
[ -x "$ED" ] || hata "$ED yok (motorda cmake --build yapi)"
args=(--yeni-oyun "$DIZIN" --kimlik "$KIMLIK" --onek "$ONEK" --tr "$TR" --en "$EN" --tur "$TUR" --yon "$YON")
[ -n "$PAKET" ] && args+=(--paket "$PAKET")
"$ED" "${args[@]}" || exit 2
KOK="$(cd "$DIZIN" && pwd)"
PAKET="$(sed -n 's/^package = "\(.*\)"/\1/p' "$KOK/tulpar.toml")"
if [ "${YENI_OYUN_BOZ:-0}" = 1 ]; then echo 'func bozuk( {' >> "$KOK/oyun.tpr"; echo "POZITIF KONTROL: YENI_OYUN_BOZ=1 sablonu bozdu" >&2; fi

echo "iskelet: $KOK (tur $TUR, paket $PAKET, onek $ONEK)"
ln -sfn "$MOTOR/yapi" "$KOK/.motor"   # motorun ikili dizini (tulpar.toml: .motor/tulpar-ext)
bash "$KOK/tools/dogrula.sh"; rc=$?
if [ $rc -ne 0 ]; then echo "YENI OYUN: KIRMIZI — uretilen iskelet dogrulamadan gecmedi (rc $rc)" >&2; exit 1; fi
if [ "$GIT" = 1 ]; then
  ( cd "$KOK" && git init -q && git add -A && git commit -q -m "iskelet: $TR ($TUR sablonu, tulpar-engine $(git -C "$MOTOR" describe --tags --always 2>/dev/null))" ) \
    && echo "git: ilk commit atildi"
fi
echo "YENI OYUN: YESIL — $KOK"
