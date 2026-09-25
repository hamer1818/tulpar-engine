# Editör içi güncelleme — çekirdek

GitHub'da yeni bir Release çıkınca editör onu bulur, indirir, doğrular ve kurulu
paketin yerine koyar. Bu belge önce **çekirdeği** anlatır (`app/updater.{hpp,cpp}`);
menü, rozet, pencere, ayar dosyası, komut satırı ve yeniden başlatma editör
arayüzünün işidir (`app/editor_update.{hpp,cpp}`) — aşağıda
[Kullanıcı akışı](#kullanıcı-akışı-editör-arayüzü). Sözleşme başlığı
`app/updater.hpp` — adlar iki tarafın ortak dilidir, **eklenebilir, değiştirilemez**.

Paket biçimi (`SURUM.txt`, `DOSYALAR.txt`) paketleyicide tanımlı:
`tools/paket_manifest.py` (PR #63). Sürüm/platform etiketi ikilide:
`core/build_info.hpp` (PR #62).

## Akış

```
check()    → [curl releases/latest → latest.json] → ayrıştır → UpToDate | Available
download() → [curl SHA256SUMS] → [curl arşiv.kismi] → ad düzelt → boyut denetimi
           → Verifying:  arşivin SHA-256'sı (poll başına bayt bütçesi)
           → Extracting: [tar] → paket kökü + SURUM.txt + yeni DOSYALAR.txt
                         → paket dosyaları özetlenir (bütçeli)
                         → kurulu dosyalar özetlenir (bütçeli) → plan
           → Staged
install()  → (senkron) yedeğe taşı → yeniyi yerine taşı → DOSYALAR.txt EN SON
           → Installed (yeniden başlatma gerekli)   | hata: ters sırayla geri al → Failed
```

Her ok bir `poll()` geçişidir: alt süreç bitmemişse ya da bütçe dolduysa `poll()` hemen
döner. Hiçbir adım bir karede bütün paketi özetlemez.

### Arayüzün bilmesi gerekenler

| konu | kural |
|---|---|
| bellek | `Updater::arena_bytes()` kadar arena (ölçüldü: 1 267 144 B, kanarya payı dahil). Disabled iken yalnız `Impl` ayrılır (ölçüldü: 28 832 B). Yer yoksa `init` **false** döner, arenanın Fatal politikasına düşmez. |
| `poll()` | Editör her kare çağırır. İş yoksa O(1) (ölçüldü: ~1 ns), `operator new` yok (AllocGate ile ölçülür). |
| `cleanup(dir)` | Açılışta **bir kez**, ilk `check()`ten ÖNCE (bir iş sürerken çağrılırsa onun çalışma alanını da siler). `.guncelleme/` altını siler (eski yedekler, yarım indirmeler). Silinemeyen dosya (Windows'ta eski süreç hâlâ açık) hata değil, sonraki açılışa kalır. `GERI-ALMA-EKSIK.txt` işareti varsa HİÇBİR ŞEY silmez. |
| `release().html_url` | Yalnız `https://github.com/` ile başlıyorsa dolu (arayüz tarayıcıda açabilir); aksi boş. |
| `cancel()` | Çalışan curl/tar'ı öldürür ve toplar (en çok ~5 s bloklar), çalışma alanını siler. Editör kapanırken iş sürüyorsa çağrılmalı: POSIX'te curl yetim kalıp indirmeye devam ederdi (Windows'ta iş nesnesi zaten kapatır). |
| `install()` | Yalnız `Staged`'de. Senkron (yalnız yeniden adlandırma; ölçüldü < 1 ms). Başarıda `Installed`: yeni ikililer diskte, **çalışan süreç eski** — arayüz yeniden başlatmayı önerir. |
| `kept_path(i)` | GÖRELİ yol, `.yeni` EKSİZ (ör. `tests/assets/editor.sahne`); yenisi `<yol>.yeni` olarak yanında. |
| `progress()` | Downloading: `.kismi` boyutu / Release boyutu (~100 ms'de bir `stat`); Verifying: özetlenen / arşiv boyutu; Extracting: tar sürerken **< 0** (bilinmiyor), sonra özetlenen dosya / toplam dosya; Staged ve sonrası 1. |
| `reason()` | Disabled/Failed açıklaması, ASCII Türkçe. Tam metinler aşağıda. |
| ortam | `TULPAR_GUNCELLEME_URL` (yalnız `api_url` null iken): API adresini ezer ve `file://`'a izin verir — yerel sınama için. |
| kurulum dizini | `install_dir` null → çalışan ikilinin dizini, **W-API ile UTF-8** (`platform::exe_dir()` Windows'ta A-API'dir; bkz. Tuzaklar 8cl). |

### Durumlar ve `reason()` metinleri

**Disabled** (init hatası değil; `init` true döner):

| koşul | reason (başı) |
|---|---|
| `current_version` boş | `kaynak derlemesi (surum yok): ...` |
| sürüm ayrıştırılamadı | `kurulu surum ayristirilamadi: ...` |
| platform {linux-x86_64, macos-arm64, windows-x86_64} dışı | `bu platform icin paket yayinlanmiyor: ...` |
| `DOSYALAR.txt` yok | `kurulum dizininde DOSYALAR.txt yok — paket kurulumu degil (...)` |
| `SURUM.txt` yok / biçimsiz | `kurulumda SURUM.txt yok ...` / `kurulu SURUM.txt bicimsiz` |
| `SURUM.txt` = `kaynak <platform>` | `surumsuz paket (SURUM.txt: kaynak) — ...` |
| `SURUM.txt` ≠ çalışan sürüm/platform | `kurulu SURUM.txt (... ...) calisan editorle (... ...) uyusmuyor` |
| kurulu manifest geçersiz | `kurulu manifest gecersiz: DOSYALAR.txt N. satir: ...` |
| önceki kurulumun geri alması eksik (`.guncelleme/GERI-ALMA-EKSIK.txt`) | `onceki bir guncellemenin geri almasi eksik kaldi — ... elle incelenmeli ...` |
| dizin yazılamaz | `kurulum dizini yazilamaz: ... (hata N)` |

**Failed** (kurulum dizini DEĞİŞMEDİ; `check()` ile yeniden denenir):

| adım | örnek reason |
|---|---|
| denetim | `surum denetimi basarisiz: <curl sebebi>` |
| curl sebepleri (`upd_curl_reason`) | 6/5 `sunucu adi cozulemedi — internet baglantisi yok ...`, 7 `sunucuya baglanilamadi — ...`, 22+403/429 `GitHub istek siniri asildi (HTTP 403) — ...`, 22+404 `bulunamadi (HTTP 404) ...`, 28 `zaman asimi — ...`, 1 `protokol reddedildi (yalniz https ...)`, 60/77 `sunucu sertifikasi dogrulanamadi`, bilinmeyen: `curl cikis kodu N: <stderr son satiri>` |
| araç yok | `curl bulunamadi (PATH'te yok) — ...` / Windows: `curl bulunamadi (System32'de curl.exe yok; Windows 10 1803+ gerekli)` |
| JSON | `JSON gecersiz (...)`, `bu platform icin paket yok (<arsiv adi>)`, `Release'te ozet dosyasi yok (...)` |
| adres politikası | `paket adresi reddedildi (yalniz https://github.com/): ...` |
| indirme | `ozet dosyasi indirilemedi: ...`, `paket indirilemedi: ...`, `indirilen boyut uyusmuyor: ...` |
| doğrulama | `paket ozeti tutmuyor (bozuk ya da yarim indirme): ...` |
| açma | `paket acilamadi (tar cikis kodu N): ...`, `paket koku bulunamadi ...` |
| paket | `pakette SURUM.txt bicimsiz: ...`, `SURUM.txt uyusmuyor: '...' (beklenen '...')`, `yeni paket reddedildi: DOSYALAR.txt N. satir: guvensiz yol reddedildi: ../x`, `paket dosyasi bozuk (ozet tutmuyor): ...` |
| kurulum | `kurulum basarisiz, N tasima geri alindi: <sebep>` |
| en kötü durum | `kurulum basarisiz (...) VE GERI ALMA EKSIK: N adim geri alinamadi — yedek: <dizin>`. Yedek ve açılan paket SİLİNMEZ, `.guncelleme/GERI-ALMA-EKSIK.txt` yazılır; o varken `check()` çalışmaz, `cleanup()` hiçbir şey silmez, sonraki `init` Disabled (yedek eski dosyaların tek kopyası olabilir — elle kurtarma). |

## Kullanıcı akışı (editör arayüzü)

Kod: `app/editor_update.{hpp,cpp}` (editör bağlaması `app/editor_app.cpp`'de birkaç satır,
komut satırı `app/editor.cpp`). Çekirdeği **yalnız bu dosya sürer**: `check()` ve
`download()` tek bir kapıdan geçer ve sayılır (`UpdateUi::requests`).

### Menü

**Yardım** (menü çubuğunun en sağındaki kategori; komut tablosunda, kısayolsuz):

| öğe | anahtar (`--komut`) | ne yapar |
|---|---|---|
| Güncellemeleri denetle… | `yardim.guncelleme_denetle` | Pencereyi açar ve denetler. Kaynak derlemesinde (Disabled) de **etkin**: pencere sebebi ve `git pull && ./derle.sh` yolunu söyler — soluk bir satır sebebini söylemezdi. |
| Otomatik denetle ✓ | `yardim.otomatik_denetle` | Aç/kapa; ayar dosyasına yazılır. |
| Hakkında | `yardim.hakkinda` | Sürüm, platform, kurulum dizini, ayar dosyası, güncelleyicinin durumu (+ Disabled/Failed sebebi), otomatik denetim (son denetim, açılıştaki karar), atlanan sürüm. |

### Rozet

Menü çubuğunun sağında, ürün adının solunda, tıklanabilir hap: `⬆ v0.3.0`. Görünür
olduğu durumlar: **Available** (kurulu sürümden yeni ve atlanmamış — elle denetimde
atlanmış olan da görünür), indirme/doğrulama/açma (`⬆ v0.3.0  %40`), **Staged**
(`⬆ v0.3.0 hazır`), **Failed** (sürüm biliniyorsa `⬆ v0.3.0 hata` — tekrar denemenin
yolu). Tıklayınca güncelleme penceresi açılır. Rozet yokken (`ChromeState::update_badge`
nullptr) menü çubuğu ImGui'ye **tek ek çağrı** göndermez. İkon: U+2B06, DejaVuSans'ta
(icon_check yeşil).

### Güncelleme penceresi (kipli)

| durum | içerik | düğmeler |
|---|---|---|
| Disabled | "Güncelleyici kapalı" + `reason()`; sürüm boşsa kaynak derlemesi açıklaması + `git pull && ./derle.sh` (Windows: `derle.bat`) + Kopyala; `GERI-ALMA-EKSIK` işareti yüzünden kapalıysa aşağıdaki kırmızı kurtarma kutusu | Kapat |
| Idle | "Henüz denetlenmedi." | Şimdi denetle / Kapat |
| Checking | belirsiz (akan) ilerleme çubuğu | İptal / Arka planda sürsün |
| UpToDate | ✓ En son sürüm kurulu (vX) | Kapat |
| Available | yeni sürüm, yayın tarihi, kurulu sürüm, paket adı + boyutu; **sürüm notları** (kaydırılabilir, sarılmış); `notes_truncated` ise "Notlar kısaltıldı — devamı GitHub'da" + **Bağlantıyı kopyala** / **Tarayıcıda aç** (yalnız `https://github.com/` ile başlayan adres açılır) | **İndir ve kur** / Sonra / Bu sürümü atla |
| Downloading / Verifying / Extracting | aynı sürüm bloğu + ilerleme çubuğu (`progress() < 0` ise akan) | İptal / Arka planda sürsün |
| Staged | "vY kurulmaya hazır"; N dosya güncellenecek (yeni/değişen/kaldırılan), M aynı; **korunacak kullanıcı dosyaları** listesi `yol → yol.yeni` açıklamasıyla | **Kur ve yeniden başlat** / Sonra |
| Installed | ✓ kuruldu; yeniden başlatma başarısızsa sebep | (Kapat) |
| Failed | sürüm biliniyorsa sürüm bloğu; `reason()`; "Kurulum dizini değişmedi." | **Tekrar dene** / Kapat |
| Failed, **geri alma eksik** (`... VE GERI ALMA EKSIK ...`) | "Kurulum YARIM KALDI" + `reason()` + kırmızı kutu: dizin karışık olabilir, eski dosyaların tek kopyası yedekte olabilir, editör onu silmez ve işaret kalkana kadar denemez; `<kurulum>/.guncelleme/` yolu + Yolu kopyala. "Değişmedi" **yazılmaz** (yalan olurdu), Konsol'da `KURULUM YARIM KALDI` | Kapat (Tekrar dene yok: çekirdek işaret varken denetlemez) |

*Tekrar dene* başarısız adım indirmeyse `check()` + (Available gelince) `download()`
zincirini yeniden kurar; denetimdeyse yalnız `check()`. Esc = Sonra. Pencere
kapatılınca iş arka planda sürer, rozet ilerlemeyi gösterir.

### Kur ve yeniden başlat

1. Oynatma sürüyorsa **önce durur** (oynatma düzenlemeleri geri alınır, kirli bayrağı
   oynatma öncesine döner).
2. Sahne kirliyse mevcut onay kutusu sorulur: **Kaydet ve devam et / Kaydetmeden devam
   et / Vazgeç** (adsız sahnede Kaydet, Farklı kaydet diyaloğunu açar; bekleyen eylem
   kabul edilince sürer).
3. `install()` (senkron, < 1 ms). Başarısızsa kurulum dizini eski haline dönmüştür;
   pencere sebep ve *Tekrar dene* ile yeniden açılır, durum çubuğu kırmızı.
4. Başarılıysa editör **normal kapanış yolundan** çıkar (panel düzeni, son dosyalar
   yazılır; süren iş varsa `cancel()`), en sonda `<kurulum>/engine_editor[.exe]` **aynı
   argümanlarla** `process_start_detached` ile başlar. `--scene` açık sahnenin yoluyla
   değişir (kaydedilmiş açık sahne yeniden açılsın; adsız sahnede orijinal argüman kalır).
   Yeni süreç kapanıştan **sonra** başlar: önce başlasaydı eski editörün yazacağı düzen
   dosyasını okuyacaktı. Windows'ta argv ANSI → UTF-8 çevrilir (Tuzaklar 8cl).
   Başlatılamazsa sebep `engine_hata.log`'a da yazılır (pencere kapanmak üzere).

### Ayar dosyası ve otomatik denetim

`$HOME/.tulpar_guncelleme` (HOME yoksa `%USERPROFILE%`, o da yoksa ikilinin dizini):

```
# Tulpar Editor guncelleme ayarlari (editor yazar; elle de duzenlenebilir)
otomatik evet
son_denetim 1790000000
atla v0.3.0
```

`otomatik evet|hayir` (varsayılan evet; `hayır` da kabul), `son_denetim <unix sn>` (son
**başarılı** denetim), `atla <etiket>` (tek jeton, sürüm kalıbında). Bilinmeyen anahtar,
bozuk değer, 4 KB'tan uzun dosya: **yok sayılır ve sayılır** (Konsol'a uyarı). Yazma
geçici dosya + yerine taşıma (yarım dosya kalmaz). Penceresiz kip ve komut satırı
dosyaya **yazmaz**.

Açılışta (`update_ui_init`): Disabled değilse ve penceresiz değilse `cleanup()`; sonra
karar (`upd_auto_decide`, ilk tutan sebep):

| sıra | sebep | sonuç |
|---|---|---|
| 1 | güncelleyici Disabled | denetim yok |
| 2 | **penceresiz kip** | denetim yok — ağ yok |
| 3 | `TULPAR_GUNCELLEME=0` | denetim yok |
| 4 | ayarda `otomatik hayir` | denetim yok |
| 5 | son denetim 24 saatten yeni | kalan süre sonra (oturum sürerse) |
| — | hiçbiri | **hemen denetle** |

Saat geri gittiyse (son denetim gelecekte) "yeni" sayılmaz — yanlış saatli makinede
otomatik denetim hiç koşmazdı. Uzun oturumda 24 saatte bir yeniden: tek tamsayı
karşılaştırması, saat editörün kare başında **zaten okuduğu** monoton `now_ns`
(kare başına ek syscall yok). Atlanan etiket otomatik denetimde rozet göstermez, elle
denetimde görünür.

### Ağ kuralı ve kapıları

- Penceresiz kip Updater'a **hiçbir** istek vermez: ne otomatik denetim ne menü komutu
  (`--komut yardim.guncelleme_denetle` dahil). İstek kapıda reddedilir ve reddi sayılır.
- Penceresiz editörün sonunda **güncelleme kapısı**: `Updater istegi 0` olmalı; pozitif
  kontrol aynı koşuda menü komutunun gövdesini çağırır, **red 1** olmalı (kaynak
  derlemesinde — Disabled — de ölçülür). İhlal = editör 1 ile çıkar.
- `engine_tests` aynı kapıyı **etkin** bir güncelleyiciyle koşturur (sahte paket dizini,
  otomatik açık, hiç denetlenmemiş): tek engel penceresiz kip. Pozitif kontrol:
  aynı yapılandırma pencereli kurulunca aynı sayaç **1** olur ve `file://` fikstür
  Available döner.

### Komut satırı (pencere/Vulkan açmadan)

```
engine_editor --surum                  # "<sürüm|kaynak derlemesi> <platform>", 0
engine_editor --guncelleme denetle     # 0 güncel, 10 yeni sürüm var, 1 hata, 2 bilinmeyen fiil
engine_editor --guncelleme kur         # denetle + indir + doğrula + aç + plan + kur; 0/1 (yeniden başlatmaz)
```

`poll()` 20 ms uykuyla sürülür; denetimde 120 s, indirmede 30 dk zaman aşımı
(`cancel()`). Ağ'a **yalnız** kullanıcı bu bayrakları verince çıkılır.

### Ortam değişkenleri

| ad | etki |
|---|---|
| `TULPAR_GUNCELLEME=0` | otomatik denetimi kapatır (elle denetim çalışır) |
| `TULPAR_GUNCELLEME_URL` | API adresi (çekirdek okur; `file://` de kabul) — sınama |
| `TULPAR_GUNCELLEME_DIZIN` | kurulum dizini (varsayılan: ikilinin dizini) — sınama; Windows'ta ASCII yol |
| `TULPAR_GUNCELLEME_SURUM` | kurulu sürüm yerine geçen etiket — sınama |

### Konsol

Etiket `guncelleme`: her durum geçişi bir satır (denetleniyor / güncel / yeni sürüm /
indiriliyor / doğrulanıyor / açılıyor / kurulmaya hazır + korunan her dosya / kuruldu /
başarısız + sebep / iptal), ayar dosyası uyarıları, penceresiz red.

### Ölçümler (arayüz)

Makine: AMD Ryzen 7 9800X3D + RTX 5080, Linux (CachyOS), GCC 16.2 Release — 2026-09-25.

| ne | sayı | nerede |
|---|---|---|
| editörün kare başı yolu, boşta (`update_ui_poll` + `update_ui_badge` + kapalı `update_ui_draw`) | **4.6 ns/kare** (10⁶ kare), `operator new` 0 | `editor_update_headless_never_requests_positive_control_counts` |
| pencereler kapalı, rozet yok: ImGui köşe sayısı | `update_ui_draw` ile **216 == 216** onsuz | `editor_update_badge_and_window_render_headless` |
| penceresiz editör, 60 kare p50 (aynı sahne, önce/sonra iç içe 5'er koşu, medyan; "önce" = güncelleme özelliğinden önceki main `f3babc6`) | önce **13.08 ms**, sonra **12.99 ms** (ilk tur 13.00 / 13.01) | elle (`--headless 60`) |
| açılış (1 karelik penceresiz koşunun duvar süresi, 5 koşu medyanı) | önce **159 ms**, sonra **158 ms** (ilk tur 158 / 160) | elle (`--headless 1`) |
| aynı ölçüm, güncelleyici **etkin** (sahte paket dizini) | p50 **13.18 ms**, açılış **140 ms** | elle |

### Testler (`tests/test_editor_update.cpp`)

| kapı | pozitif kontrolü |
|---|---|
| ayar dosyası gidiş-dönüş bayt bayt; 8 bozuk satır yok sayılır ve sayılır | geçerli satırlar (boşluk/CRLF) uygulanır; ikinci kayıt hedef varken yerine taşır |
| otomatik karar: her sebep tek başına | hepsi tutunca Go; tam 24 saat Go, 24 s − 1 Recent |
| rozet kuralları + metni, yeniden başlatma argv'si | sığmayan argv kırpılmaz (0) |
| penceresiz: istek 0, menü komutu reddedildi (red 1), ayar dosyası yazılmadı, kare yolu 0 ayırma | **aynı** yapılandırma pencereli: istek 1, `file://` fikstür Available, rozet `⬆ v9.9.9`, son denetim yazıldı |
| sonda: sahte Available → rozete tıkla → pencere açılır ve çizilir; fare pencereyi tarar, kimlik çakışması 0; Staged/İndiriliyor/Failed/Disabled/Hakkında çizilir (PPM) | Available değilken (Idle) rozet yok, rozet alanında piksel farkı; kapalı pencerelerde köşe sayısı eşit |
| sonda: "geri alma eksik" (Failed ve Disabled metinleri, çekirdeğin `set_reason` kalıpları) kırmızı kurtarma kutusunu HER karede çizer | sıradan Failed ("paket özeti tutmuyor") ve sıradan Disabled kutuyu **çizmez** |
| gerçek ikili: `--surum`, `--guncelleme denetle` = 10 | fikstür kurulu sürüme eşitken 0; bilinmeyen fiil 2 |
| gerçek ikili: `--guncelleme kur` uçtan uca (tar + SHA256SUMS + latest.json, file://) | kullanıcının dosyası yerinde, yenisi `.yeni`; bilinmeyen fiilde kurulum dizinine dokunulmaz |
| gerçek ikili: penceresiz editörün güncelleme kapısı, **etkin** güncelleyiciyle | karar metni "penceresiz" (Disabled değil: kapı gerçekten kipi ölçüyor) |

### Bilinen sınırlar (arayüz)

- Kur + yeniden başlat zinciri penceresiz kipte **bilerek** koşturulamaz (penceresiz kip
  indirmez); parçaları ayrı ölçülür: `kur` uçtan uca (CLI), yeniden başlatma argv'si
  (birim), onay kutusu mevcut desen. Pencereli akış elle doğrulanır.
- Başarısız otomatik denetim (ağ yok) sessiz değildir (Konsol'da uyarı satırı; elle denetim ve indirme hatası kırmızı) ama
  rozet göstermez; bir sonraki deneme 24 saat sonra ya da bir sonraki açılışta.
- Aynı makinede iki editör aynı ayar dosyasını paylaşır (son yazan kazanır).
- macOS: paket bir `.app` değil; yeniden başlatma çıplak ikiliyi açar (bugünkü paketle
  aynı).

## Paket biçimi (PR #63) ve çekirdeğin yeniden denetimi

Paketleyici kuralları zaten denetliyor; çekirdek **aynı kuralları kendisi yeniden
denetler** (savunma derinliği). İhlal eden tek satır bütün paketi reddeder ve
`counters().manifest_rejected` sayılır.

- `SURUM.txt`: tam olarak `<sürüm> <platform>\n` — tek satır, CR yok. Sürüm
  `vX.Y.Z[-önek]` ya da `kaynak`. (`upd_surum_parse`)
- `DOSYALAR.txt`: her satır `<64 küçük hex><iki boşluk><yol>\n`; `*` yok, CR yok,
  boş satır yok, dosya `\n` ile biter; bayt sıralı, tekrarsız; büyük/küçük harf
  duyarsız çakışma yok; `SURUM.txt` listelenir, `DOSYALAR.txt` listelenmez.
- Yol (`upd_manifest_path_ok`): parçalar `^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$` —
  `.` ile başlamaz (`.`/`..`, gizli dosya, `.guncelleme/` dışarıda), `/`, `\`,
  `:`, boşluk, ASCII dışı yok; `.yeni` ile bitmez; < 256 bayt.

Arşivin tek üst dizini `tulpar-engine-<tag>-<platform>/`; yoksa açılan dizindeki
tek dizin kök sayılır. Yalnız manifestte listelenen **normal dosyalar** kurulur;
pakette listelenmeyen fazlalık yok sayılır.

## Plan kuralı

Her yeni manifest girdisi için kurulumdaki hedefin özeti çıkarılır:

| durum | karar | ne olur |
|---|---|---|
| hedef yok | **add** | yerine taşınır (üst dizinler yaratılır, günlüğe girer) |
| hedef özeti == yeni | **same** | dokunulmaz |
| hedef özeti == ESKİ manifestteki | **replace** | eski yedeğe, yeni yerine |
| aksi (kullanıcı değiştirmiş ya da eski manifestte yok) | **kept_user** | hedef yerinde kalır, yenisi `<ad>.yeni`; önceki bir güncellemeden kalan `<ad>.yeni` yedeğe |
| hedef dizin/bağ | — | **Failed** (`kurulumda dosya yerinde dizin/bag var`) |

Eski manifestte olup yenide olmayan dosya: değişmemişse **remove** (yedeğe taşınır),
değişmişse ya da yoksa dokunulmaz.

Kurulumdan sonra yeni `DOSYALAR.txt` kullanıcının değiştirdiği dosya için de YENİ
özeti taşır; bir sonraki güncellemede o dosya yine "değiştirilmiş" görünür ve yine
ezilmez.

## Geri alınabilir kurulum

Sıra: (1) silinecekler yedeğe, (2) değişecek eski dosyalar ve eski `.yeni`'ler yedeğe,
(3) yeniler yerine, (4) eski `DOSYALAR.txt` yedeğe, yeni `DOSYALAR.txt` yerine —
**işleme noktası**. Yedek: `.guncelleme/yedek-<eski sürüm>/`, aynı birimde **yeniden
adlandırma** (kopya yok). Her taşıma ve her yaratılan dizin günlüğe girer; herhangi bir
adım başarısız olursa günlük TERS sırayla uygulanır ve kurulum ağacı bayt bayt eski
haline döner (`.guncelleme/` dahil — o da silinir).

`fs_move` hedefi **asla ezmez** (Windows: `MoveFileExW` REPLACE_EXISTING'siz; POSIX:
`lstat` + `rename`). Geri alma "her taşıma tersine çevrilebilir" varsayımına dayanır;
ezilen bir dosya geri getirilemezdi.

Windows çalışan bir `.exe`'nin (ve yüklü DLL'in) üstüne yazdırmaz ama **adını
değiştirtir**: çalışan editör yedeğe taşınır, yenisi yerine konur. Eski `.exe` ancak
eski süreç kapanınca silinebilir — bu yüzden yedekler bir sonraki açılışta
`cleanup()` ile silinir. Salt-okunur dosyalar (macOS paketindeki `libglfw.3.dylib`
0444) yeniden adlandırılabilir; silme yolunda Windows'ta öznitelik önce kaldırılır.

## Güvenlik modeli

- **Bütünlük:** arşiv, aynı Release'teki `...-SHA256SUMS.txt` ile; açılan her dosya yeni
  `DOSYALAR.txt` ile karşılaştırılır. Bozulmaya, yarım indirmeye, yanlış arşive karşı
  korur.
- **Taşıma:** curl `--proto =https --proto-redir =https` (yönlendirme de yalnız https).
  Varlık adresleri üretimde `https://github.com/` ile başlamak **zorunda**.
- **Araçlar:** Windows'ta `curl.exe`/`tar.exe` `%SystemRoot%\System32`'den **tam
  yolla** (PATH'e konmuş başka bir curl indirmeyi ele geçiremez; MSYS2'nin GNU tar'ı
  zip açamaz). Argümanlar dizi (kabuk yok), yollar **göreli ASCII**, çalışma dizini
  `.guncelleme/is/` (UTF-16 olarak verilir).
- **İmza YOK — bilinen sınır:** GitHub hesabı/Release ele geçirilirse saldırgan hem
  arşivi hem SHA256SUMS'u değiştirebilir. Koruma yalnız GitHub'ın kimliğine ve TLS'e
  dayanır. (GitHub API'si artık varlık başına `digest: sha256:...` da veriyor; aynı
  güven kökünden geldiği için ayrı bir koruma değil.)

## Bilinen sınırlar

- **İmza yok** (yukarı).
- **Güç kesintisi / çökme kurulum ortasında:** günlük bellekte; diske yazılmaz. İşleme
  noktasından önce kesilirse kurulum karışık kalır (bazı dosyalar yeni, `DOSYALAR.txt`
  eski). Zarar sınırlı: bir sonraki güncelleme yeni dosyaları "kullanıcı değiştirmiş"
  sayar ve **ezmez** (`.yeni` yazar); eski dosyalar `.guncelleme/yedek-*/`'da durur —
  ama `cleanup()` onları bir sonraki açılışta siler.
- **Aynı kurulumda iki editör:** ikincinin açılıştaki `cleanup()`'ı birincinin süren
  indirmesini silebilir (kilit dosyası yok).
- **Windows yol uzunluğu:** dosya işlemleri `\\?\` önekiyle MAX_PATH'i aşar, ama
  `CreateProcessW`'nin çalışma dizini (kurulum + `\.guncelleme\is`) MAX_PATH'ten kısa
  olmalı ve System32 `tar.exe` çok uzun yollarda açamayabilir. Kurulum yolu ~150
  karakterden kısa tutulmalı.
- **Sürücü harfi / birim:** yedek, çalışma alanı ve kurulum AYNI birimde olmalı
  (`.guncelleme/` kurulum dizininin içinde olduğu için öyle); taşıma birimler arası
  kopyaya düşmez, düşerse başarısız olur.
- **Delta yok:** her güncelleme tam arşiv indirir (bugün 5–8 MB). Diskte geçici olarak
  arşiv + açılmış paket + yedek (~3× paket) gerekir.
- **GitHub API sınırı:** kimliksiz istek saatte 60. Aşılınca `GitHub istek siniri
  asildi (HTTP 403)`.
- **Vekil sunucu:** curl `HTTPS_PROXY`/`https_proxy` ortam değişkenlerine uyar; başka
  ayar yok.
- **Linux/macOS:** `curl` ve `tar` PATH'te olmalı (macOS'ta ikisi de sistemle gelir).
- **Symlink:** paketleyici reddediyor; çekirdek hedefte ve pakette yalnız normal dosya
  kabul eder (`lstat`). Arşivin kendisindeki kötü niyetli bağlar tar'ın varsayılan
  korumasına kalır (arşiv zaten SHA256SUMS'tan geçti — güven kökü GitHub).

## Ölçümler

Yerel makine: AMD Ryzen 7 9800X3D, Linux (CachyOS), GCC 16.2.1 `-O2` Release, curl 8.22.0,
GNU tar 1.35 — 2026-09-25. CI koşucuları: PR #64 koşumu 36123092308 (2026-09-25).

| ne | sayı | nerede |
|---|---|---|
| SHA-256 (taşınabilir yazım, SHA-NI yok) — yerel | **417–434 MB/s** | `sha256_throughput_measured` |
| SHA-256 — CI | Windows (MSYS2) **179**, Linux **232**, macOS arm64 **238 MB/s** | aynı test, CI |
| boşta `poll()` | yerel **0.77–1.01 ns**; CI Linux 1.36, macOS 1.31, Windows 1.88 ns/çağrı (10⁷ çağrı); `operator new` her yerde 0 | `updater_idle_poll_is_o1_and_allocation_free` |
| uçtan uca fikstür (file://, 3 MB ikili, 64 KB bütçe) — yerel | denetim 2.8–3.3 ms; indirme+doğrulama+açma+plan 22–30 ms / ~115 poll; kurulum 0.1–0.2 ms; toplam **26–41 ms**; özetlenen 6.3 MB / 98 poll; poll'larda `operator new` 0 | `updater_end_to_end_install_via_file_urls` |
| uçtan uca fikstür — CI | toplam Linux **94 ms**, macOS **92 ms**, Windows **188 ms** (Windows: `kurulum-Çağrı` dizini W-API ile; kurulum 3.4 ms). Her yerde özet 98 poll'a bölündü. Windows'taki poll sayısı (~200 000) testin 0.5 ms uykusunun orada ~0'a yuvarlanmasından: ölçüm döngüsü, çekirdek değil. | aynı test, CI |
| gerçek CI paketi, elle (prova-manifest, run 36121622990, linux-x86_64, 6.7 MB arşiv, 17 dosya, o zamanki varsayılan 1 MB bütçe) | Staged'e **247 ms / 124 poll**, özetlenen 50.8 MB / 49 poll; plan add 0 / replace 2 / same 14 / kept 1 / remove 1; kurulumdan sonra `sha256sum -c DOSYALAR.txt` yalnız kullanıcının değiştirdiği dosyada tutmuyor (tasarım gereği) | elle (`engine_tests`'e girmez: ağa/artefakta bağlı) |

Varsayılan bütçe (`kUpdHashBudget`) ilk yazımda 1 MB'ti ve yalnız yerel ölçüme
dayanıyordu (≈2.4 ms/kare). CI koşucularında SHA-256 yarı hızda çıktı (179–238 MB/s):
1 MB orada 4.2–5.6 ms, 60 Hz karenin üçte biri. Bütçe **512 KB**'a indi: yerelde
~1.2 ms, CI sınıfı makinede ~2.1–2.8 ms. Gerçek paket (~50 MB özet: arşiv + paket +
kurulu dosyalar) 60 Hz'de ~1.7 s'de biter. Güncelleyici yalnız masaüstü editörde
(Android'de güncelleme mağazadan). `UpdaterConfig::hash_budget_bytes` ile değiştirilir.

## Testler ve pozitif kontroller

`tests/test_sha256.cpp`, `tests/test_updater.cpp` (yalnız Android olmayan derleme).
Hepsi **ağsız**: sahte Release `file://` ile, gerçek curl ve gerçek tar (Windows'ta
System32 `tar -a -cf` ile zip). Kurulum dizininin adında Türkçe harf var
(`kurulum-Çağrı`): Windows'ta A-API ile W-API'nin ayrıştığı yer (Tuzaklar 8cl).

| kapı | pozitif kontrolü |
|---|---|
| NIST vektörleri ("", "abc", 448/896 bit, 1M 'a') + blok sınırları + her parçalamada aynı özet | tek bayt değişince özet değişmeli |
| sürüm önceliği (v0.1.9 < v0.1.10, rc.2 < rc.10, on-ekli < on-eksiz …) | geçersizler reddedilir, çıktı değişmez |
| JSON: iç içe `author`/`uploader` (tuzak `name`/`size`), kaçışlar, `ç`, vekil çift | uzun not UTF-8 karakter ortasından **kesilmez** (tek ve çift önekle iki vaka) |
| kurulu manifest kesin biçim (CR, `\n`'siz son, büyük hex, `*`, tek boşluk, sırasız, tekrar, harf çakışması, SURUM.txt eksik, `.yeni`, `../x`, boş) | aynı dizin geçerli manifestle **Idle** olur |
| uçtan uca: add/replace/same/kept_user/remove + salt-okunur dosya + önceki `.yeni` | kareye bölme: `hash_polls ≥ 3 MB / 64 KB`; poll'larda AllocGate 0 |
| arşivde tek bayt bozuk → Failed | ağaç anlık görüntüsü (sıradan bağımsız, boş dizin dahil) **bayt bayt aynı**; anlık görüntünün kendisi tek bayt/boş dizin farkını yakalar |
| `test_fail_after(k)`: k = 0, 1, 3, 5, 9, 10, 11, 13 (ilk, salt-okunur, yeni dizinden önce/sonra, işleme noktası) | kapalı kapıyla (k = 14) aynı fikstür KURULUR ve ağaç değişir |
| `GERI-ALMA-EKSIK.txt` varken `init` Disabled, `cleanup` yedeği silmez | işaret kalkınca aynı `cleanup` yedeği siler, `init` etkin |
| manifestte `../x`, SURUM.txt uyuşmazlığı | kurulum dışına hiçbir şey yazılmaz, ağaç aynı |
| adres politikası: üretimde `file://` → curl `protokol`; `https://evil.example.com` → çekirdek reddi; yok dosya → `okunamadi` | ağaç aynı |
| mutasyonla doğrulandı (2026-09-25): geri almayı ve arşiv özet karşılaştırmasını kapatınca iki kapı KIRMIZI | — |
