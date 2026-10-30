#!/usr/bin/env bash
# SURUMDE YENIDEN KULLANIM KAPISI — tools/surum_yeniden_kullan.sh gercekten
# olcuyor mu? Sahte `gh` (PATH'in onunde) GitHub'in yerine gecer: PR, koşum ve
# artefaktlar gecici dizinden gelir; gercek git deposu (gecici).
#   1. hepsi tutuyor                                   -> reused=true, paket/$AD dolu
#   2. KONTROL: agac farkli                            -> reused=false
#   3. KONTROL: kaydedilen surum farkli (tahmin tutmadi) -> reused=false
#   4. KONTROL: kayit dogru, paketin SURUM.txt'si farkli -> reused=false
#   5. KONTROL: kayit dogru, ikilideki isaret farkli    -> reused=false, paket silindi
#   6. KONTROL: motor artefakti yok                     -> reused=false (kirmizi degil)
#   7. KONTROL: etiket degil (main push)                -> reused=false
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
h=0; ok=0
R="$T/depo"; mkdir -p "$R/tools"
cp "$KOK/tools/surum_yeniden_kullan.sh" "$KOK/tools/yeniden_kullanim_karar.sh" "$R/tools/"
(
  cd "$R" && git init -q && git config user.email t@t && git config user.name t
  echo k > kod.c && git add . && git commit -qm "ilk" && git tag v1.0.0
  echo k2 > kod.c && git commit -qam "ozellik: deneme (#7)" && git tag v1.1.0
) >/dev/null
ETIKET_SHA=$(git -C "$R" rev-parse HEAD); AGAC=$(git -C "$R" rev-parse 'HEAD^{tree}')
mkdir -p "$T/bin"
cat > "$T/bin/gh" <<'GH'
#!/usr/bin/env bash
case "$1 $2" in
  "pr view")  cat "$SAHTE/pr.txt" ;;
  "run list") cat "$SAHTE/kosum.txt" ;;
  "run view") echo success ;;
  "run download")
    ad=""; hedef=""; shift 3
    while [ $# -gt 0 ]; do case "$1" in -n) ad="$2"; shift 2 ;; -D) hedef="$2"; shift 2 ;; *) shift ;; esac; done
    [ -d "$SAHTE/art/$ad" ] || { echo "artefakt yok: $ad" >&2; exit 1; }
    mkdir -p "$hedef" && cp -r "$SAHTE/art/$ad/." "$hedef/" ;;
  *) echo "sahte gh: $*" >&2; exit 1 ;;
esac
GH
chmod +x "$T/bin/gh"; export SAHTE="$T"
P=linux-x86_64
kur() {   # $1 kayit agaci, $2 kayit surumu, $3 SURUM.txt surumu, $4 ikili isareti
  rm -rf "$T/art"; mkdir -p "$T/art/derleme-bilgisi-$P" "$T/art/motor-$P"
  printf 'agac=%s\nsurum=%s\n' "$1" "$2" > "$T/art/derleme-bilgisi-$P/derleme-bilgisi.txt"
  printf '%s %s\n' "$3" "$P" > "$T/art/motor-$P/SURUM.txt"
  printf 'ELF...tulpar-engine-surum:%s\0...%s\0' "$4" "$P" > "$T/art/motor-$P/engine_editor"
  echo dosya > "$T/art/motor-$P/DOSYALAR.txt"
  echo "basucu MERGED $ETIKET_SHA" > "$T/pr.txt"; echo 4242 > "$T/kosum.txt"
}
kos() {   # $1 GITHUB_REF -> reused degeri
  rm -f "$T/out"; rm -rf "$R/paket"
  ( cd "$R" && PATH="$T/bin:$PATH" GITHUB_REF="$1" SURUM=v1.1.0 PLATFORM=$P AD="tulpar-engine-v1.1.0-$P" \
      GITHUB_OUTPUT="$T/out" GITHUB_STEP_SUMMARY=/dev/null bash tools/surum_yeniden_kullan.sh > "$T/log" 2>&1 )
  sed -n 's/^reused=//p' "$T/out" 2>/dev/null
}
esit() { if [ "$2" = "$3" ] && { [ -z "${4:-}" ] || grep -q "$4" "$T/log"; }; then echo "  gecti  $1"; ok=$((ok+1)); else echo "  DUSTU  $1: reused='$2' (beklenen '$3')"; tail -4 "$T/log"; h=1; fi; }
AD_YOL="$R/paket/tulpar-engine-v1.1.0-$P"

kur "$AGAC" v1.1.0 v1.1.0 v1.1.0
r=$(kos refs/tags/v1.1.0); [ -f "$AD_YOL/SURUM.txt" ] || r="$r(paket-yok)"
esit "hepsi tutuyor -> yeniden kullanildi, paket yerinde" "$r" true
kur 0000000000000000000000000000000000000000 v1.1.0 v1.1.0 v1.1.0
esit "KONTROL: agac farkli -> sifirdan" "$(kos refs/tags/v1.1.0)" false "agac farkli"
kur "$AGAC" v1.0.9 v1.1.0 v1.1.0
esit "KONTROL: kaydedilen surum farkli -> sifirdan" "$(kos refs/tags/v1.1.0)" false "gomulu surum"
kur "$AGAC" v1.1.0 v1.0.9 v1.1.0
esit "KONTROL: paketin SURUM.txt'si farkli -> sifirdan" "$(kos refs/tags/v1.1.0)" false "indirilen SURUM.txt"
kur "$AGAC" v1.1.0 v1.1.0 v1.0.9
r=$(kos refs/tags/v1.1.0); [ -e "$AD_YOL" ] && r="$r(paket-kaldi)"
esit "KONTROL: ikilideki isaret farkli -> sifirdan, paket silindi" "$r" false "gomulu 'tulpar-engine-surum:v1.0.9'"
kur "$AGAC" v1.1.0 v1.1.0 v1.1.0; rm -rf "$T/art/motor-$P"
esit "KONTROL: motor artefakti yok -> sifirdan (kirmizi degil)" "$(kos refs/tags/v1.1.0)" false "indirilemedi"
kur "$AGAC" v1.1.0 v1.1.0 v1.1.0
esit "KONTROL: etiket degil -> sifirdan" "$(kos refs/heads/main)" false "etiket koşumu degil"
if [ $h -ne 0 ]; then echo "surum yeniden kullanim kapisi DUSTU"; exit 1; fi
echo "surum yeniden kullanim kapisi: $ok gecti"
