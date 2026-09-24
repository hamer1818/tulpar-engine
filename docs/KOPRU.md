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
| sarmalayıcı | `lib/engine.tpr` (gömülü, 386 satır) | `motor_ac`, `kutu`, `tus`, `yazi`, `dugme`, `kayit_*` … TR adlar, çoğunun EN ikizi (`engine_open`, `box`, `key`, `button`) |
| oyun | `examples/engine_ilk_oyun.tpr` (94), `engine_arena.tpr` (209), `engine_aksiyon.tpr` (1097) | saf Tulpar |

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
engine_sahnec examples/assets/arena.sahne           # -> arena.sahneb (runtime blob)
./tulpar examples/engine_arena.tpr                  # oyna
```

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
ölümü değil. Köprünün kendi ürettiği varlıklar (`eng_kutu_uret` …) bu yaşam döngüsünün dışında.

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

## 8. Kapsam: `SPEC` = `engine_api.h` = **195 builtin**

Sayı iki yerde birden durur ve birbirine karşı denetlenebilir: `bridge/engine_api.h`'deki `teng_*`
bildirimleri ve `tools/gen_engine_bindings.py`'deki `SPEC` satırları. Aile dağılımı (başlıktaki
bölüm yorumlarına göre):

| aile | adet | ne verir |
|---|---:|---|
| yaşam döngüsü | 21 | `eng_init` / `running` / `frame_begin` / `frame_end` / `shutdown`, dt, zaman, kare, fps, ölçü, pencersiz kip, **log ve log seviyesi**, ekran görüntüsü, GPU adı, son hata, **hata ve uyarı sayacı** |
| dünya / kamera | 11 | güneş, ortam, gölge hacmi, yerçekimi, **parlama (bloom)**, kamera (göz+hedef ya da yörünge), kamera konumu |
| derlenmiş sahne (`.sahneb`) | 21 | yükle / boşalt / yüklü mü (**bölüm geçişi**), sayı, ada göre bul, konum, ad, hız, dinamik mi, hız ver, dürtü, **atanmış betik yolu + etkin mi**, **sahne karakteri** (karakter mi, yürü + zıpla, zeminde mi, zıplama hızı) |
| varlıklar (köprü sahibi) | 25 | kutu / küre / zemin / model / ışık / **tetik kutusu / tetik küresi** üret, sil, canlı mı, konum, renk, ölçek, yaw, hız, dürtü, dinamik mi, uyanık mı |
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
| ölçüm | 4 | çizim / gövde / ışık sayısı, son kare p50 |

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
        int n = yakinlar(konum_x(oyuncu), konum_y(oyuncu), konum_z(oyuncu), 2.0, oyuncu);
        int i = 0;
        while (i < n) { itme(yakin_id(i), 0, 3, 0); i = i + 1; }

        // kamera + HUD
        kamera_takip(konum_x(oyuncu), konum_y(oyuncu), konum_z(oyuncu), 9.0, 4.0, 0.0);
        yazi("skor 0", 16, 16, 2.0, BEYAZ);

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
(menü akışı da ölçülsün diye `ui_test_tikla` ile enjekte tıklama; "Gölge Salonları" böyle koşuyor).

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
