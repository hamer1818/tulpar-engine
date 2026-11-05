# {{OYUN_TR}} — ortak ortam (bash betikleri `source` eder; tek basina da kosar).
# Motor ve derleyici DISARIDAN gelir:
#   TULPAR_ENGINE_DIR  motor calisma agaci (derlenmis yapi/tulpar-ext). Varsayilan: .motor'un
#                      gosterdigi yer, yoksa /mnt/veri/yazilim/tulpar-engine
#   TULPAR             derleyici (PATH'teki tulpar)
#   EXT                eklenti paketi ($TULPAR_ENGINE_DIR/yapi/tulpar-ext)
# Yan etki: depo kokunde .motor -> $TULPAR_ENGINE_DIR baglantisi (gitignore'lu).
if [ -z "${BASH_VERSION:-}" ]; then echo "HATA: tools/ortam.sh bash ister" >&2; return 1 2>/dev/null || exit 1; fi
OYUN_KOK="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_onceki="$(readlink "$OYUN_KOK/.motor" 2>/dev/null || true)"
TULPAR_ENGINE_DIR="${TULPAR_ENGINE_DIR:-${_onceki:-/mnt/veri/yazilim/tulpar-engine}}"
TULPAR="${TULPAR:-$(command -v tulpar || echo "$HOME/.local/bin/tulpar")}"
EXT="${EXT:-$TULPAR_ENGINE_DIR/yapi/tulpar-ext}"
export TULPAR_ENGINE_DIR
if [ -d "$TULPAR_ENGINE_DIR" ] && [ "$_onceki" != "$TULPAR_ENGINE_DIR" ]; then ln -sfn "$TULPAR_ENGINE_DIR" "$OYUN_KOK/.motor"; fi
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  echo "motor:   $TULPAR_ENGINE_DIR ($(git -C "$TULPAR_ENGINE_DIR" describe --tags --always 2>/dev/null))"
  echo "tulpar:  $TULPAR ($("$TULPAR" --version 2>&1 | head -1))"
  echo "eklenti: $EXT"
fi
