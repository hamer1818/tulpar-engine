# {{OYUN_TR}} ({{OYUN_EN}})

Tulpar Engine oyun fabrikasından (`tools/yeni_oyun.sh`, arena şablonu) üretildi.

```bash
tools/ortam.sh                                        # .motor bağlantısı + sürümler
tulpar oyun.tpr                                       # pencereli
TULPAR_ENGINE_HEADLESS=1500 DISPLAY= tulpar oyun.tpr  # penceresiz otopilot + [kapi] satırı
tools/dogrula.sh                                      # kapılar (pozitif kontrol dahil)
```

- Kimlik `{{KIMLIK}}`, ortam öneki `TULPAR_{{ONEK}}_*` (OTOPILOT, DIL=tr|en, ACILIS=0, KAYIT_YAZ=1 …).
- Android paketi `{{PAKET}}` (`tulpar.toml` `[android]`).
- Genel parçalar motorun ortak kütüphanesinden: `kit_kayit`, `kit_dil`, `kit_ses`, `kit_acilis`,
  `kit_arayuz` (motor deposu `tulpar/README.md`). Ses dosyaları `assets/sesler/` (yoksa sentetik ton),
  müzik `assets/sesler/muzik_menu.mp3` / `muzik_oyun.mp3` (yoksa sessiz).
