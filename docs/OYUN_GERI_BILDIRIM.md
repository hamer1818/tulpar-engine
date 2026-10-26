# Oyun geri bildirimi — "Küpler ile Kürelerin Savaşı"

> İlk gerçek oyun (Çağlar Boyu Savaş tarzı, 2026-10-07). Amaç ikili: telefonda oynanır bir oyun **ve**
> motoru/dili gerçek bir oyunla zorlayıp hata bulmak. Her madde: ne oldu, nasıl yeniden üretilir, nerede
> ölçüldü, öncelik, önerilen düzeltme, durum.
>
> **Oyun bu depoda değil (2026-10-26):** ayrı, özel bir depoya taşındı; motoru dışarıdan kullanıyor ve kendi
> kapıları orada. Bu belge yalnız **motor/dil derslerini** tutar — oyunun iç ayrıntıları (tasarım, denge, oyun
> tarafındaki düzeltmeler) oyunun deposunda. Eski `tulpar/oyunlar/kup_kure_savasi/` kopyası (#94) main'den
> çıktı (geçmişte ve eski sürüm etiketlerinde duruyor); 9600 karelik oyun kapısının yerini motorun kendi
> `tulpar/examples/engine_savas.tpr`'si aldı (`tools/tulpar_dogrula.sh --tam`). Aşağıda "oyunun X'i" diye
> geçen yollar oyunun deposundadır.
>
> Ölçüm yerleri: **masaüstü** = RTX 5080 + Ryzen 7 9800X3D, Linux, TulparLang v3.40.10;
> **telefon** = Huawei P20 Pro (CLT-L09), Mali-G72, Vulkan 1.1, Android 10, pencere 2159x1080.
> Öncelik: **Y** yüksek (oyuncu görür / sessiz bozulma), **O** orta, **D** düşük (geliştirici deneyimi).
>
> **Düzen (2026-10-08):** maddeleri düzelten PR'lar başlıkta `(Geri bildirim #N)` yazar; madde PR birleşince
> "düzeltildi" olur ve oyun yeni API'ye geçer, geçici çözüm kalkar.

## Özet

| # | alan | başlık | öncelik | durum |
|---|---|---|---|---|
| 1 | motor / Android | APK varlık süzgeci `.mp3`/`.flac` çıkarmıyordu — müzik telefonda sessizce yoktu | Y | **düzeltildi** (#91, Tuzaklar 8cu) |
| 2 | motor / perf | P20 Pro'da boş sahne bile 60 fps değildi (piksel başına bedel); iç çözünürlük ölçeği yoktu | Y | **düzeltildi** (#92 `eng_render_scale`; oyun telefonda 0.7) |
| 3 | motor / sahne | `partikul_renk` (+ `_fizik`, `_teps`) blob'a girmiyor: oyunda parçacık hep beyaz | O | **düzeltildi** (#95 blob v9; oyunun meşaleleri turuncu) |
| 4 | dil | içe aktarılan modüldeki hata, İÇE AKTARAN dosyanın adı ve satır metniyle raporlanıyor | Y | **düzeltildi** (TulparLang #483) |
| 5 | dil | modül, kendisini içe aktaran dosyanın fonksiyonunu göremiyor (global'ini görüyor) | O | **düzeltildi** (TulparLang #484; oyun yeni sürümle sadeleşecek) |
| 6 | dil | `import` yolu çalışma dizinine göre, içe aktaran dosyaya göre değil | O | **düzeltildi** (TulparLang #485) |
| 7 | dil | çoklu bildirim tek tip ister ve fonksiyonu YALNIZ bu dosyanın import'unda arar | D | **düzeltildi** (TulparLang #485) |
| 8 | motor / Android | geri tuşu oyunu duraklatmıyor, etkinliği kapatıyor (savaş kaybolur) | O | **düzeltildi** (`geri_basildi`; targetSdk 36 için Tuzaklar 8de) |
| 9 | motor / Android | arka planda ses durmuyor (AAudio akışı `started` kalıyor) | O | **düzeltildi** (motor arka planda sesi kendisi durdurur, `arka_planda`; oyunun geçici çözümü kalktı) |
| 10 | motor / köprü | fizik duraklatılamıyor (duraklat menüsünde mermiler uçmaya devam eder) | O | **düzeltildi** (#96; oyun geçti) |
| 11 | motor / köprü | sabit gövdeyi `isinla` her çağrıda gövdeyi YENİDEN kuruyor; kinematik gövde yok | O | **düzeltildi** (#97 kinematik gövde; sabit `isinla` ayırmasız) |
| 12 | motor / köprü | kameranın görüş açısı ve ekran→ışın API'si yok; oyun `pi/3.5`'i kopyalıyor | D | **düzeltildi** (#98; oyun `FOV_TAN` kopyasını bıraktı) |
| 13 | motor | Tulpar oyununda kare içi C++ ayırma (AllocGate) ölçülemiyor | O | **düzeltildi** (#105 ayırma sayacı) |
| 14 | motor / perf | betik kancası dağıtımı telefonda masaüstünün ~16 katı (6.6 µs / 0.4 µs çağrı, gövdeler dahil) | D | **iyileştirildi** (#100, Tuzaklar 8cv) |
| 15 | motor / arayüz | düğme yazısı yalnız yükseklikten ölçekleniyor, uzun etiket taşıyor | D | **düzeltildi** (#105; oyun tam adlara döndü) |
| 16 | motor / arayüz | yarı saydam tam ekran karartma Mali'de ~3.5 ms (TBDR) — uyarı köprüde görünmüyor | D | **düzeltildi** (#105 `sahne_karart`; oyun tam ekran karartmayı bıraktı) |
| 17 | TulparLang / Android | ekran çentiği alanı kullanılmıyor (2240 yerine 2159 piksel, solda siyah şerit) | D | **düzeltildi** (TulparLang #486 `cutout` + motor güvenli alanı `guvenli_*`, sistem çubukları dahil) |
| 18 | motor / Android | `android cmd ?`: yaşam döngüsü günlüğünde adsız komutlar | D | **düzeltildi** (#102) |
| 19 | araç / Android | Huawei'de kayıt dosyası (iç depolama) adb ile okunamıyor; ayar test edilemiyor | D | **düzeltildi** (debug derlemede kayıt adb'ye açık) |
| 20 | motor / font | `—` gibi aralık dışı karakter `?` basıyor (ASCII + Latin-1 + Türkçe) | D | **düzeltildi** (#105 tipografik noktalama) |
| 21 | TulparLang / araç | `tulpar.toml`'da TANINMAYAN tek bir anahtar BÜTÜN dosyayı yok saydırıyor: `[ext] paths` de düşer, `import "engine"` yanıltıcı hatayla ("eklenti yuklu degil") patlar | O | **düzeltildi** (TulparLang: uyarı + o anahtarı atla) |
| 22 | motor / köprü | `eng_render_scale` günlüğü float/double karşılaştırması yüzünden 0.70'i "(kenetlendi)" diye basıyor | D | **düzeltildi** |
| 23 | araç | telefon bağlantısı koparken `adb` "waiting for device"ta sonsuza bekliyor; kilit altındaki betik kilidi bırakmıyordu | D | **düzeltildi** (`tools/android_*.sh`: `adb get-state` zaman aşımı) |
| 24 | test / motor | `engine_tests bridge_` (süzgeçli alt küme) `bridge_frame_allocs_*`'ı kırmızı gösterdi: önündeki "yükleyici yok" testlerinden sonra `teng_init` düştü; tam koşu yeşil | D | ölçüm (sıra bağımlılığı) |
| 25 | motor / köprü | çarpışma süzme (grup/katman) API'si yok: menzilli birliğin mermisi öndeki DOST gövdeye çarpıp kalıyordu (kullanıcı, telefonda) | Y | **düzeltildi** (`carpisma_grubu` / `carpisma_cifti`) |
| 26 | motor / köprü | dinamik gövdenin dönmesi kilitlenemiyor: devrilen birlik gövdesi, üstündeki gövdesiz kafa/kalkan modellerinin içine geçiyor | O | **düzeltildi** (`donme_kilidi`) |
| 27 | motor / içerik | LOD'u üretilmemiş model kameradan 20 m ötede renderer'ın İLK mesh'iyle çiziliyor (sıfırlanmış tutamaç geçerli): ekranın kenarında model bozuk, renk farklı (kullanıcı, telefonda) | Y | **düzeltildi** (Tuzaklar 8cx) |
| 28 | dil | `A[min(i, j)]` / `A[max(i, j)]` dizini sessizce 0 okuyor; `min()` sonucu `int[]` öğesine yazılınca o öğe de dizin olarak 0 | Y | **düzeltildi** (TulparLang: ondalık dizin sıfıra doğru kesilir) |
| 29 | motor / arayüz | ekran değişimi aynı karede yeni ekranı da çizdiriyor: aynı tıklama (gerçek ya da `ui_test_tikla_ad`) iki ekranın aynı yerdeki düğmesine iner | O | açık (desen: karede TEK ekran çiz) |
| 30 | oyun | şeridi tarayan engel / iri fizik gövdesi kapıya takılıyor | — | oyun tasarımı (ayrıntı oyunun deposunda) |
| 31 | motor / girdi | `eng_stick_action` sağ yarıya < 250 ms dokunuşu BIRAKINCA sayıyor, yetenek düğmesine basmak da saldırı sayılıyor; sanal çubuk görünmüyor | O | açık (oyun kendi çubuğunu çiziyor) |
| 32 | motor / köprü | "doğuşta küçük kur, karede büyüt" kalıbında büyütme yolunu atlayan varlık sessizce görünmez; çizilen boy okunamıyordu | Y | **düzeltildi** (`eng_drawn_height`, Tuzaklar 8df) |
| 33 | oyun | büyük gövde birlikleri fizikte itip düşman safına sürüyordu | — | oyun tasarımı (çarpışma grubuyla çözüldü; ayrıntı oyunun deposunda) |
| 34 | oyun | hava efekti ekranda görünmüyordu (güneş inerken ortam parlıyor, ikisi birbirini götürüyordu) | — | oyun tasarımı (ayrıntı oyunun deposunda) |

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

## 3. `partikul_renk` blob'a girmiyor — DÜZELTİLDİ (#95, blob v9)

**Ne:** `.sahne`'deki `partikul_renk br bg bb sr sg sb` ayrıştırılıyor ve yazılıyor ama
`SceneBlobParticle` (content/scene_blob.hpp, 48 bayt) renk alanı taşımıyor; `SceneRuntime::update`
`ParticleEmitterConfig`'in renklerini hiç doldurmuyor. Editörün hazır yayıcıları (ateş turuncusu, duman
grisi — app/editor_app.cpp) derlenmiş oyunda **beyaz**. `partikul_fizik` ve `partikul_teps` de aynı sınıf.
**Yeniden üret:** oyunun sahnesindeki meşale yayıcıları (`partikul_renk 1 0.75 0.2 0.9 0.2 0.05`),
oyunda parçacıklar beyaz. `grep particle_color_start content/scene_runtime.cpp` boş.
**Öneri:** blob v9: `SceneBlobParticle`'a `color_start[3], color_end[3]` (+ yerçekimi), runtime `cfg`'ye
aktarsın; `scene_check.py` "yazıcının yazıp blob'un taşımadığı alan" sınıfını da ölçsün (sessiz kayıp).

## 4. İçe aktarılan modüldeki hata yanlış dosyayla raporlanıyor — DÜZELTİLDİ (TulparLang #483)

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

## 5. Modül, içe aktaranın fonksiyonunu göremiyor; global'ini görüyor — DÜZELTİLDİ (TulparLang #484)

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

## 6. `import` yolu çalışma dizinine göre — DÜZELTİLDİ (TulparLang #485)

`a/b/m1.tpr` içindeki `import "d/c.tpr"` (dosya `a/b/d/c.tpr`), `a/` dizininden `tulpar b/m1.tpr` ile
koşunca `Import dosyasi acilamadi 'd/c.tpr'`. Örnekler bu yüzden `import "examples/davranis/x.tpr"` yazıyor ve
yalnız `tulpar/` dizininden çalışıyor. Oyun kendi dizininden çalışacak şekilde yazıldı.
**Öneri:** önce içe aktaran dosyanın dizini, sonra çalışma dizini.

## 7. Çoklu bildirim tek tip ister ve fonksiyonu yalnız bu dosyanın import'unda arar — DÜZELTİLDİ (TulparLang #485)

- `bool bas, px, py = isaretci();` (`isaretci(): (bool, float, float)`) üçünü de `bool` yapıyor; tip
  hatası "expected float, got bool" diye ATAMA satırlarında çıkıyor, bildirimde değil. Geçici çözüm:
  `(float, float, float)`, basılı = 1.0/0.0.
- `float sx, sy = ekrana(...)`: `ekrana` başka bir modülde (ana dosyanın import ettiği `kamera.tpr`) ise
  bu dosya onu KENDİSİ import etmedikçe "coklu bildirimin sag tarafi `: (T, T)` bildiren bir fonksiyonun
  dogrudan cagrisi olmali" — normal çağrı aynı fonksiyonu buluyor.

## 8. Geri tuşu etkinliği kapatıyor — DÜZELTİLDİ (`geri_basildi`)

`android_host.cpp` `on_input` yalnız dokunmayı işliyor, tuş olaylarına 0 dönüyor; sistem geri tuşunda
`NativeActivity`'yi bitiriyor (`DESTROY` → oyun `motor_kapat`). Oyuncu savaşın ortasında geri tuşuna
basınca savaş kaybolur (ölçüldü P20 Pro: `android cmd DESTROY` → `bitis: ekran 3` → yeni oturum menüden).
**Öneri:** `AKEYCODE_BACK` → köprünün tuş durumuna `"ESC"` (ya da `"GERI"`) + 1 dön; oyun ESC'yi zaten
duraklat/geri olarak işliyor. İki kez geri = çıkış kararı oyunun.

## 9. Arka planda ses sürüyor — DÜZELTİLDİ (motor durdurur)

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

## 11. Sabit gövdeyi taşımak gövdeyi yeniden kuruyor — DÜZELTİLDİ (#97)

`teng_set_pos` dinamik olmayan gövdede `remove` + `make_body` yapıyor (Jolt'ta sabit gövdeye konum
yazılabildiği hâlde). 60 birlik sabit gövdeyle sürülseydi saniyede ~3600 gövde kurma olurdu. Oyun
birlikleri **dinamik** gövdeyle (`yuru_v`) sürüyor, kafa/silahı **gövdesiz model** yapıyor
(oyunun model üretici betiği). **Öneri:** kinematik gövde türü (`eng_spawn_box(..., kinematik)`) ve
`MoveKinematic`.

## 12. Görüş açısı ve ekran→ışın API'si yok — DÜZELTİLDİ (#98)

Dokunuşla kule yuvası seçmek ve birlik üstü can çubuğu için oyun izdüşümü kendisi kuruyor; köprünün
`perspective(pi/3.5, …)` sabitini **kopyalamak** zorunda (`kamera.tpr` `FOV_TAN`). Sabit değişirse oyun
sessizce yanlış yere dokunur. Doğrulandı: otopilot yuvanın dünya konumunu ekrana izdüşürüp oradan ışın
atıyor; ışın yuvaya çarpmazsa kapı kırmızı (`kule_isin=2 kule_yakin=0 kule_iska=0`). **Öneri:**
`eng_camera_fov()` ya da `eng_screen_ray(sx, sy)` + `eng_world_to_screen`.

## 13. Tulpar oyununda AllocGate ölçülemiyor — DÜZELTİLDİ (#105)

`core/memory/alloc_gate_override.cpp` yalnız `engine_tests`/demo/editör yürütülebilirlerine bağlanıyor;
eklenti paketinin bildirimi onu linklemiyor. "Kare içi C++ ayırma 0" iddiası Tulpar oyununda ölçülemiyor.
Oyunun kapısı RSS eğimini ölçüyor (alt çeyrek 0 KB/1000 kare, 9600 kare; pozitif kontrol: 2 KB/kare
enjekte sızıntıda 2040 → KIRMIZI). **Öneri:** köprü kapanış raporuna kare içi `new` sayısı (override
eklenti paketinde, isteğe bağlı).

## 14. Kanca dağıtımı telefonda pahalı — İYİLEŞTİRİLDİ (#100)

Kapanış satırı (P20 Pro, 7 dk savaş): `kare icinde 101149 kanca cagrisi, 671.25 ms, cagri basina 6636.2 ns`
(kanca gövdeleri dahil; mermi çarpışma + enkaz güncelle + kale tetiği). Masaüstünde aynı oyun 397 ns.
Kare başına ortalama ~0.026 ms, yani bütçeyi tehdit etmiyor. Kısa (80 sn) oturumda ilk 8 çağrı 74 ms
ölçüldü (çağrı başına 9.3 ms) — ilk çağrılardaki tek seferlik bedel (sembol çözümü/sayfa hatası?)
ayrıştırılmadı; ilk karelerde takılma olarak hissedilebilir.

**#100 (Tuzaklar 8cv):** iki sebep ayrıştırıldı — soğuk önbellek (dağıtım sıcakken 31–60 ns/çağrı, kare
içinde vsync/çizim önbelleği boşaltıyor) ve `guncelle`nin her kare BÜTÜN varlık yuvalarını taraması (bağlı
olmayan yuva başına ~200 ns). Artık sıralı liste: telefonda kanca aşaması 85–105 → 32–45 µs/kare. Oyunun
kalemdeki bedeli zaten kare başına ~26 µs idi; oyun değişiklik gerektirmedi.

## 15. Düğme yazısı genişliğe göre ölçeklenmiyor — DÜZELTİLDİ (#105)

`ui_scale(h)` yalnız yükseklikten; "Savaş Arabası", "Kaya Yağmuru" 2240 genişlikte 6 düğmelik çubukta
düğmeden taştı (ekran görüntüsü). Oyun adları kısalttı ("Araba", "Kayalar"). **Öneri:** `min(yükseklik
ölçeği, genişlik / metin genişliği)`.

## 16. Tam ekran yarı saydam karartma Mali'de pahalı — DÜZELTİLDİ (#105 `sahne_karart`)

Ayarlar ekranı (tam ekran `RENK_KARART` + panel) p50 21.15 ms, menü (karartmasız) 17.6 ms — parlama
kapalı, ölçek 1.0, P20 Pro. Renderer'da `ui_fullscreen_blend_ratio` uyarısı var ama köprü/oyun onu
görmüyor. **Öneri:** köprü kapanış raporuna "tam ekran harmanlı dörtgen" sayacı.

## 17. Ekran çentiği alanı kullanılmıyor — DÜZELTİLDİ (TulparLang #486 + motor güvenli alanı)

Pencere 2159x1080, ekran 2240x1080: solda ~81 px siyah şerit. Manifest/tema
`android:windowLayoutInDisplayCutoutMode="shortEdges"` vermiyor (TulparLang'in ürettiği manifest).

## 18. `android cmd ?` — DÜZELTİLDİ (#102)

`cmd_name` bazı komutları tanımıyor (`APP_CMD_CONTENT_RECT_CHANGED`, `WINDOW_REDRAW_NEEDED`,
`SAVE_STATE`…): her açılışta 4-5 `android cmd ?` satırı.

## 19. Telefonda kayıt dosyası okunamıyor — DÜZELTİLDİ (debug derlemede adb'ye açık)

Kayıt çıkarma kökünde (`/data/user/0/<paket>/files/assets/tulpar_kayit.txt`); Huawei'de `run-as` çalışmadığı
için (Tuzaklar 8n) adb ile okunamıyor/yazılamıyor. Ayar testi ekrandan dokunarak yapıldı. **Öneri:** hata
ayıklama derlemesinde `TULPAR_ENGINE_SAVE` benzeri bir harici yol.

## 20. Font aralığı — DÜZELTİLDİ (#105 tipografik noktalama)

`content/font.hpp`: ASCII + Latin-1 + Türkçe (Ğğ İı Şş …). `—` (U+2014) `?` basıyor; Türkçe harflerin
hepsi çiziliyor (masaüstü DejaVu, telefon Roboto — ekran görüntüleriyle doğrulandı). Oyun metinleri
`—` kullanmıyor.

---

---

## Sonraki bulgular (2026-10-08 → 2026-10-23, oyunu "adam ederken" ve telefonda)

> 30, 33, 34 oyunun kendi tasarım hataları; ayrıntıları oyunun deposunda. Diğerleri motor/dil dersi.

### 21. Tanınmayan `tulpar.toml` anahtarı bütün dosyayı yok saydırıyor — DÜZELTİLDİ (TulparLang)

Oyunun `tulpar.toml`'una `[android] cutout = "short_edges"` (TulparLang #486'nın getirdiği anahtar) eklenince
kurulu tulpar v3.40.10: `[manifest] tulpar.toml: line 25: [android] key 'cutout' is not recognised (...) (ignoring)`
— ve **dosyanın tamamını** yok sayıyor: `[ext] paths` de gidiyor, `import "engine"` "su an hicbir eklenti yuklu degil"
diye düşüyor (asıl sebep görünmüyor). Android derlemesinde aynı yol `assets`/`icon`/`package`'ı da düşürür: oyun
varlıksız ve yanlış paket adıyla paketlenirdi. **Düzeltme (TulparLang):** bilinmeyen anahtar/bölüm UYARI + atla,
dosyanın gerisi geçerli (`tests/toml_taninmayan_anahtar.sh`, eski ikili kırmızı). Yeni toml anahtarları
(`internet`, `lan`) ancak bundan sonra güvenle eklenebildi.

### 22. `eng_render_scale` günlüğü "(kenetlendi)" — DÜZELTİLDİ (D)

`teng_render_scale(0.7)` kenetlenmediği hâlde `ic cozunurluk olcegi 0.70 (kenetlendi)` basıyordu: uygulanan ölçek
`float` (0.7f), istenen `double` (0.7) — `!=` her zaman doğru. Düzeltme: `fabs(a - b) > 1e-4`.

### 23. Telefon kopunca `adb` bekleyip kilidi tutuyor — DÜZELTİLDİ (D)

2026-10-08: telefon bağlantısı koptuğunda `adb install` "waiting for device" durumunda kaldı; betik telefon kilidini
(`flock`) tutarken bekliyordu, diğer işler kilitlendi. Oyunun betiği her oturumdan önce `timeout 5 adb get-state`
soruyordu; motorun ortak araçları (`tools/android_run.sh`, `android_yasam_dongusu.sh`, `android_kayit_dis.sh`) da
artık aynı denetimi yapıyor: bağlı cihaz yoksa beklemeden adıyla çıkıyor.

### 24. Süzgeçli test alt kümesinde sıra bağımlılığı — ÖLÇÜM (D)

Entegrasyon dalında (main + #105 + bir özellik dalı) `engine_tests bridge_` 23/24: `bridge_frame_allocs_are_counted_motor_hook_game`
ikinci `teng_init`'te düştü; önünde "yükleyici yok" testleri koşuyor. Test tek başına ve tam koşu (638/638) yeşil.
Muhtemelen 8bs sınıfı (aynı süreçte çok `VkInstance`); ayrıştırılmadı. **İkinci ölçüm (2026-10-26, RTX 5080,
upstream yeniden oynatma dalı):** `engine_tests bridge_` 31 koştu, **9 ATLANDI** (`vkCreateInstance
(VK_ERROR_INCOMPATIBLE_DRIVER)` — kurulum geri alındı, test "Vulkan cihazı/kurulum yok" diye atladı); aynı ikilinin
tam koşusu 652/652, 0 atlandı. Süzgeçli koşu "yeşil" görünür ama GPU'yu ölçmez — atlama sayacını oku. Açık iş:
süzgeçli alt kümeyi tam koşuyla aynı sonuca getirmek (test yalıtımı).

### 25. Mermi dost gövdeye çarpıyor — DÜZELTİLDİ (Y)

Kullanıcı: "bizim taraftaki karakterler tankların arkasından ateş ederken TANKLARA ateş ediyorlar; mermi
içlerinden geçip düşmana gitmiyor." Mermiler dinamik gövde; motorda çarpışma süzme yoktu, oyunun kancası
"dost" deyip hasarı yok sayıyordu ama fizik çarpmıştı (mermi orada sekip kalıyordu). **Motor:** 16 grup +
simetrik çift tablosu (`sim::Physics::set_collision_group` / `set_groups_collide`, Jolt `GroupFilter`; köprü
`eng_collision_group*` / `eng_collision_pair*`, Tulpar `carpisma_grubu` / `carpisma_cifti` / `carpisir_mi`).
Kapılar: `physics_collision_groups_skip_pairs_deterministic` (kapalı çift: küre kutunun içinden geçer, zemine
çarpar, iki koşum aynı özet; POZİTİF KONTROL açık çiftte kutuya çarpar), `bridge_collision_groups_pass_through_and_survive_rebuild`
(ışınlanıp yeniden kurulan dinamik gövde grubunu korur — grubu yeniden yazmayan köprüde kırmızı, ölçüldü).
Oyunda ölçüldü: dost değmesi 9600 karede 0 (tablo açıkken 111, RTX 5080); telefonda (P20 Pro, 8 dk) 866 mermide 0.
Motorun kendi kapısı aynı sayacı taşıyor: `engine_savas` `[kapi] fizik ... dost_degme=0`, pozitif kontrol
`TULPAR_ENGINE_SAVAS_DOST_SUZGEC=0` (gruplar atanır, tablo açık) -> > 0.

### 26. Devrilen gövde parçaların içine geçiyor — DÜZELTİLDİ (O)

Birliğin kafası/kalkanı gövdesiz model (`model_koy`), gövdeyle dönmez; Jolt kutuyu devirince parçalar gövdeye
girdi ("küpün içinde küp"). **Motor:** `donme_kilidi(id, true)` (Jolt `EAllowedDOFs` öteleme; kilitlenince dik
konum). Kapı: `physics_lock_rotation_keeps_body_upright` (üst kenarına ağır küre çarpan uzun kutu: kilitli
|w| 1.000000, 5.4 m itildi; POZİTİF KONTROL kilitsiz ~110° devrildi). **Yan etki (ölçüldü, oyunda):** dönemeyen
gövde navmesh yolunun keskin köşesine takılıp arkasındaki sırayı kilitleyebilir — yol köşelerini engelden açmak
ve mantık konumunu gövdeden kopunca bekletmek gerekti.

### 27. Ekranın kenarında model bozuluyor — DÜZELTİLDİ (Y, Tuzaklar 8cx)

Kullanıcı: "küreler bazı alanlarda küp gibi", "ışın kenarda duvara dönüşüyor", "kenardaki birliklerin rengi
değişiyor". İzole sonda: 1 × 0.12 × 0.12 sopa x = ±18 m'de 0.76 m KÜP olarak çiziliyordu; kamera kayınca kusur
nesneyle değil EKRAN KONUMUYLA gidiyordu. Sebep: LOD tutamaç dizisi sıfırla doldurulmuş, sıfır id GEÇERLİ
(renderer'ın ilk mesh'i); LOD'u üretilmemiş model 20 m'den ötede "LOD1" diye ilk mesh'le çizildi. Düzeltme +
kapı + pozitif kontrol: Tuzaklar 8cx; LOD seçiminin görüş derinliğiyle yapılması 8cy. Telefonda doğrulandı.

### 28. `A[min(i, j)]` / `A[max(i, j)]` dizini sessizce 0 okuyor — DÜZELTİLDİ (TulparLang)

Otopilotun ordu nüfusu her zaman 0 çıkıyordu: `n += NUFUS[min(SINIF_SAYISI - 1, sinif)]`.
Sonda (TulparLang v3.41.0-1-g760e9439, masaüstü AOT, 2026-10-09):

```
int[] A = [5, 6, 7, 8];  int x = 3;
A[min(2, x)] -> 0   A[max(2, 1)] -> 0   F[min(2, x)] (float[]) -> 0
int i = min(2, x); A[i] -> 7   A[abs(-2)] -> 7   A[toInt(2.0)] -> 7   A[iki()] -> 7
```
Hata yok, uyarı yok. **İkinci yüz (2026-10-16):** `min()` sonucu bir `int[]` ÖĞESİNE yazılınca o öğe de dizin
olarak 0 okutuyor (`G[0] = min(3, 0); A[G[0]] -> 0`; `toInt(min(3, 0))` doğru). Oyunda iki sessiz bozulma
veriyordu (bir bedel tablosu hep 0 okunuyordu; düzelince eski kayıt "uyuşmuyor" diye sıfırlanacaktı).

**Kök neden (2026-10-18):** `min`/`max` int argümanda da float döndürüyor (bilerek, typeinfer'de ölçülmüş imza).
Dizi okumasının satır içi hızlı yolu yalnız INT etiketli dizini alıyor; float dizin çalışma zamanının
`vm_get_element`'ine düşüyor, o da dizide int olmayan anahtarı "yok" sayıp **hatasız 0** döndürüyordu (json için
konmuş bir sessizlik). `int[]` öğesine yazma float'ı int'e çevirmiyor, öğe float tutuyor. **Düzeltme (TulparLang):**
dizi/dizgi hedefinde ondalık dizin sıfıra doğru kesiliyor (`int k = 2.7` ile aynı kural), okuma ve yazma tutarlı,
NaN/sonsuz dizin yakalanabilir hata; kapı `tests/dizin_ondalik.test.tpr`. Aynı sınıfın AOT yüzü (int hedefe giden
ondalık değer ham bit deseni: `return v % max(1, n)` = 4611686018427387904) ayrı düzeltildi
(`tests/int_hedef_ondalik.test.tpr`). `min`/`max` dönüş tipi değişmedi (`min(3, 5) / 2` hâlâ 1.5; ayrı karar:
`imin`/`imax` + typecheck uyarısı). `int[]` öğesine float yazmanın çevrilmemesi ayrı, açık bir tip deliği.

### 29. Ekran değişimi aynı karede yeni ekranı da çizdiriyor — AÇIK (desen)

Ekranlar ardışık `if`lerle çizilince: A'daki düğme `g_ekran = B` yapar, B aynı karede çizilir ve aynı tıklama
B'nin aynı yerdeki düğmesini de tetikler (oyunda ölçüldü 2026-10-09: menüye bir satır eklenince bir tıklama iki
ekranda iki düğmeye indi). Aynı karede iki ekranın aynı etiketli düğmesi ("Geri") köprüde çift kimlik HATA'sı da
veriyor. Desen: karenin başında çizilecek ekranı sabitle, karede **tek** ekran çiz; otopilot yalnız o karede
**çizilmiş** widget'a tıklasın. `engine_savas.tpr` ilk denemesinde de aynısı oldu ("Ayarlar" -> aynı karede
"Geri") ve bu desenle yazıldı. Motor tarafı öneri: `eng_ui_click_once` (karede ilk isabet tıklamayı tüketir).

### 31. Sanal çubuğun eylemi bırakışta ve her yerde — AÇIK (O)

Kullanıcı telefonda (P20 Pro, 2026-10-13) doğrudan kontrol edilen birlikle vurmanın çok zor olduğunu söyledi. Motor
yanı: `eng_stick_action` sağ yarıya < 250 ms dokunuşu **bırakınca** sayıyor (basılı tutma yok), yetenek düğmesine
basmak da saldırı sayılıyor, çubuk ekranda görünmüyor (yalnız ipucu). Oyun kendi görünen çubuğunu + "Vur"
düğmesini çizip parmakları kimliğiyle (`eng_touch_id`) izleyerek çözdü. **Öneri:** `eng_stick_visible(on)`,
`eng_stick_exclude(i, x, y, w, h)` (düğme alanı saldırı sayılmaz), basılı-tut kipi.

### 32. Varlık ekranda yok ama sayılarda var — DÜZELTİLDİ (Tuzaklar 8df)

Kullanıcı telefonda büyük düşmanları göremedi: can çubuğu azalıyor, cephede havada yalnız bir can çubuğu. Sebep oyunda:
görsel 0.1 × boy ölçekle kurulup doğuş büyümesi bir fonksiyonda koşuyordu; bir tür o fonksiyonu atladığı için savaş
boyunca 20 kat küçük çizildi. Motor dersi: "çizildi" sayacı ve mantık durumu doğruyken görüntü yanlış olabilir;
köprü artık çizilen dikey boyu veriyor (`eng_drawn_height` = ölçek × model sınır kutusu), oyunun kapısı en küçük
çizilen boy / mantık boyu ≥ 1.0 ister (pozitif kontrol: büyütme kapalı -> < 0.5). Ayrıntı Tuzaklar 8df.
