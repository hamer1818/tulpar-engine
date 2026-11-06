# {{OYUN_TR}} — ortak ortam (bash betikleri `source` eder; tek basina da kosar).
# Motor ve derleyici DISARIDAN gelir:
#   TULPAR_ENGINE_DIR  motor: ya kaynak agaci (yapi/tulpar-ext, yapi/engine_sahnec) ya da kurulu editorun
#                      dizini / derleme dizini (tulpar-ext, engine_sahnec dogrudan icinde). Editorun
#                      Oyun menusu bunu kendi dizini olarak verir. Varsayilan: .motor'un gosterdigi yer.
#   TULPAR             derleyici (PATH'teki tulpar, yoksa ~/.local/bin/tulpar)
#   EXT                eklenti paketi (yukaridaki iki duzenden hangisi varsa)
# Yan etki: depo kokunde .motor -> motorun IKILI dizini (tulpar-ext + engine_sahnec'in durdugu yer:
# kaynak agacinda <motor>/yapi, kurulu editorde kurulum dizini) baglantisi (gitignore'lu);
# tulpar.toml'un [ext] yolu (.motor/tulpar-ext) iki duzende de ayni.
if [ -z "${BASH_VERSION:-}" ]; then echo "HATA: tools/ortam.sh bash ister" >&2; return 1 2>/dev/null || exit 1; fi
OYUN_KOK="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_onceki="$(readlink "$OYUN_KOK/.motor" 2>/dev/null || true)"
TULPAR_ENGINE_DIR="${TULPAR_ENGINE_DIR:-$_onceki}"
if [ -z "$TULPAR_ENGINE_DIR" ]; then
  echo "HATA: motor bilinmiyor — TULPAR_ENGINE_DIR verin (motor deposu ya da editorun kurulu dizini) ya da" >&2
  echo "      projeyi editorden acip Oyun menusunu kullanin" >&2
  return 1 2>/dev/null || exit 1
fi
TULPAR="${TULPAR:-$(command -v tulpar || echo "$HOME/.local/bin/tulpar")}"
# Duz duzen (kurulu editor / yapi dizini) once, kaynak agaci duzeni (yapi/) sonra.
if [ -d "$TULPAR_ENGINE_DIR/tulpar-ext" ]; then _ikili="$TULPAR_ENGINE_DIR"; else _ikili="$TULPAR_ENGINE_DIR/yapi"; fi
EXT="${EXT:-$_ikili/tulpar-ext}"
SAHNEC="${SAHNEC:-$_ikili/engine_sahnec}"
export TULPAR_ENGINE_DIR
if [ -d "$_ikili" ] && [ "$_onceki" != "$_ikili" ]; then ln -sfn "$_ikili" "$OYUN_KOK/.motor"; fi
# Sahne blob'lari (.sahneb) turetilmis ve gitignore'lu: eksik ya da kaynagindan (.sahne) eskiyse
# motorun engine_sahnec'i ile derlenir (editorun "Derle"siyle ayni cikti, belirlenimli).
for _s in "$OYUN_KOK"/assets/*.sahne; do
  [ -e "$_s" ] || continue
  _b="${_s%.sahne}.sahneb"
  # sahnec blob'dan yeniyse de: motor blob surumunu yukseltince (v9 -> v10) eski .sahneb acilmaz,
  # .sahne degismedigi icin mtime bunu yakalamazdi.
  if [ ! -f "$_b" ] || [ "$_s" -nt "$_b" ] || [ "$SAHNEC" -nt "$_b" ]; then
    if [ -x "$SAHNEC" ] || [ -x "$SAHNEC.exe" ]; then
      "$SAHNEC" "$_s" "$_b" >/dev/null || echo "UYARI: $(basename "$_s") derlenemedi" >&2
    else
      echo "UYARI: $_b yok/bayat ve engine_sahnec yok ($SAHNEC; motorda cmake --build yapi)" >&2
    fi
  fi
done
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  echo "motor:   $TULPAR_ENGINE_DIR ($(git -C "$TULPAR_ENGINE_DIR" describe --tags --always 2>/dev/null))"
  echo "tulpar:  $TULPAR ($("$TULPAR" --version 2>&1 | head -1))"
  echo "eklenti: $EXT"
fi
