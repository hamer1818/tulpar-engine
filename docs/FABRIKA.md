# Oyun fabrikası — motor 1.0 ve "yeni oyun" hattı

> Karar (kullanıcı, 2026-11-05): "mevcut projeyi bitirelim, bu motorla oyun üretme fabrikası yapacağız."
> Seçimler: **motor = oyun için gerekeni bitir, kalanı gerekçesiyle ERTELE, 1.0'da dondur**;
> **fabrika = şablon + ortak oyun kütüphanesi (kit) + tek komut yayın.** Tasarım metninden YZ ile
> üretim bu planın dışında (kit ve şablonlar oturunca üstüne kurulur).

Fabrikanın ölçütü tek cümle: **yeni bir oyun deposu tek komutla kurulur, ilk gün telefonda açılır,
tek komutla Play dahili kanala gider — ve Küp-Küre'nin yazdığı genel kodu YENİDEN yazmaz.**

## Aşama 0 — Motor 1.0 kapsamı (bugünkü açıklar ve kararı)

Ölçüt: bir mobil oyunun (Küp-Küre ve bir sonraki şablon oyunları: arena / koşu / bulmaca, 30×30 m'ye
kadar sahne, TR/EN, Android) buna **bugün** ihtiyacı var mı? Yoksa ERTELE — ilk ihtiyaç duyan oyun
gelince açılır. Kaynak listesi: DURUM.md §6 ve §6.1.

| madde | karar | gerekçe |
|---|---|---|
| Ödüllü reklam + uygulama içi satın alma, **gerçek sağlayıcı** (AdMob / Play Billing) | **AL (fabrika aşama 4)** | Köprü + durum makinesi var (KOPRU §9), yalnız `"test"` sağlayıcısı. Gelir getiren her oyun ister. Java/AAR tarafı (Gradle host) + hesap kullanıcıda |
| GameActivity göçü (IME, insets) | ERTELE | Metin girişi isteyen oyun yok; insets/çentik zaten var (#17) |
| Memory Advice / Performance Tuner | ERTELE | Play Vitals verisi gelmeden ölçecek şey yok |
| Çok dilli metin shaping (HarfBuzz) | ERTELE | TR/EN Latin; Arapça/Hint dili isteyen oyun yok |
| VSM | ERTELE | CIHAZ-MATRISI: oda/arena ölçeğinde CSM yeter |
| Virtual texture | ERTELE | Aynı ölçek; doku bütçesi ASTC + LOD ile tutuyor |
| Vis buffer A/B, FDM, 3 cihaz kapıları (bant genişliği, 10 dk sustained) | ERTELE | Cihaz yok (kullanıcı kararı: pas); Küp-Küre P20 Pro'da p99 22.8 ms ölçüldü |
| Faz 8.2 GPU tip sistemi (`uint`), 8.5 permutation/reflection, fp16 | ERTELE | Shader'lar GLSL'den çevrimdışı; 23/23 Tulpar karşılığı bayt kapısında (2026-11-05) |
| Telefon demosunda sahne blob'u | ERTELE | Oyun blob'u telefonda kullanıyor (Küp-Küre); demo ayrı ikili |
| iOS (Faz 10) | RAFTA | macOS cihaz yok |

**Sonuç:** motorun C++ tarafında 1.0'a engel **yok**; tek "AL" kalemi para sağlayıcısı ve o da
fabrika hattının bir parçası. Motor 1.0 etiketi (`Surum: major`) kit + şablon ilk oyunu çıkardığında
basılır — "1.0 = bir oyunu fabrikadan çıkarabilen motor".

## Aşama 1 — Ortak oyun kütüphanesi: `kit_*` modülleri

Yer: `tulpar/kit/*.tpr`, motorun eklenti paketiyle gelir (`tulpar-ext.json` `modules`, kaynak
`tools/gen_engine_bindings.py` `KIT_MODULES`). Oyun `import "kit_dil";` yazar — yol yok, sürüm motorla
aynı. Her modül `import "engine";` eder; oyuna ÖZGÜ hiçbir şey taşımaz (ad, renk, metin parametre).

Küp-Küre'den ayıklanacaklar (satır sayısı oyunda bugün):

| modül | içerik | Küp-Küre kaynağı |
|---|---|---|
| `kit_dil` | `L(tr, en)`, cihaz dili çözümü (tr → TR, diğer her şey EN), kayıtlı seçim, ortam ezmesi | `oyun.tpr` dil_* (~60), `sabitler.tpr` L |
| `kit_kayit` | kayıt dosyası yolu (oyuncu / otopilot / penceresiz geçici), otopilot kayda yazmaz, ayar sürüm göçü, ses/ölçek/grafik ayarları | `oyun.tpr` kayit_dosyasi, ayarlari_oku (~70) |
| `kit_ses` | iki yuvalı müzik çapraz geçişi, efekt yedek tonu, ses kısma | `ses.tpr` (356) |
| `kit_acilis` | stüdyo + "Made with Tulpar Engine" kartları, girdi sızmaz | `acilis.tpr` (397) |
| `kit_arayuz` | 9 dilim atlas kiti (panel, düğme stilleri, modal, anahtar, ilerleme), tasarım birimi, yedek dikdörtgen; atlas üreticisi araç | `arayuz.tpr` (645) + `tools/kup_kure_arayuz.py` |
| `kit_kapi` | otopilot/penceresiz algısı, `[kapi]` satırları, uyarı sayacı, kapanış özeti | `oyun.tpr` dağınık |

Her modülün kapısı `tulpar/tests/kit.test.tpr` (tools/tulpar_dogrula.sh içinde) + **Küp-Küre'nin
kendi kapıları**: oyun kit'e geçince `tools/dogrula.sh` aynı taban çizgilerini vermeli. Oyunun
geçişi kit'in pozitif kontrolüdür (kit yanlışsa oyun kapısı düşer).

## Aşama 2 — Şablonlar + `yeni_oyun`

`tools/yeni_oyun.sh <depo-dizini> --ad "Oyun Adı" --paket dev.tulparlang.x --tur arena|kosu|bulmaca`:
depo iskeleti (tulpar.toml, `.motor` bağlantısı, `oyun.tpr` = şablon, `apk_derle.sh`, `tools/dogrula.sh`,
ikon üreticisi, `.gitignore`), ilk koşum penceresiz `[kapi]` satırı basar. Şablon başına bir örnek
`tulpar/sablonlar/<tur>/` (motor deposunda; oyun içeriği değil, oynanabilir en küçük döngü).

## Aşama 3 — Tek komut yayın

`apk_derle.sh` ve `tools/play_yayinla.py` / `play_sunucu.sh` paket adı / anahtar / sürüm parametreli hâle
gelip motor deposunun `tools/yayin/`'una taşınır; MyTinyURL düzeni (kullanıcı kuralı) aynen: kimlik
sunucuda kalır, dahili kanala yükle → terfi. Yeni uygulama için kullanıcının bir kerelik adımları
(Play Console'da uygulama + hizmet hesabı yetkisi + içerik formları + yükleme anahtarı) kontrol
listesi olarak basılır.

## Aşama 4 — Gerçek para sağlayıcısı

`"admob"` / `"play"` sağlayıcıları (Java köprüsü). Hesap/kimlik kullanıcıda; bu aşama kullanıcı
"reklamlı sürüm" deyince açılır.

## Aşama 5 — İlk fabrika oyunu

Şablondan ikinci küçük oyun, telefonda + Play dahili kanalda. Motor 1.0 etiketi burada.

## Durum

| aşama | durum |
|---|---|
| 0 | ✅ 2026-11-05 (bu belge) |
| 1 | ✅ `kit_kayit`, `kit_dil`, `kit_ses`, `kit_acilis`, `kit_arayuz` (Küp-Küre beşine geçti; açılış 24 kare, arayüz turu 58 kare bayt aynı, bütün kapılar aynı). `kit_kapi` ertelendi: oyunların kapı satırları oyuna özgü, ortak kısmı (`kit_kip_adi`, sayaçlar) zaten modüllerde |
| 2 | ✅ `tools/yeni_oyun.sh` + `tulpar/sablonlar/arena` + `ortak/` (toml, ortam, dogrula, OKUBENI, gitignore) + ikon. **2026-11-05:** üretim C++'ta tek kaynak (`app/editor_fabrika.cpp`; `engine_editor --yeni-oyun`, editörün "Yeni oyun" formu da aynı fonksiyon) — Windows kullanıcısında bash/Python gerekmez; `yeni_oyun.sh` yalnız `.motor` + doğrulama + git ekler, `ikon_uret.py` kalktı (ikon stb_truetype + stb_image_write ile). Üretilen depo kendi `tools/dogrula.sh`'ini koşar (otopilot turu, KAPI_BOZ pozitif kontrolü, NULL ses); `yerel_ci.sh` her koşumda bir oyun üretir + bozuk şablonun kırmızı döndüğünü ölçer |
| 3 | 🟡 yerel kısım ✅: iskelete `apk_derle.sh` (APK + APK'nin kendisinden statik doğrulama: paket, targetSdk ≥ 36, ABI, debuggable, çentik, TAM izin kümesi, ikon, varlık listesi) ve `tools/android_dene.sh` (debug APK → cihaz/emülatör → `debug.tulpar.env` otopilot → logcat'te `[kapi] TAMAM`; pozitif kontrol `EK_ENV=TULPAR_<ONEK>_KAPI_BOZ=1` → KIRMIZI). Ölçüldü 2026-11-05, Pixel 10 Pro XL emülatörü (x86_64, llvmpipe): üretilen oyun 36 s'de yeşil, açılış/menü/ayarlar ekranda, dil seçimi yeniden başlatmada korunuyor. **Play'e yükleme YOK**: kullanıcı kararı (2026-11-05) — yeni oyun Play'e ancak açıkça istenince; imzalı AAB + MyTinyURL hattı o zaman eklenir |
| 4–5 | ⏳ |
