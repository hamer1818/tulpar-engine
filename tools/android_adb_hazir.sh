# Tulpar Engine — adb cihazi HAZIR mi? (tools/android_*.sh kaynak olarak okur: `. android_adb_hazir.sh`)
#
# NEDEN (OYUN_GERI_BILDIRIM #23, 2026-10-08): telefon bagi koparken `adb install` / `adb shell`
# "waiting for device"ta SONSUZA bekliyor; betik telefon kilidini (flock) tutarken bekleyince diger
# isler de kilitlendi. Her oturumdan once `adb get-state` 5 sn zaman asimiyla sorulur: cihaz yoksa,
# yetkisizse ya da yanit gelmezse betik beklemeden, sebebiyle cikar.
#
# Olculdu 2026-10-26 (gelistirme makinesi, cihaz bagli degil): ANDROID_SERIAL=yok tools/android_run.sh
# -> aninda "HATA: adb cihazi hazir degil ... not found", cikis 1. POZITIF KONTROL (zaman asimi yolu):
# PATH'e `get-state`te 30 sn uyuyan sahte bir adb konunca betik 5. saniyede "5 sn'de yanit yok" ile
# cikis 1 verdi (beklemeyi gercekten kesiyor).
adb_hazir() {
  local s
  if ! command -v adb >/dev/null 2>&1; then echo "HATA: adb yok (Android SDK platform-tools PATH'te degil)"; return 1; fi
  s="$(timeout 5 adb ${ANDROID_SERIAL:+-s "$ANDROID_SERIAL"} get-state 2>&1)"; local rc=$?
  if [ $rc -ne 0 ] || [ "$s" != device ]; then
    if [ $rc -eq 124 ]; then s="5 sn'de yanit yok (waiting for device?)"; fi
    echo "HATA: adb cihazi hazir degil${ANDROID_SERIAL:+ (ANDROID_SERIAL=$ANDROID_SERIAL)}: $s"
    adb devices 2>/dev/null | sed 's/^/  /'
    return 1
  fi
}
