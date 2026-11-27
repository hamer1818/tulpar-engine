# Tulpar Engine — Durum Özeti (2026-09-15 akşamı, sonraki aşama için)

> **Depo notu.** Bu belge motor deposuna taşındı; ağaç yolları artık `engine/` öneksiz
> (`core/…`, `rhi/…`, `tools/…`). Metinde geçen `src/`, `lib/*.tpr`, `runtime/`, `examples/`,
> `android/host/`, `build.sh`, `./tulpar` ve `docs/mindmap/` **derleyici deposundadır**
> ([hamer1818/TulparLang](https://github.com/hamer1818/TulparLang)) — olduğu gibi bırakıldı.

> Tek sayfada "ne var, ne ölçüldü, ne yok". Ayrıntı: FAZ0–FAZ3.md, KOPRU.md, CIHAZ-MATRISI.md, PLAN.md.
> Kural: her sayı ya masaüstünde (RTX 5080, Linux) ya telefonda (Huawei P20 Pro, Mali-G72) ölçüldü.

## 1. Ne var (katman katman)

| katman | içerik | durum |
|---|---|---|
| L0 platform | zaman, bellek, thread, çökme raporu, **GLFW dlopen pencere**, **dokunmatik durum** | ✅ |
| L1 core | arena ailesi + slotmap, **fiber job sistemi** (x86_64 + AArch64 asm), profiler (+ **Tracy** istemcisi, seçenek), math, konteynerler, `AllocGate` | ✅ |
| L2 rhi | Vulkan **dlopen**, `Device` (iki aşama, yüzey), **Swapchain** (ön-döndürme, sunum kipi, **sunum kancaları**), offscreen, PSO cache, `allocate_dedicated`, **Mali linter** (BestPractices+Arm), **tile bütçesi** (kodla zorlanır) | ✅ |
| L3 renderer (+ **derlenmiş render graph**, **bloom/post**, **temporal** ve **GPU cull + dolaylı çizim**, hepsi varsayılan kapalı) | depth prepass → renk; **kademeli gölge** (CSM: 3 kademe tek atlas, kapsamaya göre seçim, texel kenetleme, odak = kamera hedefi; D16, PCF, dünya-uzayı normal eğilimi); **doku + malzeme** (klasik descriptor set, mip blit); **8–32 kümelenmiş nokta ışık** (CPU atama); **2B arayüz** (tek batch, opak-önce, retained blok, **SDF örnekleme**, ölçülmüş overdraw); **doğrusal aydınlatma + sRGB hedef**; **GPU skinning** (gölge dahil), **paketlenmiş vertex** (oktahedral normal + yarım UV: 32 -> 20 bayt); **jitter + hareket vektörü + dinamik çözünürlük** | ✅ ilk dilim |
| L3 audio | **miniaudio** cihaz (AAudio/Pulse/ALSA/CoreAudio/null), kilitsiz **karıştırıcı** (32 ses, 0 ayırma), klip yükleme, **3B uzamsal** (mesafe sönümü + panlama + kafa gölgesi), **DSP zinciri** (kazanç/LP/limiter), **oklüzyon** (fizik ışını üst katmandan) | ✅ ilk dilim |
| L4 sim | archetype ECS, sistem zamanlayıcı, sabit adım + replay, **Jolt** fiber job'larda, **Recast/Detour** navmesh, sıkıştırılmış animasyon | ✅ yazılım tarafı |
| L6 content | **glTF 2.0** (cgltf + stb_image; mesh, malzeme, doku, **iskelet + animasyon**), **meshoptimizer + ayrık LOD**, **KTX2 + ASTC** (astc-encoder; `engine_texpack`), **font atlası** (stb_truetype, Türkçe), **sahne veri modeli** (`.sahne` deterministik metin, işlem günlüğü geri al/yinele, gövde kurulumu), **SDF font atlası** (+ GPU'suz kalite ölçümü), **derlenmiş sahne blob** (`.sahneb` **sürüm 4**: deterministik, bütünlük denetimli; yerleşik küme + bake edilmiş **navmesh** + **küme DAG** + **GI sondaları**; `SceneRuntime` tüketici — ayrıştırma/ayırma yok), **`.tpak` pack + delta yama + içe aktarma önbelleği** | ✅ ilk dilim |
| L6 bridge | **Tulpar köprüsü** (`bridge/`: **156** fonksiyonluk düz skaler C ABI — yaşam döngüsü, dünya/kamera, sahne + sıcak yükleme, varlık/fizik, girdi, HUD, **anlık-kip arayüz**, **kalıcı kayıt**, ses, animasyon, bloom, **ışın/örtüşme/navmesh sorguları**; üretilmiş bağlama, masaüstü + Android host) — oyun mantığı Tulpar'da, motor C++ | ✅ |
| L6 app | `engine_demo` (masaüstü pencere / headless), **Android NativeActivity host** (+ **Swappy** seçenek), sanal joystick, HUD, **`engine_editor`** (ImGui + ImGuizmo; sahne yükle/kaydet/geri al/yinele/ekle/sil, tıkla-seç, **çoklu seçim (grup işlem)**, **kaynak tarayıcı**, **ışık/gölge/güneş gizmoları**, **Dünya paneli** (güneş/ortam/gölge/kamera), **Derle** → `.sahneb`, oynat = gövdeler fizikte; headless), **`engine_demo --scene x.sahneb`** (yazar içeriği blob'dan); **Editör içi güncelleme çekirdeği** (`app/updater`, 2026-09-25: Release denetimi/indirme curl ile, SHA-256, kareye bölünmüş doğrulama, kullanıcı dosyasını ezmeyen geri alınabilir kurulum — [GUNCELLEME.md](GUNCELLEME.md)). | ✅ |
| L6 bridge örnek | `examples/engine_ilk_oyun.tpr` (kod, 94 satır), **`engine_arena.tpr` + `assets/arena.sahne`** (editör sahnesi → derlenmiş blob → Tulpar oyunu, 209 satır), **`engine_aksiyon.tpr` "Gölge Salonları"** (Faz 7 dikey dilimi: iki bölüm, düşman durum makinesi, menü/duraklat/ayarlar, 758 satır; bölüm verisi — başlangıç, kapı, 9 düşman ve sayıları — 2026-09-25'ten beri sahnede, E7; 2026-09-21'den beri `enum` + çoklu dönüş + tipli struct dizisi kullanıyor: sihirli sayı, yön globali ve 11 paralel dizi yok); **`engine_savas.tpr`** (2026-10-26: motorun uzun kapısı — iki yapay zekâlı şerit savaşı, sabit tohum, 9600 kare; eski oyun kopyasının 9600 karelik kapısının yerine, oyun ayrı özel depoda) | ✅ |
| araçlar | `layer_check.py` (katman kuralı = build hatası), `compile_shaders.py`, `clang_syntax_check.sh`, `android_run.sh`, `make_test_gltf.py`, `fetch_vvl_android.sh`, `tracy_check.sh`, **`engine_sahnec`** (`.sahne` → `.sahneb`; `--check`, `--dump`, `--kanonik`, `--kume=`, `--gi*`, `--pack`, `--yama`, `--onbellek`), **`gen_engine_bindings.py`** (köprünün tek `SPEC`'i → 4 dosya), `build_bridge_android.sh`, `tests/dist_archive_audit.py`, `tests/paket_boyut_audit.py` | ✅ |

## 2. Telefonda çalışan sahne (tek APK)
Zemin (dama doku), duvar, 40 dinamik Jolt kutusu (glTF dama küpü), 24 navmesh ajanı + eklem zinciri
animasyonu, oyuncu (dokunmatik joystick, kamera izler), gölge, 8 dönen nokta ışık, HUD.
**60 fps (FIFO)**, vsync açıkken ~223 fps / 3.8 ms; kare içinde **0 `operator new`**.

## 3. Ölçülmüş gerçekler (Mali-G72, Vulkan 1.1)
- Sahne özeti 600 tick: **masaüstü = telefon = emülatör bit eşit** (`1513845f8ca5afd9`).
- Jolt altın özeti x86_64 = arm64 = telefon.
- Bedeller: gölge ~%3, doku ölçülemez, 8 ışık ~%5. Kademeli gölge varsayılanı 3 x 1024 = **6 MB** (tek 2048 = 8 MB'tan az). CPU: kayıt ~1.2 ms, submit+present ~2.5 ms.
- `LAZILY_ALLOCATED` bellek var (TBDR doğrulandı). Zaman damgası, GPL, subpass merge feedback **yok**.
- Plan L2 "zorunlu" listesi (descriptorIndexing/timeline/BDA) bu cihazda **yok** → kapı rapora çevrildi (REV-3).
- **Swappy** (2026-09-15): `SwappyVk_setQueueFamilyIndex` şart (yoksa binder beklemesi, siyah ekran); açıkken p99 18–20 ms / max 19–22 (FIFO 21–22 / 23–26), bedel kare başına 7 `new`; `VK_GOOGLE_display_timing` var; FIFO 10 s: 0 geç kare (sonda). Ses: AAudio 253 callback, düşen komut 0. Varsayılan Swappy KAPALI.

- **Sahne yükle/boşalt bedeli** (M5, 2026-10-09, masaüstü RTX 5080, ilk oyunun savaş sahnesi `.sahneb` 33 KB, 100 varlık ilkel, navmesh'li;
  oyunun yükle/boşalt ölçüm betiği — oyun ayrı özel depoda —, 20 tur, ısınma turundan sonra): RSS **~720 KB / yükleme** (321640 → 336048 KB),
  mesh 12/256 ve doku 3/128 **sabit**. Malzeme ise yükleme başına ~96 artıyordu: 5. yüklemede renderer'ın 512'lik havuzu doldu ve
  sahne varlıkları **sessizce çizilmedi** (Tuzaklar 8dc) — düzeltmeden sonra 21 yüklemede malzeme 99 sabit (96 kuruldu, 1920 yeniden
  kullanıldı). Telefonda ölçülmedi.

## 4. Kapılar

**Kaynaktan sayım (bugün):** `tests/*.cpp` içinde **141 `ENGINE_TEST`** bloğu (Android'de editör (5) +
köprü (1) derlenmediği için **135**); Tulpar tarafında `tests/engine_bridge.test.tpr` **17** test; katman
denetimi **159 dosya, 0 ihlal**; köprü `SPEC` = `engine_api.h` = **156 builtin**.

**Kayıtlı tam koşular (2026-09-15 akşamı, GI / Faz 4 UI / GPU cull dilimleri dahil):**
- masaüstü `engine_tests` **149/149**, **0 atlandı** (2026-09-16; PBR, stokastik ışıklandırma, PSO
  önbelleği ve glTF-PBR kapıları eklendikten sonra) — `141 RUN / 141 PASS` tek tek sayıldı. Vulkan
  yükleyicisi mevcut; `rhi_*`, `renderer_*`, `render_graph_*`, `temporal_*`, `editor_*`, `bridge_*`
  ailelerinin hepsi gerçekten koştu, yani 8al'deki "sonraki kapılar sessizce ATLANDI'ya düşer" sınıfı
  bu koşumda **görülmedi**.
- `./build.sh suites` **82/82** (iki paket denetimi + Faz 8 shader kapısı dahil), Tulpar suite'leri
  toplam **1295 iddia**; `Tests:` özet satırı basmayan (dolayısıyla asla
  kırmızı olamayacak) suite **yok**. `tests/engine_bridge.test.tpr` **17/17**.
- Örnekler **102/102** yeşil (`build.sh`'in `COMPILE_ONLY_TESTS` filtresiyle).
- Telefon **78/78** (2026-09-15 sabahı, katmanla, 3 görünür ATLANDI) — bu sayı hâlâ o günün kaydı;
  emülatör 67/67 (editör testi masaüstü).

`bridge_runs_a_scripted_game_headless` kapanışta **14 hata** logluyor ve yine de PASS: bunlar kapının
bilerek koşturduğu **hata yolları** (ölü nesil id'si, sabit gövdeye kuvvet, kare dışında `teng_text`,
bilinmeyen tuş adı, kapalı ses cihazı). Hata sayacının sıfır olmaması burada doğru davranıştır.

Her görsel özelliğin açık/kapalı karşılaştırmalı testi ve pozitif/negatif kontrolü var: gölge (koyulaşan
piksel + 6 m kaydırma kontrolü), doku (keskin geçiş oranı), nokta ışık (kırmızı piksel, görüş dışı 0),
UI metni (boş metin 0), UI sıralaması (opak-önce piksel aynı, yanlış sıra kontrolü), kümeleme
(yerel/konservatif), joystick, glTF sayıları, adanmış bellek serbest bırakma (blok ayırıcı pozitif
kontrolü), fizik altın özeti, GI sondası (güneşi kapatınca oran çöker), çökme adı basma (`--cokme-kontrol`).

## 5. Öğrenilen cihaz tuzakları (Tuzaklar 8k–8ao)
y ters çevirme + CW; kare yuvası tek kaynaktan; SUBOPTIMAL = recreate değil (20→60 fps); adb shell GPU
görmez; plan "zorunlu" dedi cihaz vermedi; bump ayırıcı + pencere ömrü; `depthBias` sürücüye bağlı
(Mali'de gölge yok); UI framebuffer uzayında yatık, atlas taşınca "font yok"; katman "etkin" ama mesaj kanalı yok =
sahte yeşil (8s); `compositeAlpha OPAQUE` Huawei'de yok; katmanın seyrek-indeks taraması alt-ayırma offset'ini atlar.
Sonrakiler (8aa–8ao): istatistik kayıtta sayılır; ön-döndürme unutulunca 3B yatar ama HUD düzgün görünür;
profiler tamponu kare kapasitesinin iki katı olmalı; APK varlıkları alt dizine montaj edilir; bloom kendi
temizleme rengini kullanır; **denetim tek sembol ailesine bakınca "temiz" derken web hedefi tamamen kırıktı**
(8aj); wasm32'de işaretçi 4 bayt, nesne başlığı 20 (8ak); her kapı kendi `VkInstance`'ını açarsa sonrakiler
sessizce ATLANDI'ya düşer (8al); türetilmiş dosya bayatlayınca kapı sessizce atlanır (8am); yeni bir kapının
ilk sonucu "hepsi bozuk" ise bozuk olan genelde ölçümdür (8ao); aynı modülü iki hedef için
emit etmek ikinciyi bozar (8ap).

## 5.1 Web ve Android hedefi (2026-09-15)
Köprü masaüstünde ve Android'de koşarken **web hedefi tamamen kırıktı ve hiçbir kapı söylemiyordu**: üç
sessiz hata üst üste binmişti (kör denetim, wasm32 `ObjArray` yerleşimi, eksik `runtime_net.cpp`). Üçü de
onarıldı; `dist_archive_audit.py` artık **codegen'in adıyla bildirdiği** sembol kümesini denetliyor ve yeni
`tests/paket_boyut_audit.py` paket boyutu + açılış süresi + **SPIR-V tazeliği** (21 shader) için eşik
koyuyor. Ayrıntı: [FAZ3.md](FAZ3.md) "Web ve Android hedefleri onarıldı".

## 5.2 Oyun bir Android yüzeyinde koştu — ve bir derleyici hatası ortaya çıktı (2026-09-15)
"Gölge Salonları" Android 16 x86_64 emülatöründe açıldı: ana menü + `salon1.sahneb` sahnesi (13 varlık,
2 ışık, 10 gövde, 28 poligon navmesh), kademeli gölgeler, bloom, AAudio sesi. `examples/arcade_zipla.tpr`
de aynı emülatörde koşuyor, yani tame hattı da sağlam.

Yolda iki hata çıktı. Birincisi bayat bir ikili: depo kökündeki `./tulpar` kopyası `lib/engine.tpr`
güncellemesinden eskiydi, stdlib ikilinin *içine* gömülü olduğu için derleme var olan bir fonksiyonu
"bulunamadı" diyordu. İkincisi asıl bulgu: **aynı `LLVMModule`'den iki ABI üretmek ikincisini bozuyordu.**
`LLVMTargetMachineEmitToFile` modülü yerinde değiştirdiği için x86_64 emisyonu arm64'ün alçaltılmış IR'i
üstüne biniyor, hizalı bir `movapd` hizasız yuvaya düşüyor ve uygulama ilk karede çöküyordu. Emit artık
her hedef için `LLVMCloneModule` üstünde çalışıyor; `aot_pipeline.cpp` ABI döngüsünün iki ucunda IR özeti
karşılaştırıp klonlama kaldırılırsa derlemeyi kırmızıya çeviriyor (pozitif kontrol yapıldı).
**İlk ABI (arm64) her zaman doğruydu** — telefonda yapılmış doğrulamalar geçerli; şüpheli olan, bu
tarihten önce emülatör için üretilmiş her şeydir. Ayrıntı: [FAZ3.md](FAZ3.md), tuzak kaydı
Tuzaklar (`docs/mindmap/Tuzaklar.md`, derleyici deposu) 8ap.

## 5.3 Faz 6 kalanı ve Faz 8 fizibilitesi (2026-09-16)
PBR (Cook-Torrance/GGX, malzeme başına seçilen gölgeleme modeli, glTF'ten akan metallic/roughness),
düşük segment için **stokastik tile ışıklandırma** (varsayılan kapalı, kapalıyken bit-aynı), ve
**PSO ön-üretimi** (`VkPipelineCache` + diske kalıcılık + kurulumda ön ısınma) geldi. Faz 8 için
fizibilite dilimi bitti ve backend kararı **Slang'den GLSL+glslc'ye** döndü (PLAN ⚠️ REV-4).

Android'de PSO önbelleğinin iki ayrı yoldan sessizce kapandığı bulundu ve kapatıldı; emülatörde
süreçler-arası kazanç ölçüldü: boru hattı kurulumu soğuk **6.5 ms** → sıcak **1.1 ms**.
Ayrıntı: [FAZ3.md](FAZ3.md) "Faz 6 kalanı + Faz 8 fizibilitesi", tuzaklar 8aq–8au.

## 6. Fazların durumu (PLAN §7'ye karşı)

| faz | durum | not |
|---|---|---|
| Faz 0 — Ölçüm ve Temel | ✅ kapandı | `FAZ0.md`; kare içinde 0 ayırma, pozitif kontrollü |
| Faz 1 — RHI, ilk piksel, Android host | 🟡 yazılım tarafı bitti | Kapı ("G-buffer DRAM'e inmedi", 3 cihaz) **cihaz bekliyor**; Kotlin host/JNI iskeleti derlenmedi (SDK yok) |
| Faz 2 — Simülasyon | ✅ yazılım tarafı kapandı | ECS, zamanlayıcı, Jolt, navmesh, animasyon, replay bit eşit |
| Faz 3 — Renderer çekirdeği | 🟡 cihaz gerektirmeyen kalemler kapandı | CSM, paketlenmiş vertex, render graph + bloom, kümelenmiş ışık, sRGB, skinning, **PBR (dokular dahil)**. **VRS** (2026-11-04): boru hattı başına parça boyu (`RendererConfig::shading_rate` 2/4, `TULPAR_ENGINE_VRS`), yalnız sahne renk geçişi — derinlik ön geçişi/gölge/arayüz tam oranda; cihaz vermezse sebebiyle 1'e düşer. Kapı `renderer_vrs_coarse_shading_makes_2x2_blocks` (RTX 5080: 2x2'de blokların %100'ü tekdüze, 1x1'de %26.6; ışıklı alan farkı %0). Mali-G72 uzantıyı vermiyor; **GPU kazancı ölçülmedi**. **Açık:** bandwidth < 8 GB/s (3 cihaz), vis buffer A/B, FDM (fragment density map) |
| Faz 4 — UI ve Ses | ✅ kapı geçildi | UI: tek batch, opak-önce, retained, SDF, **ölçülmüş overdraw**, < 1,5 ms. Ses: uzamsal + DSP + oklüzyon. **Gerçek HRTF** (2026-11-04, `audio/hrtf.hpp`, kaynak başına `SpatialParams::hrtf`): MIT KEMAR compact (368 yön, 48 kHz'e yeniden örneklenmiş, kulak başına saf gecikme + 64 tap; tutulan enerji ort. %99.0, en az %93.4), konum değişince çapraz geçiş. Kapılar: ITD 0.625 ms + ILD 13.8 dB (tam sağ), ayna bit aynı, ön/arka ve yükseklik ayrışıyor (panlama ayıramıyor — kontrol), dönen kaynakta sıçrama sabit kaynakla aynı (çapraz geçiş kapatılınca 13.8x — pozitif kontrol), render'da 0 ayırma; bedel 38.9 ns/ses-kare (RTX 5080 makinesinin CPU'su). Köprüde 3B ses (`cal_3b`, `ses_konum`, `dinleyici`, `ses_3b`; KOPRU §13; dinleyici varsayılan kamera). **Metin shaping** (2026-11-23, `content/text_shape.hpp`, vendored `kb_text_shape` 4eec43b, Zlib): OpenType GSUB/GPOS + bidi; glif KİMLİĞİYLE atlas, metin tablosu yüklemede shape edilir (çeviri metni motorda yok — topluluk çevirir). Kapılar (`shaping_*`, DejaVuSans): Latin+Türkçe 21/21 cmap ile aynı; سلام 4 kod noktası → 3 glif (lam-elif bitişik, sin baş biçimi 5293≠1377); RTL görsel sıra; karışık `abc سلام def` UAX#9 L2 sırası; 800 shape'te kütüphane ayırması 0; taşma sayılır; tablo dışı glif sayılır (Kiril 3). **Köprüye bağlandı** (2026-11-25, KOPRU §23): `eng_text_table_add` / `eng_text_table_load` (Tulpar `metin_tablosu_ekle` / `metin_tablosu_yukle`); `eng_text` metnin bütün glifleri shaping'li atlastaysa onunla, değilse eski fontla çizer, genişlik ölçümü aynı kuralla. Kapı `bridge_text_table_shapes_arabic` (RTX 5080): سلام shaping'le çizildi (sayaç 1), tablo dışı "Zebra" eski fonta düştü, eski fontun '?' sayacına shaping'li çizim bir şey eklemedi, piksel genişliği 80 px / beklenen 85; çok kelimeli RTL paragraf kapısı (`shaping_rtl_paragraph_word_order`, İbranice + Arapça iki kelime: küme dizisi azalan). Tulpar betiğinden uçtan uca ekran görüntüsüyle doğrulandı. **Açık:** editör (ImGui) bağlantısı, RTL metinde "…" kısaltması, satır kırma, >2 gömme düzeyi |
| Faz 5 — Temporal | 🟡 yazılım tarafı bitti | Jitter, hareket vektörü, dinamik çözünürlük, yükseltici arayüzü — hepsi varsayılan kapalı. Kapı (10 dk sustained, 99p < 18 ms, 3 cihaz) **cihaz bekliyor** |
| Faz 6 — İçerik boru hattı | 🟡 büyük kısmı bitti | `.tpak` + delta yama + önbellek, blob v2/v3/v4 (yerleşik küme, navmesh, küme DAG, **GI sondaları**), KTX2 kopyasız, cihaz sınıfı başına bake. **PSO ön-üretimi geldi** (VkPipelineCache + diske kalıcılık; Android'de ölçülen kazanç 6.5 → 1.1 ms). **Virtual texture sayfa önbelleği** (2026-11-23, `renderer/vt_cache.hpp`; GPU'suz çekirdek): geri bildirimle yerleşiklik, kare başına yükleme bütçesi, LRU, en kaba mip sabit → çözümleme hiç geçersiz dönmez. Kapılar (`vt_*`, 5): 2 731 (mip, sayfa) geçerli çözüldü; 100 istek / bütçe 32 → 68 ertelendi, 4 karede yerleşik; sabit sayfa tahliye edilmez, tükenme sayılır; 500 kare / 7 939 tahliye, 0 geçersiz, 0 `operator new`. **VT geri bildirimi** (2026-11-23, `mesh_vtfb.frag` = Faz 8.5 mesh.frag ailesinin varyantı, `renderer/gpu_request` = VSM'le ORTAK GPU istek tablosu, `RendererConfig::vt_feedback` varsayılan kapalı): VT malzemesinin görünür (doku, mip, sayfa) anahtarları 1/16 piksel deseninde sahne geçişinde yazılır, liste bir kare gecikmeyle `VtCache::request_feedback`'e. Kapı: analitik başvuruyla RTX 5080 / Mali-G72 640×360 GPU 25 / CPU 24, ortak 24 (fark mip sınırındaki türev); KONTROL sanal boy ×4 → 0/25; doğrulama hatası 0. **Maliyet (Mali-G72 1080p)**: yan etkili fragment shader ilk sürümde +42,8 ms (Mali erken derinlik reddini kapatır); varyant "yalnız VT çizimlerine" + `early_fragment_tests` → +16,1 ms; 1/64 desen +15,1 ms. ⚠️ **Bu iki sayı geçersiz** (2026-11-25, Tuzaklar 8ex): `materials_` sıfırlı ayrıldığı için `vt_id` varsayılanı yoktu, HER malzeme "VT dokusu 0" sayılıyordu — "yalnız VT çizimleri" gerçekte 48 küp dahil bütün statik çizimlerdi. **Düzeltmeden sonra Mali-G72'de yeniden ölçüldü** (2026-11-25, `renderer_device_cost_vt_feedback` artık kipleri DÖNÜŞÜMLÜ ölçer — 5 tur × 4 kip; sıralı bloklarla aynı ikili iki koşumda taban 43,8 / 28,8 ms, "tam" −1,2 / +15,6 ms verdi, telefonun saat/ısı kayması farka giriyordu): iki koşumda kapalı 40,5 / 39,5 ms, **tam +4,3 / +1,9 ms**, kod elenmiş boru hattı +0,9 / +0,7, anahtar hesabı (SSBO yok) +3,5 / +2,7; tur başına yayılım −1…+5 ms (ilk turda saat değişimi). Yani bedel birkaç ms düzeyinde, gürültüden tam ayrışmıyor; "+16 ms sabit bedel" büyük ölçüde 8ex'ti. RTX 5080: gürültü düzeyinde (düzeltme öncesi/sonrası ±0,3 ms; tekil sayfa 109 → 108, eksilen küplerin sahte anahtarı). Kapı `render_graph_vt_feedback_ignores_plain_materials` (düz + varsayılan malzeme → 0 anahtar; hata geri getirilince kırmızı, ölçüldü). **Fiziksel atlas + dolaylılık tablosu** (2026-11-23, `renderer/vt_atlas`): `VtCache` yükleme listesi + sabit en kaba mip'ler `VtPageSource`'tan staging'e, tek kopyalama komutuyla karolara; tablo (SSBO) her sayfa için `resolve()`. Kapı (RTX 5080, doğrulama açık): bütçe 16 ile 18/16/8/0 yükleme, 42 yerleşik sayfanın karosu BAYT AYNI (KONTROL yanlış tohum 170 705 fark), 10 tahliyede yeniden yazılan yuvalarda bayat karo yok, tablo 96 hücre `resolve()` ile aynı. **VT örnekleme** (2026-11-25, `mesh_vtfb.frag` `vt_sample()`, `RendererConfig::vt_sample` varsayılan kapalı — geri bildirimi de açar): renderer kendi `VtCache` + `VtAtlas`'ını tutar; bir karenin geri bildirimi sonraki karede önbelleğe, bütçeli yüklemeler sahne geçişinden önce atlasa (sRGB görünüm), albedo dolaylılık tablosu (binding 13) + atlastan (binding 12). Mip/sayfa geri bildirimle aynı formül; sayfa içi koordinat karo içine kırpılır (yarım texel, kenar yok); mip'ler arası süzme yok. Kapı `renderer_vt_sample_matches_plain_texture` (RTX 5080, doğrulama açık, 640×360, sanal 1024² / sayfa 128, texel başına karma desen): 4 karede yakınsar (ilk kare 1 sabit sayfa, sonra 14 yükleme); analitik mip'i 0 olan **118 023 pikselin 118 021'i** aynı içeriğin düz dokusuyla aynı (≤2, en büyük fark 3 — bilinear ağırlık yuvarlaması); sayfa kenarındaki 1 texel'lik bant %41,6 aynı (kırpma dikişi, ölçülür ve basılır); ilk kare (yalnız en kaba mip) %3; KONTROL kayık sayfa %0; doğrulama hatası 0. Mali-G72'de de aynı (2026-11-25): 118 022/118 023, kenar bandı %41,6, ilk kare %3, KONTROL %0, doğrulama hatası 0; `render_graph_vt_feedback_ignores_plain_materials` 0 / 5 anahtar. **Örnekleme maliyeti** (2026-11-25, aynı dönüşümlü test, 5. kip `vt_sample`, kararlı durum — yükleme 0, 108 yerleşik sayfa; sanal 16384², zemin ekranın yaklaşık yarısı): Mali-G72 üç koşumda tabana göre **+14,4 / +15,5 / +16,2 ms** (her turda pozitif, +7…+29), yani geri bildirimin üstüne ~+12 ms. Bunun **~2,0 ms'i CPU**: dolaylılık tablosunun her karede baştan kurulması (~22 bin `resolve()`, `VtAtlasStats::table_ns`); kalan **~9–10 ms GPU** — piksel başına bağımlı okuma zinciri (tablo başlığı + hücre SSBO → atlas `textureLod`, dağınık karolar). RTX 5080: +0,5…+0,6 ms (tablo 0,39 ms). **Ucuzlatma 1** (2026-11-25): tablo yalnız `VtCache::generation` değişince (yerleşiklik değişti) o yuva için kurulur — kararlı durumda CPU 0 (25 karenin 0'ında kuruldu; kapı `vt_atlas_uploads_are_byte_exact_and_slots_reused`: değişiksiz karede kurulmaz ve tablo yine `resolve()` ile aynı, KONTROL yeni sütunlarda kurulur); shader doku boyunu (pw/ph/mips) tablodan değil malzemeden hesaplar (piksel başına 3 bağımlı SSBO okuması eksik). Mali-G72 iki koşum: örnekleme **+7,7 / +6,5 ms** (önce +14,4…+16,2). CPU payı 2 → 0 ms; kalan düşüşün shader değişikliğinden mi gürültüden mi geldiği ayrışmadı. Ölçüm kipleri (`vt_sample_probe` 2 = yalnız tablo, 3 = yalnız atlas) GPU payını **ayıramadı**: "yalnız tablo" +10,1 / +13,2 ms ile tam yoldan pahalı çıktı, tur yayılımı ±10 ms — bu çözünürlükte tablo/atlas payı iddia edilemez (daha uzun ölçüm ya da cihaz sayacı gerekir). **Açık:** VT örneklemenin Mali'de daha da ucuzlatılması (GPU payı ayrıştırılmadan), kenar dikişi için karo kenarı (border) ya da mip'ler arası süzme (ölçüm sonrası), disk akışı sağlayıcısı, mağaza/servis kalemleri (hesap gerekiyor) |
| Faz 7 — İlk oyunun dikey dilimi | 🟡 oynanabilir dilim var | "Gölge Salonları" (`engine_aksiyon.tpr`) + oyun kabuğu. **Açık:** cihazda perf CI (3 cihaz) |
| Faz 8 — Tulpar shader stage | 🟡 8.0 fizibilite + 8.1 gramer + 8.4 yerleşim geldi | Backend kararı **GLSL + glslc** (⚠️ REV-4). **23 shader'ın 23'ü** Tulpar alt kümesine taşındı (2026-11-05: `godray`, `triangle.vert` T9 dizi literali, iki compute T20), SPIR-V'leri bayt aynı; bit işlemleri/`T[N]`/`const` dile girdi (18/19 ayrışıyor — `tools/tpr_shader.py --tulpar-parse`; bu denetim 2026-09-20..27 arası ikiliyi bulamayıp "ATLANDI" diyordu, düzeltildi); bayt kapısı yerel derleyiciyle iki taraflı: **23/23 bayt aynı** (2026-11-05; kapı 14/19'a çürümüştü, `mesh.frag` ilk kez tam taşındı; `out` parametresi Tulpar demet dönüşünden üretiliyor — T21 dile yeni sözdizimi gerekmeden kapandı); bayt + gramer + CPU-GPU yerleşim denetimi `tools/yerel_ci.sh`'te, ATLANDI da kırmızı (GitHub CI'da yok: glslc). **8.2 GPU tip denetimi** (2026-11-23, `tools/tpr_tip.py`, Python çeviricide): 23/23 temiz, 3 189 ifade; 12 bozuk örnek `glslc`'ye kalmadan düşer ve `glslc` ile çapraz doğrulanır; `vecN` mutasyonu 90/90; Faz 8 bayt + tip kapıları artık GitHub Linux işinde de. **8.5 varyant ailesi** (2026-11-23, FAZ8 §10): `mesh`/`shadow` vertex aileleri tek `.tprs`'ten, **6/6 elle yazılmış varyant bayt aynı** (sonra `shadow_vsm` 7. varyant olarak 3 satırla eklendi), varyantlar birbirinden farklı (kontrolün kontrolü), 5 bozuk varyant girdisi çeviricide düşer; `.tprs` 24 → 20 dosya; `*_spv.h` 78 244 bayt SPIR-V. T3 (`0u`) TulparLang v3.47'de kapandı — `--tulpar-parse` 20/20 temiz. **Açık:** Tulpar derleyicisinin kendi tip sistemi (= PLAN §11 sistem alt kümesi), 8.5 reflection (ertelendi: doğrulama katmanı yakalıyor) ve yeni varyantlar (telefon ölçümü ister) |
| Faz 9 — Cluster geometri, VSM, tooling | 🟡 üç dilim geldi | Küme DAG builder + GPU cull/indirect (varsayılan kapalı), editör. **Üçgen/piksel oranı** (2026-11-04, `renderer/geom_density.hpp`): her karede çizimlerin sınır küresi izdüşümünden CPU tahmini (iyimser; mikro-üçgenli çizim sayısı kesin alt sınır) — editör Stats + köprü kapanış satırı (Mali'de zaman damgası olmadan da ölçülür); kapı `renderer_geom_density_matches_analytic_projection`. Ölçüm: Gölge Salonları 0.0002 üçgen/px, mikro 0 (RTX 5080, 300 kare). **VSM sayfa önbelleği** (2026-11-23, `renderer/vsm_pages.hpp`; GPU'suz çekirdek): mutlak ışık-uzayı ızgarasına oturan clipmap sayfa anahtarları, sabit fiziksel havuz + LRU, AABB ile kirletme, tükenince sayılır. Kapılar (`vsm_*`, 6): duran kamerada 2. kare 0 çizim; kaymada çizilen = anahtar kümesi farkı (83 istek, 7 yeni); nesne yalnız kesişen 4 sayfayı kirletir (uzak 0); 600 kare / 2 826 tahliye, 0 `operator new`. Derinlikten işaretleme (`vsm_mark_from_depth`, CPU başvurusu): analitik zemin sahnesinde 88/88 sayfa, fark 0; **seyrek örnekleme sayfa kaçırır** (her 4. piksel: 64/88 — işaretleme tam çözünürlükte olmalı). **GPU işaretleme** (2026-11-23, `rhi/shaders/vsm_mark.comp` — Tulpar kaynağı `.tprs`, bayt aynı — + yeni `rhi::ComputeKernel`): GPU'nun piksel başına sayfa anahtarları CPU başvurusuyla RTX 5080'de piksel farkı 0, sayfa 88/88, doğrulama hatası 0; KONTROL (yanlış seviye uzaklığı) 12 408 piksel ayrışır. Motorun ilk GPU'da koşturulup CPU'ya karşı sınanan compute shader'ı. **GPU tekilleştirme** (2026-11-23, `rhi/shaders/vsm_request.comp`, Tulpar kaynağı bayt aynı): işaretleme + açık adresli `atomicCompSwap` tablosu tek geçişte, CPU yalnız tekil listeyi okur — RTX 5080 320×180: 95/95 sayfa CPU başvurusuyla aynı, tekrar 0, okunan 396 bayt (piksel başına tampon 921 600 bayt); liste/tablo/aralık taşmaları SAYILIR (kontroller: ikinci koşum 0 yeni, liste 8 → 87 taşma, tablo 16 → tablo dolu). **Fiziksel atlas** (2026-11-23, `renderer/vsm_atlas`, `shadow_vsm.vert` = Faz 8.5 gölge ailesinin 4. varyantı): sayfa başına ortografik izdüşüm, kalıcı atlas (LOAD/STORE, yalnız render listesindeki karolar temizlenip çizilir) — RTX 5080: 88 sayfa / 360 448 texel **analitik gölgeyle fark 0** (KONTROL: 1 sayfa kaymış referans 7 213 fark); statik 2. kare 0 sayfa, atlas bayt aynı; kutu kayınca 88'in 5'i yeniden çizilir, dokunulmayan karolar bayt aynı. **Cihaz maliyeti** (2026-11-23, Mali-G72 P20 Pro, duvar saati — cihaz zaman damgası vermiyor; `vsm_device_cost_depth_store_and_request`): bütün VSM kapıları telefonda da aynı (işaretleme fark 0, tekilleştirme 95/95, atlas fark 0). 1080p derinliği belleğe yazmak geçişi ~2 katına çıkarır (0,78 → 1,50 ms, +0,73 ms; 7,9 MB/geçiş). `vsm_request.comp` 1080p: ilk sürüm 4,3 ms (aynı yuvada atomik çekişme; RTX 5080'de 215 ms) → düz okuma + iş grubu ön tekilleştirmesi ile tam ızgara 2,8–4,1 ms (koşumlar arası oynak), **seyrek (adım 2, dispatch boyundan) 1,56 ms** ve 107/107 sayfa (adım 4: 102). Yani VSM bu telefonda kare başına ~2 ms + örnekleme ister; **CSM aynı telefonda** (`renderer_device_cost_csm_shadow`, 1080p, zemin + 48 kutu, 3 × 1024² D16 + `mesh.frag` PCF): gölge açık 142,0 ms, kapalı 129,2 ms → **+12,8 ms** (kare + geri okuma, 15 kare medyanı; RTX 5080'de +0,5 ms). VSM'in ölçülen parçaları (~2,3 ms + örnekleme) bu sahnede CSM'den ucuz görünüyor — iddia değil: VSM örneklemesi ölçülmeden karşılaştırma tamamlanmaz. **Sahne derinliği graph kaynağı** (2026-11-23, `kResDepth`, `RendererConfig::vsm`): okuyan geçiş varsa saklanır, yoksa transient — karar türetilir (`graph_depth_stored`); saklanan/transient pikseller bayt aynı, doğrulama hatası 0. Aynı mekanizma VT geri bildirimi, Hi-Z, SSAO gibi derinlik okuyucularının ortak girişi. **İşaretleme geçişi renderer'da** (2026-11-23, `renderer/vsm_pass`, `vsm_request_img.comp` = ailenin görüntü varyantı): renderer'ın kendi derinliğinden GPU'nun istediği sayfalar, aynı derinlik geri okunup CPU'da işaretlendiğinde bulunanlarla aynı (RTX 5080 256×144: 50/50); statik kare 0 sayfa, kayan küp 12 sayfa kirletir, ışık dönünce sıfırlama sayılır; döküm değişimi (mesh, model) özetiyle bulunur — `prev_model`'e güvenilmez; doğrulama AÇIK cihazda hata 0. **Örnekleme `mesh.frag`'da** (2026-11-23, `VSM` özelleştirme sabiti = 2; kapalıyken CSM yolu bit bit aynı; set 0 binding 8 atlas + 9 sayfa tablosu, toroidal 16×16 ızgara, anahtar doğrulanır): uçtan uca gölge maskesi ANALİTİK maskeyle (zemin pikselinden ışığa ışın) VSM %99,94 / CSM %99,96 (RTX 5080), Mali-G72'de %99,93 / %99,95; KONTROL absürt eğilim → gölgenin %0'ı. **Uçtan uca maliyet (Mali-G72, 1080p post, 49 çizim)**: gölge kapalı 24,65 ms, CSM +10,12 ms, VSM +9,04 ms (kararlı durum, atlas çizimi 0) — VSM bu telefonda yalnız ~1,1 ms ucuz; örnekleme belirgin. Varsayılan CSM kalır. **Açık:** VSM örneklemesinin ucuzlatılması (ölçüm sonrası), iskeletli dökümler atlasta, (eğilim dahil), renderer'a bağlama (bayrakla, varsayılan kapalı) |
| Faz 10 — Metal / iOS | ⛔ başlanmadı | Mac + geliştirici hesabı gerekiyor |

## 6.1 Ne yok (sonraki aşama adayları; tarama belgesine karşı tam liste: `BOSLUK-TARAMASI.md`)
1. ~~Sahne veri modeli + dosya formatı~~ ✅, ~~runtime blob derleyici~~ ✅, ~~bake çıktıları blob'a (navmesh, ışık haritası)~~ ✅ 2026-09-15 (navmesh v2, küme DAG v3, GI sondası v4). ~~Telefon demosunda blob~~ ✅ 2026-11-23: `tools/android_run.sh demo|headless --sahne x.sahne` (masaüstünde `engine_sahnec`, blob APK varlığı, `debug.tulpar.scene`); emülatörde (x86_64, SwiftShader) ölçüldü: `editor.sahne` 8 varlık / 6 çizim / kaynak 3/3, blob özeti masaüstüyle aynı (`78dda487e9423924`), kare içi `new` 0; `--sahne`siz koşumda blob satırı yok. **Telefonda koşturulmadı** (cihaz bağlı değildi).
2. ~~**Editör**~~ ✅ çoklu seçim, kaynak tarayıcı, ışık/gölge/güneş gizmoları geldi. ~~Kare zamanı grafiği~~ ✅ 2026-11-04:
   Stats panelinde profiler halkasının son 240 karesi + p50/p99/max (implot yerine `ImGui::PlotLines`, yeni bağımlılık yok;
   `Profiler::frame_times`, test `profiler_frame_times_oldest_to_newest`). Aynı panelde ölçülmeyen üç satır düzeltildi:
   "GPU VRAM" ekran boyutundan uyduruluyordu (şimdi `Device::memory_used/reserved_bytes`), gölge satırı sabit
   "CSM 3x1024 D16" yazıyordu (şimdi `Renderer::shadow()`), "Sıcak Tahsis: 0 Bayt" ölçülmüyordu — editör `AllocGate`
   override'ını hiç linklemiyordu (şimdi linkli; headless özet "AllocGate: toplam N global new (override sayiyor)").
   "Tulpar ARC: 0 sızıntı" satırı kalktı (oyun ayrı süreç). ~~Oynatmayı geri sarma~~ ✅ 2026-11-04: F5 fizik
   önizlemesinde her 6 tikte tam fizik durumu (`Physics::save_state`/`restore_state`: Jolt SaveState + karakterler, ayırmasız
   sabit tampon) 120 kayıtlık halkaya (12 s @ 60 Hz, 15 MB arena) yazılır; **Shift+F10** duraklatır ve bir kayıt geri gider,
   oradan devam ilk koşumla bit eşit (`physics_save_restore_replays_bit_exact`: 65 kutu + karakter, kayıt 8981 B; editör
   headless "geri sarma kapisi" — geri yükleme devre dışı bırakılınca HATA, pozitif kontrol yapıldı). Partiküller geri sarılmaz.
3. ~~**Tulpar bağlaması**~~ ✅ 2026-09-15: `bridge/` + `lib/engine.tpr` + üç örnek oyun; köprü **175 builtin** (2026-09-27 sayımı: `tools/gen_engine_bindings.py` `SPEC` **208** satır) (ses, animasyon, navmesh, ışın/örtüşme sorguları, anlık-kip arayüz, kalıcı kayıt, sıcak yükleme, **çarpışma olayları** dahil — bkz. [KOPRU.md](KOPRU.md)).
   **Çarpışma olayı geldi (2026-09-16):** callback FFI olmadığı için geri çağrım değil **kuyruk** — fizik
   adımında oluşan temaslar sabit boy halkaya yazılır, oyun karede okur. Halka dolarsa olaylar sessizce
   kırpılmaz, `carpisma_dusen()` sayar ve motor karede bir kez hata logluyor.
   **Ajan/yol takibi geldi (2026-09-16):** yol ARAMA motorda (Detour), yol TAKİBİ `lib/engine.tpr`
   içinde saf Tulpar'da (`ajan_olustur` / `ajan_hedef` / `ajan_ilerlet`). Ayrım bilinçli: takip oynanış
   politikasıdır (hız, varış yarıçapı, yeniden arama sıklığı) — motora gömülseydi her oyun aynı davranışa
   mahkûm olurdu. Yollar ajan başına **kopyalanır**, çünkü motorun nokta tamponu tektir. Köprüye ayrıca
   `eng_nav_nearest` (mesh dışındaki konumu yapıştır — yoksa yol sorgusu 0 döner) ve `eng_nav_raycast`
   (doğru görüş; yol aramadan çok ucuz) eklendi.
4. Karakter modeli yok (iskelet/animasyon içe aktarma ve GPU skinning var; sanatçı varlığı gerek).
4b. **Normal haritası mip zinciri (2026-09-16):** blit ile üretilen mip'lerde ortalama normalin boyu 1
   değildi (ölçüldü: ters eğimli dama deseninde **0.60**) — uzaktaki yüzey sessizce düzleşiyordu. Normal
   haritaları artık CPU'da mip'lenip hazır seviye olarak yükleniyor (`build_normal_mips`), yeniden
   normalleştirilmiş en kötü boy **1.0000**. ORM bu yolu ALMAZ (pürüzlülük/metaliklik skalerdir).
   ~~Ayrı `occlusionTexture` okunmuyor~~ ✅ 2026-11-04: yüklemede ORM'e **birleştirilir** (G/B ORM'den, R occlusion'dan,
   en yakın komşu; ORM yoksa G/B 255 = çarpan aynen) — dördüncü sampler yine yok. Birleştirilemeyen (PBR'siz malzeme,
   arena) `Model::occlusion_unmerged`'da sayılır ve o malzemede occlusion KAPALI kalır (ORM.R başka veri taşıyabilir).
   Kapı `content_gltf_separate_occlusion_merges_into_orm` (eski ORM paketlemesi kontrol olarak birleşmez).
5. PBR, vis buffer A/B, VRS/FDM, VSM, virtual texture, PSO üretimi, gerçek HRTF/Steam Audio,
   GameActivity göçü, Memory Advice, mağaza/servis kalemleri (hesap gerekiyor),
   **üç fiziksel cihaz** (kullanıcı kararı: şimdilik pas), macOS CI çökmesi (yerelden ulaşılamıyor).
6. Her iki denetim de `build.sh suites` içinde koşuyor ve kırmızıysa suite düşüyor (`build.sh:276` dist arşiv, `build.sh:292` paket boyutu/SPIR-V/açılış).

## 6.2 CI, sürüm ve ölçüm platformları (2026-10-05 güncel)

Bu belgenin geri kalanı 2026-09-15 anlık görüntüsüdür; bu bölüm bugünkü hâli söyler.

| platform | sürücü | `engine_tests` | Tulpar köprüsü (`tulpar_dogrula.sh --tam`) | paket koşusu |
|---|---|---|---|---|
| Linux x86_64 (CI + sürüm) | lavapipe (mesa) | tam; Vulkan/katman atlaması KIRMIZI | CI: tam (dalga 2400 / aksiyon 3200 kare taban çizgileri dahil); sürüm işinde koşmuyor | zorunlu, 60 kare |
| macOS arm64 (CI + sürüm) | MoltenVK | tam; AArch64 fiber assembly'si yalnız burada | CI: tam; sürüm işinde koşmuyor | zorunlu, 60 kare |
| Windows x86_64 (CI + sürüm) | **SwiftShader** (Chrome'un ICD'si, kayıt defterinden; 2026-10-05'ten beri) | tam; Vulkan/katman atlaması KIRMIZI (sürüm provası 2026-10-05: 615/615, 9 atlandı — hiçbiri Vulkan/katman gerekçeli değil, 159 s) | CI + sürüm, `--uzun-atla`: 2 uzun taban çizgisi görünür ATLANDI, kalanı koşar (18 geçti) | zorunlu, 60 kare, hata 0 |
| RTX 5080 / Linux (yerel) | NVIDIA | tam (616/616, 0 atlandı) | tam (23 geçti) | — |

- Windows'ta GPU kapıları **artık atlanmıyor** (2026-10-05'e kadar ICD yoktu ve hepsi atlanıyordu).
  MSYS2 mesa lavapipe denendi, **elendi** (bellek bırakırken yığını bozuyor, Tuzaklar 8cr).
- `release.yml`'in Windows işi `ci.yml` ile aynı betikleri koşar (`tools/windows_vulkan_icd.sh`,
  `tools/windows_kapilar.sh`; #86); ICD kurulamazsa adım KIRMIZI (negatif provası: run 37314080867).
- Aynı süreçte motoru kapatıp yeniden açmak destekleniyor (Tuzaklar 8ct, #87): kapanış her şeyi
  bırakır ve köprü durumunu sıfırlar. Editörün F5'i bu yolu kullanmaz (oyun ayrı süreç).
- Köprü: `SPEC` **242** fonksiyon (2026-10-08: kare içi ayırma, düğme yazısı, sahne karartma, font sayaçları).
- Açık: üç fiziksel cihaz (kullanıcı kararı: pas). Android'de süreç canlıyken etkinliğin yeniden
  yaratılması (8ct yolu) **emülatörde ölçüldü** (2026-10-06, API 31 x86_64, SwiftShader Vulkan: aynı
  PID'de üç etkinlik, her oturum varlık 0 / kare 0, önceki id ölü, hata 0, kapanışta canlı vk 5);
  fiziksel cihazda (Mali) ölçülmedi.

## 7. Çalışma kuralları (kullanıcı) — 2026-09-15 anlık görüntüsü
> Bugün geçerli değil: CI var (üç platform, dal koruması zorunlu), PR'lar auto-merge ile birleşir,
> her `main` birleşmesi sürüm çıkarır (CLAUDE.md "CI ve sürüm"). Aşağıdaki metin tarihsel.

CI yok, push yok ("gönder" denene kadar); doğrulama yerel + telefon (+ emülatör yalnız işlevsel);
pencereyi ben açmam, ekran görüntüsü `adb screencap`; sayı yoksa iddia yok, her kapının kontrolü var.
PR #321 main'e birleşti (2026-09-14, squash; CI Linux + macOS yeşil). Yeni dal: `engine/faz3-sahne`
(3 yerel commit + **commit'lenmemiş büyük çalışma ağacı**: köprü, CSM, paketlenmiş vertex, render graph +
bloom, temporal, GPU cull, Faz 4 UI, GI, pack/delta, Faz 7 oyunu, web/Android onarımı — push yok,
kullanıcı kararı).
