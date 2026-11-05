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
# Uretilen depo: oyun.tpr (sablon), tulpar.toml ([ext] .motor/yapi/tulpar-ext + [android]),
# tools/ortam.sh (.motor -> motor calisma agaci), tools/dogrula.sh (penceresiz kapi + pozitif
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
# Duzenli ifade denetimleri C yerel ayariyla: tr_TR.UTF-8'de bash'in [a-z] araligi "i"yi
# ESLESTIRMIYOR ("ci_oyun" reddedildi, "deneme" gecti — olculdu 2026-11-05; Tuzaklar 8dq).
gecerli() { local LC_ALL=C; [[ "$1" =~ $2 ]]; }
gecerli "$KIMLIK" '^[a-z][a-z0-9_]{1,30}$' || hata "--kimlik kucuk harf/rakam/_ olmali (2-31): '$KIMLIK'"
gecerli "$ONEK" '^[A-Z][A-Z0-9]{1,9}$' || hata "--onek BUYUK harf/rakam olmali (2-10): '$ONEK'"
[ -n "$TR" ] && [ -n "$EN" ] || hata "--tr ve --en adlari gerekli"
case "$TR$EN" in *'"'*|*'\'*|*'{{'*) hata "ad cift tirnak, ters bolu ya da {{ icermemeli" ;; esac
PAKET="${PAKET:-dev.tulparlang.${KIMLIK//_/}}"
gecerli "$PAKET" '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$' || hata "--paket gecerli Android paket adi degil: '$PAKET'"
case "$YON" in landscape|portrait) ;; *) hata "--yon landscape|portrait" ;; esac
SABLON="$MOTOR/tulpar/sablonlar/$TUR"
[ -f "$SABLON/oyun.tpr" ] || hata "sablon yok: $SABLON (mevcut: $(ls "$MOTOR/tulpar/sablonlar" | tr '\n' ' '))"
if [ -e "$DIZIN" ] && [ -n "$(ls -A "$DIZIN" 2>/dev/null)" ]; then hata "$DIZIN bos degil (uzerine yazilmaz)"; fi
mkdir -p "$DIZIN"/tools "$DIZIN"/assets/resimler "$DIZIN"/assets/sesler
KOK="$(cd "$DIZIN" && pwd)"

yerlestir() { # $1 kaynak, $2 hedef
  python3 - "$1" "$2" "$KIMLIK" "$ONEK" "$TR" "$EN" "$PAKET" "$YON" <<'PY'
import sys
src, dst, kimlik, onek, tr, en, paket, yon = sys.argv[1:]
s = open(src, encoding="utf-8").read()
for a, b in (("{{KIMLIK}}", kimlik), ("{{ONEK}}", onek), ("{{OYUN_TR}}", tr), ("{{OYUN_EN}}", en),
             ("{{PAKET}}", paket), ("{{YON}}", yon)):
    s = s.replace(a, b)
assert "{{" not in s, "doldurulmamis yer tutucu: " + src
open(dst, "w", encoding="utf-8").write(s)
PY
}

yerlestir "$SABLON/oyun.tpr" "$KOK/oyun.tpr"
if [ "${YENI_OYUN_BOZ:-0}" = 1 ]; then echo 'func bozuk( {' >> "$KOK/oyun.tpr"; echo "POZITIF KONTROL: YENI_OYUN_BOZ=1 sablonu bozdu" >&2; fi
for f in "$MOTOR"/tulpar/sablonlar/ortak/*; do
  ad="$(basename "$f")"
  case "$ad" in
    tulpar.toml|OKUBENI.md|gitignore) yerlestir "$f" "$KOK/${ad/gitignore/.gitignore}" ;;
    ortam.sh|dogrula.sh) yerlestir "$f" "$KOK/tools/$ad"; chmod +x "$KOK/tools/$ad" ;;
  esac
done
cp "$MOTOR/tulpar/kit/varlik/acilis.png" "$MOTOR/tulpar/kit/varlik/arayuz.png" "$KOK/assets/resimler/"
python3 "$MOTOR/tools/kit/ikon_uret.py" --ad "$TR" --cikti "$KOK/ikon.png" >/dev/null || hata "ikon uretilemedi"

echo "iskelet: $KOK (tur $TUR, paket $PAKET, onek $ONEK)"
ln -sfn "$MOTOR" "$KOK/.motor"
bash "$KOK/tools/dogrula.sh"; rc=$?
if [ $rc -ne 0 ]; then echo "YENI OYUN: KIRMIZI — uretilen iskelet dogrulamadan gecmedi (rc $rc)" >&2; exit 1; fi
if [ "$GIT" = 1 ]; then
  ( cd "$KOK" && git init -q && git add -A && git commit -q -m "iskelet: $TR ($TUR sablonu, tulpar-engine $(git -C "$MOTOR" describe --tags --always 2>/dev/null))" ) \
    && echo "git: ilk commit atildi"
fi
echo "YENI OYUN: YESIL — $KOK"
