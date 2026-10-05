#!/usr/bin/env bash
# Tulpar Engine — Windows (MSYS2 MINGW64) CI'da yazilim Vulkan ICD'si:
# SwiftShader (runner'daki Google Chrome kurulumunun vk_swiftshader.dll'i).
# CI (ci.yml) Windows isi bu betigi kosar.
#
#   tools/windows_vulkan_icd.sh kur            ICD + dogrulama katmanini loader'a
#                                              tanit, bagimsiz sondalarla dogrula
#   tools/windows_vulkan_icd.sh katman-gizle   katman kaydini sil (pozitif kontrol)
#   tools/windows_vulkan_icd.sh katman-geri    katman kaydini geri koy
#
# On kosul: Google Chrome kurulu (windows-latest imajinda var; surum dizini
# ARANIR, sabit yazilmaz) ve mingw-w64-x86_64-vulkan-validation-layers.
#
# NEDEN SWIFTSHADER, MSYS2 LAVAPIPE DEGIL (olculdu CI windows-latest
# 2026-10-05, Tuzaklar 8cr): mingw-w64-x86_64-mesa 26.2.4'un lavapipe'i
# Windows'ta bellek BIRAKIRKEN sureci yiginini bozuyor (0xC0000374). Motordan
# bagimsiz sonda (tools/vk_bellek_sonda.py: ayir -> esle -> yaz -> birak)
# PageHeap altinda "VERIFIER STOP 10: corrupted start stamp", msvcrt!free <-
# vulkan_lvp. Ayni sonda SwiftShader'da temiz. `kur` o sondayi her kosumda
# kosar: secilen surucu bu sinifi tasiyorsa CI burada, nedeniyle KIRMIZI olur.
#
# NEDEN KAYIT DEFTERI, ORTAM DEGISKENI DEGIL (olculdu): runner sureci YONETICI
# ve Windows loader'i yonetici surecte VK_DRIVER_FILES / VK_ICD_FILENAMES /
# VK_ADD_LAYER_PATH / VK_LAYER_PATH'i YOK SAYAR (VK_LOADER_DEBUG: surucu arama
# konumlari BOS, yalniz HKLM). ICD ve katman loader'in kendi kayit noktalarina
# yazilir: HKLM\SOFTWARE\Khronos\Vulkan\Drivers ve \ExplicitLayers; deger adi =
# manifest'in WINDOWS yolu, DWORD 0 = etkin. reg.exe'nin /v /t /d bayraklarina
# MSYS2 yol cevirisi dokunmasin: MSYS2_ARG_CONV_EXCL='*'.
# MSYS2'de vulkaninfo yok (mingw-w64-x86_64-vulkan-tools paketi bulunamadi):
# loader duzeyindeki olcum tools/vk_sonda.py ile.
set -uo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
katman=/mingw64/bin/VkLayer_khronos_validation.json
SURUCU='HKLM\SOFTWARE\Khronos\Vulkan\Drivers'
KATMAN='HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers'
reg_() { MSYS2_ARG_CONV_EXCL='*' reg "$@"; }
gun="${RUNNER_TEMP:+$(cygpath -u "$RUNNER_TEMP")}"; gun="${gun:-${TMPDIR:-/tmp}}"

case "${1:-}" in
  kur)
    icd="$(find "/c/Program Files/Google/Chrome/Application" "/c/Program Files (x86)/Google/Chrome/Application" \
             -name vk_swiftshader_icd.json 2>/dev/null | sort | tail -1)"
    if [ -z "$icd" ] || [ ! -f "$(dirname "$icd")/vk_swiftshader.dll" ] || [ ! -f "$katman" ]; then
      echo "::error::windows_vulkan_icd: SwiftShader ICD'si (Chrome kurulumunda vk_swiftshader_icd.json + vk_swiftshader.dll) ya da $katman yok."
      ls -la "/c/Program Files/Google/Chrome/Application" 2>&1 | head || true
      exit 1
    fi
    echo "ICD: $icd"
    tr -d '\r' <"$icd"; echo
    reg_ add "$SURUCU" /v "$(cygpath -w "$icd")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_vulkan_icd: ICD kayit defterine yazilamadi"; exit 1; }
    reg_ add "$KATMAN" /v "$(cygpath -w "$katman")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_vulkan_icd: katman kayit defterine yazilamadi"; exit 1; }
    reg_ query 'HKLM\SOFTWARE\Khronos\Vulkan' /s | tr -d '\r'
    python3 -u "$kok/tools/vk_sonda.py" --cihaz SwiftShader --katman VK_LAYER_KHRONOS_validation 2>&1 | tr -d '\r' | tee "$gun/vk_sonda.log"
    if ! grep -qi 'cihaz 0: SwiftShader' "$gun/vk_sonda.log" || grep -q 'EKSIK' "$gun/vk_sonda.log"; then
      echo "::error::windows_vulkan_icd: kayit yazildi ama loader SwiftShader'i ya da dogrulama katmanini GORMUYOR."
      VK_LOADER_DEBUG=error,warn,driver,layer python3 "$kok/tools/vk_sonda.py" 2>&1 | tr -d '\r' | tail -40 || true
      exit 1
    fi
    # Surucunun bellek ayir/birak kalibi saglam mi (Tuzaklar 8cr: lavapipe'i
    # eleyen sinif). Cikis 0 + BITTI satiri sart; surec olurse KIRMIZI.
    python3 -u "$kok/tools/vk_bellek_sonda.py" --tampon 2>&1 | tr -d '\r' | tee "$gun/vk_bellek.log"; rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ] || ! grep -q 'BITTI (surec saglam)' "$gun/vk_bellek.log"; then
      echo "::error::windows_vulkan_icd: surucu motorsuz bellek sondasinda DUSTU (cikis $rc) — Tuzaklar 8cr sinifi; bu surucuyle GPU kapilarina guvenilmez."
      exit 1
    fi
    ;;
  katman-gizle) reg_ delete "$KATMAN" /v "$(cygpath -w "$katman")" /f >/dev/null || { echo "::error::windows_vulkan_icd: katman kaydi silinemedi"; exit 1; } ;;
  katman-geri)  reg_ add "$KATMAN" /v "$(cygpath -w "$katman")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_vulkan_icd: katman kaydi geri konamadi"; exit 1; } ;;
  *) echo "kullanim: tools/windows_vulkan_icd.sh kur|katman-gizle|katman-geri" >&2; exit 2 ;;
esac
