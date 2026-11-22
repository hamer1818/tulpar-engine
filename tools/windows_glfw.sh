#!/usr/bin/env bash
# Windows CI/surum: glfw3.dll'i MSYS2 ortaminin bin dizinine koyar (2026-11-22).
#
# NEDEN: MSYS2 MINGW64 ortamini kullanimdan kaldiriyor (msys2.org/news 2026-03-15, 2026-10-24: leaf paketlerin MINGW64 derlemeleri
# kaldiriliyor) ve `mingw-w64-x86_64-glfw` 2026-11-22'de depodan silinmisti: PR #149/#150'nin Windows isleri "target not found"
# ile dustu. Motor glfw3.dll'i calisma aninda dl_open'lar; tools/package.sh onu ${MSYSTEM_PREFIX}/bin'den pakete kopyalar.
# Upstream GLFW 3.4 ikilisi (lib-mingw-w64: MinGW-w64 ile derlenmis, MSVCRT) SURUM + SHA-256 SABIT indirilir; ozet tutmazsa KIRMIZI.
# Kalici cozum UCRT64'e tasinmak (docs/TUZAKLAR.md 8ep) — 2026-11-22'de yapildi (8et): UCRT64'te mingw-w64-ucrt-x86_64-glfw
# paketi var ve tools/windows_msys2_paketler.txt onu kurar; bu betik dosya varsa hicbir sey indirmez (yerel MINGW64 kurulumlari icin kalir).
set -euo pipefail
SURUM=3.4
SHA=54efa829400f2a0537f742b2b3bdd74e437bb4f2f048e4b7d3c5557d11a611e6
HEDEF="${MSYSTEM_PREFIX:-/ucrt64}/bin"
if [ -f "$HEDEF/glfw3.dll" ]; then echo "glfw3.dll zaten var: $HEDEF"; exit 0; fi
GECICI="$(mktemp -d)"
trap 'rm -rf "$GECICI"' EXIT
curl -sSLf --retry 3 -o "$GECICI/glfw.zip" "https://github.com/glfw/glfw/releases/download/$SURUM/glfw-$SURUM.bin.WIN64.zip"
OZET="$(sha256sum "$GECICI/glfw.zip" | cut -d' ' -f1)"
if [ "$OZET" != "$SHA" ]; then echo "HATA: glfw-$SURUM.bin.WIN64.zip ozeti tutmadi ($OZET != $SHA)"; exit 1; fi
python3 - "$GECICI/glfw.zip" "$HEDEF/glfw3.dll" "$SURUM" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
veri = z.read("glfw-%s.bin.WIN64/lib-mingw-w64/glfw3.dll" % sys.argv[3])
open(sys.argv[2], "wb").write(veri)
PY
echo "glfw3.dll (GLFW $SURUM, upstream MinGW-w64 ikilisi) -> $HEDEF ($(wc -c < "$HEDEF/glfw3.dll") bayt)"
