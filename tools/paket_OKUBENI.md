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
BASLAT-*.bat       (yalnız Windows) başlatıcılar — hata olursa konsolu açık tutar
*.dll              (yalnız Windows) MinGW çalışma zamanı — silmeyin
glfw3.dll          (Windows) pencere kütüphanesi: çalışma anında yüklenir, silmeyin
libglfw.so.3       (Linux) aynısı
libglfw.3.dylib    (macOS) aynısı
lisanslar/         pakete konan kütüphanelerin lisansları (GLFW: zlib)
assets/fonts/      arayüz ve HUD yazı tipleri (+ lisansları)
tests/assets/      demo ve editörün açılışta yüklediği sahne ve modeller
SURUM.txt          bu paketin sürümü ve platformu — elle DÜZENLEMEYİN
DOSYALAR.txt       paketteki her dosyanın SHA-256 özeti — elle DÜZENLEMEYİN
```

Windows'ta her ikilinin adı `.exe` ile biter (`engine_demo.exe` …).
`SURUM.txt` ve `DOSYALAR.txt` güncelleyicinin dayanağıdır; ne işe yaradıkları
aşağıda, [Güncelleme](#güncelleme) bölümünde.

## Çalıştırma

**Windows'ta `BASLAT-engine_editor.bat` dosyasına çift tıklayın**, `.exe`'ye
değil. Sebebi: `.exe` doğrudan çalıştırıldığında bir sorun çıkarsa konsol
penceresi programla birlikte kapanır, hata satırı da onunla gider — ekranda
hiçbir şey olmamış gibi görünür. Başlatıcı, çıkış kodu 0 değilse sebebi ekranda
tutar ve bir tuşa basmanızı bekler.

Program açılışta bir hatayla çıkarsa aynı satırı **ikililerin yanındaki
`engine_hata.log`** dosyasına da yazar. Bir sorun bildirirken o dosyayı ekleyin.

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

## Ne pakette, ne sizde olmalı

Motor iki kütüphaneyi link zamanında değil **çalışma anında** açar (`dlopen` /
`LoadLibrary`): pencere için **GLFW**, çizim için **Vulkan loader**. İkisinin
paketteki karşılığı bilerek farklı:

| kütüphane | pakette mi | neden |
|---|---|---|
| GLFW (`glfw3.dll` / `libglfw.so.3` / `libglfw.3.dylib`) | **evet, yanında** | Windows'ta ve macOS'ta sistemde bulunmaz; olmadan pencere hiç açılmaz. Lisansı `lisanslar/glfw/` altında (zlib). Linux'ta sisteminizde kurulu bir GLFW varsa **o** kullanılır, paketteki yalnızca yedektir. |
| Vulkan loader (`vulkan-1.dll` / `libvulkan.so.1`) | **hayır** | Loader yalnızca yönlendiricidir; çizen şey **sürücüdür (ICD)** ve loader'ı da sürücü kurulumu getirir. Yanımızda taşısaydık sürücüsü olmayan makinede yine hiçbir şey çalışmaz, sürücüsü olan makinede ise sistemdeki (genelde daha yeni) loader'ı gölgelerdi. |

## Vulkan: SDK gerekmez, **sürücü gerekir**

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

Yanındaki `*.dll` dosyaları iki gruptur ve ikisi de **aynı klasörde** durmak
zorundadır:

* **MinGW çalışma zamanı** (`libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`).
  Silinirse Windows "program başlatılamadı" der. Bu liste elle tutulmaz,
  ikilinin ithalat tablosundan üretilir.
* **`glfw3.dll`** — pencere kütüphanesi. İthalat tablosunda *görünmez*, çünkü
  çalışma anında yüklenir. Silinirse program pencere açmadan şu satırla çıkar:
  `pencere: GLFW yok (glfw3.dll): masaustu pencere acilamaz`.

> v0.1.0 paketi tam olarak bu ikinci DLL'i taşımıyordu: paketleyici DLL'leri
> `ldd` çıktısından buluyordu ve `ldd` tanımı gereği `dlopen`'lanan bir
> kütüphaneyi göremez. Artık liste **kaynaktaki `dl_open(...)` çağrılarından**
> türetiliyor ve eksikse iş kırmızıya döner.

### Sorun giderme

| belirti | sebep / çözüm |
|---|---|
| Çift tıklayınca bir an siyah pencere açılıp kapanıyor | Program bir hatayla çıktı. `BASLAT-engine_editor.bat` ile çalıştırın; satır ekranda kalır. `engine_hata.log` da aynı satırı içerir. |
| `pencere: GLFW yok (glfw3.dll)` | `glfw3.dll` klasörden silinmiş ya da ikililer klasörden çıkarılmış. Paketi bütün halinde tutun. |
| `Vulkan cihazi yok` / `GLFW: Vulkan loader bulunamadi` | GPU sürücüsü Vulkan içermiyor ya da güncel değil. Sürücüyü güncelleyin; `vulkaninfo --summary` ile doğrulayın. |
| "program başlatılamadı" (`0xc000007b` vb.) | MinGW DLL'lerinden biri eksik. Paketi yeniden çıkarın. |

## Güncelleme

### Editörden

**Yardım → Güncellemeleri denetle.** Editör GitHub'daki son sürümü sorar;
daha yenisi varsa notlarını gösterir. Onay verirseniz:

1. bu platformun arşivini indirir ve özetini aynı sürümün
   `tulpar-engine-<sürüm>-SHA256SUMS.txt` dosyasıyla karşılaştırır,
2. arşivi `.guncelleme/` altına açar ve açılan **her dosyayı** yeni paketin
   `DOSYALAR.txt`'si ile doğrular,
3. ancak bundan sonra kurulu dosyaların yerine koyar. Yeni sürüm editörü
   yeniden başlatınca devreye girer.

Bilmeniz gerekenler:

* **Paket klasörü yazılabilir olmalı.** Güncelleme dosyaları *bu klasörün
  içinde* değiştirir. Yönetici izni isteyen bir yere (`C:\Program Files`,
  `/opt`, `/Applications`) açtıysanız güncelleme başarısız olur ve klasör
  **hiç değişmez**; paketi kendi kullanıcınızın yazabildiği bir yere taşıyın.
* **Değiştirdiğiniz dosyalar ezilmez.** Bir paket dosyasının özeti eski
  `DOSYALAR.txt`'dekinden farklıysa onu siz değiştirmişsinizdir (örnek:
  editörde kaydettiğiniz `tests/assets/editor.sahne`). O dosya yerinde kalır,
  yeni sürümü yanına **`<ad>.yeni`** olarak yazılır ve editör bunları
  listeler. İki sürümü birleştirmek size kalır; birleştirdikten sonra
  `.yeni` dosyasını silebilirsiniz.
* **`.guncelleme/` dizini** güncelleyicinin çalışma alanıdır: indirilen
  arşiv, açılan paket ve `yedek-<eski sürüm>/` (yerinden alınan eski
  dosyalar). Kurulum bir adımda başarısız olursa o ana kadar yapılan her
  değişiklik geri alınır, klasör eski haline döner. Eski yedekler bir sonraki
  açılışta silinir (Windows'ta çalışan eski `.exe` ancak program kapanınca
  silinebildiği için). Editör kapalıyken bu dizini elle silmek güvenlidir.
* **Güncelleme kapalı görünüyorsa** sebebi pencerede yazar. En sık iki sebep:
  editör kaynaktan derlenmiştir (`SURUM.txt` `kaynak …` der) ya da klasörde
  `DOSYALAR.txt` yoktur.
* **Sınır:** bütünlük SHA-256 özetleriyle korunur, imzayla değil. Bu, bozuk ya
  da yarım indirmeye karşı korur; GitHub hesabının ele geçirilmesine karşı
  korumaz.

### `SURUM.txt` ve `DOSYALAR.txt` — elle düzenlemeyin

İkisi de paketlenirken üretilir.

* **`SURUM.txt`** — tek satır: `<sürüm> <platform>`, örneğin
  `v0.2.0 linux-x86_64`. Sürüm `engine_editor` ikilisinin içine gömülü
  sürümden okunur; kaynaktan derlenmiş bir pakette `kaynak <platform>` yazar.
* **`DOSYALAR.txt`** — paketteki her dosyanın (kendisi hariç) SHA-256 özeti,
  standart `sha256sum` biçiminde (`<64 onaltılık>  <göreli yol>`).
  Güncelleyici "bu dosyayı kullanıcı değiştirdi mi?" sorusunu **buna bakarak**
  cevaplar.

Bu dosyaları düzenlerseniz güncelleyici yanlış karar verir: `DOSYALAR.txt`'yi
silerseniz güncelleme kapanır; bir satırı değiştirdiğiniz dosyanın yeni
özetiyle güncellerseniz o dosya "değişmemiş" sayılır ve korunmak yerine
yenisiyle değiştirilir.

Paketin bozulmadığını (ya da hangi dosyaları değiştirdiğinizi) görmek için
paket klasöründe:

```bash
sha256sum -c --quiet DOSYALAR.txt          # Linux, MSYS2: yalnız farklı/eksik olanlar basılır
shasum -a 256 -c DOSYALAR.txt | grep -v ': OK$'   # macOS
```

```powershell
# Windows PowerShell
Get-Content DOSYALAR.txt | ForEach-Object {
  $h, $p = $_ -split '  ', 2
  if (-not (Test-Path $p) -or (Get-FileHash $p -Algorithm SHA256).Hash.ToLower() -ne $h) { "FARKLI: $p" }
}
```

### Elle güncelleme

1. GitHub'daki **Releases** sayfasından platformunuzun arşivini
   (`tulpar-engine-<sürüm>-<platform>.tar.gz`, Windows'ta `.zip`) ve
   `tulpar-engine-<sürüm>-SHA256SUMS.txt` dosyasını indirin.
2. Arşivi doğrulayın: `sha256sum -c --ignore-missing tulpar-engine-<sürüm>-SHA256SUMS.txt`
   (macOS: `shasum -a 256 <arşiv>` çıktısını özet dosyasındaki satırla,
   Windows: `Get-FileHash <arşiv>` çıktısını karşılaştırın).
3. Arşivi **yeni bir klasöre** açın; eskisinin üzerine açmayın.
4. Eski klasörde değiştirdiğiniz dosyaları yukarıdaki `sha256sum -c` komutuyla
   bulun ve yenisine taşıyın (sahneleriniz vb.). Eski klasörü sonra
   silebilirsiniz.

## Paket nasıl üretiliyor

`tools/package.sh <yapi-dizini> <çıktı-dizini>` — CI de insan da aynı betiği
koşar. Betik hem varlık listesini hem de **çalışma anında yüklenen kütüphane
listesini** kaynaktan **türetir** (ikinci bir elle yazılmış liste yok) ve paketi
sonunda denetler; eksik bir dosya işi kırmızıya çevirir. Son adımda
`SURUM.txt` ve `DOSYALAR.txt`'yi yazar (`tools/paket_manifest.py`); denetim her
satırın özetini, listede olmayan ya da pakette olmayan dosyayı ve sürümün
ikiliyle aynı olduğunu da ölçer. Yalnız denetlemek için:
`tools/package.sh --denetle <çıktı-dizini>`.
