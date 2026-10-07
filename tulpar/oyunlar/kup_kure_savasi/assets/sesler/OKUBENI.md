# Küpler ile Kürelerin Savaşı — sesler

Bu dizine konan dosyalar oyun açılışında yüklenir (`ses.tpr` `sesleri_yukle`). Dosya **yoksa** oyun
hatasız çalışır: efektler motorun sentetik tonuna düşer, müzik çalmaz.

| dosya | ne zaman | şu an |
|---|---|---|
| `muzik_menu.mp3` | ana menü (döngü) | **yer tutucu** (sentez, 16 sn) |
| `muzik_savas.mp3` | savaş sırasında (döngü) | **yer tutucu** (sentez, 16 sn) |
| `muzik_zafer.mp3` | zafer ekranı (döngü) | **yer tutucu** (sentez, 8 sn) |
| `muzik_yenilgi.mp3` | yenilgi ekranı (döngü) | **yer tutucu** (sentez, 8 sn) |
| `ses_birlik.wav` | birlik eğitimden çıktı | yok → sentetik ton |
| `ses_vurus.wav` | yakın dövüş vuruşu | yok → sentetik ton |
| `ses_ok.wav` | ok / taş / kurşun / kule atışı | yok → sentetik ton |
| `ses_patlama.wav` | birlik öldü, gülle/bomba patladı | yok → sentetik ton |
| `ses_us_hasar.wav` | kale vuruluyor | yok → sentetik ton |
| `ses_cag.wav` | çağ atlandı (ve zafer) | yok → sentetik ton |
| `ses_ozel_guc.wav` | özel güç kullanıldı | yok → sentetik ton |
| `ses_tikla.wav` | arayüz tıklaması, kule kuruldu | yok → sentetik ton |
| `ses_altin.wav` | kule satıldı (altın geri) | yok → sentetik ton |

Yer tutucu müzikler bir ses kütüphanesinden alınmadı: ffmpeg'in `aevalsrc` üretecinde sinüs arpejleriyle
sentezlendi (mono, 22050 Hz, 48 kbps MP3; lisansı bu depoyla aynı). Aynı adla gerçek müzik konunca onun
yerine geçer.

Biçim: motor (miniaudio) **WAV / MP3 / FLAC** çözer, **Ogg çözmez**. Klip açılışta arenaya TAMAMEN
çözülür (32 bit float): müzik döngüleri **60 saniyeyi geçmesin** (3 dk stereo 48 kHz ≈ 69 MB). Efektler
kısa (≤ 1 s), mono yeter. Android'de varlıklar uzantıya göre çıkarılır; `.mp3`/`.flac` 2026-10-07'den beri
çıkarılıyor (Tuzaklar 8cu).
