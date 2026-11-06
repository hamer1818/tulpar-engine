#!/usr/bin/env bash
# ===========================================================================
# Windows kurucusu (docs/EDITOR-UYGULAMA-PLANI.md U7b, K1/K2): tools/package.sh ciktisindan Inno
# Setup kurucusu uretir ve KURULU YOLDAN olcer. MSYS2 bash'te (CI Windows isi) kosar.
#
#   tools/kurulum/windows_kurucu.sh <paket-dizini> <cikti-dizini>
#
# ISCC: $ISCC, yoksa "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (GitHub windows-latest imajinda
# hazir; TulparLang kurucusu da onu kullaniyor). Yoksa KIRMIZI — atlanmaz.
#
# KAPI:
#   1. derle -> sessiz kur (/CURRENTUSER, gecici dizin, .sahne gorevi acik)
#   2. kurulu yoldan: TulparEditor.exe + engine_editor.exe var; HKCU\Software\Classes\.sahne ->
#      TulparEditor.Sahne -> "...\TulparEditor.exe" "%1"
#   3. kurulu engine_editor --headless 10 --kapisiz (TULPAR_ENGINE_KAYNAKSIZ=1: varliklar KURULUMDAN)
#      cikis 0 + kare; kurulu editorle --yeni-oyun (sablonlar kurulumda)
#   4. kaldir -> dizin ve .sahne kaydi gitti
#   5. POZITIF KONTROL: editor.sahne'si silinmis paketten kurucu -> kurulu kosum DUSMELI
# ===========================================================================
set -uo pipefail
kok="$(cd "$(dirname "$0")/../.." && pwd)"
hata() { echo "HATA: $*" >&2; exit 1; }
[ $# = 2 ] || hata "kullanim: windows_kurucu.sh <paket-dizini> <cikti-dizini>"
command -v cygpath >/dev/null || hata "MSYS2/Cygwin bash gerekir (cygpath yok)"
paket="$(cd "$1" && pwd)" || hata "paket yok: $1"
mkdir -p "$2"; cikti="$(cd "$2" && pwd)"
ISCC="${ISCC:-/c/Program Files (x86)/Inno Setup 6/ISCC.exe}"
[ -x "$ISCC" ] || hata "ISCC yok: $ISCC (Inno Setup 6) — kurucu kapisi OLCEMEZ"
[ -f "$paket/TulparEditor.exe" ] || hata "pakette TulparEditor.exe yok (Windows yapisi mi?)"
# MSYS'in '/' ile baslayan argumanlari yola cevirmesini kapat (ISCC /D..., kurucu /VERYSILENT).
export MSYS2_ARG_CONV_EXCL='*'

surum="$(cut -d' ' -f1 "$paket/SURUM.txt" 2>/dev/null)"; surum="${surum#v}"
case "$surum" in [0-9]*.[0-9]*.[0-9]*) sayisal="$(printf '%s' "$surum" | sed 's/[^0-9.].*//').0" ;; *) surum="0.0.0-kaynak"; sayisal="0.0.0.0" ;; esac

derle() {  # derle <paket> <cikti-dizini> <ad>
  "$ISCC" /Q "/DPaketDizini=$(cygpath -w "$1")" "/DAppVersion=$surum" "/DVersionInfo=$sayisal" \
    "/DCiktiDizini=$(cygpath -w "$2")" "/DCiktiAdi=$3" "$(cygpath -w "$kok/tools/kurulum/windows.iss")"
}
kur() {  # kur <kurucu.exe> <hedef-dizin> <gunluk>
  "$1" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER "/DIR=$(cygpath -w "$2")" "/TASKS=sahne" "/LOG=$(cygpath -w "$3")"
}
kaldir() {  # kaldir <kurulu-dizin>: kaldirici kendini kopyalayip HEMEN doner; dizin gidene kadar bekle
  [ -x "$1/unins000.exe" ] || return 1
  "$1/unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART || return 1
  for _ in $(seq 1 120); do [ -e "$1/engine_editor.exe" ] || return 0; sleep 1; done
  return 1
}
kos() {  # kos <kurulu-dizin> <gunluk-dizini>
  mkdir -p "$2/is"
  ( cd "$2/is" && TULPAR_ENGINE_KAYNAKSIZ=1 "$1/engine_editor.exe" --headless 10 --kapisiz --out "$(cygpath -w "$2/kare.ppm")" ) \
    > "$2/kosum.log" 2>&1 || return 1
  [ -s "$2/kare.ppm" ]
}
kayit() { reg query 'HKCU\Software\Classes\TulparEditor.Sahne\shell\open\command' /ve 2>/dev/null | tr -d '\r'; }

g="$(mktemp -d)"; kirmizi=0
derle "$paket" "$cikti" "tulpar-editor-kurulum-windows-x64" > "$g/iscc.log" 2>&1 || { tail -20 "$g/iscc.log"; hata "ISCC dustu"; }
kurucu="$cikti/tulpar-editor-kurulum-windows-x64.exe"
[ -s "$kurucu" ] || hata "kurucu uretilmedi"
echo "--- Windows kurucusu: $kurucu ($(du -h "$kurucu" | cut -f1), surum $surum) ---"

hedef="$g/kurulu"
if kur "$kurucu" "$hedef" "$g/kur.log"; then
  echo "  kur     sessiz kurulum: $hedef"
else
  echo "  KIRMIZI kurulum dustu:"; tail -10 "$g/kur.log" | sed 's/^/      /'; exit 1
fi
for f in TulparEditor.exe engine_editor.exe SURUM.txt DOSYALAR.txt assets/ikon/tulpar.ico tulpar/sablonlar/arena/oyun.tpr; do
  [ -s "$hedef/$f" ] || { echo "  KIRMIZI kurulumda $f yok"; kirmizi=1; }
done
k="$(kayit)"
if printf '%s' "$k" | grep -qi 'TulparEditor.exe.*%1'; then echo "  kayit   .sahne -> $(printf '%s' "$k" | grep -io '"[^"]*TulparEditor.exe"' | head -1)"
else echo "  KIRMIZI .sahne iliskisi yok: '$k'"; kirmizi=1; fi
if kos "$hedef" "$g/iyi"; then echo "  kosum   kurulu engine_editor --headless 10, kaynaksiz: cikis 0, kare var"
else echo "  KIRMIZI kurulu kosum dustu:"; tail -8 "$g/iyi/kosum.log" | sed 's/^/      /'; kirmizi=1; fi
if ( cd "$g" && TULPAR_ENGINE_KAYNAKSIZ=1 "$hedef/engine_editor.exe" --yeni-oyun "$(cygpath -w "$g/oyun")" --kimlik kur_oyun \
       --onek KUR --tr "Kurulum Oyunu" --en "Install Game" ) > "$g/yeni.log" 2>&1 && [ -s "$g/oyun/oyun.tpr" ]; then
  echo "  fabrika kurulu editorle Yeni oyun: $(head -1 "$g/yeni.log" | tr -d '\r')"
else echo "  KIRMIZI kurulu editorle Yeni oyun:"; tail -3 "$g/yeni.log" | sed 's/^/      /'; kirmizi=1; fi
if kaldir "$hedef" && [ -z "$(kayit)" ]; then echo "  kaldir  dizin ve .sahne kaydi temizlendi"
else echo "  KIRMIZI kaldirma: dizin $( [ -e "$hedef/engine_editor.exe" ] && echo DURUYOR || echo gitti ), kayit '$(kayit)'"; kirmizi=1; fi

# Pozitif kontrol: sahnesiz paket -> kurulu kosum duser.
cp -R "$paket" "$g/bozuk_paket"; rm -f "$g/bozuk_paket/tests/assets/editor.sahne"
if derle "$g/bozuk_paket" "$g" "bozuk" > "$g/iscc2.log" 2>&1 && kur "$g/bozuk.exe" "$g/bozuk_kurulu" "$g/kur2.log"; then
  if kos "$g/bozuk_kurulu" "$g/bozuk"; then
    echo "  KAPI BOZUK: editor.sahne'siz kurulum da kostu — kapi kurulumu olcmuyor (kaynak agacina mi sizdi?)"; kirmizi=1
  else echo "  pozitif kontrol: editor.sahne'siz kurulum dustu ($(grep -m1 -i 'sahne' "$g/bozuk/kosum.log" | tr -d '\r' | cut -c1-80))"; fi
  kaldir "$g/bozuk_kurulu" || true
else
  echo "  KIRMIZI pozitif kontrol kurucusu uretilemedi/kurulamadi"; kirmizi=1
fi
rm -rf "$g" 2>/dev/null || true
[ $kirmizi = 0 ] && echo "WINDOWS KURUCU: YESIL" || echo "WINDOWS KURUCU: KIRMIZI"
exit $kirmizi
