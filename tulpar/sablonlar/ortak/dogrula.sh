#!/usr/bin/env bash
# {{OYUN_TR}} — oyunun KAPILARI (pencere ACMAZ). Son satir her zaman ozettir:
#   {{KIMLIK}} dogrulama: N gecti, M dustu
#
# 1) penceresiz otopilot: menu -> Oyna -> tur -> sonuc; "[kapi] TAMAM" (en az 3 parilti, hata 0)
# 2) POZITIF KONTROL: TULPAR_{{ONEK}}_KAPI_BOZ=1 otopilotu durdurur -> kapi BASARISIZ demeli
#    (kapi gercekten oyunu olcuyor; otopilotsuz tur hicbir sey toplamaz)
# 3) ses yolu: TULPAR_ENGINE_AUDIO_NULL=1 ile ses cihazi acik kosum da TAMAM
set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/ortam.sh"
cd "$OYUN_KOK"
gecti=0; dustu=0
gec() { echo "✓ $*"; gecti=$((gecti + 1)); }
dus() { echo "✗ $*" >&2; dustu=$((dustu + 1)); }
gun="$(mktemp -d)"; trap 'rm -rf "$gun"' EXIT
kos() { local g="$1"; shift; env TULPAR_ENGINE_HEADLESS=1500 DISPLAY= "$@" "$TULPAR" --ext "$EXT" oyun.tpr >"$g" 2>&1; }

kos "$gun/a.log" & kos "$gun/b.log" TULPAR_{{ONEK}}_KAPI_BOZ=1 & kos "$gun/c.log" TULPAR_ENGINE_AUDIO_NULL=1 & wait
s="$(grep -m1 '^\[kapi\] {{KIMLIK}}:' "$gun/a.log")"
if grep -q '^\[kapi\] TAMAM' "$gun/a.log"; then gec "otopilot turu: ${s#\[kapi\] }"
else dus "otopilot turu: '${s:-kapi satiri yok}'"; tail -8 "$gun/a.log" >&2; fi
if grep -q '^\[kapi\] BASARISIZ' "$gun/b.log"; then gec "pozitif kontrol: KAPI_BOZ=1 -> BASARISIZ (kapi olcuyor)"
else dus "pozitif kontrol: KAPI_BOZ=1 BASARISIZ demedi"; grep '^\[kapi\]' "$gun/b.log" >&2; tail -4 "$gun/b.log" >&2; fi
s="$(grep -m1 '^\[kapi\] {{KIMLIK}}:' "$gun/c.log")"
if grep -q '^\[kapi\] TAMAM' "$gun/c.log" && echo "$s" | grep -q 'ses=true'; then gec "ses acik (NULL aygit): ${s#\[kapi\] }"
else dus "ses acik kosum: '${s:-kapi satiri yok}'"; tail -6 "$gun/c.log" >&2; fi
echo "{{KIMLIK}} dogrulama: $gecti gecti, $dustu dustu"
[ "$dustu" = 0 ]
