#!/usr/bin/env bash
# CI SURUMU (motor, ci.yml) — her derleme isinin basinda: ikiliye gomulecek
# surumu secer ve SURUM'u GITHUB_ENV'e yazar (Yapilandir -DTULPAR_SURUM,
# "Surum ikilide mi", paketin SURUM.txt'si oradan).
#
#   * main'e acilan PR: BIRLESINCE CIKACAK surumun tahmini
#     (tools/sonraki_surum.sh — otomatik-surum.yml ile ayni hesap). Seviye PR
#     aciklamasindan (PR_GOVDE) ve PR'in commit mesajlarindan okunur; taban
#     main'in son etiketi.
#   * diger her sey (main push): bos -> "kaynak derlemesi" (eskisi gibi).
#   (Etiket derlemesi release.yml'de; surumu orada hazirlik isi verir.)
#
# YARIS: PR'in tabani (merge ref'inin ilk ebeveyni, main HEAD) etiket bekleyen
# bir kod degisikligiyse ve otomatik-surum (~10 s) etiketi henuz atmadiysa,
# "son etiket" bir eksik okunurdu. O zaman en cok CI_SURUM_BEKLE saniye (120)
# etiket beklenir; yine gelmezse TAHMIN EDILMEZ (bos -> kaynak derlemesi) ve
# release.yml sifirdan derler — eski davranis, yanlis surum degil.
#
# Tahmin bir sozlesme degil, bir iddiadir: etikette kullanilmadan once
# yeniden_kullanim_karar.sh gomulu surumu etiketle KARSILASTIRIR; tutmazsa
# (araya baska bir birlesme girdi, aciklama sonradan degisti) sifirdan derlenir.
set -euo pipefail
cd "$(dirname "$0")/.."
BEKLE="${CI_SURUM_BEKLE:-120}"
ENVF="${GITHUB_ENV:-/dev/null}"
OZET="${GITHUB_STEP_SUMMARY:-/dev/null}"
yaz() { echo "SURUM=$1" >> "$ENVF"; echo "SURUM=$1"; }

if [ "${GITHUB_EVENT_NAME:-}" != "pull_request" ] || [ "${GITHUB_BASE_REF:-}" != "main" ]; then
  yaz ""
  exit 0
fi

# pull_request checkout'u refs/pull/N/merge: HEAD^1 = main ucu, HEAD^2 = PR ucu.
if ! git rev-parse -q --verify HEAD^2 >/dev/null; then
  echo "::notice::surum tahmini yok: HEAD bir birlesme commit'i degil — release.yml sifirdan derler"
  echo "- surum tahmini YOK (HEAD birlesme commit'i degil) -> release.yml sifirdan" >> "$OZET"
  yaz ""; exit 0
fi
taban="$(git rev-parse HEAD^1)"
t0=$(date +%s)
while tools/sonraki_surum.sh etiket_bekleniyor "$taban"; do
  if [ $(( $(date +%s) - t0 )) -ge "$BEKLE" ]; then
    echo "::notice::surum tahmini yok: taban ${taban:0:9} etiketini ${BEKLE} s'de almadi — release.yml sifirdan derler"
    echo "- surum tahmini YOK (taban etiketi ${BEKLE} s'de gelmedi) -> release.yml sifirdan" >> "$OZET"
    yaz ""; exit 0
  fi
  echo "taban ${taban:0:9} etiket bekliyor (otomatik-surum) ..."
  sleep 10
  git fetch -q --tags origin || true
done

g=$(mktemp -d); trap 'rm -rf "$g"' EXIT
printf '%s\n' "${PR_GOVDE:-}" > "$g/govde"
git log --format='%B' "HEAD^1..HEAD^2" > "$g/commitler"
seviye="$(tools/sonraki_surum.sh seviye "$g/govde" "$g/commitler")"
son="$(tools/sonraki_surum.sh son)"
yeni="$(tools/sonraki_surum.sh sonraki "$seviye" "$son")"
echo "surum tahmini: $yeni (son etiket ${son:-yok}, artis $seviye)"
echo "- surum tahmini: \`$yeni\` (son \`${son:-yok}\`, artis $seviye) — birlesince etiket bu olmazsa release.yml sifirdan derler" >> "$OZET"
yaz "$yeni"
