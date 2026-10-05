#!/usr/bin/env bash
# Tulpar Engine — Windows (MSYS2 MINGW64) yazilim Vulkan ICD'si: lavapipe.
# CI (ci.yml) ve surum (release.yml) Windows isleri AYNI betigi kosar.
#
#   tools/windows_lavapipe.sh kur            ICD + dogrulama katmanini loader'a
#                                            tanit, bagimsiz sondayla dogrula
#   tools/windows_lavapipe.sh katman-gizle   katman kaydini sil (pozitif kontrol)
#   tools/windows_lavapipe.sh katman-geri    katman kaydini geri koy
#
# On kosul: mingw-w64-x86_64-mesa (bin/vulkan_lvp.dll +
# share/vulkan/icd.d/lvp_icd.x86_64.json) ve
# mingw-w64-x86_64-vulkan-validation-layers (bin/VkLayer_khronos_validation.*).
#
# NEDEN KAYIT DEFTERI, ORTAM DEGISKENI DEGIL (olculdu CI windows-latest,
# 2026-10-05): runner sureci YONETICI ve Windows Vulkan loader'i yonetici
# surecte VK_DRIVER_FILES / VK_ICD_FILENAMES / VK_ADD_LAYER_PATH /
# VK_LAYER_PATH'i YOK SAYAR (VK_LOADER_DEBUG: "Searching for driver manifest
# files / In following locations: / Found no files"; yalniz HKLM taranir) —
# ilk deneme bu yuzden vkCreateInstance -9 ile dustu. ICD ve katman loader'in
# kendi kayit noktalarina yazilir: HKLM\SOFTWARE\Khronos\Vulkan\Drivers ve
# \ExplicitLayers; deger adi = manifest'in WINDOWS yolu, DWORD 0 = etkin.
# reg.exe'nin /v /t /d bayraklarina MSYS2 yol cevirisi dokunmasin diye
# MSYS2_ARG_CONV_EXCL='*'.
#
# "Kuruldu" demek yetmez: `kur` sonunda motordan BAGIMSIZ sonda
# (tools/vk_sonda.py) loader'in llvmpipe cihazini ve
# VK_LAYER_KHRONOS_validation'i gercekten listeledigini olcer; biri eksikse
# cikis 1 (MSYS2'de vulkaninfo yok — mingw-w64-x86_64-vulkan-tools paketi
# bulunamadi, 2026-10-05).
set -uo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
icd=/mingw64/share/vulkan/icd.d/lvp_icd.x86_64.json
katman=/mingw64/bin/VkLayer_khronos_validation.json
SURUCU='HKLM\SOFTWARE\Khronos\Vulkan\Drivers'
KATMAN='HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers'
reg_() { MSYS2_ARG_CONV_EXCL='*' reg "$@"; }

case "${1:-}" in
  kur)
    if [ ! -f "$icd" ] || [ ! -f /mingw64/bin/vulkan_lvp.dll ] || [ ! -f "$katman" ]; then
      echo "::error::windows_lavapipe: mesa / vulkan-validation-layers beklenen dosyalari tasimiyor ($icd, bin/vulkan_lvp.dll, $katman)."
      pacman -Ql mingw-w64-x86_64-mesa mingw-w64-x86_64-vulkan-validation-layers 2>/dev/null | grep -iE 'vulkan|icd|VkLayer' || true
      exit 1
    fi
    tr -d '\r' <"$icd"
    reg_ add "$SURUCU" /v "$(cygpath -w "$icd")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_lavapipe: ICD kayit defterine yazilamadi"; exit 1; }
    reg_ add "$KATMAN" /v "$(cygpath -w "$katman")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_lavapipe: katman kayit defterine yazilamadi"; exit 1; }
    reg_ query 'HKLM\SOFTWARE\Khronos\Vulkan' /s | tr -d '\r'
    python3 "$kok/tools/vk_sonda.py" --cihaz llvmpipe --katman VK_LAYER_KHRONOS_validation 2>&1 | tr -d '\r' | tee "${TMPDIR:-/tmp}/vk_sonda.log"
    if ! grep -qi 'cihaz 0: llvmpipe' "${TMPDIR:-/tmp}/vk_sonda.log" || grep -q 'EKSIK' "${TMPDIR:-/tmp}/vk_sonda.log"; then
      echo "::error::windows_lavapipe: kayit yazildi ama loader lavapipe'i ya da dogrulama katmanini GORMUYOR."
      VK_LOADER_DEBUG=error,warn,driver,layer python3 "$kok/tools/vk_sonda.py" 2>&1 | tr -d '\r' | tail -40 || true
      exit 1
    fi
    ;;
  katman-gizle) reg_ delete "$KATMAN" /v "$(cygpath -w "$katman")" /f >/dev/null || { echo "::error::windows_lavapipe: katman kaydi silinemedi"; exit 1; } ;;
  katman-geri)  reg_ add "$KATMAN" /v "$(cygpath -w "$katman")" /t REG_DWORD /d 0 /f >/dev/null || { echo "::error::windows_lavapipe: katman kaydi geri konamadi"; exit 1; } ;;
  *) echo "kullanim: tools/windows_lavapipe.sh kur|katman-gizle|katman-geri" >&2; exit 2 ;;
esac
