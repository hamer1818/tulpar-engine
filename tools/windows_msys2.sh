#!/usr/bin/env bash
# Windows CI: MSYS2 paketlerini KURULU AGAC onbelleginin ustune kurar (2026-11-22, Tuzaklar 8es).
#
#   tools/windows_msys2.sh kur [liste]   listeyi (varsayilan tools/windows_msys2_paketler.txt; Windows arm64 isi
#                                        tools/windows_msys2_paketler_arm64.txt verir) `pacman -S --needed` ile kurar;
#                                        GITHUB_OUTPUT'a degisti=true|false yazar (onbellek kaydedilsin mi)
#
# NEDEN: setup-msys2'nin `install:` adimi her iste ~57 s suruyordu (koşum 45030941115: imza/butunluk denetimi 12 s,
# 74 paketi / 1,6 GB'i acmak ~44 s). Paket ONBELLEGI (setup-msys2'nin kendi cache'i) indirmeyi kurtariyor ama acmayi
# degil. Burada setup-msys2 yalniz tabani kurar; /mingw64 ve pacman'in mingw-w64-* yerel kayitlari actions/cache'ten
# gelir; bu betik `pacman -S --needed` kosar. DOGRULUGU ONBELLEK DEGIL PACMAN TASIR: kayit yoksa ya da depoda daha yeni
# surum varsa pacman eksigi kurar / yukseltir (o zaman degisti=true ve yeni kayit saklanir); isabette is kalmaz.
set -euo pipefail
kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
liste="${2:-$kok/tools/windows_msys2_paketler.txt}"
case "${1:-}" in
  kur)
    mapfile -t paketler < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' "$liste" | grep -v '^$')
    [ "${#paketler[@]}" -gt 0 ] || { echo "HATA: $liste bos"; exit 1; }
    once="$(pacman -Q 2>/dev/null | sort || true)"
    t0=$SECONDS
    pacman -S --needed --noconfirm --overwrite '*' "${paketler[@]}"   # setup-msys2 ile ayni: kaydi olmayan artik dosya cakisma vermesin
    sonra="$(pacman -Q | sort)"
    # Kurulum GERCEKTEN tamam mi: listedeki her paket kurulu olmali (onbellekten gelen yarim agac sessiz gecmesin).
    for p in "${paketler[@]}"; do pacman -Q "$p" >/dev/null || { echo "HATA: $p kurulu degil"; exit 1; }; done
    degisen="$(comm -13 <(echo "$once") <(echo "$sonra") | wc -l)"
    echo "msys2 paketleri: ${#paketler[@]} istendi, $degisen paket kuruldu/yukseltildi, $((SECONDS - t0)) s"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
      if [ "$degisen" -gt 0 ]; then echo "degisti=true" >> "$GITHUB_OUTPUT"; else echo "degisti=false" >> "$GITHUB_OUTPUT"; fi
    fi
    ;;
  *) echo "kullanim: $0 kur [paket-listesi]" >&2; exit 2 ;;
esac
