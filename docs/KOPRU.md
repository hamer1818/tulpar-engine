# Tulpar Engine Köprüsü — motor C++, oyun Tulpar (2026-09-15)

> **Depo notu.** Bu belge motor deposuna taşındı; ağaç yolları artık `engine/` öneksiz
> (`core/…`, `rhi/…`, `tools/…`). Metinde geçen `src/`, `lib/*.tpr`, `runtime/`, `examples/`,
> `android/host/`, `build.sh`, `./tulpar` ve `docs/mindmap/` **derleyici deposundadır**
> ([hamer1818/TulparLang](https://github.com/hamer1818/TulparLang)) — olduğu gibi bırakıldı.

> Karar (kullanıcı, 2026-09-15): **motor C++ kalır, oyun betikleri Tulpar'da yazılır.** Bu belge o
> köprünün sözleşmesi, katmanları ve hata ayıklama yüzeyidir. Fizibilite tartışması: PLAN.md §11.

## 1. Neden köprü, neden bugün

PLAN §11 zaten "L5 gameplay Tulpar'dadır, aynı ikiliye linklenir, script sınırı yoktur" diyordu; eksik olan
onu taşıyan arayüzdü. Tulpar bugün işaretçi, atomik ve callback FFI vermiyor (kutusuz **tekil** struct,
`enum`, çoklu dönüş ve **kutusuz struct dizisi** (`Dusman[]`) 2026-09-21'den beri var — TulparLang
`plans/08_oyun_dili_p0.md`; struct GEÇİŞLİ ABI hâlâ yok, yani köprü imzaları düz skaler kalıyor) —
bu yüzden
köprü **düz skalerlerle** konuşur: `int`, `double`, `const char*`. Struct yok, callback yok, sahiplik yok.
Motor tarafı bütün durumu kendi tutar; Tulpar tarafı **tamsayı tutamaçlarla** (varlık id'si) konuşur.

Ölçülen sonuç: Unity P/Invoke 20–100 ns, GDScript Variant 100–500 ns, burada **doğrudan çağrı** — Tulpar'ın
AOT kodu `aot_eng_*_ptr` sembolünü çağırır, o da `teng_*`i çağırır. İkisi de aynı ikilide.

## 2. Katmanlar (aşağıdan yukarı)

| katman | dosya | ne yapar |
|---|---|---|
| motor | bu depo (L0–L6) | Vulkan, Jolt, renderer, içerik — değişmedi |
| C ABI | `bridge/engine_api.h` | `teng_*`: düz skaler fonksiyonlar, tek global bağlam |
| çekirdek | `bridge/engine_api.cpp` | durum, varlık tablosu, kare döngüsü, **log** |
| host | `bridge/desktop_host.cpp`, `android_host.cpp` | pencere/yüzey/girdi; `BridgeHost` sözleşmesi |
| binding | `runtime/engine_bindings.cpp` (**üretilmiş**) | `aot_eng_*_ptr` (VMValue ABI) → `teng_*` |
| sarmalayıcı | `lib/engine.tpr` (gömülü, 1040 satır) | `motor_ac`, `kutu`, `tus`, `yazi`, `dugme`, `kayit_*`, `betik_ata` … TR adlar, çoğunun EN ikizi (`engine_open`, `box`, `key`, `button`, `script_attach`); `Vec3`, oyun yardımcıları (`yol_yonu`, `goruyor_mu`) ve arayüz yerleşimi (`ui_pencere`, `ui_dugme`, `ui_test_tikla_ad`) |
| oyun | `examples/engine_ilk_oyun.tpr` (94), `engine_arena.tpr` (209), `engine_aksiyon.tpr` (720), `engine_dalga.tpr` (76 + davranışlar 57) | saf Tulpar |

**Tek kaynak:** `tools/gen_engine_bindings.py` içindeki `SPEC` tablosu. Bir komut dört dosya üretir:
binding (`runtime/engine_bindings.cpp`), backend tablosu (`src/aot/engine_builtins_table.inc`), typeinfer
imzaları (`src/typeinfer/engine_builtins_sigs.inc`), LSP girdileri (`src/lsp/engine_builtins.inc`).
CLAUDE.md'nin "5 noktada bağlama"sı burada **mekanik**: noktalar elle tutulmadığı için birbirinden kayamaz.
Yeni builtin = `SPEC`'e bir satır + `engine_api.h/.cpp`'de uygulama + `python3 tools/gen_engine_bindings.py`.

## 3. Sözleşme

- **Yaşam döngüsü:** `eng_init` → `[eng_frame_begin … eng_frame_end]*` → `eng_shutdown`.
  `eng_frame_begin` false dönerse pencere yok (arka plan); **yine de** `eng_frame_end` çağrılır (sim sürer).
- **Varlık id'si** nesil etiketlidir: `(nesil<<16)|yuva`, 0 geçersiz. Silinmiş id ile çağrı **hata loglar**
  ve sessizce 0 döner — arcade'in öğrettiği kural (ham indeks asla dışarı sızmaz).
- **Renk** paketlenmiş int: `(r<<24)|(g<<16)|(b<<8)|a`. Tulpar'da bit kaydırma **yok**, `renk(r,g,b)`
  çarpmayla paketler (tame `rgb()` ile aynı).
- **Fizik:** kutu/küre gövdeleri Jolt'ta; `eng_set_pos` dinamik gövdede **gövdeyi yeniden kurar** (Jolt'ta
  konum yazma yok) ve hız sıfırlanır — log satırı bunu söyler.
- **HUD** kare içinde kuyruklanır (`eng_text`/`eng_rect`), `eng_frame_end` çizer. Kare dışında çağrı hata loglar.
- **Sahne:** `eng_scene_load("x.sahneb")` derlenmiş sahneyi (engine_sahnec / editörde **Derle**) yükler;
  modeller, ışıklar, gövdeler ve dünya ayarları oradan gelir. `eng_scene_unload` + yeniden yükleme =
  **bölüm geçişi**; `eng_scene_watch(1)` **sıcak yükleme** (dosya damgası izlenir, değişim görüldükten
  sonra bir kontrol daha beklenir ki yarım yazılmış blob yüklenmesin).
- **Çıktı parametresi yok:** Tulpar'da işaretçi/çıktı parametresi olmadığı için onay kutusu ve kaydırıcı
  **yeni değeri döndürür** ve betik geri yazar (`ses = kaydirici(..., ses, 0, 1)`).
- **Kalıcı kayıt motor kurulmadan da çalışır:** parlama gibi ayarlar `eng_init`'ten **önce** okunur.

## 4. Hata ayıklama yüzeyi (kullanıcı isteği: "olabildiğince her şeyi logla")

Her `teng_*` çağrısı log seviyesine göre basılır ve **her seviyede** 64 satırlık halkaya yazılır:

| seviye | `TULPAR_ENGINE_LOG` | ne çıkar |
|---|---|---|
| 0 | `0` | yalnız HATA (hata her zaman basılır) |
| 1 | `1` (varsayılan) | kurulum, sahne, kapanış raporu, betiğin `logla()`'ları |
| 2 | `2` | durum değişiklikleri: her spawn/despawn, kamera, ışık, swapchain, 120 karede bir kare raporu |
| 3 | `3` | **her API çağrısı** argümanlarıyla |

- Satır biçimi: `[engine_bridge] k<kare> <seviye> <mesaj>` — hangi karede olduğu her satırda var.
- Kapanışta hata varsa **halka dökülür** (`--- log halkasi (…; son cagri teng_x; hata 3) ---`): patlamadan
  önceki son 64 olay ve son API çağrısının adı. "Nerede patladı" sorusunun cevabı budur.
- Betikten: `logla("…")` → `[tpr]` önekiyle aynı akışa. (`log()` Tulpar'da **doğal logaritmadır**, ad çakışır.)
- Android'de aynı akış logcat'e (`tulpar` etiketi) ve `files/engine_log.txt`'ye gider.
- Pencere açılamazsa motor **headless'a düşer** ve bunu UYARI olarak yazar — betik yine koşar.

## 5. Doğrulama kipi (pencere açmadan)

```bash
TULPAR_ENGINE_HEADLESS=120 TULPAR_ENGINE_OUT=/tmp/oyun.ppm ./tulpar examples/engine_ilk_oyun.tpr
```

Pencere yerine offscreen render pass, N kare, son kare PPM. Kural aynı: **ben pencere açmam**, görsel
doğrulamayı kullanıcı ya da emülatör/cihaz ekran görüntüsü yapar (`no-raylib-windows-verify` ile aynı disiplin).

### 5.1 Gömülü kip (editörün F5'i)

`TULPAR_ENGINE_GOMULU=<kanal adı>` tanımlıysa `teng_init` pencere açmaz. Editörün açtığı paylaşımlı
belleğe (`platform/game_channel.hpp`) bağlanır ve oyunu editörün **Oyun sekmesinin içinde** oynatır:

| ne | nasıl |
|---|---|
| çizim | headless yolu (offscreen); her kare kanala yazılır, editör onu Oyun sekmesinde gösterir |
| ölçü | kanalın ölçüsü (editörün Oyun sekmesi); `motor_ac`'ın istediği ölçü **yerine** geçer ve loglanır |
| girdi | editör, Oyun sekmesi odaktayken kendi tuşlarını ve fareyi (kare pikselinde) yollar; `tus`, `fare_x`, dokunuş aynı yoldan okunur |
| zaman | gerçek saat, 60 Hz tempo (ölçüldü: 59.7 kare/s; tempo kapatılınca aynı sahne 6851 kare/s — kapı üst sınırla tempoyu ölçüyor) |
| `teng_headless` | **0**: kullanıcı var, oyunlar otopilota geçmesin |
| duraklat / tek adım | oyunun kendi döngüsü `kare_basla` içinde bekler; betik, fizik, zaman aynı yerde durur |
| durdur | `calisiyor()` bir sonraki karede 0 döner, oyun kendi `bitir` yolundan kapanır |
| editör ölürse | kalp atışı 5 s durursa oyun kendini kapatır (`TULPAR_ENGINE_GOMULU_ZAMAN_ASIMI_MS` ile değişir) |

Kanal açılamazsa oyun **pencereye düşmez**, `motor_ac` false döner ve sebep loglanır: editör görüntünün
sekmesine gelmesini bekliyor, ayrı bir pencere "F5 ne yaptı" sorusunu cevapsız bırakırdı. Kapılar
`tests/test_game_channel.cpp`: protokol (tek süreç, iki eşleme), gerçek süreç sınırı (GPU'suz kobay) ve
gerçek köprü (kutu çizer, editörden gelen W rengini değiştirir).

## 6. Android

`tulpar build --target=android oyun.tpr out --apk`. `import "engine"` gören AOT, tame yerine motor
arşivlerini bağlar (`engine_link_flags()` / android kolu). Giriş noktası: `android_host.cpp`'deki
`android_main` → Tulpar objesinin `main`i (raylib'in `rcore_android` deseni; sembol doğrudan bildirilir,
yoksa `-Wl,--no-undefined` **link'te** patlar, cihazda sessiz kalmaz).

Arşivler (gitignore'lu, bir kez üretilir):
```bash
android/build_tame_android.sh            # libtulpar_runtime_android.a (+tame)
tools/build_bridge_android.sh     # libtulpar_engine_android.a + libengine_*.a
```
Font: APK varlığı yoksa `/system/fonts/Roboto-Regular.ttf` yedeği devreye girer; denenen her aday loglanır.

## 6.1 Editörde sahne, Tulpar'da oyun (döngünün kapanışı)

```bash
engine_editor --scene examples/assets/arena.sahne   # düzenle (kullanıcı açar)
engine_sahnec examples/assets/arena.sahne           # -> arena.sahneb (elle; ornekler derlemede uretilir)
./tulpar examples/engine_arena.tpr                  # oyna
```

`tulpar/examples/assets/*.sahne` motor derlenirken `.sahneb`'e çevrilir (CMake hedefi `engine_example_scenes`,
`engine_sahnec`'e ve modellere bağlı): temiz klonda da blob hazırdır, blob sürümü değişince örnekler kendiliğinden
yeniden derlenir. Bu yüzden `engine_aksiyon.tpr` haritayı ikinci kez kodda kurmaz; blob yoksa nedenini söyleyip kapanır.

`eng_scene_load` blob'u açar: modeller, ışıklar, gövdeler ve dünya ayarları (güneş, ortam, gölge hacmi)
sahneden gelir; **kamera betiğin** (oyun onu her kare sürer). Sahne varlıkları `eng_scene_find(ad)` ile
bulunur, konumları `eng_scene_x/y/z` ile okunur — dinamik gövdeler sim'den, sabitler yazar konumundan.
`examples/engine_arena.tpr` bunu kullanıyor: duvarlar, kasalar, ışıklar ve hedef şeridi sahneden, oyuncu
topu ile skor mantığı betikten.

`.sahneb` türetilmiş ve gitignore'lu olduğu için örnek, dosya yoksa aynı areni **kodda** kurar ve derleme
komutunu loglar. Böylece örnek her koşulda çalışır ve iki yolu da gösterir.

Android'de varlıklar APK'dan çıkarılır (alt dizinler dahil, JNI ile; Tuzaklar 8ag) ve çalışma dizini çıkarma
kökü yapılır: `examples/assets/arena.sahneb` yolu masaüstünde de cihazda da aynı şekilde çözülür.

## 7. Ölçülen (2026-09-15)

| ne | nerede | sonuç |
|---|---|---|
| ilk oyun, pencersiz | masaüstü RTX 5080 | 120 kare, 11 çizim, p50 13.1 ms, hata 0 |
| ilk oyun, cihaz | Android emülatörü (x86_64, GFXStream) | **60 fps** (p50 16.66 ms), 11 çizim, 11 gövde |
| dokunmatik | emülatör | sanal joystick yürüttü, sağ yarım tap `zipla` logladı |
| oyun mantığı | emülatör | kutu devrildi → **puan 1** (Tulpar tarafındaki skor döngüsü) |
| arena, cihaz | Android emülatörü | 59–60 fps, 13 çizim, 12 gövde, sahne APK'daki blob'dan (özet masaüstüyle aynı) |
| arena, oynanış | emülatör | kasa yeşil şeridi geçti, skor 1/6; 2880 karede **temiz kapanış**, 0 hata |
| C++ kapısı | `engine_tests` | `bridge_runs_a_scripted_game_headless` dahil; tam koşu **141/141, 0 atlandı** (2026-09-15 akşamı). Bu kapı kapanışta 14 hata logluyor ve yine de PASS: hepsi bilerek koşturulan hata yolları (ölü nesil, sabit gövdeye kuvvet, kare dışında çizim, bilinmeyen tuş, kapalı ses cihazı) |
| Tulpar kapısı | `tests/engine_bridge.test.tpr` | **17** test (kurulum, varlık, fizik, itme, ölçüm, hata sayacı, tuş adı, renk, ses, animasyon, sahne+bölüm, ışın, yakınlık, navmesh, arayüz, kalıcı kayıt, sıcak yükleme) |
| "Gölge Salonları" otopilotu | masaüstü, pencersiz | `kare=3200 bolum=2 gecis=1 oldurulen=9 kalan_dusman=0 can=26 skor=900 navmesh=true hata=0` (p50 12.9 ms) |
| ses, cihaz | Android emülatörü | AAudio açıldı, WAV klip APK'dan yüklendi, oyun 60 fps sürdü |
| parlama | masaüstü | açık/kapalı 26 857 piksel fark (arka plan eşitlendikten sonra) |

Kapıların kontrolleri: kutu **zeminli** y=0.480 / **zeminsiz** y=−15.13; sahne karesi ile boş kare arasında
37 402 piksel fark, **iki boş kare arasında 0** (yazıcı sabit çıktı vermiyor); ölü id çağrısı hata sayacını
**tam bir** artırır, canlı id **artırmaz**; geçersiz tuş adı hata, geçerli ad sessiz.

## 7.9 Motor → Tulpar: betik yaşam döngüsü

Köprü uzun süre **tek yönlüydü** (Tulpar çağırır, motor cevap verir) ve sebebi FFI'nin callback
taşımamasıydı. Bu yön **dil tarafından** kurulur, çünkü Tulpar zaten bir fonksiyonu **adıyla** çözüp
çağırabiliyor (`call()` builtin'inin mekanizması). Motor için yeni bir derleyici özelliği gerekmedi.

Motor **Tulpar tipi görmez**. `bridge/engine_api.h` iki işlev işaretçisi alır:

```c
typedef struct TengScriptVm {
  int (*has)(const char *fn);                                   // böyle bir fonksiyon var mı
  int (*call)(const char *fn, const double *args, int argc);    // çağır (argc <= 8)
} TengScriptVm;
void teng_set_script_vm(const TengScriptVm *vm);
```

Kurulum `aot_eng_init_ptr` içinde, `teng_init`ten **önce** yapılır — bu yüzden VM işaretçisi
`Bridge`in **dışında** bir dosya-kapsamlı değişkende durur; içinde saklansaydı o anda `Bridge`
henüz yok olduğu için kurulum sessizce kaybolurdu.

**Ad sözleşmesi:** `betik "davranis/kovala.tpr"` → taban `kovala` → dört kanca:

| kanca | imza | ne zaman |
|---|---|---|
| `baslat` | `kovala_baslat(id)` | sahne yüklenince bir kez |
| `guncelle` | `kovala_guncelle(id, dt)` | her kare, **sim adımlarından sonra** |
| `carpisma` | `kovala_carpisma(id, diger, olay, x, y, z, hiz)` | her çarpışma olayı, `guncelle`den **önce** |
| `bitir` | `kovala_bitir(id)` | sahne boşalırken / sıcak yüklemede / kapanışta |
| `tetik_girdi` / `tetik_cikti` | `alarm_tetik_girdi(id, diger, kopru)` | **bölgenin** betiği: bir gövde tetik hacmine girdi/çıktı |
| `bolge_girdi` / `bolge_cikti` | `devriye_bolge_girdi(id, bolge)` | **girenin** betiği: kendisi bir tetik hacmine girdi/çıktı |

Dördü de **isteğe bağlı**: motor hangisini bulursa onu çağırır. Hiçbiri bulunamazsa bu ayrı bir
durumdur ve görünür hata basar (aşağı bak).

Kancalar **yükleme anında** çözülür, kare içinde değil: `has` bir sembol araması ve ikili koşum
boyunca değişmiyor.

**`guncelle` sim adımlarından SONRA**: betiğin okuduğu konum ve hız o karenin fizik sonucu olsun.
Önce çağrılsaydı betiğe **bir kare eski** durum görünürdü ve "kovalama neden geriden geliyor" diye
aranırdı.

**`carpisma` `guncelle`den ÖNCE**: olay bu karenin sim adımlarında oluştu, betik aynı karenin
`guncelle`sinde ona göre davranabilsin.

- `diger` = karşı tarafın **sahne indisi**; köprü varlığı ya da sahne dışı bir gövde ise **-1**.
- `olay` = çarpışma halkasındaki indis. Yüzey normali argüman olarak **yok**, çünkü 7 + 3 = 10 ve
  dinamik çağrının tavanı 8; ayrıntı aynı kare içinde `carpisma_ny(olay)` … ile okunur (halka bir
  sonraki karenin sim adımlarına kadar duruyor).
- **Motor tekilleştirmez.** Aynı çift bir karede birden çok olay üretebilir (temas noktası başına,
  sim adımı başına) ve kanca her olay için çağrılır; zemin üstünde duran bir gövde her kare temas
  üretir. Süzgeç betiğin işi: "üç noktadan çarptı" ile "üç kez çarptı" farkını yalnız oyun bilir.
- İki taraf da betikliyse **ikisi de** haber alır: bir olay, iki çağrı.
- Kancası olmayan bir oyun halkayı hiç taramaz (ayrı bayrak; `guncelle` kullanan bir oyunda da
  açık olan `script_any` yetmezdi).

**`bitir` yıkımdan ÖNCE** çağrılır: betik bu çağrı sırasında sahneyi hâlâ sorgulayabilir
(`sahne_adi(id)`, konum, gövde). `despawn`dan sonra çağrılsaydı betiğe **boş** bir sahne görünürdü.
Kapanışta da iş sistemi/fizik/cihaz sökülmeden önce çalışır, yani kanca içinden motor çağırmak
güvenlidir. Çağrıldıktan sonra kanca tablosu **kapanır**: boşaltma ve kapanış üst üste gelebilir ve
kapanmasaydı `bitir` ikinci kez koşardı. Sıcak yükleme de buradan geçer: `bitir` → yeniden yükle →
`baslat`.

**Tetik (bölge) kancaları.** Gövde kartında "Tetik" işaretli gövde (metinde govde satırının ardından
tek başına `tetik`, blobda `SceneBlobBody::flags` bit0) çarpışma **tepkisi üretmez**, içinden geçilir.
İki taraf ayrı adlarla haber alır: bölgenin betiği `<ad>_tetik_girdi(id, diger, kopru)` — `diger` giren
gövdenin sahne indisi ya da -1, `kopru` köprünün ürettiği varlığın id'si ya da 0 (oyuncu çoğu oyunda
kodla üretiliyor) — girenin betiği `<ad>_bolge_girdi(id, bolge)`. Aynı olayda önce bölgenin kancası.

- Sensör **kinematik ve hep uyanık** kuruluyor, statik değil: Jolt'ta statik sensör yalnız aktif
  gövdeleri görür ve içinde **uyuyan** gövde için "çıktı" üretir. Ölçüldü (2026-09-23): sensörü geçici
  olarak statik yapınca zemine inip uyuyan top için sahte bir çıkış geldi; kinematik ile 0.
- Kendi nesne katmanında: statik gövdeler (zemin) bölgeye **girmez**, sensörler birbirini görmez.
- Işın testi sensörü **görmez** (görünmez bir hacim görüş hattını kesmez); sensör teması çarpışma
  halkasına **girmez** (bölgeden geçmek çarpmak değil).
- Jolt geri çağrımları iş parçacıklarından belirsiz sırada geliyor; köprü olayları (adım, sensör,
  diğer) ile sıralayıp dağıtıyor — aynı sahne her koşumda aynı kanca sırasını görür (iki koşumun
  `[tpr]` satırları aynı çıktı).
- Başlangıçta içeride duran gövde ilk adımda "girdi" sayılır; içindeyken silinen gövde bir "çıktı"
  bırakır. `tetik_*` yazılmış ama varlık tetik değilse motor bunu yüklemede **hata** olarak söyler.
- **Kodla üretilen tetik:** `tetik_kutu(x, y, z, hx, hy, hz)` / `tetik_kure(x, y, z, r)` —
  görünmez, yakınlık sorgusunda hedef değil; `isinla` tetiği **taşır**, yeniden kurmaz (yeniden
  kurmak içeride duran gövde için sahte bir "girdi" üretiyordu — ölçüldü). Betiği yok; olayları
  `tetik_olay_*` kuyruğunda. Kuyruk sahne tetiklerini de taşır, betik atamayan oyun için.
- **Karakter denetleyicisi tetikler:** sanal karakterin dünyada gövdesi yok; ona kendi katmanında
  (yalnız sensörlerle eşleşen) kinematik bir **iç gövde** veriliyor. Ölçüldü: iç gövdesiz karakter
  geçitten geçerken 0 giriş üretti, iç gövdeyle 1 giriş + 1 çıkış; içeride uzun süre dururken sahte
  çıkış yok. İç gövde dinamik/statik gövdelerle **rijit temas kurmaz** (itmeyi sanal karakter
  kendisi yapıyor; ikinci bir iten gövde kutuları iki kat iterdi).

Sahne varlıkları tek tek silinemediği için `bitir`in tetikleyicisi **sahne boşalması**dır, varlık
ölümü değil. Köprünün kendi ürettiği varlıklar (`kutu`, `kure`, `karakter`, `tetik_kutu` …) bu
tabloya girmez; onlara betik **kodla** bağlanır — §7.10.

**AOT SINIRI — en önemli madde.** Tulpar'da yorumlayıcı yok: bir betik ancak oyunun ikilisine
**derlenmişse** (yani oyun onu `import` etmişse) çalışır. Tasarımcının editörde atamış olması
yetmez. Motor bunu yükleme anında **görünür hata** ile söyler:

```
HATA betik kancasi YOK: "davranis/henuz_yazilmadi.tpr" ->
     henuz_yazilmadi_baslat/_guncelle/_bitir/_carpisma bulunamadi (oyun bu dosyayi import etti mi?)
```

Sessiz kalsaydı nesne hiçbir şey yapmaz ve sebebi "acaba fizik mi bozuk" diye aranırdı.

Kapalı betik (editördeki "Etkin" kutusu) hiç çözülmez.

**Ölçüldü** (2026-09-22, `tulpar/examples/engine_betik_dagitimi.tpr`, 240 kare pencersiz):

```
[tpr] kovala basladi: kovalayan
[tpr] devriye basladi: devriye
HATA betik kancasi YOK: "davranis/henuz_yazilmadi.tpr" -> ... (oyun bu dosyayi import etti mi?)
betik kancalari: 2 cagri, 1 eksik
[tpr] kovala YAKALADI: hiz 2.4979166984558105 normal y 7.361173288700229e-07   (kare 192)
[tpr] kovala bitti: kovalayan, 1 kez yakaladi                                   (kare 240)
[tpr] devriye bitti: faz 4.000000208616257
betik: 2 varlikta `bitir` calisti (kapanis)
```

Normal y'nin ~0 olması yan yana iki kürenin yatay teması demek — sayı halkadan `carpisma_ny(olay)`
ile, yani kancaya verilen indisin **canlı** olduğunun kanıtı. Zemin temasları log'da yok: betiğin
`diger != kovala_hedef` süzgeci onları eliyor.

Motor tarafı kapısı Tulpar'a hiç ihtiyaç duymaz — `tests/test_bridge.cpp` sahte bir `TengScriptVm`
kurar ve dört kancanın da argüman sayısını, hedefini ve `olay` indisinin okunabilirliğini ölçer.

## 7.10 Kodla üretilen varlığa betik bağlama (2026-09-24)

Sahne kancaları yalnız editörde yerleştirilen varlıklara gidiyordu; `kutu`, `kure`, `karakter`,
`tetik_kutu` ile **kodla** üretilen varlıkları oyun kendi dizisinde tutup her karede elle
dolaşıyordu. Aynı yaşam döngüsü artık onlara da bağlanıyor:

```tulpar
int d = kure(x, 0.5, z, 0.4, true, KIRMIZI);
betik_ata(d, "dusman");          // ya da "davranis/dusman.tpr": taban ad, sahneyle aynı kural
```

| builtin | Tulpar | ne yapar |
|---|---|---|
| `eng_script_attach(id, ad)` | `betik_ata` / `script_attach` | bağla; `<ad>_baslat(id)` **hemen**. false = reddedildi (sebep HATA olarak logda) |
| `eng_script_detach(id)` | `betik_kaldir` / `script_detach` | çöz; önce bağlantı kalkar, sonra `<ad>_bitir(id)`. false = bağlı betik yoktu (hata değil) |
| `eng_script_name(id)` | `betik_adi` / `script_name` | bağlı betiğin taban adı, yoksa `""` |
| `eng_script_count()` | `betikli_sayisi` / `script_count` | betik bağlı köprü varlığı sayısı |

**Kancalar** sahne kancalarının adlarıyla, **aynı yerde ve aynı aşama sırasıyla** çağrılır —
`eng_frame_end` içinde, sim adımlarından SONRA: tetik → çarpışma → güncelle. Her aşamada önce
sahne kancaları (eski yerlerinde, eski sıralarıyla), sonra kodla bağlananlar.

| kanca | imza | ne zaman / sıra |
|---|---|---|
| `baslat` | `dusman_baslat(id)` | `betik_ata` çağrısının içinde, hemen |
| `guncelle` | `dusman_guncelle(id, dt)` | her kare, **yuva sırasıyla** (bağlama sırasıyla değil) |
| `carpisma` | `dusman_carpisma(id, diger, olay, x, y, z, hiz, diger_sahne)` | her temas olayı; **(yuva, karşı gövde, nokta, şiddet) ile sıralı** |
| `tetik_girdi` / `tetik_cikti` | `tuzak_tetik_girdi(id, diger, diger_sahne)` | varlık kodla üretilmiş bir **tetik**se: biri girdi/çıktı |
| `bolge_girdi` / `bolge_cikti` | `dusman_bolge_girdi(id, bolge, bolge_sahne)` | varlık bir tetiğe (sahneden ya da koddan) girdi/çıktı |
| `bitir` | `dusman_bitir(id)` | `betik_kaldir`, `sil`, ikinci `betik_ata`, kapanış |

**Kimlik kuralı — `diger` ne?** İkinci argüman `id` ile **aynı uzaydadır**: sahne kancasında sahne
indisi (-1 = değil), kodla bağlananda **köprü id'si** (0 = köprü varlığı değil). Son argüman
aynı tarafın **karşı uzaydaki** kimliği: köprü kancasında sahne dizini (-1 = değil). Sahnedeki
`tetik_girdi(id, diger, kopru)` ile aynı desen, uzaylar yer değiştirmiş. Böylece kodla üretilen
bir düşman, köprü varlığı oyuncuya da (`diger` = oyuncu), editörde yerleştirilmiş bir duvara da
(`diger` = 0, `diger_sahne` = duvarın dizini) çarptığını tek kancada ayırır. Sondaki argüman
**isteğe bağlı**: Tulpar'ın dinamik çağrısı callee'nin aritesine göre fazlasını düşürür (ölçüldü:
7 parametreli `sonda_carpisma` 8 argümanla hatasız çağrıldı), `tuzak_tetik_girdi(id, diger)`
yazmak yeter. Çarpışmada 8 argüman dinamik çağrının tavanı; yüzey normali sahnedeki gibi
`carpisma_nx(olay)` ile okunur.

**Yaşam döngüsü kuralları** (hepsi `tests/test_bridge.cpp` 10d'de ölçülü):
- **Her `baslat`a tam bir `bitir`.** `bitir` sırasında varlık hâlâ canlı (konum, hız, ad okunur),
  bağlantı ise çoktan kalkmıştır — kanca içinden aynı varlığa `sil` / `betik_kaldir` ikinci bir
  `bitir` doğurmaz. Kapı sonda `baslat` toplamı = `bitir` toplamı ölçüyor (551 = 551).
- **İkinci `betik_ata` öncekinin YERİNE geçer**: önce eskinin `bitir`i, sonra yeninin `baslat`ı.
  Durum makinesi böyle kurulur (`betik_ata(d, "kovala")` düşman oyuncuyu görünce). Aynı adla tekrar
  bağlamak betiği yeniden başlatır. **Reddedilen** bağlama öncekine dokunmaz.
- **`sil(id)`** bağlı betiğin `bitir`ini yıkımdan ÖNCE çağırır; **kapanışta** (`motor_kapat`)
  bağlı kalan her varlık yuva sırasıyla `bitir` alır — iş sistemi ve fizik sökülmeden önce.
- **Kanca içinden** üretip bağlamak serbest; yeni bağlantı o karenin kalan kancalarını görmez
  (olaylar ondan önce oluştu), ilk `guncelle`si bir sonraki karede (ölçüldü: doğduğu kare 1216,
  ilk güncelle 1217). Oyun kodundan (kare içinde) bağlanan ise aynı karenin sonunda her şeyi görür.
- Kendi `bitir`inin içinden varlığa betik bağlanamaz (HATA); kendini silmek yok sayılır (dıştaki
  `sil` tamamlar, çift serbest bırakma yok).
- Sahne geçişi (`sahne_bosalt` / sıcak yükleme) kodla bağlananları **etkilemez**: ömürleri köprü
  varlığınınki. Sayaçları da ayrı (`teng_script_call_count` sahne yüklemesinde sıfırlanıyor).

**Görünür hatalar** (sessiz 0 yok): hiç kancası bulunamayan ad
(`HATA betik kancasi YOK: #262145 "yok_boyle" -> yok_boyle_baslat/_guncelle/.../_bolge_* bulunamadi (oyun bu dosyayi import etti mi?)`
— sahnedekinin aynısı), ölü id (her üç çağrıda), id 0 ile bağlama, VM kurulu değil, `tetik_*`
kancalı betik tetik olmayan varlığa (bağlanır ama kanca çağrılmaz — sahneyle aynı kural), kapanışta
bağlama, havuz dolu. **Kapasite sabit**: 512 bağlantı (`kMaxBoundScripts`, köprü nesnesinde sabit
dizi); dolunca HATA + false, reddedilenler kapanış raporunda sayılır. `betik_adi(0)` ve
`betik_kaldir(0)` ailenin "0 = yok" kuralıyla sessizdir — kanca içinde `betik_adi(diger)` diger = 0
iken güvenle çağrılsın.

**Belirlenimlilik.** `guncelle` yuva sırası; tetik olayları zaten (adım, sensör, diğer) ile
sıralıydı. Çarpışma halkası ise **değildi**: Jolt temasları iş parçacıklarından yazıyor, aynı sahne
her koşumda aynı olayları **farklı sırada** veriyor (Tuzaklar 8cc). Kodla bağlanan çarpışma
kancaları bu yüzden toplanıp sıralanarak çağrılıyor. Ölçüldü (2026-09-24, masaüstü, 8 koşum, 48
küre aynı adımda zemine): kanca çağrı izi **1/8** farklı, aynı karelerin halka izi **8/8** farklı.
Sahne `carpisma` kancaları eski (halka) sırasında kaldı — davranışları bu işte değişmesin diye.

**Örnek: `tulpar/examples/engine_dalga.tpr`** — dört dalga düşman (4 + 6 + 8 + 10), her biri
`betik_ata(d, "dusman")`, dört görünmez tuzak `betik_ata(tetik_kutu(...), "tuzak")`. Oyunun
döngüsünde düşman dizisi ve düşman döngüsü **yok**; dalganın bittiğini `dusman_canli` sayacından
okur (betik değişkeni: varlık başına değil, betik başına). Satır sayısı (yorumlar dahil / yorumsuz
kod): oyun 76 / 53, `davranis/dusman.tpr` 40 / 19, `davranis/tuzak.tpr` 17 / 6. Pencersiz kipte
kendi oynar:

```
$ TULPAR_ENGINE_HEADLESS=2400 DISPLAY= ./tulpar examples/engine_dalga.tpr
[kapi] kare=972 dalga=4 dogan=28 tuzakta=25 carpan=3 kalan=0 bagli=4 can=7 hata=0
```

(4 koşumda bayt bayt aynı; `bagli=4` kapanışa kalan dört tuzak, onlar da `motor_kapat`ta `bitir`
alıyor.) Kapılar: C++ `bridge_runs_a_scripted_game_headless` bölüm 10d (sahte VM: sıra, argüman,
yeniden giriş, havuz tavanı, kapanış), Tulpar `tests/engine_bridge.test.tpr` →
`run_betik_bagla` (gerçek Tulpar kancaları, float gelen id'nin köprü id'sine eşitliği).

Kapsam dışı: karakter denetleyicisinin sanal temasları çarpışma halkasına girmediği için
**karakter**e bağlı betik `carpisma` almaz (tetik ve `guncelle` alır); sahne varlığının `bolge_*`
kancası yalnız sahne bölgelerinde çalışır (kodla üretilen bölgeye giren sahne varlığını bölgenin
kendi `tetik_girdi`si `diger_sahne` ile görür).

## 7.11 Nesne özellikleri: editörde varlık başına değer (2026-09-25, E4)

Tasarımcı aynı betiği on düşmana verir ve her birine editörde **kendi** değerini yazar
(`can = 250`, `hiz = 5`, devriye noktası) — kod yazmadan. Veri modeli ve `.sahne` biçimi E3'te
geldi (`ozellik_tam "can" 250` …); bu adımda değerler `.sahneb`'ye (blob **v8**) ve köprüye
girdi. E3'ten beri `engine_sahnec` ile editörün **Derle**'si "özellikler `.sahneb`'ye girmiyor"
uyarısı basıyordu; o uyarı kalktı.

**Yalnız üstüne yazılanlar taşınır.** Varsayılan betiğin KODUNDA yaşar ve çağrıya argüman
olarak gelir; sahnede kayıt yoksa o argüman **değişmeden** döner ve bu **hata değildir**.

```tulpar
func muhafiz_baslat(i) {                                   // examples/davranis/muhafiz.tpr
    int can = ozellik_tam(i, "can", 100);
    float hiz = ozellik_sayi(i, "hiz", 3.5);
    bool kalkan = ozellik_bayrak(i, "kalkan", false);
    Vec3 a = ozellik_nokta(i, "devriye_a", v3(-2.0, 0.0, 0.0));
}
```

Adı ve varsayılanı **literal** olan bir çağrı bildirimin kendisidir: editör (E5) betiği tarayıp
denetçide "can: 100 (varsayılan)" gösterecek. Ad/varsayılan bir değişkenden gelirse değer yine
okunur, yalnız denetçide listelenmez.

| builtin | Tulpar (TR / EN) | ne yapar |
|---|---|---|
| `eng_scene_prop_num(i, ad, vars)` | `ozellik_sayi` / `prop_num` | `sayi` (float); yazılmamışsa `vars` |
| `eng_scene_prop_int(i, ad, vars)` | `ozellik_tam` / `prop_int` | `tam` (\|v\| ≤ 2^24, float'ta tam) |
| `eng_scene_prop_flag(i, ad, vars)` | `ozellik_bayrak` / `prop_flag` | `bayrak` (evet/hayır) |
| `eng_scene_prop_point(i, ad, lx, ly, lz)` + `_px/_py/_pz` | `ozellik_nokta` / `prop_point` (→ `Vec3`) | `nokta`, **dünya** konumu; "hesapla sonra oku" |
| `eng_scene_prop_has(i, ad)` | `ozellik_var` / `prop_has` | üstüne yazılmış mı (herhangi tür) |

**`nokta` dünya uzayında.** Blob düzleştirilmiş bir sahne (§ sahne ağacı): üstüne yazılmış nokta
derlemede `scene_prop_point_world` ile çevrilir — varlığın dünya konumu + dünya dönüşü × yerel
ofset, **ölçek yok** ("2 m ileride" varlık büyütülünce 4 m olmasın). Yazılmamış noktanın
varsayılanı (betikteki yerel ofset) köprüde **aynı kuralla** çevrilir
(`SceneBlobView::point_world`: blob'daki `pos` + `quat`, aynı işlem sırası). Sonuç ölçüldü:
aynı yerel ofsetten varsayılan ve üstüne yazılmış nokta **bit-tam aynı** dünya noktasını verir
(C++ 4.8 ve Tulpar `run_ozellik`). Nokta varlığın **yazar** pozuna bağlı: düşman yürüse de devriye
noktası tasarımcının koyduğu yerde kalır. Geçersiz dizinde çevrilecek dönüşüm yoktur:
`(lx, ly, lz)` olduğu gibi döner.

**Hata kuralları** (sayaç artar, varsayılan döner — oyun durmaz): sınır dışı/sahnesiz dizin; tür
uyuşmazlığı (`tam`ı nokta olarak okumak; noktada varsayılan yine dünyaya çevrilir); 23
karakteri aşan ya da `[a-z0-9_]` dışı ad — editör böyle bir ad yazamaz, yani o çağrı **hiç**
eşleşmez ve sessiz kalsaydı bir yazım hatası "tasarımcı değer girmedi" gibi görünürdü. Eksik
özellik (`ozellik_tam(i, "zirh", 0)`, kayıt yok) hata **değildir**.

**Doğumda oku** (`_baslat`), karede değil. Değerler sahne boyunca değişmez. Köprü yüklemede
varlık başına bir **aralık** kurar (`prop_first/prop_n[256]`, sabit dizi, tek geçiş; blob kayıtları
`(varlık, ad)` ile sıralı olduğu için) — ve bunu `_baslat` kancalarından **önce** yapar, çünkü
okumanın beklenen yeri orası (kapı: sahte VM'in `_baslat`ı `can = 250` okuyor; aralık sonra
kurulsaydı -1, yani varsayılan okunurdu, sessizce). Aynı sebeple **navmesh** de artık kancalardan
önce kuruluyor: eskiden kanca döngüsünün sonundaydı ve `baslat` içinde `nav_var()` navmesh'li
sahnede de false dönüyordu (Tuzaklar 8cg). Okuma aralık içinde ada göre tarar
(≤ 16 kayıt), **ayırma yok**: karesiz 1.5 M okuma döngüsü AllocGate'te 0 (bizim kod, her cihazda);
okuyan 10 kare de 0 (RTX 5080 — sürücü kare içinde `new` çağırıyorsa, test_rhi'nin ölçtüğü
MoltenVK gibi, kare sayısı cihaz verisidir ve kapı onu basıp ATLANDI der).

Ölçülen okuma maliyeti (AMD Ryzen 7 9800X3D / RTX 5080 masaüstü, GCC 16.2.1 Release,
2026-09-25; `bridge_runs_a_scripted_game_headless` 4.8, 1e5 okuma × 5 koşumun medyanı, C
ABI doğrudan — Tulpar çağrı yükü hariç; beş koşumun en düşüğü–en yükseği, masaüstü yalıtılmış
değil): bulunan özellik **6.6–8.2 ns**, eksik özellik (4 kaydın tamamı taranır) **12.4–17.7 ns**,
varsayılan nokta (tarama + dünya çevirisi) **17.4–21.8 ns**. Doğumda okunan birkaç değer için ihmal edilebilir;
yine de kare içinde okumanın gerekçesi yok.

**Blob v8** (`content/scene_blob.hpp`): `SceneBlobProp {entity, type, v[3], name[24], reserved}`
= **48 B**; başlık büyümedi (v7'nin `script_reserved0/1`'i `prop_count/prop_offset` oldu), tablo
en sonda (özelliksiz sahnenin bölüm ofsetleri v7'dekiyle aynı), özet aralığı değişmedi. Ad
**kayıtta**, metin tablosunda değil — metin muhasebesine (`plan_of`/`intern`) 16 kalem daha eklemek
v7'de belgelenen "eksik sayılırsa komşu bölümü ezer, özet yakalamaz" riskini büyütürdü.
`scene_blob_open` her kaydı doğrular ve köprünün her varsayımını ölçer: tablo sınırı/hizası,
varlık dizini, tür (1..4), ad (24 baytta NUL, `[a-z0-9_]{1,23}`, NUL sonrası sıfır), değer (sonlu;
`tam` tamsayı ve ≤ 2^24; `bayrak` 0|1; kullanılmayan bileşen +0), sıra `(varlık, ad)`, tekillik,
varlık başına ≤ 16. Her dal bir bozulma fixture'ıyla koşuyor (`scene_blob_open_rejects_corrupt_prop_records`,
23 vaka). `engine_sahnec --dump` tabloyu basar (nokta dünya değeriyle).

Örnek: `tulpar/examples/assets/ozellik.sahne` — dönük (0 90 0), ölçekli (2) bir kaidenin
çocuğu olan "muhafiz" (dört tür üstüne yazılmış) ve dönüşsüz "muhafiz_2" (hepsi varsayılan);
ikisine de `davranis/muhafiz.tpr`. Muhafızın `devriye_a`sı yerel (-2 0 -1.5) → dünya
(8.5 3 -4); dönüşsüz hesap (konum + yerel) 3.54 m ötede olurdu — kapının pozitif kontrolü.

## 8. Kapsam: `SPEC` = `engine_api.h` = **208 builtin**

Sayı iki yerde birden durur ve birbirine karşı denetlenebilir: `bridge/engine_api.h`'deki `teng_*`
bildirimleri ve `tools/gen_engine_bindings.py`'deki `SPEC` satırları. Aile dağılımı (başlıktaki
bölüm yorumlarına göre):

| aile | adet | ne verir |
|---|---:|---|
| yaşam döngüsü | 21 | `eng_init` / `running` / `frame_begin` / `frame_end` / `shutdown`, dt, zaman, kare, fps, ölçü, pencersiz kip, **log ve log seviyesi**, ekran görüntüsü, GPU adı, son hata, **hata ve uyarı sayacı** |
| dünya / kamera | 11 | güneş, ortam, gölge hacmi, yerçekimi, **parlama (bloom)**, kamera (göz+hedef ya da yörünge), kamera konumu |
| derlenmiş sahne (`.sahneb`) | 21 | yükle / boşalt / yüklü mü (**bölüm geçişi**), sayı, ada göre bul, konum, ad, hız, dinamik mi, hız ver, dürtü, **atanmış betik yolu + etkin mi**, **sahne karakteri** (karakter mi, yürü + zıpla, zeminde mi, zıplama hızı) |
| varlıklar (köprü sahibi) | 25 | kutu / küre / zemin / model / ışık / **tetik kutusu / tetik küresi** üret, sil, canlı mı, konum, renk, ölçek, yaw, hız, dürtü, dinamik mi, uyanık mı |
| nesne özellikleri | 8 | `sayi` / `tam` / `bayrak` / `nokta` (dünya; + `_px/_py/_pz`) / var mı — editörde üstüne yazılan değer, yoksa betiğin varsayılanı (§7.11) |
| kodla betik bağlama | 4 | bağla (`baslat` hemen), çöz (`bitir`), bağlı betiğin adı, bağlı varlık sayısı — kancalar sahneninkilerle aynı yerde (§7.10) |
| model animasyonu | 6 | klip sayısı / süresi / adı, varlığa klip ata (hız, döngü), klip zamanı, bitti mi |
| girdi | 12 | tuş basılı / bu karede basıldı, dokunmatik (sayı + konum), sanal joystick (x/y/eylem), bakış deltası, fare (§8.3) |
| 2B arayüz (HUD) | 3 | `eng_text`, `eng_rect`, `eng_text_width` — kare içinde kuyruklanır, `frame_end` çizer |
| anlık-kip arayüz | 12 | `ui_begin`/`ui_end`, tema, etkin/pasif, panel, etiket, **düğme**, **onay kutusu**, **kaydırıcı**, basılı mı, tıklama sayacı, pencersiz doğrulama için **enjekte tıklama** |
| kalıcı kayıt | 10 | dosya bağla, yol, sayı/metin yaz-oku, var mı, diske yaz, temizle, sayı |
| sahne sıcak yeniden yükleme | 6 | izlemeyi aç, yenilendi mi (bir kez), yenileme sayısı, yol, dosya kopyala, dosya değişim zamanı |
| ses | 13 | cihaz aç/kapat/durum/arka uç, klip yükle (WAV/FLAC/MP3), sentetik ton, çal, bip, durdur, hepsini durdur, ana seviye, çalan ses, tepe genlik |
| sorgular: ışın + yakınlık | 13 | `eng_raycast` (+ nokta / normal / çarpılan köprü varlığı / çarpılan sahne varlığı), **küre örtüşmesi** (yakından uzağa sıralı), en yakın |
| navmesh (blob'daki bake) | 13 | bake var mı, poligon sayısı, yol iste, yol kısmi mi, yol noktaları, **en yakın nokta** (mesh dışındaki konumu yapıştır), **doğru görüş** (`raycast` + çarpma parametresi) |
| çarpışma olayları | 13 | sayı, düşen, iki taraf (köprü id + sahne dizini), temas noktası, normal, şiddet — kuyruk |
| tetik olayları | 7 | sayı, düşen, bölge ve giren/çıkan (köprü id + sahne dizini), girdi mi — kuyruk, **belirlenimli sıra** |
| karakter denetleyicisi | 5 | üret (sanal kapsül), yürü + zıpla isteği, zeminde mi, zemin durumu, zıplama hızı — konum/hız/ışınla/sil/yakınlık/ışın/tetik mevcut varlık fonksiyonlarıyla |
| ölçüm | 5 | çizim / gövde / ışık sayısı, son kare p50, **süreç RSS** (`eng_rss_kb` = `bellek_kb()`, kare belleği kapısının aleti) |

(Çarpışma ailesi 2026-09-23'e kadar bu tabloda YOKTU: satırların toplamı 164 veriyordu,
başlık 177 diyordu. Toplam artık başlıkla eşit.)

**Karakter denetleyicisi** (`karakter(x, y, z, r, boy, renk)`): Jolt `CharacterVirtual` — rampada
kaymaz, 0.4 m'ye kadar basamağı yürüyerek çıkar, zemine yapışır, dinamik gövdeleri en çok 100 N ile
iter. Konum **ayak tabanı**. Hız `karakter_yuru(id, vx, vz, zipla)` ile verilir: yatay istek
**kalıcı** (durmak için 0, 0), zıplama kenar-tetikli, dikey hız motorun. `konum_*`, `hiz_*`,
`isinla` (hız sıfır), `sil`, `en_yakin`, ışın (karaktere **çarpar**) ve tetikler karakterde de
çalışır; `hiz_ver`/`itme` karakterde **hata** verir (sessizce yok sayılsaydı "neden itilmiyor"
diye aranırdı). Işının `skip_id`'si her türde **tam**: o gövde Jolt filtresiyle yok sayılır. Eski
yaklaşım (çarpınca gövdeyi kuşatan küre kadar ileriden yeniden at) gövdeye bitişik ~1 m'lik bir kör
bölge bırakıyordu; `engine_aksiyon` farkında olmadan buna göre dengelenmişti — düşmanın engel
yoklaması oyuncudan **sekiyordu**. Tam filtreye geçince oyun yeniden ayarlandı (engel yoklaması
yalnız sahne geometrisini engel sayar, vuruş hasarı 34 → 50); ölçüm tablosu oyunun başında. Örnek:
`tulpar/examples/engine_karakter.tpr` (merdiven, tetik, zıplanan duvar; penceresiz kipte kendi
oynar, `[kapi]` satırı iki koşumda aynı). Kapsam dışı: karakterin kendi çarpışma olayları
(sanal temaslar) çarpışma kuyruğuna girmiyor.

**Sahne karakteri:** editörde "Karakter Kontrolcüsü" bileşeni olan varlık sahne yüklenince karakter
olarak doğar (blob v6'dan beri karakter tablosunu yazıyordu, okuyan yoktu). Kapsül yazar konumuna
**ortalı** — gövde bileşeni gibi; `sahne_y(i)` merkezi verir (koddan üretilen karakterde konum ayak
tabanı: iki sözleşme bilerek farklı, biri yazarın yerleştirdiği nesneye, öteki koda uyuyor). Aynı
varlıkta gövde bileşeni de varsa (editörün hazır nesnesi ikisini birden koyuyor) gövde
**doğurulmuyor**: ölçüldü, ikisi birden doğunca iç içe kutu karakteri itti ve 3.00 m'lik yürüme
3.79 m çıktı. Sürmek için `sahne_karakter_yuru(i, vx, vz, zipla)`; betik kancaları ve tetikler sahne
dizinleriyle çalışır. Örnek: `engine_betik_dagitimi`'ndeki "nobetci" (`davranis/nobet.tpr`).

**Ne verilmez (bilinçli):** struct, callback, işaretçi, çıktı parametresi — Tulpar'ın bugünkü FFI'si
taşımıyor. Bunun görünür sonuçları var: (1) **çarpışma olayı geri çağrım değil kuyruktur** — fizik
adımındaki temaslar sabit boy halkaya yazılır, oyun karede okur (2026-09-16); (2) çok değerli sorgular
"hesapla + erişimci" kalıbıyla verilir (`eng_nav_nearest` sonra `eng_nav_near_x/y/z`), çünkü çıktı
parametresi yok; (3) onay kutusu ve kaydırıcı **yeni değeri döndürür**, betik geri yazar;
(4) motor **Tulpar'a geri çağrı yapabilir** ama bunu KENDİSİ kuramaz — köprüyü dil tarafı kurar
(aşağı bak).

**Ne motorda YOK ve bilerek yok:** yol TAKİBİ. Motor yol ARAR (Detour); ajanı yolda yürütmek
`lib/engine.tpr` içinde saf Tulpar'dadır (`ajan_olustur` / `ajan_hedef` / `ajan_ilerlet`). Takip oynanış
politikasıdır — hız, varış yarıçapı, yeniden arama sıklığı her oyunda farklıdır; motora gömmek her oyunu
aynı davranışa mahkûm ederdi. Beğenmeyen bu katmanı kopyalar, motoru değiştirmez. Yol noktaları ajan
başına **kopyalanır**: motorun nokta tamponu tektir, iki ajan aynı karede yol ararsa kopyalamayan bir
tasarım sessizce birbirinin yolunu takip ederdi (kapı bunu ölçüyor — `tests/engine_bridge.test.tpr`).

**Tulpar kitaplığındaki oyun parçaları (2026-09-24).** Köprü düz skaler kalıyor; vektör ve oynanış
yardımcıları `lib/engine.tpr`'de, Tulpar'ın kendisinde:
- `Vec3` (dilin kutusuz struct'ı): `konum(id)`, `yuru_v`, `itme_v`, `isinla_v`, `yon_xz`, `uzaklik_xz`,
  `normal_xz`, `yaw_yonu` / `yon_yaw`, `kameraya_gore`. Bileşenleri tek tek okuyup ayrı değişkende
  tutmak "Gölge Salonları"nda 27 satırdı.
- `yol_yonu(id, hedef)`: navmesh varsa Detour yolunun ilk köşesi, yoksa düz çizgi; önündeki SAHNE
  geometrisinden normal boyunca kayar (köprü varlıkları engel sayılmaz).
- `goruyor_mu(bakan, hedef, yaw, menzil, cos_esik)`: menzil + bakış konisi + görüş hattı.

Hepsinin kapısı `tests/engine_bridge.test.tpr` (`run_vektor`, `run_yol_gorus`). Aksiyon oyunu bunlarla
yeniden yazıldı: 1187 satırdan 729'a; 3200 karelik doğrulama özeti bayt bayt aynı kaldı. Oyun kodu
struct alanına bileşik atama kullanıyor (`dusmanlar[i].can -= 50`); bu TulparLang #344'ten önce sessizce
hiçbir şey yapmıyordu. `tools/motor_derleyici.sh` kurulumdan sonra bunu bir sonda programıyla doğrular.

**Arayüz yerleşimi (2026-09-24, yalnız `lib/engine.tpr`; köprü değişmedi).** Anlık-kip widget'ları
(`dugme(ad, x, y, w, h)` …) her düğmenin yerini ister; menü kodu yerleri elle topluyordu
(`y + 70`, `y + 140`) ve pencersiz doğrulama her düğmenin tıklama noktasını ayrı bir globalde
saklıyordu. Kitaplık artık bir **dikey yığın** veriyor: imleç (x, y, genişlik, satır yüksekliği,
aralık) widget'ları alt alta dizer.
- `ui_pencere(baslik, w, h, renk)` ekranda ortalanmış başlıklı panel kurar ve imleci içine koyar;
  `ui_dugme(ad)`, `ui_kaydirici(ad, deger, en_az, en_cok)`, `ui_onay_kutusu(ad, deger)`,
  `ui_etiket(yazi, olcek, renk)` / `ui_etiket_orta`, `ui_bosluk(h)`, `ui_satir(h)` / `ui_ara(a)`
  koordinat almaz. `ui_pencere_bitir()` içerik panele sığmadıysa **UYARI** verir ve gereken
  yüksekliği söyler (panel içerikten ÖNCE çizilir; önceki karenin ölçüsüyle otomatik boy seçilseydi
  ilk karedeki konum farklı olur, o karede istenen test tıklaması ıskalardı). Panelsiz yığın:
  `ui_yerlesim(x, y, w, satir, ara)`. EN ikizleri `ui_window`, `layout_button`, `layout_slider` …
- **Ölçek:** yerleşimdeki her sayı 720p'lik tasarım birimidir, `ui_olcek()` = `yukseklik() / 720` ile
  çarpılır; köprünün fontu da yükseklikle yüklendiği için (1080p'de 28 px) düğme ve yazısı birlikte
  büyür. Koordinatlı widget'lar ham piksel almaya devam eder, onlar için `ui_px(v)`. Dikdörtgenler tam
  piksele yuvarlanır: oranlı tıklama float'ta TAM düşer (izin %25'i 0.25 verir; 0.3 ise
  0.30000001192092896 — float32 izin hesabı, ölçüldü).
- **Adla test tıklaması:** her widget (koordinatlılar da) dikdörtgenini adıyla kaydeder.
  `ui_test_tikla_ad("Basla")`, kaydırıcıda `ui_test_tikla_oran("Ses", 0.25)` (izin payı 12 px:
  `teng_ui_slider`'daki `pad` ile aynı olmalı, `run_yerlesim` ölçer). Kayıt adın **en son** çizildiği
  yeri tutar (Gölge Salonları "Basla"yı menünün çizilmediği karede istiyor; tıklama sonraki karede menü
  gelince tüketiliyor). Hiç çizilmemiş ad, yanlış tür, [0, 1] dışı oran false döner + UYARI +
  `ui_uyari_sayisi()`; kayıt 128 adla sınırlı, dolunca UYARI.

Kapı `run_yerlesim` (konumlar, adla/oranla tıklama; KONTROL: komşu düğme tetiklenmez, ıskalayan ad
tıklama enjekte etmez, taşan pencere uyarır). Kitaplık üç kez bilerek bozuldu (pay 12 -> 10, tıklama
noktası kaydırıldı, ıskalama sessiz yapıldı): üçünde de test kırmızı. Gölge Salonları'nın üç ekranı
bununla yazıldı — koordinat alan widget çağrısı 21 -> 0, kod satırı 49 -> 38; otopilotun menü
kısmı 17 -> 8. Enjekte tıklamalar eskisiyle **kare kare** aynı (k0 Ayarlar, k1 kaydırıcı, k2 Geri,
k3 Başla) ve 3200 karelik `[kapi]` özeti bayt bayt aynı. Kapsam dışı: HUD hâlâ sabit piksel
(2400x1080'de "CAN" yazısı çubuğa sığmıyor), yatay (yan yana) yerleşim yok.

### 8.1 Bir oyunun tam yaşam döngüsü (gerçek çağrı adlarıyla)

`lib/engine.tpr` sarmalayıcıları ve `examples/engine_aksiyon.tpr`'nin akışı:

```tulpar
import "engine";

func main() {
    // 1) Kurulumdan ÖNCE: kalıcı ayarlar okunur, parlama burada açılır
    //    (iç HDR hedefi kurulumda kurulur; kare içinde yalnız eşik/yoğunluk değişir).
    kayit_ac("oyunum_kayit.txt");                  // boş yol HATA; hiç çağırmazsan varsayılan
    float ses_ayari = kayit_oku_sayi("ses", 0.7);  // (TULPAR_ENGINE_SAVE ya da tulpar_kayit.txt)
    parlama(true, 1.0, 0.55);
    if (!motor_ac("Oyunum", 1280, 720)) { print(eng_last_error()); return; }

    // 2) Dünya: ya kodda kurulur, ya editörde hazırlanmış blob'dan gelir.
    gunes(0.4, 1.0, 0.3, 0.9);
    ortam(renk(38, 42, 52));
    if (!sahne_yukle("examples/assets/salon1.sahneb")) { zemin_kur(20, ACIK_GRI); }
    sahne_izle(true);                              // editörde "Derle" -> oyun kendini yeniler
    int oyuncu = kure(0, 1, 0, 0.5, true, KIRMIZI);
    int skor = 0;
    if (ses_var()) { ses_seviyesi(ses_ayari); }

    // 3) Kare döngüsü
    while (calisiyor()) {
        kare_basla();
        float d = dt();
        if (sahne_yenilendi()) { logla("sahne yenilendi"); }

        // girdi: klavye VEYA sanal joystick (yon_x/yon_y ikisini de okur)
        yuru(oyuncu, yon_x() * 5.0, -yon_y() * 5.0);
        if (eylem_basildi() && yerde(oyuncu)) { itme(oyuncu, 0, 6, 0); }

        // sorgu tabanlı oynanış: önde küre sorgusu = vuruş
        Vec3 p = konum(oyuncu);
        int n = yakinlar(p.x, p.y, p.z, 2.0, oyuncu);
        for (int i = 0; i < n; i++) { itme(yakin_id(i), 0, 3, 0); }

        // kamera + HUD
        kamera_takip(p.x, p.y, p.z, 9.0, 4.0, 0.0);
        yazi(t"skor {skor}", 16, 16, 2.0, BEYAZ);

        // menü/ayar: anlık-kip arayüz (aynı kare içinde)
        ui_basla();
        if (dugme("Duraklat", 16, 60, 140, 40)) { logla("duraklat"); }
        ses_ayari = kaydirici("Ses", 16, 110, 200, 28, ses_ayari, 0.0, 1.0);
        ui_bitir();

        kare_bitir();
    }

    // 4) Kapanış: ayar + rekor diske, sonra motor
    kayit_yaz_sayi("ses", ses_ayari);
    kayit_kaydet();
    motor_kapat();
}
```

Pencersiz doğrulama aynı betikle: `TULPAR_ENGINE_HEADLESS=600 TULPAR_ENGINE_OUT=/tmp/x.ppm DISPLAY= ./tulpar oyun.tpr`
(menü akışı da ölçülsün diye enjekte tıklama: `ui_test_tikla(x, y)` ya da koordinatsız
`ui_test_tikla_ad("Basla")`; "Gölge Salonları" ikincisiyle koşuyor).

### 8.2 Hâlâ olmayanlar
- **Çarpışma olayı yok** (konum/hız/ışın/küre sorgusu var); callback FFI gelmeden geri çağrı yok.
- **Ajan / yol takibi köprüde yok**: navmesh **sorgusu** var (`eng_nav_*`, blob'daki bake), ama motorun
  ajan sistemi (kalabalık, kaçınma) dışarı verilmiyor — yolu betik kendisi takip eder.
- Tek motor örneği (global bağlam): iki pencere / iki dünya yok, gerekmedi.
- Kaynak (model) 16, varlık 4096, ses klibi ve arayüz durumu sabit kapasiteli — diziler sabit, 0 ayırma.
- Parlama açıkken pencere yeniden boyutlanırsa iç hedef init ölçüsünde kalır (birleştirme gerilir).

### 8.3 Masaüstünde girdi: fare bir faredir

Masaüstünde (pencere ve editörün gömülü kipi) girdi klavye ve fareden gelir. Dokunmatik
API'leri masaüstünde boş döner: `dokunus_sayisi()` 0, sanal çubuk 0, `eylem()` false.

| ne | masaüstü | dokunmatik (Android ya da `TULPAR_ENGINE_DOKUNMATIK=1`) |
|---|---|---|
| yürü (`yon_x` / `yon_y`) | WASD ve ok tuşları | + sol yarım ekran sürükleme |
| bakış (`eng_look_dx`) | sağ fare tuşu basılıyken yatay sürükleme | sağ yarım ekran sürükleme |
| eylem (`eylem()`) | yok (oyun bir tuşa bağlar) | sağ yarıma kısa dokunuş |
| fare (`fare_x`, `eng_mouse_down`) | var | var |

Eskiden fare parmak 0'dı: masaüstünde sol tıkla sürüklemek sanal çubuğu oynatıyor,
sağ yarıma tıklamak "eylem" sayılıyordu ve fareyle bakış yoktu. Kullanıcı (2026-09-24)
"kontroller garip" dedi. Mobil kontrolleri masaüstünde denemek için taklit hâlâ var:
`TULPAR_ENGINE_DOKUNMATIK=1` fareyi parmak 0 yapar. Kapılar:
`bridge_embedded_game_draws_into_the_editor_channel` (sağ tuş sürükleme bakış verir, sol tık
dokunuş değildir) ve `bridge_embedded_touch_emulation_is_opt_in` (taklit açıkken sol tık
dokunuştur: aynı kontrolün pozitif kontrolü).
