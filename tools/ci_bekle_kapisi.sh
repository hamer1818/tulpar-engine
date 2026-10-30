#!/usr/bin/env bash
# CI BEKLEME KAPISI — tools/ci_bekle.sh baska isin sonucunu gercekten tasiyor mu?
# Sahte `gh` is/artefakt durumunu sirayla verir.
#   is:       in_progress -> success : cikis 0
#   KONTROL:  failure / skipped / is gorunmuyor (zaman asimi) : cikis 1
#   artefakt: iki yoklamada yok, ucuncude var : cikis 0
#   KONTROL:  artefakt yok ve ureten is bitti : cikis 1
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
h=0; ok=0
mkdir -p "$T/bin"
cat > "$T/bin/gh" <<'GH'
#!/usr/bin/env bash
# jobs sorgusu -> is.txt'nin sonraki satiri; artifacts -> art.txt'nin sonraki satiri
case "$2" in
  *"/jobs"*)      f=is ;;
  *"/artifacts"*) f=art ;;
esac
n=$(cat "$SAHTE/$f.n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$SAHTE/$f.n"
sed -n "${n}p" "$SAHTE/$f.txt" | grep . || tail -1 "$SAHTE/$f.txt"
GH
chmod +x "$T/bin/gh"; export SAHTE="$T"
kos() {   # $1 is satirlari (|), $2 artefakt satirlari (|), geri kalan: ci_bekle argumanlari
  printf '%s\n' "$1" | tr '|' '\n' > "$T/is.txt"; printf '%s\n' "$2" | tr '|' '\n' > "$T/art.txt"
  rm -f "$T/is.n" "$T/art.n"; shift 2
  PATH="$T/bin:$PATH" GITHUB_REPOSITORY=o/r GITHUB_RUN_ID=1 CI_BEKLE=2 CI_BEKLE_ARALIK=0 \
    bash "$KOK/tools/ci_bekle.sh" "$@" > "$T/log" 2>&1; echo $?
}
esit() { if [ "$2" = "$3" ]; then echo "  gecti  $1"; ok=$((ok+1)); else echo "  DUSTU  $1: cikis $2 (beklenen $3)"; tail -2 "$T/log"; h=1; fi; }
esit "is bekleniyor, basarida 0" "$(kos 'in_progress |in_progress |completed success' '' is X)" 0
esit "KONTROL: is dustu -> 1" "$(kos 'in_progress |completed failure' '' is X)" 1
esit "KONTROL: is atlandi -> 1" "$(kos 'completed skipped' '' is X)" 1
esit "KONTROL: is gorunmuyor -> zaman asimi, 1" "$(kos '' '' is X)" 1
esit "artefakt gelince 0" "$(kos 'in_progress ' '||123' artefakt A X)" 0
esit "KONTROL: artefakt yok, ureten is bitti -> 1" "$(kos 'completed failure' '' artefakt A X)" 1
if [ $h -ne 0 ]; then echo "ci bekleme kapisi DUSTU"; exit 1; fi
echo "ci bekleme kapisi: $ok gecti"
