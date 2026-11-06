#!/usr/bin/env bash
# ===========================================================================
# Linux AppImage (docs/EDITOR-UYGULAMA-PLANI.md U7b, K3): tools/package.sh'in urettigi paket
# dizininden TEK DOSYALIK editor. Kullanici indirir, calistirilabilir yapar, cift tiklar; editor
# ilk acilista kendini uygulama menusune ve .sahne dosya turune kaydeder (app/editor_masaustu).
#
#   tools/kurulum/appimage.sh <paket-dizini> <cikti.AppImage>     # uret + kapi
#   tools/kurulum/appimage.sh --denetle <cikti.AppImage>         # yalniz kapi
#
# AppDir = paket dizininin AYNISI (varliklar ikilinin yaninda: asset_path'in 1. adimi) + AppRun +
# .desktop + ikon. GLFW zaten pakette (package.sh dlopen kutuphanelerini koyar); Vulkan yukleyicisi
# BILEREK yok (surucuyle gelir).
#
# ARACLAR SABITLENMIS (surum + SHA-256; GitHub'in yayin ozetiyle 2026-11-06'da karsilastirildi):
# appimagetool 1.9.1 ve type2-runtime 20251108. Indirme onbellegi ~/.cache/tulpar-kurulum
# (TULPAR_KURULUM_ONBELLEK). Ozet tutmazsa DURUR. appimagetool --runtime-file ile calisir: aksi
# halde derleme aninda "continuous" runtime'i indirirdi (sabitlenmemis ikili pakete girerdi).
#
# KAPI (kurulu yoldan kosum; plan: "kurulu yoldan kosum + bir dosyayi eksik koy -> duser"):
#   1. AppImage'in ici SALT OKUNUR acilip --headless 30: cikis 0, kare uretildi (kurulum dizinine
#      yazan kod duser). TULPAR_ENGINE_KAYNAKSIZ=1 — varliklar AppImage'in ICINDEN gelmeli,
#      derleme makinesindeki kaynak agacindan degil.
#   2. --masaustu-kaydet: gecici XDG_DATA_HOME'a .desktop (Exec = AppImage'in yolu) + ikon + MIME.
#   3. POZITIF KONTROL: editorun sahnesi (tests/assets/editor.sahne) silinmis AppDir'den uretilen
#      AppImage ayni kosumda DUSMELI — kapi gercekten AppImage'in icini olcuyor.
# FUSE gerekmez (--appimage-extract; masaustu kaydi APPIMAGE_EXTRACT_AND_RUN=1).
# ===========================================================================
set -uo pipefail
kok="$(cd "$(dirname "$0")/../.." && pwd)"
ONBELLEK="${TULPAR_KURULUM_ONBELLEK:-$HOME/.cache/tulpar-kurulum}"
ARAC_SURUM=1.9.1
ARAC_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_SURUM=20251108
RUNTIME_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
KIMLIK=dev.tulparlang.TulparEditor   # app/editor_masaustu.hpp kMasaustuKimlik ile AYNI

hata() { echo "HATA: $*" >&2; exit 1; }

indir() {  # indir <url> <dosya> <sha256>
  local url="$1" hedef="$ONBELLEK/$2" sha="$3"
  mkdir -p "$ONBELLEK"
  if [ ! -s "$hedef" ]; then
    curl -fsSL -o "$hedef.indiriliyor" "$url" || hata "indirilemedi: $url"
    mv "$hedef.indiriliyor" "$hedef"
  fi
  local olculen; olculen="$(sha256sum "$hedef" | cut -d' ' -f1)"
  [ "$olculen" = "$sha" ] || { rm -f "$hedef"; hata "$2 ozeti TUTMADI (beklenen $sha, olculen $olculen) — silindi"; }
  chmod +x "$hedef"
  printf '%s\n' "$hedef"
}

uret() {  # uret <paket> <cikti> [sil-yol]
  local paket="$1" cikti="$2" sil="${3:-}" is appdir arac runtime
  [ -x "$paket/engine_editor" ] || hata "paket degil (engine_editor yok): $paket"
  [ -s "$paket/assets/ikon/tulpar_256.png" ] || hata "pakette ikon yok: assets/ikon/tulpar_256.png"
  arac="$(indir "https://github.com/AppImage/appimagetool/releases/download/$ARAC_SURUM/appimagetool-x86_64.AppImage" \
    "appimagetool-$ARAC_SURUM-x86_64.AppImage" "$ARAC_SHA")" || exit 1
  runtime="$(indir "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_SURUM/runtime-x86_64" \
    "runtime-$RUNTIME_SURUM-x86_64" "$RUNTIME_SHA")" || exit 1
  is="$(mktemp -d)"; appdir="$is/Tulpar_Editor.AppDir"
  cp -R "$paket" "$appdir"
  [ -n "$sil" ] && rm -f "$appdir/$sil"
  cat > "$appdir/AppRun" <<'APPRUN'
#!/bin/sh
# AppImage girisi: editor kendi dizinindeki varliklari bulur (asset_path 1. adim).
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/engine_editor" "$@"
APPRUN
  chmod +x "$appdir/AppRun"
  # AppDir'in .desktop'u: appimagetool dogrular ve AppImage'a gomer. Kullanicinin menusune
  # yazilan (Exec = AppImage yolu) app/editor_masaustu.cpp'den gelir.
  cat > "$appdir/$KIMLIK.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Tulpar Editör
Name[en]=Tulpar Editor
Comment=Tulpar Engine ile oyun yap
Comment[en]=Make games with Tulpar Engine
Exec=engine_editor %f
Icon=$KIMLIK
Terminal=false
Categories=Development;Game;
MimeType=application/x-tulpar-sahne;
StartupWMClass=$KIMLIK
DESKTOP
  cp "$paket/assets/ikon/tulpar_256.png" "$appdir/$KIMLIK.png"
  cp "$paket/assets/ikon/tulpar_256.png" "$appdir/.DirIcon"
  mkdir -p "$(dirname "$cikti")"
  ( cd "$is" && ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$arac" --no-appstream --runtime-file "$runtime" \
      "$appdir" "$cikti" ) > "$is/arac.log" 2>&1 || { tail -15 "$is/arac.log" >&2; rm -rf "$is"; hata "appimagetool dustu"; }
  rm -rf "$is"
  [ -s "$cikti" ] || hata "AppImage uretilmedi: $cikti"
  chmod +x "$cikti"
}

kos() {  # kos <AppImage> <gunluk-dizini> -> 0 = kosum yesil
  # AppImage acilip ici SALT OKUNUR yapilir (gercek AppImage squashfs'i boyle): editor kurulum
  # dizinine bir sey yazmaya kalkarsa kapi duser. (extract-and-run yazilabilir bir kopyada kosar ve
  # bunu gizlerdi — U7b'de penceresiz duzen kapisi tests/assets'e yaziyordu, boyle bulundu.)
  local ai="$1" g="$2" rc=0
  mkdir -p "$g/is" "$g/veri"
  ( cd "$g" && "$ai" --appimage-extract > /dev/null 2>&1 ) || return 1
  chmod -R a-w "$g/squashfs-root"
  ( cd "$g/is" && APPIMAGE="$ai" TULPAR_ENGINE_KAYNAKSIZ=1 DISPLAY= \
      "$g/squashfs-root/AppRun" --headless 30 --out "$g/kare.ppm" ) > "$g/kosum.log" 2>&1 || rc=1
  chmod -R u+w "$g/squashfs-root"
  [ $rc = 0 ] && [ -s "$g/kare.ppm" ] || return 1
  return 0
}

denetle() {
  local ai; ai="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
  [ -x "$ai" ] || hata "AppImage yok ya da calistirilamaz: $1"
  local g; g="$(mktemp -d)"; local kirmizi=0
  echo "--- AppImage kapisi: $ai ($(du -h "$ai" | cut -f1)) ---"
  if kos "$ai" "$g/iyi"; then
    echo "  kosum   --headless 30, kaynaksiz: cikis 0, kare $(du -h "$g/iyi/kare.ppm" | cut -f1)"
  else
    echo "  KIRMIZI kosum dustu:"; tail -6 "$g/iyi/kosum.log" | sed 's/^/      /'; kirmizi=1
  fi
  if ( XDG_DATA_HOME="$g/veri" APPIMAGE_EXTRACT_AND_RUN=1 "$ai" --masaustu-kaydet "$ai" ) > "$g/masaustu.log" 2>&1 && \
     grep -qF "Exec=\"$ai\" %f" "$g/veri/applications/$KIMLIK.desktop" 2>/dev/null && \
     [ -s "$g/veri/icons/hicolor/256x256/apps/$KIMLIK.png" ] && [ -s "$g/veri/mime/packages/$KIMLIK.xml" ]; then
    echo "  masaustu kaydi: .desktop (Exec = AppImage), ikon, .sahne turu"
  else
    echo "  KIRMIZI masaustu kaydi:"; tail -3 "$g/masaustu.log" | sed 's/^/      /'; kirmizi=1
  fi
  rm -rf "$g"
  return $kirmizi
}

pozitif_kontrol() {  # paket -> sahnesiz AppImage DUSMELI
  local paket="$1" g; g="$(mktemp -d)"
  uret "$paket" "$g/bozuk.AppImage" "tests/assets/editor.sahne"
  if kos "$g/bozuk.AppImage" "$g/k"; then
    echo "  KAPI BOZUK: editor.sahne'siz AppImage da kostu — kapi AppImage'in icini olcmuyor (kaynak agacina mi sizdi?)"
    rm -rf "$g"; return 1
  fi
  echo "  pozitif kontrol: editor.sahne'siz AppImage dustu ($(grep -m1 -iE 'sahne|HATA' "$g/k/kosum.log" | cut -c1-90))"
  rm -rf "$g"; return 0
}

case "${1:-}" in
  --denetle) [ $# = 2 ] || hata "kullanim: appimage.sh --denetle <x.AppImage>"; denetle "$2"; exit $? ;;
  ""|-h|--help) sed -n 2,12p "$0"; exit 2 ;;
esac
[ $# = 2 ] || hata "kullanim: appimage.sh <paket-dizini> <cikti.AppImage>"
paket="$(cd "$1" && pwd)" || hata "paket dizini yok: $1"
cikti="$2"; case "$cikti" in /*) ;; *) cikti="$PWD/$cikti" ;; esac
uret "$paket" "$cikti"
rc=0
denetle "$cikti" || rc=1
pozitif_kontrol "$paket" || rc=1
[ $rc = 0 ] && echo "APPIMAGE: YESIL ($cikti)" || echo "APPIMAGE: KIRMIZI"
exit $rc
