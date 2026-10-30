#!/usr/bin/env bash
# SURUMDE YENIDEN KULLANIM (release.yml, `v*` etiketi) — PR'in test edilmis
# paketi bu surumde yayinlanabilir mi?
#
#   tools/surum_yeniden_kullan.sh      (ortam: SURUM, PLATFORM, AD, GH_TOKEN)
#   -> GITHUB_OUTPUT'a reused=true|false; true ise paket/$AD hazir (ci.yml'in
#      `motor-<platform>` artefakti = tools/package.sh ciktisi).
#
# NIYE (2026-10-30): release.yml etikette uc platformu sifirdan derleyip
# engine_tests'i ikinci kez kosuyordu (~11-12 dk); ayni agaci PR CI zaten
# birlesince cikacak surumle (tools/ci_surum.sh) derleyip test edip
# paketlemisti. Karar tools/yeniden_kullanim_karar.sh'te (PR birlesmis ve
# etiketin commit'i, agac = PR koşumunun DERLEDIGI agac, koşum yesil, kayitli
# surum = etiket). Ustune burada PAKETIN KENDISI olculur: SURUM.txt
# "<surum> <platform>" ve engine_editor'a gomulu isaret
# `tulpar-engine-surum:<surum>` (release.yml'in "Surum ikilide mi" kapisiyla
# ayni olcu). Biri tutmazsa reused=false -> is sifirdan derler (eski yol).
# Sonraki kapilar (manifest, arsivle, arsivden acilan paket) iki yolda da AYNEN
# kosar. Karar ve sebep is ozetinde.
set -uo pipefail
cd "$(dirname "$0")/.."
: "${SURUM:?}" "${PLATFORM:?}" "${AD:?}"
OUT="${GITHUB_OUTPUT:-/dev/null}"
OZET="${GITHUB_STEP_SUMMARY:-/dev/null}"
hayir() {
  echo "Sifirdan derleniyor: $1"
  echo "- **$PLATFORM: sifirdan derlendi** — $1" >> "$OZET"
  rm -rf "paket/$AD" _pr_paket
  echo "reused=false" >> "$OUT"; exit 0
}
case "${GITHUB_REF:-}" in refs/tags/v*) ;; *) hayir "etiket koşumu degil (${GITHUB_REF:-?})" ;; esac

pr_no=$(git log -1 --format='%s' | grep -oE '\(#[0-9]+\)' | tail -1 | tr -d '(#)' || true)
[ -n "$pr_no" ] || hayir "etiket commit'inin basliginda (#N) yok"
pr_bilgi=$(gh pr view "$pr_no" --json headRefOid,state,mergeCommit \
             --jq '[.headRefOid // "", .state // "", (.mergeCommit.oid // "")] | join(" ")' 2>/dev/null) \
  || hayir "PR #$pr_no okunamadi"
read -r head_sha pr_durum pr_birlesme <<< "$pr_bilgi"
kosum=$(gh run list --workflow='CI' --status=success --limit=40 --json databaseId,headSha,event \
          --jq ".[] | select(.headSha==\"$head_sha\" and .event==\"pull_request\") | .databaseId" 2>/dev/null | head -1)
pr_agac=""; pr_surum=""; kosum_sonuc=""
if [ -n "$kosum" ]; then
  kosum_sonuc=$(gh run view "$kosum" --json conclusion --jq .conclusion 2>/dev/null || true)
  b=$(mktemp -d)
  if gh run download "$kosum" -n "derleme-bilgisi-$PLATFORM" -D "$b" >/dev/null 2>&1; then
    f=$(find "$b" -name 'derleme-bilgisi.txt' | head -1)
    [ -n "$f" ] && { pr_agac=$(sed -n 's/^agac=//p' "$f"); pr_surum=$(sed -n 's/^surum=//p' "$f"); }
  fi
fi
k=$(bash tools/yeniden_kullanim_karar.sh "$SURUM" "$(git rev-parse HEAD)" "$(git rev-parse 'HEAD^{tree}')" \
      "$pr_no" "${pr_durum:-}" "${pr_birlesme:-}" "$kosum" "$kosum_sonuc" "$pr_agac" "$pr_surum")
echo "$k"
sebep=$(sed -n 's/^sebep=//p' <<< "$k")
grep -q '^karar=yeniden$' <<< "$k" || hayir "$sebep"

rm -rf "paket/$AD" _pr_paket; mkdir -p paket _pr_paket
gh run download "$kosum" -n "motor-$PLATFORM" -D _pr_paket || hayir "motor-$PLATFORM indirilemedi (koşum $kosum)"
# Artefakt kokunde paket agaci (SURUM.txt) ya da bir alt dizinde.
kaynak=$(dirname "$(find _pr_paket -name SURUM.txt | head -1)" 2>/dev/null)
[ -n "$kaynak" ] && [ -f "$kaynak/SURUM.txt" ] || hayir "motor-$PLATFORM artefaktinda SURUM.txt yok"
mv "$kaynak" "paket/$AD" || hayir "paket/$AD yerlestirilemedi"
rm -rf _pr_paket

# PAKETIN KENDISI: SURUM.txt + ikilideki isaret (indirilen baytlarda olculur).
beklenen="$SURUM $PLATFORM"
gercek="$(tr -d '\r' < "paket/$AD/SURUM.txt" | head -1)"
[ "$gercek" = "$beklenen" ] || hayir "indirilen SURUM.txt '$gercek', beklenen '$beklenen'"
exe="paket/$AD/engine_editor"; [ -f "$exe.exe" ] && exe="$exe.exe"
[ -f "$exe" ] || hayir "indirilen pakette engine_editor yok"
gomulu="$(LC_ALL=C grep -a -o -m1 -E 'tulpar-engine-surum:[0-9A-Za-z.-]*' "$exe" || true)"
gomulu="${gomulu%%$'\n'*}"
[ "$gomulu" = "tulpar-engine-surum:$SURUM" ] || hayir "indirilen engine_editor'a gomulu '${gomulu:-YOK}', beklenen 'tulpar-engine-surum:$SURUM'"
LC_ALL=C grep -a -q -F "$PLATFORM" "$exe" || hayir "indirilen engine_editor'da platform etiketi '$PLATFORM' yok"

echo "Yeniden kullaniliyor: $sebep; paket SURUM.txt '$gercek', ikili '$gomulu'"
find "paket/$AD" -maxdepth 1 | sort | head -20
echo "- **$PLATFORM: yeniden kullanildi** — $sebep; paketin SURUM.txt'si ve engine_editor isareti etiketle olculdu" >> "$OZET"
echo "reused=true" >> "$OUT"
