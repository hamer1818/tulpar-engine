# Tulpar Engine — dağıtım paketi

Bu klasör CI'ın ürettiği **çalışan** pakettir: ikililer + onların açılışta
okuduğu varlıklar. Kaynak ağacı gerekmez.

> **Klasörü bozmadan kullanın.** İkililer varlıkları önce *kendi bulundukları
> dizine* göre arar (`assets/…`, `tests/assets/…`). `engine_editor` dosyasını
> tek başına başka bir yere kopyalarsanız sahneyi ve yazı tipini bulamaz.
> Klasörün tamamını taşıyın.

## İçindekiler

```
engine_demo        örnek sahne: PBR + gölge + LOD + iskeletli animasyon + fizik
engine_editor      sahne editörü (ImGui panelleri + ImGuizmo gizmoları)
engine_sahnec      sahne derleyicisi: .sahne -> .sahneb (ve --check/--dump/--kanonik)
engine_texpack     doku paketleyici: PNG -> ASTC mip zinciri -> .ktx2
engine_clodbake    çevrimdışı cluster/LOD bake: .gltf -> .clod
*.dll              (yalnız Windows) MinGW çalışma zamanı — silmeyin
assets/fonts/      arayüz ve HUD yazı tipleri (+ lisansları)
tests/assets/      demo ve editörün açılışta yüklediği sahne ve modeller
```

Windows'ta her ikilinin adı `.exe` ile biter (`engine_demo.exe` …).

## Çalıştırma

```bash
./engine_demo                      # pencere açar (ESC: çıkış)
./engine_editor                    # editör penceresi
./engine_editor --scene benim.sahne
```

Linux/macOS'ta paketi indirdiyseniz çalıştırma izni gerekebilir:
`chmod +x engine_*`. macOS'ta ikililer imzasız olduğu için karantinaya
düşerler: `xattr -dr com.apple.quarantine .` bir kez yeter.

### Penceresiz (headless) doğrulama — `--headless N --out x.ppm`

Ekran olmadan da aynı boru hattı koşar: `--headless N` N kare çizer,
`--out x.ppm` **son kareyi** PPM olarak yazar ve çıkar. Sunucuda, SSH
oturumunda ve CI'da doğrulamanın yolu budur; bir şeyin gerçekten çizildiğini
görüntüye bakarak anlarsınız.

```bash
./engine_demo   --headless 120 --out kare.ppm
./engine_editor --headless 60  --out editor.ppm --size 1280x720
```

PPM'i PNG'ye çevirmek için depodaki `tools/ppm2png.py` kullanılabilir.

### Bayraklar

| ikili | kullanım |
|---|---|
| `engine_demo` | `[--headless N] [--out x.ppm] [--frames N] [--size G Y] [--present fifo\|mailbox\|immediate] [--no-prerotate] [--scene x.sahneb]` |
| `engine_editor` | `[--scene x.sahne] [--headless N] [--out x.ppm] [--size GxY] [--validation]` |
| `engine_sahnec` | `in.sahne [out.sahneb]` &#124; `--check in.sahne` &#124; `--dump x.sahneb` &#124; `--kanonik in.sahne [out]` |
| `engine_texpack` | `in.png out.ktx2 [--tur albedo\|orm\|normal] [--block 4x4] [--quality 60] [--linear]` |
| `engine_clodbake` | `in.gltf out_prefix [--mesh N] [--tri 124] [--vert 64]` |

`engine_demo --size` **iki** sayı alır (`--size 1280 720`), `engine_editor --size`
tek bir `GxY` yazımı ister (`--size 1280x720`). İkisi de bilerek kendi
`main()`'lerindeki ayrıştırıcıya uyar.

### Ortam değişkenleri

| değişken | etkisi |
|---|---|
| `TULPAR_ENGINE_ASSETS` | varlık dizinini elle verir (paketin içindekinin yerine) |
| `TULPAR_ENGINE_FONT` | HUD yazı tipi dosyasını doğrudan verir |
| `TULPAR_ENGINE_SCENE` | `engine_demo` için derlenmiş sahne blob'u (`.sahneb`) |
| `TULPAR_ENGINE_GPU` | birden çok GPU varsa tercih edilen cihaz adının bir parçası |
| `TULPAR_ENGINE_VK_VALIDATION` | Vulkan doğrulama katmanlarını açar (katmanlar kuruluysa) |
| `TULPAR_ENGINE_AUDIO` | demoda ses cihazını açar |

## Vulkan: SDK gerekmez, **sürücü gerekir**

Motor Vulkan loader'ını link zamanında değil, çalışma zamanında `dlopen`
(`LoadLibrary`) ile açar. Bu yüzden:

* **Vulkan SDK kurmanıza gerek yok** — başlıklar zaten derlenmiş durumda.
* Ama bir **Vulkan sürücüsü (ICD)** olmalı. Yoksa program pencere açmadan
  "Vulkan cihazı yok" diyerek çıkar; eksik olan paket değil, sürücüdür.

| sistem | gereken |
|---|---|
| Linux | GPU sürücüsünün Vulkan paketi; GPU yoksa yazılım ICD'si: `mesa-vulkan-drivers` (lavapipe) + `libvulkan1` |
| macOS | MoltenVK (`brew install vulkan-loader molten-vk`) — Metal üzerinde Vulkan |
| Windows | güncel GPU sürücüsü (Vulkan loader'ı sürücüyle birlikte gelir) |

Kurulumu `vulkaninfo --summary` ile doğrulayabilirsiniz.

## Windows'a özel

Yanındaki `*.dll` dosyaları MinGW çalışma zamanıdır (`libstdc++`, `libgcc`,
`libwinpthread` …). İkililerle **aynı klasörde** durmak zorundalar; silinirse
program "başlatılamadı" hatası verir. Paket, listeyi elle tutmak yerine
`ldd` çıktısından üretir, yani araç zinciri değişince kendiliğinden güncellenir.

## Paket nasıl üretiliyor

`tools/package.sh <yapi-dizini> <çıktı-dizini>` — CI de insan da aynı betiği
koşar. Betik varlık listesini kaynaktan **türetir** ve paketi sonunda denetler;
eksik bir dosya işi kırmızıya çevirir. Yalnız denetlemek için:
`tools/package.sh --denetle <çıktı-dizini>`.
