#!/usr/bin/env bash
# Tulpar Engine — YAYINLANMIS tulpar'i (TulparLang releases/latest) indirir ve
# SHA256SUMS.txt ile dogrular. CI (ci.yml) ve surum (release.yml) isleri AYNI
# betigi kosar: indirme + dogrulama bir kez yazilir, uc platformda ve iki is
# akisinda kayamaz. (Oncesi: ci.yml'de uc ayri satir ici kopya; Linux'unki
# ozeti HIC dogrulamiyordu, release.yml'de hic yoktu — paketin kosu kapisi
# orada ATLANDI kaliyordu.)
#
#   tools/tulpar_indir.sh <dizin>
#
# Sonuc: <dizin>/tulpar (Windows: tulpar.exe) + <dizin>/libtulpar_runtime.a
# (tulpar AOT linkte ikilinin yanindaki runtime arsivini kullanir). Son satir
# `tulpar: <yol>`; GITHUB_ENV tanimliysa (CI) `TULPAR=<yol>` oraya da yazilir.
# macOS adlari `-macos-arm64` (TulparLang v3.39.0, #463; ikili gercekten
# yalniz arm64'tu — eski `-macos-universal` adlari gecis icin ayni dosyanin
# kopyasi olarak bir sure daha yayinlaniyor).
#
# Ozet dogrulamasinin KENDI pozitif kontrolu: dogrulama gectikten sonra
# ikilinin bir kopyasinda tek bayt degistirilir ve ayni denetim DUSMELI —
# dusmezse betik KIRMIZI (denetim bir sey olcmuyordur).
#
# TULPAR_INDIR_URL: kaynak (varsayilan releases/latest/download; belirli bir
# surum icin .../releases/download/vX.Y.Z).
set -euo pipefail

d="${1:?kullanim: tools/tulpar_indir.sh <dizin>}"
url="${TULPAR_INDIR_URL:-https://github.com/hamer1818/TulparLang/releases/latest/download}"
case "$(uname -s)" in
  Linux)  ikili=tulpar-linux-x64;       rt=libtulpar_runtime-linux-x64.a;       hedef=tulpar ;;
  Darwin) ikili=tulpar-macos-arm64;     rt=libtulpar_runtime-macos-arm64.a;     hedef=tulpar ;;
  MINGW*|MSYS*|CYGWIN*) ikili=tulpar-windows-x64.zip; rt=libtulpar_runtime-windows-x64.a; hedef=tulpar.exe ;;
  *) echo "tulpar_indir: bilinmeyen platform $(uname -s)" >&2; exit 2 ;;
esac
if command -v sha256sum >/dev/null 2>&1; then ozet() { sha256sum "$@"; }; else ozet() { shasum -a 256 "$@"; }; fi

mkdir -p "$d"
for f in "$ikili" "$rt" SHA256SUMS.txt; do
  curl -fsSL --retry 3 -o "$d/$f" "$url/$f" || { echo "::error::tulpar_indir: $f indirilemedi ($url)"; exit 1; }
done
# Satirlar `<ozet> *<ad>`; Windows'ta CRLF olabilir. Adlar sabit-dizgi eslenir.
tr -d '\r' <"$d/SHA256SUMS.txt" | awk -v a="$ikili" -v b="$rt" '{ n=$2; sub(/^\*/, "", n); if (n==a || n==b) print $1 "  " n }' >"$d/beklenen.txt"
if [ "$(wc -l <"$d/beklenen.txt" | tr -d ' ')" != 2 ]; then
  echo "::error::tulpar_indir: SHA256SUMS.txt'de $ikili + $rt satirlari yok"; cat "$d/SHA256SUMS.txt"; exit 1
fi
if ! (cd "$d" && ozet -c beklenen.txt); then
  echo "::error::tulpar_indir: indirilenler SHA256SUMS.txt ile DOGRULANAMADI"; exit 1
fi
# Pozitif kontrol: bozuk kopya ayni denetimden GECMEMELI.
mkdir -p "$d/bozuk"
cp "$d/$ikili" "$d/bozuk/$ikili"
cp "$d/$rt" "$d/bozuk/$rt"
printf 'X' | dd of="$d/bozuk/$ikili" bs=1 seek=100 conv=notrunc 2>/dev/null
if (cd "$d/bozuk" && ozet -c ../beklenen.txt >/dev/null 2>&1); then
  echo "::error::tulpar_indir: pozitif kontrol — tek bayti bozulmus $ikili ozet denetiminden GECTI; denetim bir sey olcmuyor"; exit 1
fi
rm -rf "$d/bozuk"
echo "tulpar_indir: ozetler tuttu (pozitif kontrol: bozuk kopya reddedildi)"

case "$ikili" in
  *.zip)
    python3 -m zipfile -e "$d/$ikili" "$d/"
    mv -f "$d/tulpar-windows-x64.exe" "$d/$hedef" ;;
  *) mv -f "$d/$ikili" "$d/$hedef" ;;
esac
cp -f "$d/$rt" "$d/libtulpar_runtime.a"
chmod +x "$d/$hedef"
yol="$(cd "$d" && pwd)/$hedef"
"$yol" --version
# Sonraki adimlar (tools/package.sh kosu kapisi, tulpar_dogrula.sh) TULPAR'i okur.
if [ -n "${GITHUB_ENV:-}" ]; then echo "TULPAR=$yol" >> "$GITHUB_ENV"; fi
echo "tulpar: $yol"
