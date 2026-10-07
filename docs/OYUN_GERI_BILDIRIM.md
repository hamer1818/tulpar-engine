# Oyun geri bildirimi — "Küpler ile Kürelerin Savaşı"

> İlk gerçek oyun: `tulpar/oyunlar/kup_kure_savasi/` (Çağlar Boyu Savaş tarzı, 2026-10-07).
> Amaç ikili: telefonda oynanır bir oyun **ve** motoru/dili gerçek bir oyunla zorlayıp hata bulmak.
> Her madde: ne oldu, nasıl yeniden üretilir, nerede ölçüldü, öncelik, önerilen düzeltme, durum.
>
> Ölçüm yerleri: **masaüstü** = RTX 5080 + Ryzen 7 9800X3D, Linux, TulparLang v3.40.10;
> **telefon** = Huawei P20 Pro (CLT-L09), Mali-G72, Vulkan 1.1, Android 10, pencere 2159x1080.
> Öncelik: **Y** yüksek (oyuncu görür / sessiz bozulma), **O** orta, **D** düşük (geliştirici deneyimi).
>
> **Düzen (2026-10-08):** maddeleri düzelten PR'lar başlıkta `(Geri bildirim #N)` yazar; bu belgeyi
> yalnız oyunun sahibi günceller (PR birleşince madde "düzeltildi (#PR)" olur ve oyun yeni API'ye geçer,
> geçici çözüm kalkar). Oyunun kendisi ayrı PR'da (`tulpar/oyunlar/kup_kure_savasi/`); bu belgedeki oyun
> yolları o PR birleşince geçerlidir.

## Özet

| # | alan | başlık | öncelik | durum |
|---|---|---|---|---|
| 1 | motor / Android | APK varlık süzgeci `.mp3`/`.flac` çıkarmıyordu — müzik telefonda sessizce yoktu | Y | **düzeltildi** (#91, Tuzaklar 8cu) |
| 2 | motor / perf | P20 Pro'da boş sahne bile 60 fps değildi (piksel başına bedel); iç çözünürlük ölçeği yoktu | Y | **düzeltildi** (#92 `eng_render_scale`; oyun telefonda 0.7) |
| 3 | motor / sahne | `partikul_renk` (+ `_fizik`, `_teps`) blob'a girmiyor: oyunda parçacık hep beyaz | O | açık |
| 4 | dil | içe aktarılan modüldeki hata, İÇE AKTARAN dosyanın adı ve satır metniyle raporlanıyor | Y | açık (TulparLang) |
| 5 | dil | modül, kendisini içe aktaran dosyanın fonksiyonunu göremiyor (global'ini görüyor) | O | açık (TulparLang) |
| 6 | dil | `import` yolu çalışma dizinine göre, içe aktaran dosyaya göre değil | O | açık (TulparLang) |
| 7 | dil | çoklu bildirim tek tip ister ve fonksiyonu YALNIZ bu dosyanın import'unda arar | D | açık (TulparLang) |
| 8 | motor / Android | geri tuşu oyunu duraklatmıyor, etkinliği kapatıyor (savaş kaybolur) | O | açık |
| 9 | motor / Android | arka planda ses durmuyor (AAudio akışı `started` kalıyor) | O | oyunda geçici çözüm |
| 10 | motor / köprü | fizik duraklatılamıyor (duraklat menüsünde mermiler uçmaya devam eder) | O | **düzeltildi** (#96; oyun geçti) |
| 11 | motor / köprü | sabit gövdeyi `isinla` her çağrıda gövdeyi YENİDEN kuruyor; kinematik gövde yok | O | açık |
| 12 | motor / köprü | kameranın görüş açısı ve ekran→ışın API'si yok; oyun `pi/3.5`'i kopyalıyor | D | açık |
| 13 | motor | Tulpar oyununda kare içi C++ ayırma (AllocGate) ölçülemiyor | O | açık |
| 14 | motor / perf | betik kancası dağıtımı telefonda masaüstünün ~16 katı (6.6 µs / 0.4 µs çağrı, gövdeler dahil) | D | ölçüm (cihaz verisi) |
| 15 | motor / arayüz | düğme yazısı yalnız yükseklikten ölçekleniyor, uzun etiket taşıyor | D | oyunda geçici çözüm |
| 16 | motor / arayüz | yarı saydam tam ekran karartma Mali'de ~3.5 ms (TBDR) — uyarı köprüde görünmüyor | D | açık |
| 17 | TulparLang / Android | ekran çentiği alanı kullanılmıyor (2240 yerine 2159 piksel, solda siyah şerit) | D | açık (TulparLang) |
| 18 | motor / Android | `android cmd ?`: yaşam döngüsü günlüğünde adsız komutlar | D | açık |
| 19 | araç / Android | Huawei'de kayıt dosyası (iç depolama) adb ile okunamıyor; ayar test edilemiyor | D | bilgi |
| 20 | motor / font | `—` gibi aralık dışı karakter `?` basıyor (ASCII + Latin-1 + Türkçe) | D | bilgi |

İyi haber (ölçüldü, telefonda): Tuzaklar 8ct'nin yolu **gerçek oyunla** da temiz — geri tuşu
etkinliği kapattı (`DESTROY` → tam kapanış, `hata 0, uyari 0`), simgeye basınca **aynı PID**'de
ikinci oturum `k0` ile kuruldu, sahne + 8 model + ses yeniden yüklendi. 7 dk 15 sn'lik oturum
(25937 kare): **p50 16.67 ms, p99 22.81 ms**, RSS 211-212 MB düz, GPU 46 °C / CPU 61 °C (soğutmasız,
masada), hata 0.

---

## 1. APK varlık süzgeci `.mp3`/`.flac` çıkarmıyordu — DÜZELTİLDİ (#91)

**Ne:** oyunun `assets/sesler/muzik_*.mp3` dosyaları masaüstünde çalıyor, telefonda çalmıyordu; log
sessizdi (oyun `file_exists` ile sorup atlıyordu). Cihaz satırı `android: varlik 17 dosya` (APK'da 21) ve
`ses: 0 dosya, 9 sentetik ton`.
**Sebep:** Android host'u varlıkları uzantı süzgeciyle çıkarıyor; listede `.wav`, `.ogg` vardı,
`.mp3`, `.flac` yoktu (kod çözücü WAV/MP3/FLAC çözer, Ogg çözmez). Liste yalnız Android'de derlenen
dosyadaydı, masaüstü kapısı görmüyordu.
**Düzeltme:** `bridge/asset_filter.hpp` (ortak, masaüstünde test edilir), `.mp3`/`.flac` + büyük/küçük
harf duyarsız. Cihazda: `varlik 21 dosya`, `ses: 4 dosya`. Ayrıntı Tuzaklar **8cu**.

## 2. Telefonda piksel başına bedel — DÜZELTİLDİ (#92 `eng_render_scale`)

**Ne:** P20 Pro, 2159x1080, **boş** savaş alanı (96 çizim), 600 karelik aralıkların duvar saati:

| ayar | fps | p50 |
|---|---|---|
| parlama açık, ölçek 1.0 | **43.8** | 22.7 ms |
| parlama kapalı, ölçek 1.0 | 55.6 (savaşta 150+ çizimle de 55.7) | 17.6 ms |
| parlama açık, **ölçek 0.7** | **59.6** (60 birlikli savaşta da 59.6) | 16.5 ms |

Çizim sayısı 96 → 160 iken fps değişmedi: bedel çizim başına değil, **piksel başına**. Renderer'da
dinamik çözünürlük vardı, köprüde yoktu. **Düzeltme:** `eng_render_scale(s)` (parlama açıkken; kapalıysa
1.0 + UYARI). Oyun telefonda varsayılan 0.7 kullanıyor (`ANDROID_ROOT` varsa), Ayarlar'da kaydırıcı.
**Kalan:** parlama kapalıyken ölçek yok (iç hedef yok) — ölçek için iç hedefi parlamadan bağımsız kurmak
(bloom yoğunluğu 0) daha temiz olur.

## 3. `partikul_renk` blob'a girmiyor — AÇIK

**Ne:** `.sahne`'deki `partikul_renk br bg bb sr sg sb` ayrıştırılıyor ve yazılıyor ama
`SceneBlobParticle` (content/scene_blob.hpp, 48 bayt) renk alanı taşımıyor; `SceneRuntime::update`
`ParticleEmitterConfig`'in renklerini hiç doldurmuyor. Editörün hazır yayıcıları (ateş turuncusu, duman
grisi — app/editor_app.cpp) derlenmiş oyunda **beyaz**. `partikul_fizik` ve `partikul_teps` de aynı sınıf.
**Yeniden üret:** `tulpar/oyunlar/kup_kure_savasi/assets/savas.sahne` → `kup_mesale_*` (`partikul_renk 1 0.75 0.2 0.9 0.2 0.05`),
oyunda parçacıklar beyaz. `grep particle_color_start content/scene_runtime.cpp` boş.
**Öneri:** blob v9: `SceneBlobParticle`'a `color_start[3], color_end[3]` (+ yerçekimi), runtime `cfg`'ye
aktarsın; `scene_check.py` "yazıcının yazıp blob'un taşımadığı alan" sınıfını da ölçsün (sessiz kayıp).

## 4. İçe aktarılan modüldeki hata yanlış dosyayla raporlanıyor — AÇIK (TulparLang)

**Ne:** `savas.tpr` içindeki tanımsız çağrı, `oyun.tpr:311` diye ve **oyun.tpr'nin 311. satırının metniyle**
raporlandı (satır numarası doğru, dosya ve gösterilen kaynak yanlış). Hatayı arayan, masum bir satıra bakar.
**Asgari yeniden üretim:**
```
// modul.tpr
func modul_f(): int {
    return ana_yardimci() + 1;
}
// ana.tpr
// satir 1
import "modul.tpr";
// satir 3: bu satir masum
func ana_yardimci(): int { return 41; }
print(modul_f());
```
`tulpar ana.tpr` → `hata: 'ana_yardimci' adında bir fonksiyon bulunamadı --> ana.tpr:2 | 2 | import "modul.tpr";`
(beklenen: `modul.tpr:2 | return ana_yardimci() + 1;`). TulparLang v3.40.10, 2026-10-07.
**Öncelik Y:** çok modüllü her oyun bunu yaşar.

## 5. Modül, içe aktaranın fonksiyonunu göremiyor; global'ini görüyor — AÇIK (TulparLang)

Aynı yeniden üretim (madde 4): `modul.tpr` `ana_yardimci`yı bulamıyor. Ama global DEĞİŞKEN görülüyor:
```
// ana2.tpr                         // modul2.tpr
int g = 5;                          func modul_g(): int { return g + 1; }
import "modul2.tpr";
print(modul_g());                   // -> 6
```
Kural "modül, kendinden önce ayrıştırılanı görür" ise globaller için de geçerli olmalı; değilse
fonksiyonlar da görülmeli. Oyunda geçici çözüm: ses/kamera/efekt fonksiyonları `savas.tpr`'den önce
içe aktarılan modüllere taşındı, doğrulama kodu ana dosyaya kondu.

## 6. `import` yolu çalışma dizinine göre — AÇIK (TulparLang)

`a/b/m1.tpr` içindeki `import "d/c.tpr"` (dosya `a/b/d/c.tpr`), `a/` dizininden `tulpar b/m1.tpr` ile
koşunca `Import dosyasi acilamadi 'd/c.tpr'`. Örnekler bu yüzden `import "examples/davranis/x.tpr"` yazıyor ve
yalnız `tulpar/` dizininden çalışıyor. Oyun kendi dizininden çalışacak şekilde yazıldı
(`cd tulpar/oyunlar/kup_kure_savasi && tulpar oyun.tpr`; `tools/tulpar_dogrula.sh` de öyle koşturuyor).
**Öneri:** önce içe aktaran dosyanın dizini, sonra çalışma dizini.

## 7. Çoklu bildirim tek tip ister ve fonksiyonu yalnız bu dosyanın import'unda arar — AÇIK (D)

- `bool bas, px, py = isaretci();` (`isaretci(): (bool, float, float)`) üçünü de `bool` yapıyor; tip
  hatası "expected float, got bool" diye ATAMA satırlarında çıkıyor, bildirimde değil. Geçici çözüm:
  `(float, float, float)`, basılı = 1.0/0.0.
- `float sx, sy = ekrana(...)`: `ekrana` başka bir modülde (ana dosyanın import ettiği `kamera.tpr`) ise
  bu dosya onu KENDİSİ import etmedikçe "coklu bildirimin sag tarafi `: (T, T)` bildiren bir fonksiyonun
  dogrudan cagrisi olmali" — normal çağrı aynı fonksiyonu buluyor.

## 8. Geri tuşu etkinliği kapatıyor — AÇIK

`android_host.cpp` `on_input` yalnız dokunmayı işliyor, tuş olaylarına 0 dönüyor; sistem geri tuşunda
`NativeActivity`'yi bitiriyor (`DESTROY` → oyun `motor_kapat`). Oyuncu savaşın ortasında geri tuşuna
basınca savaş kaybolur (ölçüldü P20 Pro: `android cmd DESTROY` → `bitis: ekran 3` → yeni oturum menüden).
**Öneri:** `AKEYCODE_BACK` → köprünün tuş durumuna `"ESC"` (ya da `"GERI"`) + 1 dön; oyun ESC'yi zaten
duraklat/geri olarak işliyor. İki kez geri = çıkış kararı oyunun.

## 9. Arka planda ses sürüyor — OYUNDA GEÇİCİ ÇÖZÜM

Ana ekrana dönünce `dumpsys audio`: oyunun AAudio akışı `state:started` (P20 Pro, 2026-10-07). Köprü
`APP_CMD_PAUSE`'u yalnız logluyor. Oyun artık `kare_basla()` false dönünce (pencere yok) ana seviyeyi 0
yapıyor ve savaşı duraklatıyor; dönünce seviye geri. **Öneri:** host PAUSE/RESUME'da sesi durdursun ya
da `eng_app_paused()` gibi bir olay versin (pil: akış sessiz de olsa açık).
Ek gözlem: arka planda döngü ~10 fps sürüyor (p50 100.8 ms; sim sürer, sözleşme bu).

## 10. Fizik duraklatılamıyor — DÜZELTİLDİ (#96)

Duraklat menüsünde oyun mantığı durur ama `eng_frame_end` fiziği her kare adımlıyor: havadaki mermi,
kaya, enkaz düşmeye devam eder. Oyun birliklerin hızını sıfırlıyor (geçici çözüm). **Öneri:**
`eng_physics_pause(bool)` ya da zaman ölçeği.
**Düzeltme (#96):** `fizik_duraklat` / `fizik_durakli` / `zaman_olcegi`. Oyun duraklat (ve duraklattan açılan
ayarlar) ekranında fiziği donduruyor, devamda açıyor; hız sıfırlama geçici çözümü kalktı. Kapı: `[kapi] arayuz`
satırında `fizik_dondu=1` ve sonda fizik açık olmalı; köprü kapanışı "1 duraklatma, 1 kare durakli" diyor
(RTX 5080, 2026-10-08).

## 11. Sabit gövdeyi taşımak gövdeyi yeniden kuruyor — AÇIK

`teng_set_pos` dinamik olmayan gövdede `remove` + `make_body` yapıyor (Jolt'ta sabit gövdeye konum
yazılabildiği hâlde). 60 birlik sabit gövdeyle sürülseydi saniyede ~3600 gövde kurma olurdu. Oyun
birlikleri **dinamik** gövdeyle (`yuru_v`) sürüyor, kafa/silahı **gövdesiz model** yapıyor
(`tools/kup_kure_modelleri.py`). **Öneri:** kinematik gövde türü (`eng_spawn_box(..., kinematik)`) ve
`MoveKinematic`.

## 12. Görüş açısı ve ekran→ışın API'si yok — AÇIK (D)

Dokunuşla kule yuvası seçmek ve birlik üstü can çubuğu için oyun izdüşümü kendisi kuruyor; köprünün
`perspective(pi/3.5, …)` sabitini **kopyalamak** zorunda (`kamera.tpr` `FOV_TAN`). Sabit değişirse oyun
sessizce yanlış yere dokunur. Doğrulandı: otopilot yuvanın dünya konumunu ekrana izdüşürüp oradan ışın
atıyor; ışın yuvaya çarpmazsa kapı kırmızı (`kule_isin=2 kule_yakin=0 kule_iska=0`). **Öneri:**
`eng_camera_fov()` ya da `eng_screen_ray(sx, sy)` + `eng_world_to_screen`.

## 13. Tulpar oyununda AllocGate ölçülemiyor — AÇIK

`core/memory/alloc_gate_override.cpp` yalnız `engine_tests`/demo/editör yürütülebilirlerine bağlanıyor;
eklenti paketinin bildirimi onu linklemiyor. "Kare içi C++ ayırma 0" iddiası Tulpar oyununda ölçülemiyor.
Oyunun kapısı RSS eğimini ölçüyor (alt çeyrek 0 KB/1000 kare, 9600 kare; pozitif kontrol: 2 KB/kare
enjekte sızıntıda 2040 → KIRMIZI). **Öneri:** köprü kapanış raporuna kare içi `new` sayısı (override
eklenti paketinde, isteğe bağlı).

## 14. Kanca dağıtımı telefonda pahalı — ÖLÇÜM (cihaz verisi)

Kapanış satırı (P20 Pro, 7 dk savaş): `kare icinde 101149 kanca cagrisi, 671.25 ms, cagri basina 6636.2 ns`
(kanca gövdeleri dahil; mermi çarpışma + enkaz güncelle + kale tetiği). Masaüstünde aynı oyun 397 ns.
Kare başına ortalama ~0.026 ms, yani bütçeyi tehdit etmiyor. Kısa (80 sn) oturumda ilk 8 çağrı 74 ms
ölçüldü (çağrı başına 9.3 ms) — ilk çağrılardaki tek seferlik bedel (sembol çözümü/sayfa hatası?)
ayrıştırılmadı; ilk karelerde takılma olarak hissedilebilir.

## 15. Düğme yazısı genişliğe göre ölçeklenmiyor — GEÇİCİ ÇÖZÜM (D)

`ui_scale(h)` yalnız yükseklikten; "Savaş Arabası", "Kaya Yağmuru" 2240 genişlikte 6 düğmelik çubukta
düğmeden taştı (ekran görüntüsü). Oyun adları kısalttı ("Araba", "Kayalar"). **Öneri:** `min(yükseklik
ölçeği, genişlik / metin genişliği)`.

## 16. Tam ekran yarı saydam karartma Mali'de pahalı — AÇIK (D)

Ayarlar ekranı (tam ekran `RENK_KARART` + panel) p50 21.15 ms, menü (karartmasız) 17.6 ms — parlama
kapalı, ölçek 1.0, P20 Pro. Renderer'da `ui_fullscreen_blend_ratio` uyarısı var ama köprü/oyun onu
görmüyor. **Öneri:** köprü kapanış raporuna "tam ekran harmanlı dörtgen" sayacı.

## 17. Ekran çentiği alanı kullanılmıyor — AÇIK (TulparLang, D)

Pencere 2159x1080, ekran 2240x1080: solda ~81 px siyah şerit. Manifest/tema
`android:windowLayoutInDisplayCutoutMode="shortEdges"` vermiyor (TulparLang'in ürettiği manifest).

## 18. `android cmd ?` — AÇIK (D)

`cmd_name` bazı komutları tanımıyor (`APP_CMD_CONTENT_RECT_CHANGED`, `WINDOW_REDRAW_NEEDED`,
`SAVE_STATE`…): her açılışta 4-5 `android cmd ?` satırı.

## 19. Telefonda kayıt dosyası okunamıyor — BİLGİ

Kayıt çıkarma kökünde (`/data/user/0/<paket>/files/assets/tulpar_kayit.txt`); Huawei'de `run-as` çalışmadığı
için (Tuzaklar 8n) adb ile okunamıyor/yazılamıyor. Ayar testi ekrandan dokunarak yapıldı. **Öneri:** hata
ayıklama derlemesinde `TULPAR_ENGINE_SAVE` benzeri bir harici yol.

## 20. Font aralığı — BİLGİ

`content/font.hpp`: ASCII + Latin-1 + Türkçe (Ğğ İı Şş …). `—` (U+2014) `?` basıyor; Türkçe harflerin
hepsi çiziliyor (masaüstü DejaVu, telefon Roboto — ekran görüntüleriyle doğrulandı). Oyun metinleri
`—` kullanmıyor.

---

## Oyunun kendi hataları (motor değil; bulundu ve düzeltildi)

- **Menzilli birlik yolu tıkıyordu:** menzile girince duruyordu; sırada öndeyse arkadaki yakın dövüşçüler
  hiç temasa giremedi (7200 karede 0 yakın vuruş). Artık yürürken ateş ediyor.
- **Ölü id ile çağrı (21 HATA):** alan hasarı döngüsünde bir ölüm, enkaz halkasından eski parçayı
  silebiliyor; sonraki sorgu sonucu ölü id oluyordu. Motorun "ölü id → HATA" kuralı bunu hemen gösterdi.
- **Kule yuvaları kale kulelerinin arkasındaydı:** kameradan atılan ışın kuleye çarpıyordu (55 ıska).
  Yuvalar damın ön kenarına alındı; otopilotun dokunuşları artık ışınla isabet.
- **Kolay çok zordu:** kullanıcı telefonda Kolay'da 4 dk 2 sn'de yenildi (yapay zekâ Sanayi'ye,
  oyuncu Demir'e). Yapay zekânın geliri/XP çarpanı düşürüldü, kale hasarının XP'si yarıya indi;
  otopilot Kolay'ı 140 sn'de yeniyor.
- **Simetrik eşleşmede çıkmaz:** otopilot Normal'e karşı 6 dk'da iki taraf da Gelecek Çağı'nda, kaleler
  dolu. İnsan oyunu simetriyi bozuyor; yine de uzun savaşta bir tırmanma kuralı (ör. son çağda XP'nin
  hasar bonusuna dönmesi) değerlendirilmeli.
