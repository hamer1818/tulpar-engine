#!/usr/bin/env bash
# Tulpar Engine koprusunun iOS arsivleri + iOS eklenti paketi (yalniz macOS:
# Apple SDK'si orada). Android'deki tools/build_bridge_android.sh'in karsiligi.
#
#   MOLTENVK_DIR=<MoltenVK.xcframework'u iceren dizin> tools/build_bridge_ios.sh
#   TULPAR_IOS_SDKS=iphonesimulator tools/build_bridge_ios.sh     # tek SDK
#   TULPAR_EXT_IOS_DIR=/yol tools/build_bridge_ios.sh               # paket yeri
#
# Cikti (varsayilan yapi/tulpar-ext-ios/): masaustu paketinin aynisi (engine.tpr,
# assets/fonts) + ios/<sdk>/ altinda libtulpar_engine_ios.a, libengine_*.a ve
# MoltenVK'nin STATIK arsivi + "link.ios" bolumlu tulpar-ext.json. Bolum ana
# bildirimde (tulpar/generated/tulpar-ext.json) DEGIL: "ios" anahtarini tanimayan
# tulpar surumleri bildirimin tamamini reddeder; iOS'u bilen surum yayinlaninca
# gen_engine_bindings.py'ye tasinir.
#
# Oyun: TULPAR_IOS_SDK=iphoneos tulpar build --target=ios --ext yapi/tulpar-ext-ios oyun.tpr
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ "$(uname -s)" = "Darwin" ] || { echo "HATA: iOS arsivleri macOS'ta derlenir (Apple SDK)"; exit 1; }
command -v xcrun >/dev/null || { echo "HATA: xcrun yok (Xcode komut satiri araclari)"; exit 1; }
MVK="${MOLTENVK_DIR:-}"
XCF="$MVK/MoltenVK.xcframework"
[ -d "$XCF" ] || { echo "HATA: MOLTENVK_DIR/MoltenVK.xcframework yok ($XCF) — MoltenVK surum arsivinin static/ dizini"; exit 1; }
SDKS="${TULPAR_IOS_SDKS:-iphoneos iphonesimulator}"
MIN="${TULPAR_IOS_MIN:-15.0}"
PKG="${TULPAR_EXT_IOS_DIR:-$ROOT/yapi/tulpar-ext-ios}"
mkdir -p "$PKG/assets/fonts"
cp "$ROOT/tulpar/engine.tpr" "$PKG/"
cp "$ROOT/assets/fonts/DejaVuSans.ttf" "$ROOT/assets/fonts/DejaVu-LICENSE.txt" "$PKG/assets/fonts/"
LIBS="tulpar_engine_ios engine_content engine_renderer engine_sim engine_rhi engine_audio engine_core engine_platform engine_jolt engine_recast engine_meshopt engine_astcenc"
for SDK in $SDKS; do
  case "$SDK" in
    iphoneos) SLICE_RE='^ios-arm64$' ;;
    iphonesimulator) SLICE_RE='^ios-arm64.*-simulator$' ;;
    *) echo "HATA: bilinmeyen sdk $SDK"; exit 1 ;;
  esac
  BUILD="$ROOT/build-ios-$SDK"
  echo "=== $SDK -> $BUILD"
  cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN" -DCMAKE_BUILD_TYPE=Release \
        -DENGINE_MEM_CANARY=OFF >/dev/null
  cmake --build "$BUILD" --target tulpar_engine_ios -j"$(sysctl -n hw.ncpu)"
  DIST="$PKG/ios/$SDK"
  mkdir -p "$DIST"
  for a in $LIBS; do
    f="$BUILD/lib$a.a"
    [ -f "$f" ] || { echo "HATA: $f yok"; exit 1; }
    cp "$f" "$DIST/"
  done
  SLICE=""
  for d in "$XCF"/*/; do
    n="$(basename "$d")"
    if [[ "$n" =~ $SLICE_RE ]]; then SLICE="$d"; fi
  done
  [ -n "$SLICE" ] || { echo "HATA: MoltenVK.xcframework icinde $SDK dilimi yok"; ls "$XCF"; exit 1; }
  MA="$(find "$SLICE" -name 'libMoltenVK.a' | head -1)"
  [ -f "$MA" ] || { echo "HATA: $SLICE icinde libMoltenVK.a yok (dinamik dilim mi verildi?)"; exit 1; }
  cp "$MA" "$DIST/libMoltenVK.a"
  ls -la "$DIST"
done
# Bildirim: masaustu bildirimi + link.ios. {abi} = SDK adi (iphoneos | iphonesimulator).
python3 - "$ROOT/tulpar/generated/tulpar-ext.json" "$PKG/tulpar-ext.json" "$LIBS" <<'PY'
import json, sys
src, dst, libs = sys.argv[1], sys.argv[2], sys.argv[3].split()
d = json.load(open(src, encoding="utf-8"))
d["link"] = {"ios": {
    "lib_dirs": ["ios/{abi}"],
    "libs": libs + ["MoltenVK"],
    # _main kopru arsivinde (bridge/ios_host.mm): -u ile arsivden cekilir.
    "flags": ["-Wl,-u,_main", "-lc++",
              "-framework UIKit", "-framework QuartzCore", "-framework Metal", "-framework IOSurface",
              "-framework CoreGraphics", "-framework AVFoundation", "-framework AudioToolbox",
              "-framework CoreAudio", "-framework CoreFoundation", "-framework Foundation"],
}}
json.dump(d, open(dst, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
PY
echo "Tamam: $PKG (tulpar build --target=ios --ext $PKG oyun.tpr)"
