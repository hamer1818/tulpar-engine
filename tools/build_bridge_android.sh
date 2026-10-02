#!/usr/bin/env bash
# Tulpar Engine koprusunun Android arsivleri. Motor TulparLang'e bir YEREL
# EKLENTI olarak baglanir (K303): `tulpar build --target=android --ext
# yapi/tulpar-ext oyun.tpr` bildirimin link.android bolumunu okur ve
# arsivleri PAKETIN android/<abi>/ dizininde arar (libtulpar_engine_android.a
# + libengine_*.a). Bu betik iki ABI icin NDK'yla derler ve oraya kopyalar.
#   tools/build_bridge_android.sh            # arm64-v8a + x86_64
#   TULPAR_ANDROID_ABI=x86_64 tools/build_bridge_android.sh   # tek ABI
#   TULPAR_EXT_DIR=/yol/tulpar-ext tools/build_bridge_android.sh  # baska paket
# NDK: TULPAR_ANDROID_NDK ya da ~/Android/Sdk/ndk/* (android_run.sh ile ayni).
# Runtime arsivi (libtulpar_runtime_android.a) TulparLang'in
# android/build_tame_android.sh'indan gelir; derleyici onu kendi arama
# dizinlerinde (TULPAR_ANDROID_LIB_DIR) bulur, paketten degil.
# 2026-10-02'ye kadar bu arsiv TulparLang kaynak agacini (TULPAR_ROOT, VMValue
# basliklari) istiyordu; artik dis bagimlilik yok.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # depo koku
NDK="${TULPAR_ANDROID_NDK:-}"
if [ -z "$NDK" ]; then
    NDK=$(ls -d "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/android-ndk-* 2>/dev/null | sort -V | head -1 || true)
fi
[ -z "$NDK" ] && { echo "HATA: NDK yok (TULPAR_ANDROID_NDK)"; exit 1; }
ABIS="${TULPAR_ANDROID_ABI:-arm64-v8a x86_64}"
for ABI in $ABIS; do
    if [ "$ABI" = "arm64-v8a" ]; then BUILD="$ROOT/build-android"; else BUILD="$ROOT/build-android-$ABI"; fi
    echo "=== $ABI (NDK $(basename "$NDK")) -> $BUILD"
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD" -j --target tulpar_engine_android 2>&1 | grep -E "error|Error" | grep -v third_party && { echo "HATA: derleme"; exit 1; } || true
    DIST="${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext}/android/$ABI"
    mkdir -p "$DIST"
    for a in tulpar_engine_android engine_content engine_renderer engine_sim engine_rhi engine_audio engine_core engine_platform engine_jolt engine_recast engine_meshopt engine_astcenc; do
        f="$BUILD/lib$a.a"
        [ -f "$f" ] || { echo "HATA: $f yok"; exit 1; }
        cp "$f" "$DIST/"
    done
    ls -la "$DIST"/libtulpar_engine_android.a "$DIST"/libengine_core.a
done
echo "Tamam. 'tulpar build --target=android --ext ${TULPAR_EXT_DIR:-$ROOT/yapi/tulpar-ext} oyun.tpr' bu arsivleri kullanir."
