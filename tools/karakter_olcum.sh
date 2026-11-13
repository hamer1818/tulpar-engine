#!/usr/bin/env bash
# Karakter bütçesi ölçümü (docs/MODEL-ANIMASYON-PLANI.md §6 madde 4): N iskeletli karakter sahnede, kare süresi.
#
#   tools/karakter_olcum.sh [eklem=60] [ucgen=15000] [N...=1 50 100 200]
#
# make_test_karakter.py ile eklem/üçgen sayısı verilen animasyonlu karakter üretir, her N için .sahne -> .sahneb
# (engine_sahnec) kurar ve küçük bir Tulpar oyunuyla (sahne_yukle + kare döngüsü) TULPAR_ENGINE_HEADLESS=500 kare koşturur;
# köprünün kapanış satırındaki p50/p99 basılır. N=1 taban çizgisidir (sahnenin kendi maliyeti), FARK karakter maliyetidir.
# SAYI CİHAZA ÖZGÜDÜR: bu makinede ölçülür ve BASILIR, başka cihaz için iddia edilmez (mobil: aynı betik cihazda).
# Pozitif kontrol: N=200 çizim sayısı taban+199 olmalı (eskiden eklem matrisi tavanı 4096 iken 68'de takılıyordu).
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
EKLEM="${1:-60}"; UCGEN="${2:-15000}"; shift 2 2>/dev/null || true
NLER=("${@:-1 50 100 200}"); [ $# -eq 0 ] && NLER=(1 50 100 200)
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
TULPAR="${TULPAR:-$(command -v tulpar || echo "$HOME/.local/bin/tulpar")}"
python3 "$KOK/tools/make_test_karakter.py" "$EKLEM" "$UCGEN" "$T/karakter.gltf" || exit 2
cat > "$T/olcum.tpr" <<'TPR'
import "engine";
func main() {
    if (!motor_ac("olcum", 1280, 720)) { print("motor acilamadi"); return; }
    if (!sahne_yukle("SAHNE.sahneb")) { print("sahne yuklenemedi"); return; }
    kamera(0.0, 7.0, 14.0, 0.0, 1.0, -3.0);
    while (calisiyor()) { kare_basla(); kare_bitir(); }
    motor_kapat();
}
main();
TPR
echo "karakter: $EKLEM eklem, ~$UCGEN ucgen; GPU: $(DISPLAY= "$KOK/yapi/engine_demo" --headless 1 2>&1 | sed -n 's/.*GPU: \(.*\)/\1/p' | head -1)"
for n in "${NLER[@]}"; do
  python3 - "$n" "$T/s$n.sahne" <<'PY'
import sys, math
n = int(sys.argv[1]); s = ["tulpar-sahne 1", "isik-yon 0.5 1 0.35", "isik-ortam 0.3 0.3 0.35", "isik-gunes 0.85", "golge 0 1 -1 30 90",
                           "kamera 0 1.5 0 0.0 0.75 30", 'kaynak "karakter.gltf"']
cols = max(1, int(math.ceil(math.sqrt(n * 1.6))))
for i in range(n):
    s += ['nesne "k%d"' % i, "  konum %.2f 0 %.2f" % ((i % cols - cols / 2) * 0.9, -(i // cols) * 0.9), "  donus 0 0 0", "  olcek 0.3 0.3 0.3",
          "  model kaynak 0 1 1 1", "  animasyon 0 %.3f 1" % ((i * 0.037) % 2.0), "son"]
open(sys.argv[2], "w").write("\n".join(s) + "\n")
PY
  "$KOK/yapi/engine_sahnec" "$T/s$n.sahne" "$T/s$n.sahneb" >/dev/null 2>&1 || { echo "HATA: sahnec N=$n"; exit 2; }
  sed "s/SAHNE/s$n/" "$T/olcum.tpr" > "$T/o$n.tpr"
  satir="$(cd "$T" && TULPAR_ENGINE_HEADLESS=500 DISPLAY= TULPAR_ENGINE_AUDIO_NULL=1 timeout 180 "$TULPAR" --ext "$KOK/yapi/tulpar-ext" "o$n.tpr" 2>&1 | grep -E 'kapanis: [0-9]+ kare')"
  cizim="$(DISPLAY= timeout 120 "$KOK/yapi/engine_demo" --headless 60 --scene "$T/s$n.sahneb" 2>&1 | sed -n 's/.*kare 60 .*| cizim \([0-9]*\) .*/\1/p' | tail -1)"
  printf 'N=%-4s cizim %-4s %s\n' "$n" "${cizim:-?}" "$(echo "$satir" | sed -E 's/.*(p50 [0-9.]+ ms p99 [0-9.]+ ms).*/\1/')"
done
