# Tulpar Engine

Mobil öncelikli (ARM + Vulkan), tek stüdyoya özel C++17 oyun motoru. Genel amaçlı
değil: kendi oyunlarımız için yazılıyor. Bu depo **motorun kendisidir** — TulparLang
derleyicisinden [`git subtree split`](https://github.com/hamer1818/TulparLang) ile
ayrıldı, geçmişi korunarak. Ayrım 2026-09-20'de tamamlandı: dil deposunda artık
motora ait hiçbir şey yok, derleyici yalnız dilin kendi özelliklerini derliyor.

Plan ve ölçümler `docs/` altında: [PLAN.md](docs/PLAN.md) (yol haritası),
[MIMARI.md](docs/MIMARI.md) (mimari ve teknoloji kararları),
[VIZYON.md](docs/VIZYON.md), [DURUM.md](docs/DURUM.md) (tek sayfa "ne var, ne ölçüldü,
ne yok"), faz raporları [FAZ0](docs/FAZ0.md)–[FAZ8](docs/FAZ8.md),
cihaz matrisi [CIHAZ-MATRISI.md](docs/CIHAZ-MATRISI.md),
Tulpar köprüsü [KOPRU.md](docs/KOPRU.md).
**Bir şey kırıldığında ilk bakılacak yer:** [TUZAKLAR.md](docs/TUZAKLAR.md) — motorun
tekrar tekrar düştüğü 66 hata sınıfı (8a–8ap).

## Ağaç

```
platform/   L0  fatal, zaman, OS bellek, iş parçacığı, pencere (GLFW dlopen), çökme raporu
core/       L1  Arena ailesi + Pool<T>, fiber iş sistemi (x86_64/AArch64 asm), profiler,
                konteynerler, matematik, BVH/spatial hash, EBR
rhi/        L2  Vulkan (loader dlopen'lı — link zamanı bağımlılık yok), swapchain,
                pipeline cache, karo bütçesi, shader'lar (GLSL -> depoya giren *_spv.h)
renderer/   L3  forward Lambert, gölge atlası (3 kademe), bloom, cluster/LOD, derlenmiş
                render grafiği (geçişler veri, kod değil)
audio/      L3  kilitsiz SPSC mixer (32 ses), miniaudio cihazı
sim/        L4  ECS, çizelgeleyici, Jolt fiziği, Recast/Detour navmesh, animasyon
content/    L6  glTF 2.0 (cgltf+stb), KTX2/ASTC, sahne veri modeli (.sahne) ve blob (.sahneb)
bridge/     L6  Tulpar köprüsü: düz skaler `teng_*` C ABI + masaüstü/Android host
app/        L6  birleştirme kökü: engine_demo, engine_editor (ImGui+ImGuizmo)
tulpar/     L6  motorun TulparLang tarafı: engine.tpr sarmalayıcı, üretilmiş köprü
                bindingleri, örnek oyunlar, köprü testi (bkz. tulpar/README.md)
tests/          engine_tests — tek ikili, bütün kapılar
tools/          layer_check.py (ihlal = derleme hatası), compile_shaders.py,
                gen_engine_bindings.py, texpack/sahnec/clodbake, android_run.sh,
                faz8_shader_audit.py, layout_audit.py, ...
android/host/   Kotlin Activity + JNI host (libtulparengine.so)
third_party/    vendored: Vulkan başlıkları, Jolt, Recast, meshoptimizer, cgltf, stb,
                miniaudio, astcenc, ImGui, Tracy, GLFW başlıkları
```

## Derleme

### Tek komut

```bash
./derle.sh        # Linux / macOS
derle.bat         # Windows (çift tıklanabilir)
```

Betik önce **bağımlılıkları adıyla** denetler, eksik olanın kurulum komutunu yazar ve
"otomatik kurayım mı?" diye sorar; onaylarsanız kendisi kurar, sonra yapılandırıp derler.
Amacı şu: eksik bir paket, CMake'in ortasında anlaşılmaz bir hata yerine **başta ve
adıyla** görünsün.

| Seçenek | Ne yapar |
|---|---|
| *(yok)* | Denetle → eksikse sor → derle |
| `--otomatik` (`-y`) | Sormadan kur ve derle |
| `--sadece-denetle` / `--denetle` | Yalnız rapor; kurmaz, derlemez |
| `--temiz` | `yapi/` dizinini silip sıfırdan kurar |

Paket yöneticisi tanınır: **pacman, apt, dnf, zypper, brew** (Windows'ta **pacman**).
Tanınmayan bir sistemde betik uydurma bir komut yazmaz — eksikleri adıyla listeleyip
elle kurmanızı ister.

Zorunlu olmayan iki bağımlılık ayrı raporlanır, çünkü **derlemeyi engellemezler**:
`ccache` (yeniden derlemeyi hızlandırır) ve `glslc` (shader **bayt** kapısı; yoksa özet
kapısı yine koşar). Bir de *çalıştırma* bağımlılıkları var — `libvulkan` ve `libglfw`
yoksa derleme yine biter, ama editör pencere açamaz. Betik bunu ayrıca söyler.

### Elle

Gerek duyulanlar: **CMake 3.14+**, C++17 derleyici (GCC ya da Clang), **Ninja** (ya da
make), **python3** (katman denetimi ve shader araçları için). Vulkan SDK **gerekmez** —
başlıklar vendored, loader çalışma zamanında `dlopen` ediliyor.

```bash
cmake -S . -B yapi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build yapi -j
```

Windows'ta aynı komutlar **MSYS2 MINGW64** kabuğunda koşar. **MSVC desteklenmiyor**:
fiber geçişi GNU sözdizimli `.S` dosyası (Win64 dalı MinGW için yazılı), derleme
bayrakları da GCC/Clang yazımında. `derle.bat` MSYS2'yi bulamazsa `winget` ile kurmayı
önerir; asıl işi (paket denetimi + derleme) `tools/derle_mingw.sh` yapar — mantık cmd ile
bash arasında bölünmesin diye tek yerde durur.

```bash
pacman -S --needed mingw-w64-x86_64-{gcc,cmake,ninja,python,ccache,glfw}
```

Android çapraz derlemesi üst ağaç istemez:

```bash
cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
```

Seçenekler: `ENGINE_MEM_CANARY` (arena taşma kanaryaları, varsayılan ON — ship'te kapat),
`ENGINE_TRACY` (Tracy istemcisi, varsayılan OFF), `ENGINE_SWAPPY` (Android kare temposu).

### Ne çıkıyor

| hedef | ne |
|---|---|
| `engine_tests` | bütün kapılar tek ikilide |
| `engine_demo` | uygulama; `--headless N --out kare.ppm` ile penceresiz doğrulanır |
| `engine_editor` | sahne editörü (ImGui + ImGuizmo); `--headless N --out kare.ppm` |
| `engine_sahnec` | `.sahne` → `.sahneb` derleyicisi (`--check`, `--dump`, `--kanonik`) |
| `engine_texpack` | PNG → ASTC mip zinciri → `.ktx2` |
| `engine_clodbake` | çevrimdışı cluster/LOD bake |

### Editörü açmak

```bash
./editor.sh                   # Linux / macOS — varsayılan sahne (tests/assets/editor.sahne)
./editor.sh yol/x.sahne       # verilen sahneyle
editor.bat                    # Windows (çift tıklanabilir)
```

İkili yoksa önce `derle.sh`'yi (Windows'ta `tools/derle_mingw.sh`) bağımlılık denetimiyle
birlikte koşturur. Varsa **artımlı** derler: kaynak değiştiyse editör taze açılır,
değişmediyse ninja hiçbir şey yapmadan döner (ölçüldü 2026-09-23, Linux 16 çekirdek:
165–179 ms, kapılar dahil). Derleme düşerse editör **açılmaz** — eski ikiliyi açmak,
yaptığınız değişikliği görüyormuşsunuz gibi yanıltırdı.

| Seçenek | Ne yapar |
|---|---|
| `yol/x.sahne` | Sahne. Çağırdığınız dizine göre çözülür; yoksa betik başta durur |
| `--derleme-yok` | Derlemeyi atla, mevcut ikiliyi aç |
| `--headless N --out k.ppm` | Penceresiz N kare, son kareyi yaz |
| `--size WxH`, `--validation` | Editöre aynen geçer |
| `--bekleme-yok` | (`editor.bat`) Hatada tuş bekleme |

Tanınmayan seçenek **reddedilir**, editöre geçirilmez: editör tanımadığı argümanı sessizce
yok sayar, yani `--headles 30` gibi bir yazım hatası hata vermeden pencere açardı.

**Doğrudan ikiliyi çağırırken dikkat:** `engine_editor` sahneyi yalnız `--scene` ile alır.
Çıplak yol (`engine_editor x.sahne`) **sessizce yok sayılır** ve varsayılan sahne açılır;
varsayılanı verdiğinizde fark görünmez. `editor.sh` çıplak yolu `--scene`'e çevirir.

## Test

```bash
./yapi/engine_tests            # hepsi
./yapi/engine_tests rhi        # ada göre süzülür (alt dize)
```

Özet satırı şöyle biter: `engine tests: N passed, 0 failed, M atlandi (N/N kosuldu)`.

**KOŞMAYAN KAPI YEŞİL DEĞİLDİR.** Donanım ya da araç yoksa test sessizce `return`
etmez; `ATLANDI: <sebep>` basar ve özet satırındaki sayaca girer. Bu yüzden:

* `M` (atlandı) sayısını **oku**. Vulkan loader yoksa bütün RHI/renderer/içerik
  kapıları atlanır ve geriye kalan "passed" sayısı GPU hakkında hiçbir şey söylemez.
* `TULPAR_ENGINE_NO_VULKAN=1` bu yolu zorlar — atlama mekanizmasının pozitif kontrolü.
* CI (`.github/workflows/ci.yml`) **üç platform / iki mimari** koşar:
  `linux` x86_64 (lavapipe), `macos` arm64 (MoltenVK), `windows` x86_64 (MSYS2).
  **Ne zaman, nasıl — TulparLang ile aynı model (2026-09-21):** `main`'e açılan
  her PR üç işi koşturur; üçü `main`'in dal korumasında **zorunlu** ve dal
  `main`'i içermek zorunda (`strict`). `.github/workflows/auto-merge.yml` taslak
  olmayan her PR'da squash auto-merge'ü açar (gizli anahtar `AUTOMERGE_PAT`):
  üç iş yeşil olunca PR kendiliğinden `main`'e girer, dal silinir. `main`'e
  gelen squash commit'inde CI yeniden derlemez, PR koşumunun artefaktını
  (`motor-*`) yeniden kullanır. Yalnız `README.md` / `docs/*.md` değişen PR'da
  işler koşar ama adımları atlanır (check yine yeşil raporlar).
* **Sürüm otomatik (2026-09-21):** `main`'e giren her değişiklik bir sürüm
  çıkarır. `.github/workflows/otomatik-surum.yml` bir sonraki SemVer adımını
  hesaplayıp etiketi iter (varsayılan **yama**: `v0.1.4` → `v0.1.5`; PR
  açıklamasına tek satır `Surum: minor` ya da `Surum: major` yazarak
  büyütürsünüz), `release.yml` o etiketi görüp **üç platformda derleyip
  testleri koşturduktan sonra** Release'i yayınlar. Ölçülmemiş ikili
  yayınlanmaz. Yalnız belge değişen merge'de ve zaten etiketli bir commit'te
  sürüm çıkmaz (ikisi de iş özetine yazılır). Elle `v*` etiketi itmek hâlâ
  çalışır.
  Ölçüldü (2026-09-21): PR #19 birleşince `main` koşumu üç platformda da
  derlemeden PR artefaktını yükledi, boyutlar bayt bayt aynı.
  İlk ikisinde Vulkan sürücüsü kurulur ve yolun gerçekten koştuğu doğrulanır —
  "Vulkan yok" gerekçeli bir atlama işi **kırmızıya** çevirir; Windows'ta yazılım
  ICD'si olmadığı için GPU kapıları beklendiği gibi atlanır ve sebepleri iş
  özetine yazılır. macOS ayağı bir tekrar değil: fiber geçişi mimariye özel elle
  yazılmış assembly ve **AArch64 dalı yalnız orada** koşuyor.
* Zamanlama satırları (`[profiler]`, `[bilgi]`) bilgi basar, karar vermez.

Penceresiz doğrulama kuralı: **doğrulamak için pencere açma.** Demo ve editör
`--headless N --out x.ppm` ile aynı boru hattını offscreen koşturur; pencereli yolu
yalnız kullanıcı çalıştırır.

## Katman kuralı — sözleşme değil, mekanizma

`tools/layer_check.py` `engine_core`'un ön koşuludur: ihlal = **derleme hatası**.

1. Bir katman yalnız **altındaki** katmanları `#include` eder. Yukarı çağrı yok.
   `platform`=L0, `core`=L1, `rhi`=L2, `renderer`/`audio`=L3, `sim`=L4, `gameplay`=L5,
   `content`/`app`/`bridge`=L6, `tools`=L7; `tests/` her şeyi görür.
2. **STL konteyneri yok** (`vector`, `string`, `map`, `memory`, `functional`, …) —
   **testler dahil**. Test kodunda `vector` kullanmak "kare içinde 0 ayırma"
   iddiasını gizlerdi.
3. `-fno-exceptions -fno-rtti`; kare içinde global `new` yok (`AllocGate` sayar,
   Faz 0 kapısı pozitif kontrolle 0 ayırma iddia eder).

## TulparLang ile ilişkisi

Motor C++ kalır, **oyun betikleri Tulpar'da yazılır**. Bağlantı `bridge/`:

* `bridge/engine_api.h` — `teng_*`: **düz skaler** C ABI (struct yok, callback yok;
  Tulpar'ın bugünkü FFI'ının taşıdığı tek şekil). Çok değerli sorgu "hesapla sonra oku"
  kalıbıyla, çarpışma ise **kuyrukla** verilir — callback olmadığı için.
* `bridge/desktop_host.cpp` / `android_host.cpp` — pencere/yüzey/girdi (`BridgeHost`).
* `tools/gen_engine_bindings.py` içindeki `SPEC` tablosu **tek kaynaktır**: tek komutla
  dört üretilmiş dosyayı birden yazar. Elle tutulan nokta olmadığı için bağlama
  noktaları birbirinden kayamaz.
* Köprünün TulparLang tarafı — `engine.tpr` sarmalayıcı, üretilmiş bindingler, örnek
  oyunlar, köprü testi — **bu depodadır**: [`tulpar/`](tulpar/README.md).

TulparLang derleyicisi (<https://github.com/hamer1818/TulparLang>) motoru **tanımıyor**:
`eng_*` builtin'leri, `lib/engine.tpr` ve `engine_link_flags()` oradan kaldırıldı, o depo
yalnız dili derliyor. Motoru bir TulparLang kopyasına yeniden bağlamak için
`tools/gen_engine_bindings.py --tulpar <kök>` var; ayrıntısı
[tulpar/README.md](tulpar/README.md).

Tek dış bağımlılık, Android köprü arşivini kurarken istenen `TULPAR_ROOT`: bindingler
derleyicinin değer ABI'sini (`VMValue`) gördüğü için `<TulparLang>/src/vm/vm.hpp`
gerekir. Verilmezse `tulpar_engine_android` hedefi kurulmaz ve nedeni yazılır; motorun
kendi hedefleri etkilenmez.

Sözleşmenin tamamı: [docs/KOPRU.md](docs/KOPRU.md).

## Lisans

Motor kaynağı TulparLang projesinin lisansına tabidir. `third_party/` altındaki
vendored kütüphaneler kendi lisanslarıyla gelir (Jolt MIT, Recast zlib, meshoptimizer MIT,
cgltf MIT, stb public domain/MIT, miniaudio MIT/public domain, astc-encoder Apache-2.0,
ImGui MIT, Tracy BSD-3, Vulkan başlıkları Apache-2.0, `assets/fonts/DejaVuSans.ttf`
Bitstream Vera).
