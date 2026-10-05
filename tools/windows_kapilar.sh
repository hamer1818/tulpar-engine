#!/usr/bin/env bash
# Tulpar Engine — Windows (MSYS2 MINGW64, SwiftShader) is adimlarinin ORTAK
# kapilari. CI (ci.yml) ve surum (release.yml) Windows isleri AYNI betigi
# kosar: ikisi de once tools/windows_vulkan_icd.sh kur ile ICD'yi kurar, sonra
# buradaki alt komutlari. Kapi bir kez yazilir, iki is akisinda kayamaz.
# (Oncesi, 2026-10-05: ci.yml'de satir ici kopya; release.yml'in Windows isi
# ICD'siz, GPU kapilari atlanarak "gpusuz" kipte kosuyordu, #83.)
#
#   tools/windows_kapilar.sh atlama-kontrol   TULPAR_ENGINE_NO_VULKAN=1 ile
#                                             Vulkan atlama satiri basiliyor mu
#   tools/windows_kapilar.sh katman-kontrol   katman kaydi gizliyken bes katman
#                                             kapisi ATLANDI diyor mu
#   tools/windows_kapilar.sh testler          engine_tests + ozet satiri kapisi;
#                                             Vulkan / katman atlamasi KIRMIZI
#   tools/windows_kapilar.sh kopru <tulpar>   tools/tulpar_dogrula.sh --tam
#                                             --uzun-atla (+ is ozeti)
#
# Ortam: KAPI_BASLIK (is ozeti basligi), KAPI_EK (hata satirlarinin sonuna;
# surumde " Surum YAYINLANMAYACAK."). GITHUB_STEP_SUMMARY yoksa ozet atlanir.
set -uo pipefail

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$kok" || exit 1
ozet_dosyasi="${GITHUB_STEP_SUMMARY:-/dev/null}"
ek="${KAPI_EK:-}"
tests=./yapi/engine_tests.exe
[ -f "$tests" ] || tests=./yapi/engine_tests

case "${1:-}" in
  # Linux isindeki pozitif kontrolun Windows ayagi: asagidaki "Vulkan
  # atlamasi = KIRMIZI" kuralinin bos bir grep olmadigini gosterir (ayni ifade).
  atlama-kontrol)
    TULPAR_ENGINE_NO_VULKAN=1 "$tests" rhi_ 2>&1 | tr -d '\r' > kontrol.log || true
    n=$(grep -E 'ATLANDI:.*Vulkan' kontrol.log | grep -cv 'tukendi')
    if [ "$n" -eq 0 ]; then
      echo "::error::Pozitif kontrol basarisiz: TULPAR_ENGINE_NO_VULKAN=1 ile bile Vulkan atlama satiri basilmadi.$ek"
      tail -40 kontrol.log
      exit 1
    fi
    echo "Pozitif kontrol tamam: $n atlama satiri uretildi."
    ;;

  # Katman GIZLEME: yonetici surecte VK_LAYER_PATH yok sayildigi icin
  # (tools/windows_vulkan_icd.sh) Linux'taki "bos dizine yonlendir" yolu burada
  # bir sey gizlemez. Kayit girdisi kontrol suresince SILINIR ve trap ile geri
  # konur; geri konmazsa `testler` katman kapilarini atlar ve KIRMIZI olur
  # (unutulan bir geri koyma da gorunur).
  katman-kontrol)
    trap 'bash tools/windows_vulkan_icd.sh katman-geri' EXIT
    bash tools/windows_vulkan_icd.sh katman-gizle || exit 1
    "$tests" mali_best_practices 2>&1 | tr -d '\r' > katman_kontrol.log || true
    "$tests" validation_layer 2>&1 | tr -d '\r' >> katman_kontrol.log || true
    n=$(grep -cE 'ATLANDI:.*VK_LAYER_KHRONOS_validation' katman_kontrol.log || true)
    if [ "$n" -lt 5 ]; then
      echo "::error::Pozitif kontrol basarisiz: katman GIZLIYKEN beklenen 5 atlama satirindan yalnizca $n basildi.$ek"
      tail -60 katman_kontrol.log
      exit 1
    fi
    echo "Pozitif kontrol tamam: katman gizliyken $n atlama satiri uretildi (bes kapi)."
    bash tools/windows_vulkan_icd.sh katman-geri && trap - EXIT
    python3 tools/vk_sonda.py --katman VK_LAYER_KHRONOS_validation | tr -d '\r' | tail -1
    ;;

  # SwiftShader KURULU: GPU kapilari Windows'ta da KOSAR. Linux isiyle ayni
  # kural: "Vulkan ... yok" ya da "VK_LAYER_KHRONOS_validation yok" gerekceli
  # tek bir atlama KIRMIZI. Sure is ozetine yazilir. Cokme ozet teshisinden
  # ONCE betigi oldurmesin diye `-e` YOK (olculdu 2026-10-05: msys2 kabugu
  # `bash -e`, cikis 127, log bos kaliyordu).
  testler)
    t0=$SECONDS
    "$tests" 2>&1 | tr -d '\r' | tee sonuc.log
    kod=${PIPESTATUS[0]}
    sure=$((SECONDS - t0))

    ozet="$(grep -E '^engine tests: [0-9]+ passed' sonuc.log | tail -1 || true)"
    if [ -z "$ozet" ]; then
      echo "::error::Ozet satiri YOK — engine_tests cokmus ya da asilmis olabilir (cikis $kod).$ek"
      exit 1
    fi
    echo "$ozet"
    gecen=$(sed -E 's/^engine tests: ([0-9]+) passed.*/\1/'   <<< "$ozet")
    kalan=$(sed -E 's/.*, ([0-9]+) failed.*/\1/'              <<< "$ozet")
    atlanan=$(sed -E 's/.*, ([0-9]+) atlandi.*/\1/'           <<< "$ozet")
    kosan=$(sed -E 's/.*\(([0-9]+)\/[0-9]+ kosuldu\).*/\1/'   <<< "$ozet")
    kayitli=$(sed -E 's/.*\([0-9]+\/([0-9]+) kosuldu\).*/\1/' <<< "$ozet")
    {
      echo "### ${KAPI_BASLIK:-Windows (MSYS2 MINGW64, SwiftShader)}"
      echo ""
      echo "\`$ozet\` — ${sure} s"
      echo ""
      if [ "$atlanan" -gt 0 ]; then
        echo "<details><summary>Atlanan $atlanan kapi</summary>"
        echo ""
        echo '```'
        grep -E 'ATLANDI:' sonuc.log | sort | uniq -c | sort -rn
        echo '```'
        echo ""
        echo "</details>"
      fi
    } >> "$ozet_dosyasi"

    if grep -q 'KAYIT TASMASI' sonuc.log; then
      echo "::error::Kayit tasmasi: bazi testler HIC kosmadi (tests/test.hpp icindeki Registry::kMax).$ek"
      exit 1
    fi
    if [ "$kosan" != "$kayitli" ]; then
      echo "::error::Kayitli $kayitli testin yalnizca $kosan tanesi kosmus.$ek"
      exit 1
    fi
    if [ "$kalan" != "0" ] || [ "$gecen" -le 0 ]; then
      echo "::error::$kalan test kirmizi.$ek"
      exit 1
    fi
    vulkan_atlama="$(grep -E 'ATLANDI:.*Vulkan' sonuc.log | grep -v 'tukendi' || true)"
    if [ -n "$vulkan_atlama" ]; then
      echo "$vulkan_atlama"
      echo "::error::SwiftShader kurulu (HKLM Khronos Vulkan Drivers) oldugu halde Vulkan kapilari atlandi — ICD gelmemis.$ek"
      exit 1
    fi
    katman_atlama="$(grep -E 'ATLANDI:.*VK_LAYER_KHRONOS_validation' sonuc.log || true)"
    if [ -n "$katman_atlama" ]; then
      echo "$katman_atlama"
      echo "::error::Dogrulama katmani kurulu ve vk_sonda listeliyor oldugu halde katman kapilari atlandi.$ek"
      exit 1
    fi
    echo "engine_tests (Windows, SwiftShader): ${sure} s"
    exit "$kod"
    ;;

  # TULPAR KOPRUSU — YAYINLANMIS tulpar.exe bu agacin yapi/tulpar-ext
  # paketiyle motor oyunlarini derleyip linkler ve kosar (link.windows).
  # --uzun-atla: dalga (2400) ve aksiyon (3200) kare kapilari Windows'ta
  # KOSMAZ, ATLANDI diye sayilir. Olculdu (CI windows-latest 2026-10-05,
  # SwiftShader ~210 ms/kare): ikisi adimin 26 dakikasinin 24'u; ayni satirlar
  # o gun burada Linux'la bayt bayt ayni cikti ve her PR'da Linux/macOS'ta
  # olculuyor. --gpusuz-izinli YOK: kurulum duserse adim KIRMIZI.
  kopru)
    tul="${2:?kullanim: tools/windows_kapilar.sh kopru <tulpar.exe>}"
    bash tools/tulpar_dogrula.sh --tulpar "$tul" --tam --uzun-atla 2>&1 | tee tulpar.log
    kod=${PIPESTATUS[0]}
    ozet="$(grep -E '^tulpar dogrulama: ' tulpar.log | tail -1 || true)"
    {
      echo "### ${KAPI_BASLIK:-Windows}: Tulpar koprusu (link.windows)"
      echo ""
      echo "\`${ozet:-ozet satiri YOK}\`"
      echo ""
      echo '```'
      grep -E 'ATLANDI:|kurulum dustu|\[bilgi\]|\[kapi\]|link adiyla|HATA Vulkan|GPU:' tulpar.log || true
      echo '```'
    } >> "$ozet_dosyasi"
    if [ -z "$ozet" ]; then
      echo "::error::tulpar_dogrula.sh ozet satiri basmadi — betik yarida kesilmis olabilir.$ek"
      exit 1
    fi
    exit "$kod"
    ;;

  *) echo "kullanim: tools/windows_kapilar.sh atlama-kontrol|katman-kontrol|testler|kopru <tulpar>" >&2; exit 2 ;;
esac
