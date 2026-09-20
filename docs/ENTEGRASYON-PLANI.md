# Entegrasyon Planı — ne bitmedi, neden önemli, hangi sırada

> Kardeş belgeler: [DURUM.md](DURUM.md) ("ne var, ne ölçüldü"), [TUZAKLAR.md](TUZAKLAR.md)
> ("bir şey kırıldığında ilk bakılacak yer"), [PLAN.md](PLAN.md) (faz yol haritası).
> Bu belge üçünün arasındaki boşluğu doldurur: **açık kalan işlerin kanıtlı, sıralı ve
> ölçülebilir listesi.** Her maddenin bir kimliği (`EP-nn`) vardır; commit ve issue
> başlıklarında o kimlikle atıf yapın.
>
> Son ölçüm turu: **2026-09-20**, `main` = `0898be4`.

---

## 0. Bu planın kuralları

1. **Özellik SİLİNMEZ.** Plan toplamalıdır: var olanın üstüne kurar. Bir maddenin daha
   iyi çözümü bir şeyin yerine geçmekse, bu **açıkça yazılır ve gerekçelendirilir** —
   sessizce varsayılmaz. Bu belgede hiçbir madde bir özelliğin, kapının ya da aracın
   kaldırılmasını önermiyor. Üç yerde **yedek/ikinci yol** eklenmesi öneriliyor
   (EP-01 SHA-256 tazelik izi, EP-02 derlemeye bağlanan ikinci koşum, EP-08 eski
   katman ayar mekanizması) — üçünde de mevcut yol **yerinde kalır**.
2. **Koşmayan kapı yeşil değildir.** "493 passed" cümlesi, atlanan kapı sayısı
   okunmadan hiçbir şey söylemez. Bu belgedeki en pahalı madde sınıfı (S) tam olarak
   budur: yeşil görünen ama hiçbir şey ölçmeyen kapı.
3. **Pozitif kontrol şart.** "Artık çökmüyor / artık doğru" cümlesi, hatayı **kasıtlı
   geri koyup** kapının kırmızı döndüğü görülmeden ölçülmemiş bir iddiadır
   (Tuzaklar 8bd, 8bt).
4. **Doğrulamak için pencere açılmaz.** Masaüstünde `--headless N --out x.ppm`,
   telefonda `adb screencap`. Pencereli yolu yalnız kullanıcı çalıştırır.
5. **Sayı yoksa iddia yok.** Aşağıdaki her madde ya bir ölçümle ya da "bu
   **doğrulanmadı**" cümlesiyle gelir. §8 doğrulanamayanların tam listesidir.
6. **Her maddenin bir "ilk somut adım"ı ve bir "bitti ölçütü" vardır.** Ölçütü
   yazılamayan madde bu belgeye girmez; dilek listesi değil iş listesidir.

### Madde sınıfları

| sınıf | anlamı | neden bu sıra |
|---|---|---|
| **S** | *Sessizce yanlış* — bir şey yeşil görünüyor ama ölçmüyor ya da yanlış ölçüyor | Bu deponun en pahalı hata sınıfı. Üstüne kurulan her "yeşil" o yalanı miras alır |
| **E** | *Eksik ölçüm* — kapı var, doğru şeyi ölçüyor, ama bu ortamda koşmuyor | Yanlış değil ama körüz; ne zaman kör olduğumuz bilinmeli |
| **Y** | *Eksik özellik/donanım* — yapılmadı ya da cihaz yok | Dürüst eksik; sıra planla belirlenir |
| **N** | *Hijyen* — belge kayması, ölü yol, eskiyen sayı | Ucuz, ama okuyanı yanlış yere götürür |

---

## 1. Ölçüm künyesi

Bu belgedeki her sayı aşağıdaki iki kaynaktan birinden gelir. Başka makinede
tekrarlanırsa sonuç değişebilir; o yüzden makine de yazılıdır.

**(A) Yerel masaüstü** — Linux (CachyOS), RTX 5080 / NVIDIA 615.71.09,
Vulkan yükleyici 1.4.357, `VK_LAYER_KHRONOS_validation` **1.4.357**, glslc 2026.3,
GCC 16.2.1, 16 çekirdek. Komutlar:

```bash
cmake -S . -B yapi -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build yapi -j16
DISPLAY= ./yapi/engine_tests            # -> 493 passed, 0 failed, 0 atlandi, cikis 0
python3 tools/layer_check.py .          # -> 354 dosya, 0 ihlal
python3 tools/layout_check.py .         # -> KIRMIZI (bkz. EP-01/EP-02)
python3 tools/scene_check.py .          # -> 0 uyusmazlik
python3 tools/icon_check.py .           # -> 0 eksik glif
```

**(B) Gerçek CI koşumu** — `main` üzerindeki `0898be4` push'u, GitHub Actions run
**35506012140** (2026-09-20, başarılı). Üç ayak, `gh run view … --log` ile okundu:

| ayak | ortam | özet satırı |
|---|---|---|
| Linux x86_64 | Ubuntu 24.04, lavapipe (llvmpipe LLVM 20.1.2), VVL **1.3.275** (apt) | `493 passed, 0 failed, **7 atlandi**` |
| macOS arm64 | macos-latest, MoltenVK / "Apple Paravirtual device", VVL **1.4.357** (brew) | `493 passed, 0 failed, **38 atlandi**` |
| Windows x86_64 | MSYS2 MINGW64, yazılım ICD'si yok | `493 passed, 0 failed, **92 atlandi**` |

Aynı ağaç, aynı ikili sayısı, **üç ayrı ölçüm gerçekliği.** Bu tablo bu belgenin
omurgasıdır: "493 passed" üç ayakta da aynı, ölçülen şey aynı değil.

---

## 2. Şu anda başkasının elinde (bu planın dışında)

İki iş **devam ediyor**; bu plan onları açık iş saymaz ve dosyalarına dokunmaz.

| iş | dokunduğu yer | bu planla kesişimi |
|---|---|---|
| **Windows sürüm paketleme** | `tools/package.sh`, `.github/**` | EP-02 ve EP-08'in "CI'a bağla" adımları `.github/**`'a dokunur. **Önce CMake üstünden bağlayın** (§EP-02); workflow'a dokunmak gerekirse bu iş bitene kadar bekleyin. |
| **Godray geçişinin aktive edilmesi** | `renderer/**` | EP-01'in düzeltmesi `rhi/shaders/*_spv.h`'yi yeniden üretir; **`godray_frag_spv.h` ve `compose_frag_spv.h` de o kümede.** Aynı dosyaları iki taraftan yazmayın — EP-01'i o iş birleşince koşun (§EP-01 "kapsam notu"). |

Godray geçişinin bugünkü durumu (yalnız bilgi, iş değil): `GraphDesc::godray`
kodun hiçbir yerinde `true` yapılmıyor; `renderer/graph.hpp`'de
`kMaxGraphPasses = 16` ve `test_render_graph` godraysız en geniş tablonun **tam 16**
olduğunu iddia ediyor — yani kapasite artmadan geçiş bağlanamaz. Bu not
`0898be4`'ün commit gövdesinde de var.

---

## 3. Özet tablo

| id | sınıf | başlık | bugünkü kanıt |
|---|:--:|---|---|
| [EP-01](#ep-01) | **S** | Depodaki SPIR-V 3 shader'da bayat; `compose.frag`'ın FSR1/RCAS dalı ikilide **yok** | 23 shader'ın 3'ü kaynakla eşleşmiyor; `compose_frag_spv.h` GLSL'inden 3 commit eski |
| [EP-02](#ep-02) | **S** | `layout_check.py` bugün **KIRMIZI** ve hiçbir otomasyon onu koşmuyor | yerel `rc=1`; CMake'te ve CI'da hiç geçmiyor |
| [EP-03](#ep-03) | **S** | Statik TLS tavanı bütün testlerce paylaşılıyor; `pin_icd_once()` yalnız editör sondasında ve **geç** | tam koşumda 21 ICD `dlopen`; tavanı yarıya indirince **67 kapı sessizce atlanıyor, çıkış yine 0** |
| [EP-04](#ep-04) | **S** | "Vulkan cihazı yok" yalanı 10 dosya / 56 çağrı yerinde duruyor | `NoVulkan/NoDevice/Exhausted` ayrımı yalnız `editor_probe.cpp`'de |
| [EP-05](#ep-05) | **S** | `.sahne` → `.sahneb` alan kaybı: camera/audio/script blob'a hiç girmiyor | `scene.hpp`'de `cam_*`, `audio_*`, `script_*` var; `scene_blob.hpp`'de yok |
| [EP-06](#ep-06) | **S** | Köprü (`tulpar/`) hiçbir otomasyonda derlenmiyor, hiçbir testi koşmuyor | `engine_bindings.cpp` masaüstü derleme grafiğinde yok; 17 `.tpr` testi koşan yok |
| [EP-07](#ep-07) | **S** | Araç düzeyinde yalan yeşil: `faz8_shader_audit.py` "TAM koşmadı" deyip çıkış 0 | rc=0, "2 adım ATLANDI" |
| [EP-08](#ep-08) | **E** | Mali/Arm BestPractices dört kapısı CI'da hiç ölçmüyor | Linux ayağında 7 atlamanın **4'ü** bu |
| [EP-09](#ep-09) | **E** | macOS'ta loader atlanıyor; **kök sebep hâlâ basılmıyor** | aynı runner'da `vulkaninfo` loader üstünden cihazı **görüyor** |
| [EP-10](#ep-10) | **E** | Hangi ayakta neyi ölçtüğümüzün haritası yok | macOS 38, Windows 92 atlama; hiçbiri sınıflandırılmıyor |
| [EP-11](#ep-11) | **E** | `engine_editor --headless` / `engine_demo --headless` hiçbir CI ayağında koşmuyor | 8br'nin ikili düzeyindeki kapısı ölü |
| [EP-12](#ep-12) | **Y** | Üç fiziksel cihaz yok → Faz 1/3/5/7 kapıları kapanamıyor; telefon koşumu 141 test dönemine ait | bugün 493 test |
| [EP-13](#ep-13) | **Y** | Editör bileşen matrisinin kalanı | `EDITOR-DURUM.md` §1 |
| [EP-14](#ep-14) | **Y** | Faz 8 kalanı, Faz 9 VSM, Faz 10 Metal | `PLAN.md` / `DURUM.md` §6 |
| [EP-15](#ep-15) | **N** | Belge kayması (README, DURUM, DEVAM_PLANI, araç varsayılan yolları) | README "66 tuzak (8a–8ap)", gerçek 73 (8a–8bu) |
| [EP-16](#ep-16) | **N** | `Registry::kMax` başlığı — **bugün sağlıklı**, izlemede | 493 / 640 |

---

## 4. S sınıfı — sessizce yanlış

Bu bölümün ortak paydası: **bir şey yeşil, ama ölçtüğünü sandığımız şeyi ölçmüyor.**

<a id="ep-01"></a>
### EP-01 — Depodaki SPIR-V bayat; `compose.frag`'ın FSR1/RCAS dalı ikilide yok

**Belirti.** `rhi/shaders/*_spv.h` dosyaları depoya girer (CI'da glslc gerekmesin
diye, bilinçli karar — `tools/compile_shaders.py` başlığı). Ama GLSL kaynağı
değiştiğinde onları yeniden üretmeyi **hiçbir şey zorlamıyor**: ne CMake, ne CI,
ne bir test. Sonuç: kaynakta duran kod ikilide yok.

**Kanıt (ölçüldü, yerel — A).**

1. `tools/compile_shaders.py` bu makinenin glslc'siyle (2026.3 / SPIR-V 1.4.357)
   yeniden koşuldu: **23 shader'ın 20'si depodaki başlıkla BAYT AYNI** çıktı —
   yani araç zinciri depodakiyle uyuşuyor. Uyuşmayan üçü: `compose.frag`,
   `godray.frag`, `mesh.frag`.
2. `compose.frag` en son `465acc9`'da değişti; `compose_frag_spv.h` en son
   **üç commit önce**, `e239a1f`'te. Yani kaynak, türevinden sonra düzenlenmiş.
3. `465acc9`'ın `compose.frag` farkı **yeni bir dal ekliyor**: `kind == 3` →
   `fsr1_rcas(uv)` (AMD FSR1 / RCAS keskinleştirme). `renderer/renderer.hpp`
   bunu API olarak da yayınlıyor: `UpscalerKind::FSR = 3`.
4. Depodaki SPIR-V bu dalı **içermiyor.** Ayrıştırıldı (`spirv-dis`): RCAS'ın
   getirdiği `1e-5` sabiti (`OpConstant %float 9.99999975e-06`) yeniden
   üretilende **var**, depodakinde **yok**. Komut çıktısı birebir:

   ```
   == depo == (sabit yok)
   == yeni == %float_9_99999975en06 = OpConstant %float 9.99999975e-06
   ```

   Yani bugün `UpscalerKind::FSR` seçilirse shader `else` dalına düşer ve
   **sessizce bilinear** çalışır: hata yok, uyarı yok, yalnız görüntü farkı.
5. `tools/layout_check.py` bunu **zaten kendisi söylüyor** — ama kimse koşmuyor
   (EP-02): `glslc tazelik: 23 shader, 3 bayat`.

**Neden pahalı.** Bu, Tuzaklar **8am**'in ("türetilmiş dosya bayatlayınca kapı
sessizce atlanır") tam olarak gerçekleşmiş hâli, ve bu sefer kapının kendisini de
bozuyor: bayat `godray_frag_spv.h` yüzünden yerleşim denetimi bir bloğu
adlandıramıyor ve o shader'ı **kapsam dışında bırakıyor** — üç başlık yeniden
üretildiğinde kapsam **39 blok örneğinden 40'a** çıktı (ölçüldü, aşağıda).

**İlk somut adım.**
```bash
python3 tools/compile_shaders.py      # 3 basligi yeniden uretir
python3 tools/layout_check.py .       # -> "glslc tazelik: 23 shader, 0 bayat", YESIL
DISPLAY= ./yapi/engine_tests          # regresyon
```
Ölçüldü: bu üç başlık değiştirilmiş bir kopyada `layout_check.py` **rc=0**,
`SONUC: 40 blok ornegi, …, 0 UYUSMAZLIK`, `glslc tazelik: 23 shader, 0 bayat`.

**Bitti ölçütü.** (a) `layout_check.py` tazelik bölümü 0 bayat diyor;
(b) bayatlığı **kapı yakalıyor** — yani bir `.frag` dosyasına boşluk ekleyip
başlığı yeniden üretmeden koşulan denetim KIRMIZI dönüyor (pozitif kontrol);
(c) FSR yolunun bir piksel kapısı var ya da yokluğu yazılı.

**Kapsam notu (silme yok).** Depoya SPIR-V girme kararı **korunuyor** — önerilen
şey o kararı kaldırmak değil, kararın gerektirdiği kapıyı eklemek. Kalıcı çözüm
için ucuz ve araç-bağımsız bir yol var: `compile_shaders.py` üretilen başlığın
yorum satırına **kaynak GLSL'in SHA-256'sını** yazsın; kapı da yalnız o özeti
karşılaştırsın. Böylece tazelik denetimi **glslc olmadan da** koşar (bugünkü
`freshness()` glslc yoksa görünür atlıyor — yani CI'da hiç ölçmez).
**Çakışma:** `godray_frag_spv.h` ve `compose_frag_spv.h`, devam eden godray işinin
dosyalarıyla komşu. Yeniden üretimi o iş birleştikten sonra, tek commit'te koşun.

---

<a id="ep-02"></a>
### EP-02 — `layout_check.py` bugün KIRMIZI ve hiçbir otomasyon onu koşmuyor

**Belirti.** CPU struct'ı ile GPU uniform/push bloğunun yerleşimini alan alan
karşılaştıran denetim (`tools/layout_check.py`, kardeşi `layout_audit.py`)
**var, çalışıyor, gerçek bir şey ölçüyor** — ama ne `CMakeLists.txt`'te bir
hedefin ön koşulu, ne de `.github/workflows/*.yml`'de bir adım. `layer_check.py`
(`engine_core`'un ön koşulu), `scene_check.py` (`engine_content`) ve
`icon_check.py` (`engine_editor`) bağlı; bu ikisi değil.

**Kanıt (ölçüldü — A).**
```
$ python3 tools/layout_check.py .   -> rc=1
KIRMIZI ayristirma: godray.frag blok Push: 'texel' yolu adlandirilamadi
SONUC: 39 blok ornegi, 21 grup, 12 blok<->struct cifti, 7 skaler dizi,
       2 kapsam disi, 1 UYUSMAZLIK
  KIRMIZI compose.frag / godray.frag / mesh.frag: depodaki *_spv.h GLSL
          kaynagiyla ayni degil (BAYAT)
YERLESIM DENETIMI: KIRMIZI
```
`layout_audit.py` ayrıca **kendi pozitif kontrollerini koşuyor ve ikisi de
ateşliyor**: bir alanı 4 bayt kaydırınca ve iki alanın sırasını değiştirince
kapı kırmızı dönüyor (`pozitif kontroller: 2/2 KIRMIZI`). İkinci kontrolün
notu ayrıca kıymetli: *"bozuk tanımda `static_assert(sizeof(MaterialUbo)==64)`
HÂLÂ GEÇİYOR"* — yani bu denetim `static_assert`'ün **göremediği** bir hata
sınıfını ölçüyor.

**Neden pahalı.** CPU-GPU yerleşim kayması sessiz bozulmanın ders kitabı örneği:
derleme geçer, doğrulama katmanı susar, ekranda yalnız "bir şey yanlış görünüyor"
olur. Denetim var ve bugün kırmızı; ölçmediğimiz için haberimiz yok.

**İlk somut adım.** Önce EP-01'i koşun (üç bayat başlık kırmızının bir
bölümünü üretiyor — kalanı da o). Sonra `CMakeLists.txt`'te
`engine_layer_check` / `engine_scene_check` ile aynı kalıpta bir
`engine_layout_check` hedefi ekleyip `engine_renderer`'a `add_dependencies` ile
bağlayın.

**Neden CMake, neden `.github` değil:** katman ve sahne denetimleri zaten böyle
bağlı ve **üç CI ayağında da** derlemenin parçası olarak koşuyorlar; workflow'a
tek satır eklemeye gerek kalmıyor. Bu, devam eden Windows paketleme işiyle
çakışmamanın da yolu (§2).

**Bitti ölçütü.** (a) `cmake --build` yerleşim ihlalinde **derlemeyi kırıyor**;
(b) pozitif kontrol: bir alanı bilerek kaydırınca derleme kırmızı, geri alınca
yeşil; (c) glslc'siz ortamda tazelik bölümü **görünür atlıyor** ya da
EP-01'in SHA-256 yolu sayesinde yine ölçüyor.

**Kapsam notu (silme yok).** `layout_audit.py` (ayrıntılı, pozitif kontrollü,
elle koşulan rapor) **kalır**; derlemeye bağlanan `layout_check.py`'dir. İki araç
bilinçli olarak iki farklı iş yapıyor.

---

<a id="ep-03"></a>
### EP-03 — Statik TLS tavanı bütün testlerce paylaşılıyor; ICD sabitleme yalnız editör sondasında

**Belirti.** Tuzaklar **8bs**: aynı süreçte tekrar tekrar `VkInstance` açmak,
NVIDIA ICD'sinin `libnvidia-tls.so`'su üzerinden glibc'nin **sabit** static TLS
fazlasını tüketir; tükendiğinde `vkCreateInstance` `VK_ERROR_INCOMPATIBLE_DRIVER`
döner — adı "sürücü yok" diyen bir hata, çalışan bir GPU'nun üstünde. Düzeltme
(`pin_icd_once()`) yalnız `tests/editor_probe.cpp`'de ve ancak **ilk editör
sondası koştuğunda** devreye giriyor. Bütçe ise süreç genelinde ortak.

**Kanıt (ölçüldü — A).** `LD_AUDIT` ile her `dlopen` sayıldı:

| koşum | ICD (`libGLX_nvidia.so.0`) yükleme sayısı |
|---|---|
| `engine_tests` (tam) | **21** |
| `engine_tests editor` (süzgeçli) | **4** — sabitleme çalışıyor |

Tam koşumdaki 21 yükleme **koşumun sonuna kadar** dağılmış durumda; yani editör
sondası bütçeyi sabitlemeden önce de sonra da başka testler ICD'yi açıp kapıyor.

Tavana ne kadar yakın olduğumuz, bütçeyi küçülterek ölçüldü
(`GLIBC_TUNABLES=glibc.rtld.optional_static_tls=N`, varsayılan **512**):

| N | özet satırı | çıkış |
|---|---|:--:|
| 512 / 256 / 224 / 192 / 160 | `493 passed, 0 failed, **0 atlandi**` | 0 |
| **128** | `493 passed, 0 failed, **67 atlandi**` | **0** |
| 64 / 32 | `493 passed, 0 failed, 68 atlandi` | 0 |

**128'deki 67 atlamanın dökümü** — çalışan bir RTX 5080'in üstünde:
```
26  ATLANDI: Vulkan cihazi acilamadi (yukleyici var): dev.init (#N sonda): ...
23  ATLANDI: Vulkan cihazi yok
 6  ATLANDI: Vulkan cihazi yok (paylasilan PBR cihazi)
 6  ATLANDI: Vulkan cihazi acilamadi (yukleyici var): chrome_stopped: ...
 3  ATLANDI: Vulkan loader/cihazi yok
 …
```
**Ve çıkış kodu yine 0.** Yerel koşumda bunu kırmızıya çeviren hiçbir kapı yok.

Burada asıl bulgu, 8bt'nin `Exhausted` ayrımının **bu senaryoda ateşlemediği**:
editör sondası, bütçe ondan *önce* tükendiği için hiçbir zaman başarılı olmuyor,
`g_device_ever_ok` false kalıyor ve durum `NoDevice` = **görünür atlama** olarak
raporlanıyor. Yani 8bt'nin düzeltmesi, tavanı **sondanın kendisi** harcadığında
koruyor; başkası harcadığında korumuyor.

**Neden pahalı.** Bu sınıf tam olarak geliştirici makinesinde (NVIDIA ICD) olur ve
tam olarak orada kapı yoktur. CI Linux ayağında bir ağ var — `ATLANDI:.*Vulkan`
görürse iş kırmızı — ama lavapipe initial-exec TLS baskısı yapmadığı için CI bu
sınıfı hiç görmez. Yani: **hatanın olduğu yerde kapı yok, kapının olduğu yerde
hata yok.**

**İlk somut adım (üçü de toplayıcı, hiçbir şey silmiyor).**
1. `pin_icd_once()`'ı `editor_probe.cpp`'den ortak bir test yardımcısına taşıyın
   ve **ilk başarılı `vk_api_load()`'da** çağırın (ör. `test.hpp`/`test_main.cpp`
   tarafında bir `test::vk_pin_icd()`). Editör sondasındaki çağrı kalabilir;
   `static bool tried` zaten ikinci çağrıyı yutuyor.
2. `g_device_ever_ok` bayrağını **süreç geneline** çıkarın: "bu süreçte bir kez
   cihaz açıldı, şimdi açılmıyor" bilgisi tek yerde tutulsun ve `editor_probe`
   dışındaki çağrı yerleri de onu okuyabilsin (EP-04 ile aynı yardımcı).
3. `engine_tests` özet satırına **ICD yükleme sayısını** (ya da açılan
   `VkInstance` sayısını) bilgi olarak ekleyin — sayı görünmeyen bir tavan
   izlenemez.

**Bitti ölçütü.** `GLIBC_TUNABLES=glibc.rtld.optional_static_tls=128` ile koşulan
tam paket **kırmızı** dönüyor (çıkış 1) ve sebebini yazıyor; varsayılan bütçede
hâlâ `0 atlandi`. Bu tek komut kalıcı pozitif kontroldür — `TULPAR_ENGINE_PROBE_LIMIT`
gibi belgeye girsin.

---

<a id="ep-04"></a>
### EP-04 — "Vulkan cihazı yok" yalanı 10 dosya / 56 çağrı yerinde duruyor

**Belirti.** `editor_probe` için ayrıştırılan dört durum
(`NoVulkan` / `NoDevice` / `Exhausted` / `Fail`) yalnız orada var. Geri kalan
testler hâlâ `if (!dev.init(...)) skip("Vulkan cihazi yok")` yazıyor; yani
**ortam eksiği ile süreç içi tavanı aynı cümleye** sığdırıyorlar.

**Kanıt (ölçüldü — A).** `tests/*.cpp` içinde "cihaz açılamadı"yı "Vulkan cihazı
yok" diye raporlayan **56 çağrı yeri, 10 dosya**:
`test_rhi.cpp`, `test_temporal.cpp`, `test_pack.cpp`, `test_renderer.cpp`,
`test_render_graph.cpp`, `test_content.cpp`, `test_scene_blob.cpp`,
`test_editor.cpp`, `test_editor_overlay.cpp`, `test_bridge.cpp`.
(Görev tanımında üç dosya adı geçiyordu; ölçüm **on** dosya diyor.)

EP-03'ün 128 deneyinde bu cümlelerin **23 + 6 + 3 = 32 tanesi** doğrudan yalan
söyledi: makinede çalışan bir GPU vardı.

Not: `test_pack.cpp`'nin GPU bölümü `if/else` ile yazılı, yani 8bt'deki
"skip ettikten sonra devam etme" kusuru **orada yok**; sorun kontrol akışı değil,
**mesajın kendisi**.

**Neden pahalı.** Bu, 8bt'nin bulduğu sınıfın ta kendisi: *gerçek bir hata, iyi
huylu bir ortam atlaması gibi görünüyor.* Bir kez "ortam eksiği" diye
sınıflandırıldığında kimse bakmaz.

**İlk somut adım.** `editor_probe.hpp`'deki kalıbı **ortak bir yardımcıya**
yükseltin — silmeden, toplayarak:
```cpp
// tests/vk_probe.hpp (yeni) — editor_probe bu yardimciyi KULLANIR, yerine gecmez
enum class VkOpen { Ok, NoLoader, NoDevice, Exhausted };
VkOpen vk_open_device(rhi::Device&, SystemArena&, const rhi::DeviceConfig&);
bool   vk_not_ok(VkOpen, const char* err, const char* file, int line);  // Exhausted -> KIRMIZI
```
56 çağrı yerini mekanik olarak bu yardımcıya çevirin (dosya dosya, her dosya ayrı
commit `EP-04/<dosya>` — 8bk'nın "aynı sınıf kontrol üç yere kopyalanınca koruması
geride kalır" dersi tam olarak bunu söylüyor).

**Bitti ölçütü.** (a) `grep -c 'skip("Vulkan cihazi yok' tests/*.cpp` = 0;
(b) EP-03'ün 128 deneyi kırmızı dönüyor; (c) `TULPAR_ENGINE_NO_VULKAN=1` hâlâ
**görünür atlama** üretiyor (yanlış yöne kaymadığımızın kontrolü).

---

<a id="ep-05"></a>
### EP-05 — `.sahne` → `.sahneb` alan kaybı: camera/audio/script blob'a hiç girmiyor

**Belirti.** `465acc9`/`0898be4` turunda **21 yeni alanın** hiç serileştirilmediği
elle bulundu ("editörde değer gir, kaydet, aç: gitmiş"). Aynı sınıfın bir katmanı
daha duruyor ve kapısı **hâlâ yok**: alan `.sahne` metnine yazılıyor ama
**derlenmiş blob'a** (`.sahneb` — oyunun gerçekten yüklediği şey) girmiyor.

**Kanıt (ölçüldü — A).**
* `content/scene.hpp`: `cam_fov`, `cam_near`, `cam_far`, `audio_clip`,
  `audio_volume`, `audio_pitch`, `audio_loop`, `audio_spatial`, `script_file`,
  `script_enabled` — hepsi `SceneEntity` alanı.
* `content/scene_blob.hpp`: `camera`, `audio`, `script` diye **hiçbir şey yok**
  (grep 0 sonuç).
* `EDITOR-DURUM.md` §1 bileşen matrisi bunu zaten yazıyor: Camera → *"editörde
  çalışır, oyunda kaybolur"*, Audio → *"hiçbir yerde ses çalmaz"*, Script →
  *"`.tpr` hiçbir yorumlayıcıya gitmez"*, Character → *"blob'a yazılır, okunmaz"*.
* Kapı neden yakalamıyor: `tools/scene_check.py` kendi başlığında amacını yazıyor —
  *"yazıcı ile ayrıştırıcı AYNI dili konuşsun"*. Yani **yalnız `.sahne` metin
  biçimini** ölçüyor; `.sahne → .sahneb → runtime` zinciri kapsamı dışında.

**Neden pahalı.** Editörde ayarlanan bir değerin "Derle"de sessizce düşmesi,
kullanıcının hatayı **oyunda** bulduğu ve motoru suçladığı sınıftır. 21 alanlık
turda bu elle yakalandı; kapı olmadığı için bir sonraki sefer yakalanmayacak.

**İlk somut adım.** `scene_check.py`'nin kardeşi olarak **üçüncü halka kapısı**:
`SceneEntity`'nin her alanı için ya blob'da bir karşılık ya da
"bilerek blob'a girmiyor" diyen **açık bir muafiyet listesi** olsun
(`scene_check.py`'deki `BRANCHY` muafiyetiyle aynı disiplin: muafiyet eklemek,
sayıyı değiştirmekten farklı bir şeydir).

**Bitti ölçütü.** (a) Kapı, blob'da karşılığı olmayan ve muaf da olmayan her alan
için kırmızı; (b) pozitif kontrol: `scene_blob.hpp`'den bir alan silinince kapı
kırmızı; (c) camera/audio/script ya blob'a girer ya muafiyet listesinde
**gerekçesiyle** yazılıdır.

**Kapsam notu (silme yok).** `.sahneb` biçimi bugün **sürüm 4**. Alan eklemek
sürümü 5'e çıkarır; **4 okuyucusu kalır** (mevcut `.sahneb` dosyaları çalışmaya
devam eder). Bu ekleme, değiştirme değil.

---

<a id="ep-06"></a>
### EP-06 — Köprü (`tulpar/`) hiçbir otomasyonda derlenmiyor, hiçbir testi koşmuyor

**Belirti.** Depo ayrımından (2026-09-20) sonra köprünün Tulpar tarafı bu depoya
geldi ama **onu koşturan otomasyon gelmedi.** Eskiden dil deposunun
`build.sh suites`'i `tests/engine_bridge.test.tpr`'yi koşuyordu; burada öyle bir
koşucu yok.

**Kanıt (ölçüldü — A).**
1. `tulpar/generated/engine_bindings.cpp` **masaüstü derleme grafiğinde yok**:
   `grep engine_bindings yapi/build.ninja` → 0 sonuç. CMake onu yalnız
   `tulpar_engine_android` hedefinde, `TULPAR_ROOT` verildiğinde derliyor;
   CI'da hiçbir ayak vermiyor. Yani **175 builtin'in bağlama dosyası hiçbir CI
   ayağında derlenmiyor.**
2. `tulpar/tests/engine_bridge.test.tpr` (17 test, `KOPRU.md` §"Tulpar kapısı")
   hiçbir betikten çağrılmıyor: `grep -rn engine_bridge.test .` yalnız **belgelerde**
   geçiyor.
3. C++ tarafındaki köprü kapısı **tek test**:
   `ENGINE_TEST(bridge_runs_a_scripted_game_headless)` (`tests/test_bridge.cpp`).
4. Üretilmiş dosyaların tazeliği için **kapı yok** — ama bugün **tazeler**
   (ölçüldü): `python3 tools/gen_engine_bindings.py <gecici-kok>` ile yeniden
   üretilen dört dosya, depodakilerle **bayt eşit**. `SPEC` = **175** giriş ve
   hepsinin `bridge/engine_api.h`'de `teng_*` karşılığı var (fark kümesi boş).

**Neden pahalı.** `SPEC`'e bir satır eklemek dört dosya üretir; üretmeyi unutmak
ya da üretileni derlenemez hâle getirmek bugün **hiçbir yerde** görünmez. Kayma
cihazda, `UnsatisfiedLinkError` ya da sessiz yanlış çağrı olarak çıkar
(CLAUDE.md'nin "5 noktada bağlama" uyarısının mekanikleştirilmiş hâli tam da bunu
önlemek içindi).

**İlk somut adım (iki küçük kapı, ikisi de toplayıcı).**
1. **Tazelik kapısı:** `gen_engine_bindings.py`'ye `--check` ekleyin (geçici
   dizine üret, depodakiyle bayt karşılaştır, fark varsa çıkış 1) ve CMake'te
   `engine_bridge`'in ön koşulu yapın — `layer_check` kalıbı.
2. **Derlenebilirlik kapısı:** `engine_bindings.cpp`'yi **sözdizimi düzeyinde**
   denetleyin. `TULPAR_ROOT` yoksa bugün de olduğu gibi **görünür atlansın**
   (sessiz geçmesin); `TULPAR_ROOT` verilen bir koşumda derlensin.
   `tools/clang_syntax_check.sh` zaten bu iş için var.

**Bitti ölçütü.** (a) `SPEC`'e satır ekleyip üretmeden derleme yapınca **derleme
kırmızı**; (b) `TULPAR_ROOT` verilmemiş koşumda "köprü ABI'si derlenmedi" diyen
**görünür** bir satır var; (c) 17 `.tpr` testi için ya bir koşucu var ya da
"bu depoda koşmuyor, dil deposunda koşuyor" cümlesi `tulpar/README.md`'de yazılı.

**Doğrulanmadı:** 17 `.tpr` testinin bugün geçip geçmediği. Bu depoda TulparLang
derleyicisi yok; koşulmadı.

---

<a id="ep-07"></a>
### EP-07 — Araç düzeyinde yalan yeşil

**Belirti.** Testlerdeki "atlandı sayacı" disiplini Python araçlarına **uygulanmamış**:
bir araç "tam koşmadım" diyip çıkış 0 verebiliyor.

**Kanıt (ölçüldü — A).**
```
$ python3 tools/faz8_shader_audit.py .        -> rc=0
[bilgi] 17/19 dosya depodaki SPIR-V ile bayt ayni (2 atlandi: kaynak degismis)
Faz 8 shader denetimi: 22 yesil, 0 kirmizi, 2 atlandi
UYARI: 2 adim ATLANDI (referans GLSL degismis (--pin ile yeniden ported
       edilmeli)) — bu denetim TAM kosmadi.
```
Araç kendi cümlesiyle "TAM koşmadım" diyor ve **çıkış 0**. (İki atlanan adım,
EP-01'deki bayat shader'larla aynı kökten.)

İkinci, daha küçük örnek:
```
$ python3 tools/feature_matrix.py --check-doc  -> rc=1
HATA: docs/engine/MADDE-LISTESI-DURUM.md bulunamadi.
```
Varsayılan yol **mono-repo kalıntısı** (`docs/engine/…`); dosya bugün
`docs/MADDE-LISTESI-DURUM.md`. Doğru yol verilince sonuç
`OK: … script ile birebir ayni` (yani içerik kaymamış, yalnız varsayılan yol ölü).

**İlk somut adım.** Bir satırlık sözleşme: **"atlanan adım varsa çıkış 0 değildir"**
— ya çıkış 2 (kısmi) ya `--strict` ile 1. `faz8_shader_audit.py`'de
`atlandi > 0 → sys.exit(2)`; `feature_matrix.py --check-doc` varsayılanını
`docs/MADDE-LISTESI-DURUM.md` yapın.

**Bitti ölçütü.** Her `tools/*.py` için: "hiçbir şey ölçmeden çıkış 0" durumu
üretilebiliyor mu? Üretilebiliyorsa kapı yok demektir. Üç aracın (`faz8_shader_audit`,
`layout_check`, `feature_matrix`) her biri için bu sorunun cevabı belgelenmiş olmalı.

---

## 5. E sınıfı — kapı doğru, ama bu ortamda ölçmüyor

<a id="ep-08"></a>
### EP-08 — Mali/Arm BestPractices dört kapısı CI'da hiç ölçmüyor

**Belirti.** Dört kapı — `renderer_mali_best_practices_gate`,
`render_graph_post_mali_best_practices`, `render_graph_gpu_cull_mali_best_practices`,
`temporal_mali_best_practices` — CI Linux ayağında her koşumda şunu basıyor:
`ATLANDI: dogrulama katmani Arm BestPractices kurallarini tanimiyor (surum) —
Mali kapisi olculemedi`. İş yeşil kalıyor (bu atlama bilerek başka sınıf sayılıyor,
`ci.yml`'de yorumla yazılı).

**Kanıt.**
* CI (B), Linux ayağındaki **7 atlamanın 4'ü** bu satır. Aynı koşumda
  `vulkaninfo`: `VK_LAYER_KHRONOS_validation … **1.3.275**` (Ubuntu 24.04 apt).
* Yerelde (A), VVL **1.4.357** ile dördü de **koşuyor ve ölçüyor**. Ortak
  pozitif kontrol (`test::arm_rules_missing`, LOD kırpan sampler) dört yerde de
  şunu bastı: `Arm uyarisi 0 -> 1`. Dört test de PASS.
* macOS ayağında (B) katman sürümü **zaten 1.4.357** (brew) — yani orada engel
  sürüm **değil**, EP-09'daki loader atlaması.

**Bu bir cihaz sorunu değil.** Arm BestPractices kuralları doğrulama
katmanının mesajlarıdır; lavapipe üzerinde de üretilirler. Engel tamamen
katman sürümü/ayarı.

**Seçenekler ve bedelleri.**

| # | seçenek | bedel | risk |
|---|---|---|---|
| **a** | CI Linux ayağına **LunarG apt deposunu** ekleyip güncel `vulkan-validationlayers` kurmak | Bir apt kaynağı + anahtar, ~1 adım, onlarca MB | Depo sürümü Ubuntu'nunkiyle çakışabilir; **kurulan sürümün Arm kurallarını tanıdığı doğrulanmadı** |
| **b** | LunarG SDK tar.gz'ini indirip `VK_LAYER_PATH` ile göstermek | Yüzlerce MB indirme, önbelleklenebilir | Yavaş, sürüm sabitleme işi |
| **c** | Katmanı kaynaktan derlemek | CI'da dakikalar | En pahalısı, bakım yükü |
| **d** | **macOS loader yolunu düzeltmek** (EP-09) | EP-09'un maliyeti | macOS'ta katman **zaten 1.4.357**; düzelirse dört kapı orada ölçmeye başlar — **en ucuz gerçek kazanç** |
| **e** | Kabul et + belgele: kapılar yerelde ve telefonda ölçülür, CI'da ölçülmez | 0 | Bugünkü durum; ama "ölçülmedi" bilgisi ölçüm kaydına girmeli |

**Önerilen sıra: (d) → (a) → (e).** (d) bedava sayılır çünkü EP-09 zaten
yapılacak; (a) denenir ve işe yaramazsa (e)'ye düşülür.

**İlk somut adım — ve önce şu deneyi yapın (ucuz, ayırt edici).**
Bugünkü açıklama "katman **sürümü** kuralları tanımıyor". Bu **doğrulanmadı**:
en az üç ihtimal aynı belirtiyi verir — (i) kurallar o sürümde yok,
(ii) `VK_EXT_layer_settings` o sürümde desteklenmiyor,
(iii) ayar **anahtarı** farklı (`validate_best_practices_arm` yerine eski
`enables = VALIDATION_CHECK_ENABLE_VENDOR_SPECIFIC_ARM` mekanizması).
Motor ayarı yalnız `VkLayerSettingsCreateInfoEXT` üzerinden veriyor
(`rhi/device.cpp`). Deney: 1.3.275 katmanı olan bir ortamda (ör. CI'da tek
adımlık bir koşum) eski mekanizmayı ortam değişkeniyle zorlayıp tek kapıyı
koşturun:
```bash
VK_LAYER_ENABLES=VALIDATION_CHECK_ENABLE_VENDOR_SPECIFIC_ARM \
  ./yapi/engine_tests renderer_mali_best_practices_gate
```
`Arm uyarisi 0 -> 1` görürseniz sorun sürüm değil **ayar mekanizması**dır ve
çözüm tek satırlık bir ortam değişkeni olur (hem ucuz hem toplayıcı: mevcut
`VkLayerSettingsCreateInfoEXT` yolu **kalır**, yedek mekanizma eklenir).

**Bitti ölçütü.** Ya (i) CI Linux ayağında dört kapı **koşuyor** ve
`Arm uyarisi 0 -> 1` satırını basıyor ve `ci.yml`'deki "başka sınıf" muafiyeti
kaldırılıyor; ya da (ii) muafiyet duruyor ama iş özetinde **"bu koşumda Mali
kapıları ölçülmedi"** uyarısı **görünür** (macOS'un loader uyarısındaki kalıp)
ve DURUM.md'de en son nerede ölçüldüğü tarihiyle yazılı.

---

<a id="ep-09"></a>
### EP-09 — macOS loader atlaması: yedek artık konuşuyor, kök sebep hâlâ bilinmiyor

**Belirti.** macOS'ta motor, loader üzerinden instance kuramayınca
`vk_api_load_moltenvk_direct()`'e düşüp `libMoltenVK.dylib`'i doğrudan açıyor.
Katmanlar bir **loader** mekanizması olduğu için o süreçte katman zinciri yok;
doğrulama ve BestPractices kapıları ölçemiyor (Tuzaklar **8bu**). `0898be4` ile
yedek **kendini bildiriyor** (`[rhi] loader ATLANDI: …`) ve `DeviceCaps::loader_bypassed`
bunu kapılara taşıyor — yani sessizlik bitti. **Ama neden atladığı hâlâ basılmıyor.**

**Kanıt (CI — B, run 35506012140, macOS ayağı).**
* Motor 8 kez `[rhi] loader ATLANDI: MoltenVK DOGRUDAN yuklendi` basmış;
  buna bağlı **8+ kapı** atlanmış (3 Mali kapısı + API doğrulama + GPL bilgisi).
* **Aynı runner'da, aynı ortam değişkenleriyle, `vulkaninfo` LOADER ÜSTÜNDEN
  çalışıyor:**
  ```
  Instance Layers: count = 1
  VK_LAYER_KHRONOS_validation  Khronos Validation Layer  1.4.357
  Devices:
  GPU0:  apiVersion = 1.4.357   deviceName = Apple Paravirtual device
         driverName = MoltenVK
  ```
  Yani ICD json'u bulunuyor, loader instance kuruyor, cihazı görüyor, katmanı
  listeliyor. **Ortam çalışıyor; atlayan motorun kendi isteği.**
* Logda **hangi daldan** atlandığı yok. `rhi/device.cpp`'de iki `__APPLE__` dalı
  var ve ikisi de aynı satırı bastırıyor: (1) `vkCreateInstance != VK_SUCCESS`,
  (2) instance kuruldu ama `vkEnumeratePhysicalDevices` **0** döndü.
  `VkResult` hiçbir yerde basılmıyor (o yolda `fail()` çağrılmıyor).

**Bu, TUZAKLAR 8bu'nun bugünkü metnini daraltıyor.** 8bu ve `ci.yml` yorumu
"loader MoltenVK ICD'si üzerinden instance **kuramıyor**" diyor; yukarıdaki
`vulkaninfo` çıktısı bunu bir **ortam gerçeği** olmaktan çıkarıp **motora özel bir
fark** hâline getiriyor. Hangi fark olduğu **doğrulanmadı**; makul adaylar:
katmanın instance'ta **etkinleştirilmesi** (vulkaninfo yalnız listeliyor,
etkinleştirmiyor), `pNext`'teki `VkLayerSettingsCreateInfoEXT`, istenen
`apiVersion`, ya da `vk_api_load`'un hangi kütüphaneyi açtığı
(liste `libvulkan.1.dylib` → … → `libMoltenVK.dylib` sırasıyla deniyor; son üç
isim **loader'ı hiç kullanmadan** da başarılı olabilir).

**İlk somut adım (üç satırlık teşhis, davranış değişikliği yok).**
1. `vk_api_load`: **hangi ismin açıldığını** bas (`[rhi] yukleyici: <yol>`).
2. `rhi/device.cpp`: yedeğe düşmeden önce **`VkResult`'u ve dalı** bas
   (`vkCreateInstance basarisiz: <r>` / `loader 0 fiziksel cihaz gordu`).
3. macOS ayağında yedek tetiklendiğinde **katmansız bir kez daha dene** ve sonucu
   bas — böylece "katmanı etkinleştirmek mi öldürüyor" sorusu tek koşumda cevaplanır.

**Bitti ölçütü.** macOS CI logunda "loader neden atlandı" sorusunun cevabı **tek
satırda** okunuyor. Kök sebep düzelirse: `[rhi] loader ATLANDI` satırı kayboluyor,
o ayakta doğrulama + dört Mali kapısı **koşuyor** (EP-08/d).

**Kapsam notu (silme yok).** Doğrudan-MoltenVK yedeği **kalır** — bugün
macOS'ta motorun açılmasını o sağlıyor. Eklenecek olan teşhis ve, kök sebep
bulunursa, loader yolunun **tercih edilmesi**.

---

<a id="ep-10"></a>
### EP-10 — Hangi ayakta neyi ölçtüğümüzün haritası yok

**Belirti.** Üç CI ayağı da `493 passed, 0 failed` diyor; atlanan sayı 7 / 38 / 92.
Bu üç sayı iş özetinde **listeleniyor** (güzel), ama hiçbir yerde
*"bu ayak şu aileyi ölçer, şunu ölçmez"* diye **sınıflandırılmıyor**. Sonuç:
"CI yeşil" cümlesi üç ayrı anlama geliyor ve hangisi olduğu okunmuyor.

**Kanıt (CI — B).**
* **macOS, 38 atlama:** ~30'u `sanal GPU (Apple Paravirtual, CI macOS): piksel
  kapisi gercek cihazda olculur`, 8+'i loader/katman ailesi (EP-09).
  Yani macOS ayağı **piksel kapılarının neredeyse hiçbirini** ölçmüyor;
  değeri (README'nin de dediği gibi) **ikinci mimari**: AArch64 fiber geçişi ve
  libm farkları (Tuzaklar 8d).
* **Windows, 92 atlama:** `41 × ATLANDI: Vulkan cihazi yok` + varyantları —
  beklenen (yazılım ICD'si yok), `ci.yml` bunu bilerek serbest bırakıyor.
  Yani Windows ayağı **GPU yolunu hiç ölçmüyor**; derleme + CPU tarafı kapısı.
* **Linux, 7 atlama:** 4 Mali (EP-08), 1 lavapipe UI bütçesi, 1 ses cihazı,
  1 `SCHED_FIFO`. Gerçek ölçümün yapıldığı ayak bu.

**Neden önemli.** Bugün kimse yanılmıyor çünkü sayıları okuyan kişi bunları
biliyor. Belgelenmediği an "üç platformda yeşil" cümlesi, tek platformda ölçülmüş
bir şeyi üç kat güvenilir gösterir (Tuzaklar **8bo**: *"atlandı sayısını iki
platform arasında karşılaştır"*).

**İlk somut adım.** `engine_tests` özet satırının altına **atlama sınıfı dökümü**
bas (ortam / sanal GPU / süreç tavanı / özellik yok) ve `ci.yml`'in zaten yazdığı
`<details>` bloğunu bu sınıflarla etiketle. Sonra README'nin "Test" bölümüne üç
satırlık bir tablo: *hangi ayak neyi ölçer.*

**Bitti ölçütü.** Bir koşumun özetine bakan biri, hangi ailenin ölçülmediğini
**kaynağa bakmadan** söyleyebiliyor.

---

<a id="ep-11"></a>
### EP-11 — `engine_editor --headless` hiçbir CI ayağında koşmuyor

**Belirti.** Tuzaklar **8br**'nin düzeltmesi iki katmanlı bir kapı kurdu:
(1) `editor_probe_render` her sondada ImGui hata sayacını sıfırlayıp denetliyor
— bu `engine_tests` içinde **koşuyor**; (2) `engine_editor --headless` sayaç
sıfır değilse **çıkış 1** dönüyor — bu **hiçbir yerde koşmuyor**.

**Kanıt (ölçüldü — A/B).** `ci.yml`'de `headless` geçmiyor; `release.yml` yalnız
ikililerin **var olduğunu** denetliyor (`for t in engine_demo engine_editor …`).
`release.yml`'in kendi yorumu da bunu yazıyor: *"`engine_demo` / `engine_editor`
ASLA çalıştırılmaz"*.

**Şiddeti düşük** çünkü sınıfın kendisi (1) ile kapalı. Ama ikili düzeyindeki
kapı — paketlenip indirilen şeyin gerçekten açıldığını ölçen tek kapı — ölü.

**İlk somut adım.** Linux ayağına testlerden **sonra** tek satır:
```bash
DISPLAY= ./yapi/engine_editor --headless 3 --out /tmp/editor.ppm   # cikis 0 bekleniyor
DISPLAY= ./yapi/engine_demo   --headless 3 --out /tmp/demo.ppm
```
**Not:** bu `.github/**`'a dokunur → devam eden Windows paketleme işi (§2)
bitene kadar bekletin, ya da `tools/package.sh`'in tamlık kapısına değil,
ayrı bir betiğe koyun.

**Bitti ölçütü.** Bir `ImGui::End()` dengesizliği geri konduğunda CI **kırmızı**
(pozitif kontrol; 8br'de bu zaten yerelde yapılmıştı).

---

## 6. Y sınıfı — eksik özellik ve donanım

<a id="ep-12"></a>
### EP-12 — Üç fiziksel cihaz yok; telefon ölçümü 141 test dönemine ait

**Durum.** `CIHAZ-MATRISI.md` §2 üç cihaz sınıfı (düşük/orta/yüksek) tanımlıyor ve
üçünün de **elde olmadığını** yazıyor; elde olan tek cihaz Huawei P20 Pro
(Kirin 970 / Mali-G72, Vulkan **1.1.97**, 2018 sürücüsü), matriste **"sınıf
doldurmuyor, Mali ve eski sürücü vekili"** olarak işaretli. `DURUM.md` §6.1
kullanıcı kararını da kaydediyor: *"üç fiziksel cihaz — şimdilik pas"*.

**Bunun bloke ettikleri (PLAN §7 kapılarına göre):**

| faz | kapı | neden kapanamıyor |
|---|---|---|
| Faz 1 | "G-buffer DRAM'e inmedi", 3 cihaz | cihaz |
| Faz 3 | bant genişliği < 8 GB/s, 3 cihaz | cihaz |
| Faz 5 | 10 dk sürdürülebilir, p99 < 18 ms, 3 cihaz | cihaz |
| Faz 7 | cihazda perf CI | cihaz + koşucu |

**Ayrıca ölçülmemiş bir şey var ve bu cihazla ilgili değil:** `DURUM.md`'deki
telefon sayısı **78/78 (2026-09-15)**. O gün depoda **141** `ENGINE_TEST` vardı;
bugün **493** kayıtlı test var. Yani telefon kaydı bugünkü paketin **yaklaşık
altıda birini** temsil ediyor. Telefonda o tarihten sonra tam koşum yapıldığına
dair bir kayıt bulunamadı (**doğrulanmadı** — belgelerde yok; kullanıcı biliyor
olabilir).

**İlk somut adım (cihaz almadan yapılabilecekler).**
1. **P20 Pro'da tam paketi bir kez daha koş** (`tools/android_run.sh tests`) ve
   sayıyı tarihiyle DURUM.md'ye yaz. Tek cihazlık "trend" ölçümü bir kapı değil
   ama 6 katlık boşluğu kapatır.
2. `CIHAZ-MATRISI.md` §5'teki açık madde *"cihaz farm'ı için adb üzerinden
   koşturucu (install_run.sh'ın perf modu)"* **bu depoda karşılıksız**:
   `tools/` içinde `install_run.sh` yok, `android_run.sh`'in de `tests`/`demo`
   dışında kipi yok. Maddeyi bu depoya göre yeniden yazın: *"`android_run.sh`'e
   perf kipi: 10–15 dk pencere, 3–5 koşu, medyan + p99, cihaz kimliği satırı"*.
3. Üç cihaz gelene kadar **hangi kapının ne dediği** yazılı olsun: "cihaz bekliyor"
   ile "ölçtük, geçti" aynı tabloda ayrı sembollerle duruyor (bugün PLAN/DURUM
   bunu yapıyor — bozmayın).

**Bitti ölçütü.** (a) Telefon satırı bugünkü test sayısıyla güncel;
(b) perf kipi var ve ölçüm disiplinini (§3) **kodla** uyguluyor;
(c) cihaz kapıları hâlâ açıkken bile "ölçülmedi" cümlesi tek yerde okunuyor.

---

<a id="ep-13"></a>
### EP-13 — Editör bileşen matrisinin kalanı

`EDITOR-DURUM.md` §1 hâlâ geçerli bir açık iş listesi (EP-05 onun en pahalı
dilimi). Kalanlar, o belgedeki şiddet sırasıyla: paneli olmayan bileşenler
(Character, Particle, Voxel), tel kafes görünümü, malzeme grafiği, zaman çizelgesi,
arazi fırçası, proje ayarlarının sahneye yazılması, çoklu viewport.
`DURUM.md` §6.1'in editör satırı da iki madde bırakıyor: **oynat/durdur sim geri
sarımı** ve **implot ile kare zamanı grafiği**.

Bu maddeler **dürüst eksikler** — kapı yalanı yok, yapılmadılar. Sıraları
EP-01..EP-07'den **sonra**: sessizce yanlış olan bir şeyin üstüne özellik
koymak, yanlışı büyütür.

**İlk somut adım.** `EDITOR-DURUM.md` §1 matrisini bugünkü ağaca karşı bir kez
daha üretin (belge `./build.sh test  # 475 test, tavan 512` diyor — ikisi de
eskimiş, bkz. EP-15) ve ilk üç satırı EP-05'in kapısına bağlayın.

---

<a id="ep-14"></a>
### EP-14 — Faz 8 kalanı, Faz 9 VSM, Faz 10 Metal

`DURUM.md` §6'daki faz tablosu bu üçü için doğru ve güncel görünüyor
(**doğrulanmadı**: tablodaki faz yüzdeleri bu turda tek tek ölçülmedi).
Kısaca: Faz 8'de **8.2 GPU tip sistemi**, `uint`, **8.5 permutation/reflection**
açık; Faz 9'da **VSM** ve üçgen/piksel oranı ölçümü açık; Faz 10 (Metal/iOS)
başlanmadı ve Mac + geliştirici hesabı istiyor.

Bir nokta bu turda ölçüldü ve belgeyle uyuşmuyor: **shader sayısı 23**
(`rhi/shaders/*.vert|frag|comp`), `DURUM.md` ve `FAZ8.md` **21** diyor.
Faz 8'in "21 shader'ın 19'u taşındı" cümlesi bu yüzden bugünkü ağaçta
**yeniden sayılmalı** (EP-15).

---

## 7. N sınıfı — hijyen

<a id="ep-15"></a>
### EP-15 — Belge kayması

Hiçbiri tehlikeli değil; hepsi okuyanı yanlış yere götürüyor. Ölçülenler:

| belge | yazan | bugünkü ölçüm |
|---|---|---|
| `README.md` | "66 hata sınıfı (**8a–8ap**)" | `TUZAKLAR.md`'de **73** giriş, **8a–8bu** |
| `README.md` §Ağaç | "shader'lar (GLSL -> depoya giren `*_spv.h`)" | doğru, ama **tazelik kapısı olmadığı** yazılmıyor (EP-01) |
| `DURUM.md` §4 | "masaüstü `engine_tests` **149/149**", "141 `ENGINE_TEST`" | **493 passed / 493 kayıtlı** |
| `DURUM.md` §4 | "telefon **78/78** (2026-09-15)" | o gün 141 test vardı; bugün 493 (EP-12) |
| `DURUM.md` §1 | köprü "**156** fonksiyon" (§6.1'de 175) | `SPEC` = **175**, `engine_api.h` ile eşleşiyor |
| `DURUM.md` §6.1/6 | "`build.sh suites` içinde koşuyor (`build.sh:276`, `:292`)" | bu depoda **`build.sh` yok**; `dist_archive_audit.py` / `paket_boyut_audit.py` de yok (dil deposunda kaldılar) |
| `DURUM.md` §6.1/5 | "macOS CI çökmesi (yerelden ulaşılamıyor)" | macOS ayağı **yeşil**: `493 passed, 0 failed` (B) |
| `DURUM.md` / `FAZ8.md` | "21 shader" | **23** |
| `docs/DEVAM_PLANI.md` | Faz A (sahne kalıcılığı), Faz B (davranış/kamera/tetikleyici) "yapılacak" | ikisi de **var**: `.sahne`/`.sahneb`, `test_behavior_tree.cpp`, `test_camera_rig.cpp`. Belge 2026-09-15 öncesi durumu anlatıyor |
| `tools/feature_matrix.py` | `--check-doc` varsayılanı `docs/engine/MADDE-LISTESI-DURUM.md` | dosya `docs/MADDE-LISTESI-DURUM.md` (içerik **senkron**, yalnız yol ölü) |
| `EDITOR-DURUM.md` §0 | "`./build.sh test` 475 test, tavan 512" | `./yapi/engine_tests`, 493 test, tavan **640** |
| `docs/KOPRU.md` §2 | binding yolu `runtime/engine_bindings.cpp` | bu depoda `tulpar/generated/engine_bindings.cpp` |

**İlk somut adım.** `DEVAM_PLANI.md`'ye tepeden bir "bu belge 2026-09-15 öncesini
anlatır, güncel sıra `ENTEGRASYON-PLANI.md`'dedir" notu (silmeyin — tarihsel
kayıt); diğerleri tek tek düzeltme.

**Bitti ölçütü.** `DURUM.md` §4'teki her sayının yanında **ölçüm tarihi** var ve
hiçbiri bu belgedeki künyeyle (§1) çelişmiyor.

---

<a id="ep-16"></a>
### EP-16 — `Registry::kMax` başlığı: bugün sağlıklı, izlemede

**Ölçüm (A).** `Registry::kMax = 640`, kayıtlı test **493** → **%77 dolu,
147 boş**. Taşma **sessiz değil**: `Registrar` taşan her testi basıyor,
`Registry::overflow` sayıyor, `test_main.cpp` çıkışı 1 yapıyor ve CI ayrıca
`KAYIT TASMASI` dizgisini arıyor (üç ayakta da).

**Ama büyüme hızı hızlı:** 141 (2026-09-15) → 485 (`test.hpp` yorumu) → **493**
(bugün). Beş günde 3,5 kat. Kapı kırmızıya döndüğü için bu bir **S sınıfı** değil;
yine de tavan bir ölçüm değildir, tavandır (Tuzaklar: "256 bir ölçüm değil,
tavandı").

**İlk somut adım.** `test.hpp`'deki yorumdaki sayıyı güncelleyin ("bugün 485")
ve özet satırına `493/640` gibi **doluluk** bilgisi ekleyin; böylece sayı
görünür olur, kimse tavanı ölçüm sanmaz.

---

## 8. Doğrulayamadıklarım (açıkça)

Bu bölüm bilerek uzun: bu depoda ölçülmemiş bir cümlenin ölçülmüş gibi durması,
belgenin kendisini EP-01 sınıfına sokar.

1. **VVL 1.3.275'in Arm kurallarını neden tanımadığı.** Ölçtüğüm şey belirti:
   CI'da 1.3.275 ile dört kapı atlıyor, yerelde 1.4.357 ile dördü de
   `Arm uyarisi 0 -> 1` basıyor. **Sebep ayrıştırılmadı** — kural yokluğu,
   `VK_EXT_layer_settings` desteğinin yokluğu ve ayar anahtarı değişikliği aynı
   belirtiyi verir. EP-08'deki `VK_LAYER_ENABLES` deneyi bu üçünü ayırır;
   ben 1.3.275 katmanına ulaşamadığım için **koşamadım**.
2. **LunarG deposunun/SDK'sının Ubuntu 24.04'te hangi VVL sürümünü verdiği** ve o
   sürümün Arm kurallarını tanıyıp tanımadığı. İndirilmedi, denenmedi.
3. **macOS'ta loader'ın hangi daldan atlandığı.** `vulkaninfo`'nun aynı runner'da
   çalıştığını ölçtüm; motorun hangi çağrıda düştüğünü **ölçemedim** (log o bilgiyi
   basmıyor, elimde macOS makinesi yok). EP-09'daki adaylar **hipotez**.
4. **`godray_frag_spv.h` ve `mesh_frag_spv.h` farkının sebebi.** `compose.frag`
   için bayatlık **kanıtlı** (git geçmişi + eksik SPIR-V sabiti). Diğer ikisinde
   fark var (godray: 265 → 254 komut; mesh: 994 → 993) ama bunun kaynak
   farkından mı yoksa glslc sürümü farkından mı geldiğini **ayrıştıramadım**.
   `layout_check.py` üçüne de "BAYAT" diyor; ben yalnız birini kanıtlayabildim.
5. **17 `.tpr` köprü testinin bugünkü sonucu.** Bu depoda TulparLang derleyicisi
   yok; koşulmadı.
6. **Telefonda 2026-09-15'ten sonra tam koşum yapılıp yapılmadığı.** Belgelerde
   kaydı yok; yokluğu kanıt değildir.
7. **`DURUM.md` §6'daki faz yüzdeleri ve `MADDE-LISTESI-DURUM.md`'nin 500 maddesi.**
   Tek tek denetlenmedi; `feature_matrix.py --check-doc` yalnız markdown'ın
   script'le **tutarlı** olduğunu söylüyor, maddelerin **doğru** olduğunu değil.
8. **EP-03'ün gerçek pay marjı.** Ölçtüğüm şey: tam koşumda 21 ICD yüklemesi ve
   `optional_static_tls`'i 512 → 128 indirince 67 kapının sessizce ölmesi (cliff
   128–160 arasında). "Kaç test daha eklenirse varsayılan bütçede tavana
   çarpılır" sorusunu **ölçemedim** — glibc'nin LIFO geri kazanımı yükleme
   sırasına bağlı, ve 8bs'nin "24 sonda" sayısı **farklı bileşimli** bir süreçte
   ölçülmüştü; iki sayı doğrudan karşılaştırılamaz.
9. **`.github/**`, `tools/package.sh` ve `renderer/**` altındaki güncel iş.**
   İki ajan orada çalışıyor (§2); okudum, **ölçmedim**, dokunmadım.

---

## 9. Önerilen sıra

Tek kural: **ölçmeyen bir kapının üstüne özellik konmaz.**

| sıra | madde | neden önce |
|---|---|---|
| 1 | **EP-01** (+ godray işi birleşince) | Kaynakta duran kod ikilide yok. Tek komutluk düzeltme; EP-02'nin de önkoşulu |
| 2 | **EP-02** | Var olan, pozitif kontrollü bir kapı bugün kırmızı ve kimse koşmuyor. CMake'e bağlamak `.github`'a dokunmadan üç ayağı birden kapsar |
| 3 | **EP-03 + EP-04** (tek iş) | Yerel koşumda 67 kapı sessizce ölebiliyor ve çıkış 0. Ortak yardımcı ikisini birden kapatır |
| 4 | **EP-09** | Ucuz teşhis; EP-08'in en ucuz çözüm yolunu (d) açar |
| 5 | **EP-08** | (d) işe yaramazsa (a), o da olmazsa (e) — ama "ölçülmedi" kaydı şart |
| 6 | **EP-06** | Köprü bugün taze ama **kapısız**; iki küçük kapı kalıcı çözüm |
| 7 | **EP-05** | Kullanıcının oyunda bulacağı veri kaybı; blob sürümü 5 (ekleme, değiştirme değil) |
| 8 | **EP-07, EP-10, EP-11, EP-16** | Ucuz hijyen; birlikte tek turda |
| 9 | **EP-15** | Belge turu — yukarıdakiler bittikten sonra, çünkü sayılar o zaman kesinleşir |
| 10 | **EP-12, EP-13, EP-14** | Dürüst eksikler; sıra PLAN.md'nin fazlarına göre |

**Sıra dışı kalan tek şey:** EP-12'nin birinci adımı (telefonda tam paketi bir kez
koşup sayıyı kaydetmek). Kimseyi beklemiyor, bir saatlik iş ve `DURUM.md`'deki en
büyük sayı boşluğunu kapatıyor — istenen anda araya sokulabilir.
