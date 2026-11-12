#!/usr/bin/env bash
# Motor kaynaklarini clang++ ile SOZDIZIMI denetiminden gecirir (derlemez).
# Neden: GCC bagimli olmayan sablon kodunda eksik tipi kabul ediyor, Clang
# etmiyor — macOS CI (clang) yerel GCC'nin gecirdigi ecs.hpp'yi reddetti
# (2026-09-14, "subscript of pointer to incomplete type"). Push'tan once bunu
# kosturmak bir CI turu kazandirir. clang++ yoksa GORUNUR atlar.
set -u
cd "$(dirname "$0")/.." || exit 1   # depo koku
if ! command -v clang++ >/dev/null 2>&1; then
    echo "clang_syntax_check: ATLANDI (clang++ yok)"; exit 0
fi
FLAGS="-std=c++17 -fsyntax-only -fno-exceptions -fno-rtti -Wall -Wextra -DENGINE_MEM_CANARY=1 -DENGINE_SOURCE_DIR=\".\" -DPHYSICS_GOLDEN_HASH=0ull -DJPH_CROSS_PLATFORM_DETERMINISTIC -I. -Ithird_party/vulkan -Ithird_party/jolt -Ithird_party/recast/Recast/Include -Ithird_party/recast/Detour/Include -Ithird_party/glfw -Ithird_party/cgltf -Ithird_party/stb -Ithird_party/meshoptimizer -Ithird_party/imgui -Ithird_party/miniaudio -Ithird_party/astcenc -Ithird_party/iconfont -Ithird_party/debug_draw -Ithird_party/mbedtls/include -Ithird_party -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES"
case "$(uname -m)" in x86_64) FLAGS="$FLAGS -DJPH_USE_SSE4_1 -DJPH_USE_SSE4_2 -msse4.2";; esac
# YALNIZ HATA kirmizi: macOS CI -Werror kullanmiyor, yani clang uyarisi orada
# isi dusurmez. Eskiden uyari da HATA sayiliyordu ve betik hep kirmiziydi
# (2026-11-04: 180 dosyalik liste, cogu .claude/worktrees altindaki eski ajan
# kopyalari) — her zaman kirmizi olan kapi bir sey soylemez. Uyarilar sayilir
# ve basilir. .claude/ (ajan calisma agaclari) ve ios/ derleme dizinleri disarida.
fail=0; n=0; nuyari=0
while IFS= read -r f; do
    n=$((n+1))
    log=$(clang++ $FLAGS "$f" 2>&1)
    hata=$(grep -E 'error:' <<< "$log" | grep -v third_party)
    u=$(grep -cE 'warning:' <<< "$log" || true)
    nuyari=$((nuyari + u))
    if [ -n "$hata" ]; then echo "== $f"; echo "$hata" | head -6; fail=1; fi
done < <(find . -name '*.cpp' -not -path '*/third_party/*' -not -path '*/android/*' -not -path './tulpar/*' -not -path './yapi*/*' -not -path './build*/*' -not -path './.claude/*' -not -path './renderer/nextgen_render.cpp' | sort)  # nextgen_render: CMake'te yok (bagimlilik bekliyor)
if [ $fail -ne 0 ]; then echo "clang_syntax_check: HATA ($n dosya, $nuyari uyari)"; exit 1; fi
echo "clang_syntax_check: $n dosya hatasiz ($nuyari clang uyarisi; CI'da -Werror yok)"
