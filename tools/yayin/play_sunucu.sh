#!/usr/bin/env bash
# Tulpar oyunu — Play yayını iurl sunucusu üzerinden (MyTinyURL düzeni; kullanıcı kuralı). Aşama 3 (docs/FABRIKA.md).
# Hizmet hesabı anahtarı SUNUCUDA kalır (/root/.config/play-publisher/key.json), buraya/depoya kopyalanmaz.
# Bu betik AAB'yi, sembolleri, sürüm notlarını ve mağaza dosyalarını sunucuya kopyalar, play_yayinla.py'yi orada koşar.
#
# Oyun deposunun kökünde (ya da OYUN_KOK) `yayin.env` olmalı:
#   PAKET=dev.tulparlang.oyun          # Android paket adı (zorunlu)
#   DOSYA=oyun                         # cikti/apk/aab/<DOSYA>.aab ve <DOSYA>_semboller.zip
#   UZAK=oyun                          # sunucuda /root/<UZAK> çalışma dizini (varsayılan DOSYA)
#   MAGAZA=docs/yayin/magaza           # liste_tr.md, liste_en.md, ikon_play_512.png, tanitim_*.png (liste için)
#   EKRAN=cikti/magaza                 # ekran_tr/ ekran_en/ (liste için)
#
#   tools/yayin/play_sunucu.sh yukle [notlar.md]   # AAB -> DAHİLİ kanal
#   tools/yayin/play_sunucu.sh liste               # mağaza metni + görseller
#   tools/yayin/play_sunucu.sh durum | kapali | terfi [--oran 1] | tamamla [--gun 2]
# `--kuru`: sunucuya HİÇBİR ŞEY gönderme, yalnız ne yapılacağını yaz (yeni oyun kontrolü).
# Yeni uygulama için kullanıcının bir kerelik adımları: tools/yayin/KONTROL_LISTESI.md.
set -euo pipefail
YAYIN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KURU=0
[ "${1:-}" = "--kuru" ] && { KURU=1; shift; }
KOK="${OYUN_KOK:-$PWD}"
[ -f "$KOK/yayin.env" ] || { echo "HATA: $KOK/yayin.env yok (başlık yorumuna bak)" >&2; exit 2; }
# shellcheck disable=SC1091
. "$KOK/yayin.env"
: "${PAKET:?yayin.env: PAKET yok}" "${DOSYA:?yayin.env: DOSYA yok}"
UZAK_AD="${UZAK:-$DOSYA}"
SUNUCU="${TULPAR_PLAY_SUNUCU:-root@45.155.124.196}"
UZAK_DIZIN="/root/$UZAK_AD"
PY=/opt/play-publisher/bin/python
k="${1:-durum}"; shift || true
calis() { if [ "$KURU" = 1 ]; then echo "[kuru] $*"; else "$@"; fi; }
ssh_c() { calis ssh "$SUNUCU" "$1"; }
ssh_c "mkdir -p $UZAK_DIZIN/magaza $UZAK_DIZIN/ekran"
calis scp -q "$YAYIN/play_yayinla.py" "$SUNUCU:$UZAK_DIZIN/"
ENV="TULPAR_PLAY_PAKET=$PAKET"
case "$k" in
  yukle)
    N="${1:-$KOK/docs/yayin/surum_notlari/1.0.md}"
    A="$KOK/cikti/apk/aab/$DOSYA.aab"; S="$KOK/cikti/apk/aab/${DOSYA}_semboller.zip"
    [ "$KURU" = 1 ] || { [ -f "$A" ] || { echo "HATA: $A yok (once AAB=1 ./apk_derle.sh)" >&2; exit 2; }; [ -f "$N" ] || { echo "HATA: surum notlari yok: $N" >&2; exit 2; }; }
    calis scp -q "$A" "$S" "$N" "$SUNUCU:$UZAK_DIZIN/"
    ssh_c "$ENV $PY $UZAK_DIZIN/play_yayinla.py yukle $UZAK_DIZIN/$DOSYA.aab $UZAK_DIZIN/$(basename "$N") --semboller $UZAK_DIZIN/${DOSYA}_semboller.zip" ;;
  liste)
    M="$KOK/${MAGAZA:-docs/yayin/magaza}"; E="$KOK/${EKRAN:-cikti/magaza}"
    calis scp -q "$M"/liste_tr.md "$M"/liste_en.md "$M"/ikon_play_512.png "$M"/tanitim_1024x500_*.png "$SUNUCU:$UZAK_DIZIN/magaza/"
    calis scp -q -r "$E/ekran_tr" "$E/ekran_en" "$SUNUCU:$UZAK_DIZIN/ekran/"
    ssh_c "$ENV $PY $UZAK_DIZIN/play_yayinla.py liste $UZAK_DIZIN/magaza $UZAK_DIZIN/ekran" ;;
  durum|terfi|tamamla|detay|kapali)
    ssh_c "$ENV $PY $UZAK_DIZIN/play_yayinla.py $k $*" ;;
  *) echo "kullanım: $0 [--kuru] yukle [notlar] | liste | durum | kapali | terfi [--oran 1] | tamamla [--gun 2] | detay --site <url> --eposta <adres>" >&2; exit 2 ;;
esac
