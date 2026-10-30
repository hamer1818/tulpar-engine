#!/usr/bin/env bash
# CI BEKLEME — ayni koşumdaki baska bir isin sonucunu ya da artefaktini bekler
# (ci.yml, Linux savas bolumu ayri iste; 2026-10-30).
#
#   tools/ci_bekle.sh is <is adi>              -> is 'success' ile bitene kadar;
#                                                  baska sonucla bitti / zaman asimi -> cikis 1
#   tools/ci_bekle.sh artefakt <ad> <is adi>  -> artefakt yuklenene kadar; uretecek
#                                                  is (<is adi>) bitti ama artefakt yoksa
#                                                  ya da zaman asimi -> cikis 1
# Ortam: GH_TOKEN, GITHUB_REPOSITORY, GITHUB_RUN_ID; CI_BEKLE (s, 1500),
# CI_BEKLE_ARALIK (s, 10).
#
# NIYE: lavapipe tek koşuda 4 cekirdegi zaten dolduruyor (2026-10-30 olculdu:
# savas 3200 kare LP_NUM_THREADS=4 36.5 s duvar / 125 s kullanici; ayni koşucuda
# yedi uzun kosuyu eszamanli baslatmak 288 s'lik seri yolu 300-330 s'ye
# YAVASLATTI). Kazanc ancak ayri koşucudan gelir; zorunlu check adi degismesin
# diye ana Linux isi ayri isin sonucunu TASIR — o is duserse ana is kirmizi.
set -uo pipefail
BEKLE="${CI_BEKLE:-1500}"; ARALIK="${CI_BEKLE_ARALIK:-10}"
kip="${1:?is|artefakt}"; shift
is_durumu() {   # $1 is adi -> "status conclusion"
  gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}/jobs?filter=latest&per_page=100" \
    --jq ".jobs[] | select(.name==\"$1\") | .status + \" \" + (.conclusion // \"\")" 2>/dev/null | head -1
}
t0=$(date +%s)
gecti() { echo "$1 ($(( $(date +%s) - t0 )) s beklendi)"; exit 0; }
dustu() { echo "::error::$1"; exit 1; }
while :; do
  case "$kip" in
    is)
      d=$(is_durumu "$1")
      case "$d" in
        "completed success") gecti "'$1' isi basarili" ;;
        completed*) dustu "'$1' isi '${d#completed }' ile bitti — bu is onun sonucunu tasir" ;;
      esac ;;
    artefakt)
      a=$(gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}/artifacts?per_page=100" \
            --jq ".artifacts[] | select(.name==\"$1\") | .id" 2>/dev/null | head -1)
      [ -n "$a" ] && gecti "artefakt '$1' hazir"
      d=$(is_durumu "$2")
      case "$d" in completed*) dustu "'$2' isi '${d#completed }' ile bitti ama '$1' artefakti yok" ;; esac ;;
    *) echo "kullanim: $0 is <ad> | artefakt <ad> <is>" >&2; exit 2 ;;
  esac
  [ $(( $(date +%s) - t0 )) -lt "$BEKLE" ] || dustu "$BEKLE s'de beklenen olmadi ($kip $*; son durum '${d:-yok}')"
  sleep "$ARALIK"
done
