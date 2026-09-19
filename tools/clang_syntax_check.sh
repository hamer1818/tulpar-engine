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
FLAGS="-std=c++17 -fsyntax-only -fno-exceptions -fno-rtti -Wall -Wextra -DENGINE_MEM_CANARY=1 -DENGINE_SOURCE_DIR=\".\" -DPHYSICS_GOLDEN_HASH=0ull -DJPH_CROSS_PLATFORM_DETERMINISTIC -I. -Ithird_party/vulkan -Ithird_party/jolt -Ithird_party/recast/Recast/Include -Ithird_party/recast/Detour/Include -Ithird_party/glfw -Ithird_party/cgltf -Ithird_party/stb -Ithird_party/meshoptimizer -Ithird_party/imgui -Ithird_party/miniaudio -Ithird_party/astcenc -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES"
case "$(uname -m)" in x86_64) FLAGS="$FLAGS -DJPH_USE_SSE4_1 -DJPH_USE_SSE4_2 -msse4.2";; esac
fail=0; n=0
while IFS= read -r f; do
    n=$((n+1))
    out=$(clang++ $FLAGS "$f" 2>&1 | grep -E 'error|warning' | grep -v third_party)
    if [ -n "$out" ]; then echo "== $f"; echo "$out" | head -6; fail=1; fi
done < <(find . -name '*.cpp' -not -path '*/third_party/*' -not -path '*/android/*' -not -path './tulpar/*' -not -path './yapi*/*' -not -path './build*/*' | sort)
if [ $fail -ne 0 ]; then echo "clang_syntax_check: HATA"; exit 1; fi
echo "clang_syntax_check: $n dosya temiz"
