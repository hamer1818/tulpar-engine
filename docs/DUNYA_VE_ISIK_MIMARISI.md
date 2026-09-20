# 🌍 TULPAR ENGINE: DÜNYA, ÇEVRE VE AYDINLATMA MİMARİSİ
## Derin Sistem Analizi, Endüstri Standardı Mimari ve Devrimsel Işık Çözümleri
**Belge Sürümü:** 1.0.0  
**Tarih:** 2026-09-20  
**Yazar:** Tulpar Engine Çekirdek Mimari Ekibi  
**Durum:** ONAYLANDI / UYGULAMA PLANI  

---

## 📑 İÇİNDEKİLER
1. [BÖLÜM 1: Mimari Felsefe ve Mevcut Hatanın Analizi](#bölüm-1-mimari-felsefe-ve-mevcut-hatanın-analizi)
   - 1.1 Skybox Neden Bir "Varlık Bileşeni (Entity Component)" Olamaz?
   - 1.2 Endüstri Standartlarının İncelenmesi (UE5, Unity HDRP, Godot 4, Frostbite)
   - 1.3 Tulpar Engine İçin Doğru Model: `Dünya / Çevre Sistemi` vs `Varlık Hiyerarşisi`
2. [BÖLÜM 2: Dünya, Gökyüzü ve Atmosfer Mimarisi](#bölüm-2-dünya-gökyüzü-ve-atmosfer-mimarisi)
   - 2.1 Fiziksel Tabanlı Atmosferik Saçılma (Bruneton / Nishita Modeli)
   - 2.2 HDRI Panoramik Skybox ve Çift Katmanlı IBL (Diffuse SH L2 + Specular Split-Sum)
   - 2.3 Prosedürel Güneş ve Ay Diski (24 Saat Dinamik Gece/Gündüz Döngüsü)
   - 2.4 Hacimsel Bulutlar ve Üstel Yükseklik Sisi (Volumetric Clouds & Height Fog)
   - 2.5 Çevre ve Post-Process Hacimleri (Environment & Post-Process Volumes)
3. [BÖLÜM 3: Tulpar Devrimsel Işık Paketi (13 Temel Işıklandırma Sistemi)](#bölüm-3-tulpar-devrimsel-işık-paketi-12-temel-işıklandırma-sistemi)
   - 3.1 Yönsel Işık (Directional Light - Güneş / Ay) + 4-Kademeli CSM + Işık Hüzmeleri (God Rays)
   - 3.2 Nokta Işık (Point Light) + Fiziksel Ters-Kare Sönümleme + Kübik Derinlik
   - 3.3 Spot Işık (Spot Light) + İç/Dış Koni Açısı + Penumbra + IES Profilleri
   - 3.4 Dikdörtgen / Alan Işık (Rect / Area Light) + LTC (Linearly Transformed Cosines)
   - 3.5 Tüp / Kapsül Işık (Tube / Capsule Light)
   - 3.6 Disk Işık (Disk Light)
   - 3.7 Gök Işığı (Sky Light / Ambient Dome) + Gerçek Zamanlı Sahne Yakalama
   - 3.8 Yansıma Sondaları (Reflection Probes) + Parallaks Düzeltmeli Kutu/Küre
   - 3.9 İrradiance Işık Hacmi Sondaları (Irradiance Volume / GI Probes)
   - 3.10 Emissive (Işıma Yapan) Yüzeyler ve Mesh Işıkları
   - 3.11 Hacimsel Sis / Katılımcı Ortam Hacmi (Volumetric Fog Volume)
   - 3.12 Fotometrik Birimler ve Kelvin Renk Sıcaklığı Skalası
   - 3.13 Mobil Uyumlu Işık Hüzmeleri (God Rays) ve Hacimsel Koni Işıkları
4. [BÖLÜM 4: GPU Veri Yapıları, Vulkan Clustered Forward+ ve Bellek Mimarisi](#bölüm-4-gpu-veri-yapıları-vulkan-clustered-forward-ve-bellek-mimarisi)
   - 4.1 Bellek Düzeni: std140 / std430 UBO & SSBO Paketleme
   - 4.2 Clustered Forward+ Izgarası (Mobil TBDR Dostu 16×8×24 Hücreleme)
   - 4.3 Sıfır Dinamik Tahsisat ve Determinizm Garantisi
5. [BÖLÜM 5: Editör Kullanıcı Arayüzü ve Sağ Tık Menü Yeniden Yapılandırması](#bölüm-5-editör-kullanıcı-arayüzü-ve-sağ-tık-menü-yeniden-yapılandırması)
   - 5.1 Sağ Tık Sahne Hiyerarşi Menüsünün Yeni Anatomisi
   - 5.2 Inspector (Özellikler) Panelinde Işık ve Çevre Arayüzleri
   - 5.3 Dünya (World Settings) Panelinin Evrimi
6. [BÖLÜM 6: SROS (Scene Repair & Optimization System) Denetim Kuralları](#bölüm-6-sros-scene-repair--optimization-system-denetim-kuralları)
7. [BÖLÜM 7: Fazlandırılmış Uygulama Yol Haritası](#bölüm-7-fazlandırılmış-uygulama-yol-haritası)
8. [BÖLÜM 8: Devrimsel Mobil Uyumlu Partikül ve VFX Mimarisi (Particle Systems)](#bölüm-8-devrimsel-mobil-uyumlu-partikül-ve-vfx-mimarisi-particle-systems)

---

## BÖLÜM 1: Mimari Felsefe ve Mevcut Hatanın Analizi

### 1.1 Skybox Neden Bir "Varlık Bileşeni (Entity Component)" Olamaz?
Mevcut kod tabanında (`content/scene.hpp` ve `app/editor_widgets.cpp`) `kSceneSkybox` adında bir bayrak tanımlanmış ve şu yorum düşülmüştür:
> `// kSceneSkybox'in alani YOK: bileseni tasimak (gokyuzu var mi) tek veridir.`

Bu yaklaşım üç temel mimari çelişki barındırır:
1. **Uzamsal Anlamsızlık (Spatial Incoherence):** Bir `SceneEntity` konum (`pos`), rotasyon (`rot_deg`), ölçek (`scale`) ve hiyerarşik ebeveyn (`parent`) taşır. Sonsuz uzaktaki bir gökyüzünün veya atmosferik saçılmanın sahnede bir konumu (`pos = {x, y, z}`) veya ölçeği olamaz. Bir karakterin veya sandığın altına "Skybox" bağlamak anlamsızdır.
2. **Veri Eksikliği (Data Starvation):** Gökyüzü bir "boolean bayrak" değildir. Gökyüzü; güneş yönü, atmosfer yoğunluğu, ozon tabakası soğurması, Rayleigh/Mie katsayıları, bulanıklık (turbidity), gökyüzü parlaklığı (exposure) ve HDRI harita yollarını barındıran zengin bir parametrik alandır.
3. **Sahne Kirliliği ve Editoryal Karmaşa:** Sahne hiyerarşisinde `Küp`, `Küre`, `Oyuncu` gibi nesnelerin yanında `Skybox` adında içi boş bir varlık yaratmak kullanıcının zihnini karıştırır ve motorun acemice tasarlandığı izlenimini verir.

### 1.2 Endüstri Standartlarının İncelenmesi

| Motor | Gökyüzü / Atmosfer Modeli | Işık Sistemi Mimarisi | Çevre & Post-Process |
|---|---|---|---|
| **Unreal Engine 5** | `SkyAtmosphere`, `VolumetricCloud`, `SkyLight` (Dünya veya Çevre Aktörü) | `DirectionalLight`, `PointLight`, `SpotLight`, `RectLight`. IES profilleri, Lux/Lumen birimleri. | `PostProcessVolume` (Unbound veya Bounded), `ExponentialHeightFog`. |
| **Unity (HDRP)** | `Volume Framework` -> `Physically Based Sky`, `HDRI Sky` | `Directional`, `Point`, `Spot`, `Area` (Rectangle/Tube/Disc). Candela/Lumen/Lux. | `Volume` (Global / Local), `Density Volume` (Sis). |
| **Godot 4** | `WorldEnvironment` -> `Sky` (PhysicalSkyMaterial / PanoramaSkyMaterial) | `DirectionalLight3D`, `OmniLight3D`, `SpotLight3D`. Enerji, Mesafe sönümleme. | `Environment` (Tonemap, Glow, Fog, Ambient Light, SDFGI). |
| **Frostbite** | `SkyComponent` & `AtmosphereComponent` (Dünya Ayarları Katmanı) | Clustered Deferred / Forward+. Fiziksel ışık birimleri, LTC Area Lights. | Hacimsel veri tabloları, `Enlighten` / Real-Time GI. |

### 1.3 Tulpar Engine İçin Doğru Model
Tulpar Engine'in mimarisi iki net ayrım üzerine kurulmalıdır:
1. **Dünya & Çevre Ayarları (`SceneWorld`):**
   - Sahneye global olarak uygulanan, sonsuz uzayı ve atmosferi yöneten ayarlar.
   - Gökyüzü (Fiziksel Atmosfer veya HDRI), Global Güneş (Directional Light), Ortam Aydınlatması (Ambient / Sky Light), Global Hacimsel Sis (Exponential Fog), Ton Haritalama ve Pozlama.
   - Bu ayarlar `SceneEntity` DEĞİLDİR; sahnenin temel tabanında (`SceneWorld`) yaşar ve editörün **Dünya (World)** panelinden yönetilir.
2. **Yerel Çevre Hacimleri (`Environment Volumes`):**
   - Eğer bir iç mekana (mağara, bina içi, oda) girildiğinde gökyüzü ışığı kesilecek, sis rengi değişecek veya özel bir yansıma probu devreye girecekse, bunlar hiyerarşide birer **Hacim (Volume)** varlığı olarak yer alır (`kSceneRefProbe`, `kSceneFogVolume`, `kScenePostProcessVolume`).
3. **Varlık Hiyerarşisi (`SceneEntity`):**
   - Yalnızca fiziksel varlığı, koordinatı ve hacmi olan aktörler: 3B Modeller, İlkeller, Nokta/Spot/Alan Işıkları, Karakterler, Ses Kaynakları, Parçacıklar.

---

## BÖLÜM 2: Dünya, Gökyüzü ve Atmosfer Mimarisi

```
+-----------------------------------------------------------------------------------+
|                                 SCENE WORLD                                       |
+-----------------------------------------------------------------------------------+
| [Fiziksel Atmosfer / Sky]  | [Güneş / Ay (Directional)] | [Ortam & Gök (Sky Light)]|
| - Rayleigh / Mie Saçılması | - Yön (Sun Dir), Açı       | - SH L2 İrradiance       |
| - Bulanıklık (Turbidity)   | - Işık Akısı (Lux)         | - Prefiltered Specular   |
| - Ozon Soğurması           | - 4-Kademe CSM Gölge       | - Gerçek Zamanlı Capture |
| - HDRI Cubemap Seçeneği    | - Güneş Diski & Corona     | - Çevre Yansımaları      |
+----------------------------+----------------------------+--------------------------+
| [Hacimsel Sis (Fog)]       | [Sonradan İşleme (Post)]   | [GI Işık Haritası]       |
| - Taban Yoğunluğu / Yüksekl| - ACES Tonemapping, Bloom  | - Voxel / Sonda Grid     |
| - Saçılma Rengi (Albedo)   | - Pozlama (EV100 / Manuel) | - Offline CPU Ray-Trace  |
| - Anizotropi (Mie g)       | - Keskinleştirme, TAA      | - Dinamik Obje Enterpol. |
+-----------------------------------------------------------------------------------+
```

### 2.1 Fiziksel Tabanlı Atmosferik Saçılma (Bruneton / Nishita Modeli)
Gök kubbenin mavi görünmesi ve gün batımında kızıllaşması Rayleigh saçılmasının dalga boyuna ($\lambda^{-4}$) bağımlılığından kaynaklanır:
- **Rayleigh Saçılması (Hava Molekülleri):**
  $$\beta_R(\lambda) = \frac{8\pi^3(n^2-1)^2}{3 N \lambda^4}$$
  Tulpar dalga boyları referansı: Kırmızı ($680\text{ nm}$), Yeşil ($550\text{ nm}$), Mavi ($440\text{ nm}$).
- **Mie Saçılması (Aerosol ve Toz Parçacıkları):**
  İleriye doğru yönlü saçılma (Henyey-Greenstein faz fonksiyonu, $g \approx 0.76 - 0.85$).
- **Ozon Soğurması:**
  Güneş ufkun altındayken gökyüzünün tepe noktasının derin mavi kalmasını sağlayan Chappuis bant soğurması.

### 2.2 HDRI Panoramik Skybox ve Çift Katmanlı IBL
Eğer dinamik atmosfer yerine önceden çekilmiş stüdyo veya dış mekan HDRI (.hdr / .ktx2) kullanılacaksa:
1. **Diffuse Irradiance:** 9 katsayılı 2. Derece Küresel Harmonikler (Spherical Harmonics - $L_2$). UBO'da yalnızca $9 \times \text{vec4} = 144\text{ bayt}$ kaplar, $O(1)$ GPU maliyetiyle mükemmel ortam difüz aydınlatması sağlar.
2. **Specular Reflection:** GGX Split-Sum yaklaşımı (Brian Karis, UE4 PBR). 5-6 mip seviyeli kübik doku (prefiltered environment map) + 2D BRDF LUT.

### 2.3 Prosedürel Güneş ve Ay Diski (24 Saat Döngüsü)
Güneş doğrultusu ($\vec{L}_{sun}$) ve Ay doğrultusu ($\vec{L}_{moon}$) günün saatine ($0.0 - 24.0$) göre astronomik olarak hesaplanır:
- Güneş ufkun üzerindeyken atmosfer saçılması ve gün ışığı devrededir.
- Ufkun altına indiğinde yumuşak alacakaranlık (twilight) geçişi yapılır ve zayıf ay ışığı (gece mavisi, 0.05 Lux) ile yıldız kubbesi (prosedürel voronoi yıldız haritası) devreye girer.

### 2.4 Hacimsel Bulutlar ve Üstel Yükseklik Sisi
- **Üstel Yükseklik Sisi (Exponential Height Fog):**
  $$\rho(z) = \rho_0 \cdot e^{-\lambda(z - z_0)}$$
  Zeminde yoğun, yükseldikçe azalan analitik sis entegrali. Piksel başına shader'da kapalı formda hesaplanır, sıfır ek bellek harcar.
- **Hacimsel Sis (Volumetric Fog):**
  $160 \times 90 \times 64$ boyutunda 3D Froxel gridi. Işıkların hacim içindeki saçılımını ve gölge hüzmelerini (God Rays) gerçek zamanlı hesaplar.

---

## BÖLÜM 3: Tulpar Devrimsel Işık Paketi (12 Temel Işıklandırma Sistemi)

Modern bir AAA / İleri Düzey motorda ışıklandırma yalnızca "nokta ışık"tan ibaret olamaz. Aşağıdaki 12 sistem Tulpar Engine'in aydınlatma omurgasını oluşturur:

```
                            TULPAR LIGHTING MATRIX
                            
  [Yönsel Işıklar]         [Nokta & Koni Işıklar]      [Alan / Yüzey Işıkları]
  ├── Directional (Sun)    ├── Point Light (Omni)      ├── Rect / Area Light (LTC)
  └── Directional (Moon)   └── Spot Light (Cone+IES)   ├── Tube / Capsule Light
                                                       └── Disk Light
                                                       
  [Ortam & Global Işıklar] [Sondalar & Hacimler]       [Görsel Efekt Işıkları]
  ├── Sky Light (Ambient)  ├── Reflection Probe (Parall)├── Emissive Mesh Surfaces
  └── GI Irradiance Volume └── Volumetric Fog Froxels  └── Particle Light Injectors
```

### 3.1 Yönsel Işık (Directional Light - Güneş / Ay)
- **Karakteristik:** Sonsuz mesafeden gelen paralel ışık ışınları.
- **Gölge:** 4-Kademeli Kaskad Gölge Haritalaması (Cascaded Shadow Maps - CSM). Yakın mesafe için yüksek çözünürlük, ufuk için geniş kapsama.
- **Özellikler:** Kaskad bölünme mesafeleri, Derinlik eğilimi (Depth Bias), Normal kaydırma (Normal Offset), Yumuşak gölge süzmesi (PCF 3x3 veya PCSS), Işık hüzmesi (Sun Shafts / God Rays) şiddeti.

### 3.2 Nokta Işık (Point Light - Küresel Omni Işık)
- **Karakteristik:** Bir noktadan her yöne yayılan ışık kaynağı (ampul, meşale, ateş).
- **Fiziksel Sönümleme:** Standart ters-kare ($1/d^2$) yasası ve sınırlı etki yarıçapında sıfıra yumuşak iniş sağlayan pencereleme fonksiyonu:
  $$f_{\text{att}}(d) = \frac{\max\left(1 - \left(\frac{d}{R}\right)^4, 0\right)^2}{d^2 + 1}$$
- **Gölge:** Donanım Kübik Derinlik Haritası (Cube Depth Shadow Map) veya Çift Paraboloid (Dual-Paraboloid) gölge.
- **Kaynak Yarıçapı (Source Radius):** PBR yüzeylerde nokta gibi değil, küre gibi parlayarak gerçekçi yumuşak speküler vurgu üretir.

### 3.3 Spot Işık (Spot Light - Koni Işığı)
- **Karakteristik:** Belirli bir yöne doğru koni şeklinde yayılan ışık (el feneri, araba farı, sahne spotu).
- **Parametreler:**
  - `inner_cone_angle`: Tam aydınlanan iç çekirdek açısı (derece).
  - `outer_cone_angle`: Sönümün sıfıra indiği dış sınır açısı (derece).
  - `penumbra_falloff`: İç ve dış koni arasındaki geçiş eğrisi (doğrusal / pürüzsüz hermite).
- **IES Işık Profili:** Mimari aydınlatma armatürlerinin gerçek dünyadaki laboratuvar ölçümlerini içeren 1D/2D IES doku maskesi desteği.

### 3.4 Dikdörtgen / Alan Işık (Rect / Area Light)
- **Karakteristik:** Genişliği ve yüksekliği olan dikdörtgen bir yüzeyden yayılan ışık (TV ekranı, floresan panel, stüdyo softbox'ı, tavan penceresi).
- **Matematiksel Çözüm:** **LTC (Linearly Transformed Cosines)** algoritması.
  - GGX dağılımını çizgisel olarak dönüştürülmüş kosinüs formuna sokarak analitik olarak yüzey integrali alır.
  - PBR yüzeylerde dikdörtgenin şeklini yansıtan kusursuz, fiziksel olarak doğru speküler yansımalar ve yumuşak gölgeler üretir.
- **Parametreler:** `width`, `height`, `barn_door_angle`, `barn_door_length`.

### 3.5 Tüp / Kapsül Işık (Tube / Capsule Light)
- **Karakteristik:** İki nokta arasında uzanan silindirik ışık çizgisi (neon tüpler, uzun LED şeritler, lazer kılıçları).
- **Çözüm:** En yakın segment noktası yaklaşımı (Segment-to-Point PBR integral aproximation) ile GPU dostu gerçek zamanlı speküler parlama.
- **Parametreler:** `length`, `radius`.

### 3.6 Daire / Disk Işık (Disk Light)
- **Karakteristik:** Yuvarlak tavan spotları ve dairesel ışık panelleri.
- **Çözüm:** Disk LTC formülasyonu.
- **Parametreler:** `radius`.

### 3.7 Gök Işığı (Sky Light / Ambient Dome)
- **Karakteristik:** Tüm gökkubbenin sahneye difüz ve speküler yansıması.
- **Çözüm:**
  - Sahne açıldığında veya gökyüzü değiştiğinde gök kubbenin GPU üzerinde anlık yakalanması (Sky Capture).
  - Güneş gölgede kalan yerlerin tamamen zifiri karanlık olmasını engeller; gerçek dünyadaki gibi maviye çalan gökyüzü dolgu ışığı sağlar.

### 3.8 Yansıma Sondaları (Reflection Probes - Parallax-Corrected)
- **Karakteristik:** Belirli bir bölgenin (oda, koridor, bina) 360 derece yansımasını tutan kübik harita.
- **Parallaks Düzeltmesi:** Basit cubemap yansımaları sonsuz uzakta kabul eder ve iç mekanlarda duvarlar kayar. **Kutu (Box) ve Küre (Sphere) Parallaks Düzeltmesi** ile yansıyan ışın odanın duvar sınırlarıyla kesiştirilir; ayna gibi yüzeylerde odanın gerçek yansıması elde edilir.
- **Parametreler:** `box_extents`, `blend_distance`, `intensity`, `priority`.

### 3.9 İrradiance Işık Hacmi Sondaları (GI Irradiance Volume)
- **Karakteristik:** 3B ızgara (örneğin 16×16×8 probe) halinde sahneye yerleştirilen küresel harmonik düğümleri.
- **Fonksiyon:** Statik geometriye bake edilen ışık haritasını, hareket eden dinamik karakterlerin ve nesnelerin üzerine taşır. Karakter mağaraya girince kararır, kırmızı bir duvarın yanından geçerken üzerine kırmızı ışık sekmesi (color bleed) düşer.

### 3.10 Emissive (Işıma Yapan) Yüzeyler ve Mesh Işıkları
- PBR materyallerinde `emissive_color` ve `emissive_strength` taşıyan poligonlar.
- Bloom geçişinde parlar, GI bake işleminde sahneye ışık saçar.

### 3.11 Hacimsel Sis / Katılımcı Ortam Hacmi (Volumetric Fog Volume)
- Belirli bir koordinata yerleştirilen yerel sis kutusu veya küresi (örneğin kuyu içi, su üstü sisi, yangın dumanı).

### 3.12 Fotometrik Birimler ve Kelvin Renk Sıcaklığı Skalası
Profesyonel sanatçıların ve ışık tasarımcılarının rastgele "0.0 - 1.0" sayılarıyla değil, fiziksel birimlerle çalışabilmesi:
- **Işık Akısı Birimleri:**
  - Yönsel Işık (Güneş): **Lux** ($lm/m^2$). Doğrudan güneş: $\approx 100,000\text{ Lux}$, Dolunay: $\approx 0.1\text{ Lux}$, Ofis: $\approx 500\text{ Lux}$.
  - Nokta ve Spot Işık: **Lumen** ($lm$) veya **Candela** ($cd$). 60W Ampul: $\approx 800\text{ Lumen}$.
- **Kelvin Renk Sıcaklığı (Color Temperature):**
  Renk seçicinin yanına Planck kara cisim ışıması (Planckian Locus) dönüşümü eklenir:
  - $1800\text{ K}$: Mum alevi
  - $2700\text{ K}$: Sıcak akkor ampul
  - $4000\text{ K}$: Floresan beyazı
  - $5500\text{ K}$: Doğal öğle güneşi
  - $6500\text{ K}$: D65 standart gün ışığı / bulutlu hava
  - $10000\text{ K}$: Açık mavi gökyüzü gölgesi

### 3.13 Mobil Uyumlu Işık Hüzmeleri (God Rays / Sun Shafts) & Hacimsel Koni Işıkları
Masaüstü GPU'larda kullanılan tam çözünürlüklü 3D froxel raymarch, mobil TBDR mimarilerinde (Mali, Adreno) aşırı bellek bant genişliği tüketir ve termal kısmaya (thermal throttling) yol açar. Tulpar Engine, mobil için iki devrimsel hafif teknik kullanır:

#### A. Ekran Uzayı Işınsal Engelleme Süzmesi (Screen-Space Radial Occlusion Sun Shafts)
- **1. Adım (Çeyrek Çözünürlük Maske - 1/4 Res):** Sahne derinlik tamponu ($Z$-Buffer) ve güneş diskini çeyrek çözünürlüğe indirger. Güneşin önünü kapatan tüm katı nesneler (binalar, ağaçlar, dağlar) siyah maske üretir; açık kalan güneş pikselleri beyaz parlar.
- **2. Adım (Işınsal Yönlü Dağıtma - Radial Directional Blur):**
  Piksel koordinatından ekran üzerindeki güneş izdüşümüne ($\vec{S}_{uv}$) doğru tek geçişli ışın yürütme (raymarch):
  $$I(u, v) = \sum_{i=0}^{N-1} \text{Sample}\left((u,v) + i \cdot \text{step} \cdot (\vec{S}_{uv} - (u,v))\right) \cdot \text{decay}^i \cdot \text{weight}$$
  Mobil için $N = 8 - 12$ örnekleme + Blue Noise titretmesi (Dither) ile sıfır banting ve $< 0.25\text{ ms}$ GPU süresi!
- **3. Adım (Bileşik Katman - Additive Blend):** Oluşan ışık hüzmesi tamponu doğrudan ana HDR sahneye güneşin gerçek rengi ve şiddetiyle eklenir.

#### B. Analitik Hacimsel Işık Konileri (Spot & Point Lights)
- Spot ışıklar için hacimsel raymarch yerine hafif analitik poligon konisi çizilir.
- **Yumuşak Derinlik Sönümü (Soft Depth Fade):** Koni poligonu zeminle veya duvarla kesiştiğinde sert kenar çizmez; sahne derinliğiyle yumuşak şeffaflık integrali alır:
  $$\alpha = 1.0 - e^{-\frac{Z_{\text{sahne}} - Z_{\text{koni}}}{\text{fade\_distance}}}$$
- Böylece el feneri, araba farı veya sahne spotları mobil cihazlarda sıfır maliyetle sinematik tozlu ışık hüzmesi saçar.

---

## BÖLÜM 4: GPU Veri Yapıları, Vulkan Clustered Forward+ ve Bellek Mimarisi

### 4.1 Bellek Düzeni: std140 / std430 UBO & SSBO Paketleme
Tulpar Engine'in sıfır tahsisat ve determinizm ilkeleri gereği tüm ışık ve çevre verisi GPU'da tek bir ardışık tamponda (Buffer) toplanır.

```cpp
// ============================================================================
// content/scene_lights.hpp - Tulpar Birleşik Işık Türleri ve GPU Düzeni
// ============================================================================

enum class LightKind : uint32_t {
  Directional = 0, // Sonsuz uzak paralel ışık (Güneş/Ay)
  Point       = 1, // Küresel her yöne ışık
  Spot        = 2, // Koni odaklı ışık
  Rect        = 3, // Dikdörtgen alan ışığı (LTC)
  Capsule     = 4, // Tüp / Çizgi ışığı
  Disk        = 5  // Dairesel alan ışığı
};

// GPU std430 hizalamasında tam 64 bayt (Cache-line dostu)
struct alignas(16) GpuPackedLight {
  // float4: pos (xyz) + radius (w)
  float pos_x, pos_y, pos_z, radius;
  // float4: color_rgb (xyz) + intensity_lux_or_lumen (w)
  float color_r, color_g, color_b, intensity;
  // float4: direction (xyz) + kind (w: LightKind)
  float dir_x, dir_y, dir_z, kind;
  // float4: params (Spot: inner/outer cos; Rect/Tube: width, height; Disk: radius)
  float param0, param1, param2, flags;
};
static_assert(sizeof(GpuPackedLight) == 64, "GpuPackedLight std430: 64 bayt");

// Sahne Çevre ve Dünya UBO (Frame Global UBO)
struct alignas(16) GpuWorldEnvironmentUbo {
  Mat4  view_proj;
  Mat4  inv_view_proj;
  Mat4  sun_cascade_vp[4]; // 4 Kademe CSM
  
  // Güneş & Gökyüzü Parametreleri
  float sun_dir[4];        // xyz: yön, w: sun_disk_size
  float sun_color[4];      // rgb: renk, w: sun_lux
  float ambient_color[4];  // rgb: taban ortam rengi, w: ambient_intensity
  
  // Atmosferik Saçılma Katsayıları
  float rayleigh_scattering[4]; // rgb: beta_R, w: rayleigh_scale_height
  float mie_scattering[4];      // rgb: beta_M, w: mie_g (anizotropi)
  float ozone_absorption[4];    // rgb: beta_Ozone, w: turbidity
  
  // Üstel Sis (Exponential Fog)
  float fog_params[4];     // x: density, y: height_falloff, z: start_dist, w: max_opacity
  float fog_color[4];      // rgb: sis rengi, w: sun_scattering_intensity
  
  // Kümeleme (Clustered Lighting Grid)
  uint32_t cluster_grid_x; // 16
  uint32_t cluster_grid_y; // 8
  uint32_t cluster_grid_z; // 24
  uint32_t light_count;    // Aktif paketlenmiş ışık adedi
};
```

### 4.2 Clustered Forward+ Izgarası (Mobil TBDR Dostu)
Mobil GPU'larda (Mali G710, Adreno 740 vb.) bant genişliği en büyük darboğazdır. Clustered Forward+:
1. Görünüm piramidi (Frustum) $16 \times 8 \times 24$ (3072 küme) hücreye ayrılır.
2. Z ekseni logaritmik derinlik dilimlerine bölünür (yakındaki ışıklar için hassas, uzaktakiler için geniş).
3. Compute shader veya CPU SIMD ile ışıkların küre/kutu sınırları küme indeksleriyle kesiştirilir ve 64-bit maske (`uint64_t light_mask`) oluşturulur.
4. Piksel shader'ı yalnızca kendi hücresindeki ışıkları hesaplar; aşırı döngü (overdraw) ve gereksiz ışık testi sıfıra iner.

### 4.3 Sıfır Dinamik Tahsisat ve Determinizm Garantisi
- Sahne başına maksimum ışık kapasitesi: $N_{\text{max}} = 256$ (Sıfır `malloc`/`std::vector` kuralı).
- Tüm ışık dizisi frame başında sabit ring-buffer'a yazılır ve Vulkan `vkCmdBindDescriptorSets` ile GPU'ya gönderilir.

---

## BÖLÜM 5: Editör Kullanıcı Arayüzü ve Sağ Tık Menü Yeniden Yapılandırması

### 5.1 Sağ Tık Sahne Hiyerarşi Menüsünün Yeni Anatomisi
Mevcut acemice hazırlanmış menü (Skybox'ın bir varlık olarak sunulduğu yapı) kaldırılır ve endüstri standardı bir kategorizasyon getirilir:

```
[SAĞ TIK: SAHNE HİYERARŞİSİ]
│
├── ➕ Yeni Varlık Ekle...
│   ├── 🧊 3B İlkeller (Primitive Mesh)
│   │   ├── Küp (Box)
│   │   ├── Küre (Sphere)
│   │   ├── Kapsül (Capsule)
│   │   ├── Silindir (Cylinder)
│   │   ├── Koni (Cone)
│   │   ├── Düzlem (Plane)
│   │   └── Simit (Torus)
│   │
│   ├── 💡 Işıklandırma (Lights)
│   │   ├── ☀️ Yönsel Işık (Directional Light / Güneş)
│   │   ├── 💡 Nokta Işık (Point Light - Küresel)
│   │   ├── 🔦 Spot Işık (Spot Light - Koni)
│   │   ├── 🔲 Alan / Dikdörtgen Işık (Rect / Area Light - LTC)
│   │   ├── 📏 Kapsül / Tüp Işık (Tube Light)
│   │   └── 🔘 Disk Işık (Disk Light)
│   │
│   ├── 🌐 Çevre & Hacimler (Volumes & Probes)
│   │   ├── 🪞 Yansıma Sondası (Reflection Probe - Box/Sphere)
│   │   ├── 🔮 Işık Hacmi Sondası (Irradiance GI Probe)
│   │   ├── 🌫️ Hacimsel Sis Hacmi (Fog Density Volume)
│   │   ├── 🎛️ Post-Process Hacmi (Yerel Renk / Pozlama)
│   │   └── 🔊 Ses Yankı Alanı (Reverb Volume)
│   │
│   ├── ⛰️ Doğa & Dünya Aktörleri (Environment Actors)
│   │   ├── 🏔️ Yükseklik Haritası Arazisi (Heightmap Terrain)
│   │   ├── 🌊 Su / Okyanus Düzlemi (Gerstner Waves)
│   │   ├── 🧊 Voksel Izgarası (Voxel Grid)
│   │   └── 🍃 Rüzgar Alanı (Wind Emitter)
│   │
│   ├── ⚙️ Fizik (Physics)
│   │   ├── Sabit Çarpıştırıcı (Static Collider)
│   │   ├── Dinamik Rijit Gövde (Dynamic Rigidbody)
│   │   ├── Karakter Kontrolcüsü (Character Controller)
│   │   └── Fizik Eklemi (Constraint Joint)
│   │
│   ├── 🎮 Oynanış & Mantık (Gameplay)
│   │   ├── ⚔️ GAS Karakter (Health + Ability)
│   │   ├── 🎒 Envanter Sandığı (Inventory Container)
│   │   └── 🤖 NavMesh Ajanı (AI NavAgent)
│   │
│   ├── 🔊 Ses & Kamera
│   │   ├── 📢 3B Uzamsal Ses Kaynağı
│   │   └── 🎥 Kamera Aktörü
│   │
│   └── 📄 Boş Varlık (Empty Node)
│
├── ✂ Pano & Düzenleme (Kes, Kopyala, Yapıştır, Çoğalt, Sil)
├── 🔍 Gezinme (Seçime Odaklan 'F', Tümünü Seç, Katlamaları Aç/Kapat)
└── 🛠 SROS Hızlı Onarım (Ölçekleri Düzelt, Bozuk Işıkları Onar, Sahneleri Optimize Et)
```

> **ÖNEMLİ NOT:** Gökyüzü (Skybox) bu menüden tamamen ÇIKARILMIŞTIR! Çünkü Gökyüzü bir nesne değil, sahnenin tavanıdır. Gökyüzü ayarları **Dünya (World Settings)** panelinden veya sahneye eklenebilecek `Post-Process / Environment Volume` üzerinden yönetilir.

### 5.2 Inspector Panelinde Işık Arayüzleri
Bir ışık nesnesi seçildiğinde Inspector panelinde zengin, endüstri standardı kontroller açılır:
1. **Işık Türü Seçici (Combo):** `Directional`, `Point`, `Spot`, `Rect`, `Tube`, `Disk`.
2. **Renk & Sıcaklık:**
   - RGB Renk Paleti + Kelvin Sıcaklık Kaydırıcısı ($1800\text{ K} - 10000\text{ K}$).
   - Renk önizleme şeridi.
3. **Şiddet & Birim:**
   - Lux / Lumen / Candela veya Normalize güç seçeneği.
4. **Mesafe & Sönümleme:**
   - Etki Yarıçapı ($m$).
   - Ters-Kare sönümleme eğrisi önizlemesi.
5. **Koni Kontrolleri (Spot Işık için):**
   - İç Açı ($^\circ$) ve Dış Açı ($^\circ$).
   - Penumbra yumuşaklık kaydırıcısı.
6. **Yüzey Boyutları (Alan & Kapsül Işıklar için):**
   - Genişlik ($m$), Yükseklik ($m$), Uzunluk ($m$).
7. **Gölge Kontrolleri:**
   - Gölge Dök (Cast Shadows) Açık/Kapalı.
   - Gölge Çözünürlüğü (512, 1024, 2048, 4096).
   - Derinlik Eğilimi (Depth Bias) ve Normal Offset kaydırıcıları.
   - PCF Filtre Genişliği.

### 5.3 Dünya (World Settings) Panelinin Evrimi
Editörün `Dünya` sekmesi üç ana başlık altında toplanır:
1. **☀️ Atmosfer ve Gökyüzü (Sky & Atmosphere):**
   - Gökyüzü Modu: `Fiziksel Atmosfer (Nishita)` / `HDRI Cubemap` / `Düz Renk`.
   - Güneş Açısı, Yüksekliği ve Gün Saati (0:00 - 24:00 zaman kaydırıcısı).
   - Atmosfer Bulanıklığı (Turbidity), Ozon Etkisi, Rayleigh Saçılım Skalası.
   - HDRI Doku Dosya Seçicisi (.hdr/.ktx2) ve Pozlama ayarı.
2. **🌫️ Küresel Sis (Global Fog):**
   - Sis Açık/Kapalı.
   - Sis Taban Yoğunluğu, Yükseklik Düşüşü (Height Falloff).
   - Sis Albedo Rengi ve Güneş Saçılım Katkısı.
   - Hacimsel Sis (Volumetric Froxels) Açık/Kapalı.
3. **💡 Küresel Aydınlatma & Gölge (GI & Shadows):**
   - Kaskad Gölge Mesafeleri (Split Distances).
   - Ortam Aydınlatma Şiddeti (Ambient Sky Multiplier).
   - Işık Haritası (GI) Pişirme ve Önizleme Butonları.

---

## BÖLÜM 6: SROS (Scene Repair & Optimization System) Denetim Kuralları

SROS, aydınlatma ve çevre için şu kuralları otomatik olarak denetler ve tek tıkla onarır:

| Kural Kodu | Denetim Tanımı | Tespit Durumu | SROS Otomatik Onarımı |
|---|---|---|---|
| `SROS-LIGHT-01` | **Sıfır veya Negatif Işık Yarıçapı** | Bir ışığın yarıçapı $\le 0.01\text{ m}$ ise | Varsayılan fiziksel yarıçapa getirir ($5.0\text{ m}$). |
| `SROS-LIGHT-02` | **Aşırı Parlak / Patlayan Işık** | Işık şiddeti güvenli HDR sınırını aşıyorsa ($> 100,000\text{ Lux}$ / $10,000\text{ cd}$) | Şiddeti güvenli fiziksel aralığa kelepçeler (clamp). |
| `SROS-LIGHT-03` | **Çakışan Koni Açıları (Spot)** | Spot ışığın iç açısı dış açısından büyük veya eşitse | İç açıyı dış açının \%80'ine çeker ($penumbra \ge 5^\circ$). |
| `SROS-LIGHT-04` | **Mobil Gölge Bütçesi Aşımı** | Bir sahnede 4'ten fazla gölge döken dinamik ışık varsa | Yalnızca ana güneşi ve en yakın 2 ışığı gölgeli bırakır, diğerlerinin gölgesini kapatır. |
| `SROS-LIGHT-05` | **Normalizasyonsuz Güneş Vektörü** | `sun_dir` vektörünün boyu $\neq 1.0$ ise | Vektörü otomatik normalize eder ($\vec{L} / \|\vec{L}\|$). |
| `SROS-LIGHT-06` | **Ucube Skybox Bileşeni Tespiti** | Varlıklar üzerinde `kSceneSkybox` bileşeni varsa | Bileşeni varlıktan kaldırır ve sahnenin `SceneWorld` gökyüzü modunu aktif eder. |
| `SROS-LIGHT-07` | **Yalıtılmış İç Mekan Sızıntısı** | Kapalı iç mekanda yansıma sondası yoksa | Mekanın merkezine kutu parallaks düzeltmeli `kSceneRefProbe` önerir. |
| `SROS-LIGHT-08` | **Sıfır Alanlı Işık (Rect/Tube)** | Alan veya tüp ışığın genişlik/yükseklik/uzunluğu 0 ise | Varsayılan $1.0\text{ m} \times 0.5\text{ m}$ boyutlarına getirir. |

---

## BÖLÜM 7: Fazlandırılmış Uygulama Yol Haritası

### 🚀 FAZ 1: Sahne Hiyerarşisi ve Menü Temizliği (Hemen)
1. `SahnePanelMenu` içinden ucube `Skybox Gökyüzü` bileşen oluşturucusunu kaldır.
2. Sağ tık menüsüne zengin ışık ailesini ekle:
   - ☀️ Yönsel Işık (Directional)
   - 💡 Nokta Işık (Point)
   - 🔦 Spot Işık (Spot)
   - 🔲 Alan / Dikdörtgen Işık (Rect)
   - 📏 Tüp / Kapsül Işık (Tube)
   - 🔘 Disk Işık (Disk)
3. Sağ tık menüsüne Çevre Hacimlerini ekle (Yansıma Sondası, Sis Hacmi, Yankı Alanı).
4. `SceneEntity` içindeki `SceneLightType` enum'ını `Point`, `Directional`, `Spot`, `Rect`, `Capsule`, `Disk` olarak genişlet.

### 🚀 FAZ 2: Inspector ve Parametre Kontrolleri
1. Inspector'da her ışık türü için özel parametre arayüzü (İç/Dış Koni, Genişlik/Yükseklik/Uzunluk, Kelvin Renk Sıcaklığı şeridi).
2. `Dünya` panelinde Atmosfer & Gökyüzü (Nishita / HDRI) ve Sis (Exponential Fog) parametre kontrollerini inşa et.

### 🚀 FAZ 3: Vulkan Render ve Clustered Forward+ Entegrasyonu
1. `GpuPackedLight` (64 bayt) veri yapısını `renderer.hpp` ve shader'lara geçir.
2. Clustered Forward+ Compute Shader'ını ve Spot/LTC Area Light shader hesaplamalarını aktif et.

### 🚀 FAZ 4: SROS Otomatik Onarım Entegrasyonu
1. `SROS-LIGHT-01` .. `SROS-LIGHT-08` kurallarını `content/sros.hpp` analiz ve onarım pipeline'ına ekle.
2. Tek tıkla "Işıkları ve Çevreyi Onar" işlemini editoryel geri-al (undo) günlüğüne bağla.

## BÖLÜM 8: Devrimsel Mobil Uyumlu Partikül ve VFX Mimarisi (Particle Systems)

Mevcut motordaki `content/particles.hpp` yalnızca basit bir CPU veri dizisidir ve `renderer/` içinde herhangi bir GPU çizim karşılığı bulunmamaktadır. Modern AAA ve mobil oyunların ihtiyaç duyduğu efektler (ateş, duman, patlama, büyü, kıvılcım, toz, yağmur) için Tulpar Hibrit Partikül Mimarisi inşa edilir.

### 8.1 İki Katmanlı Hibrit Simülasyon Modeli
1. **Katman 1: Deterministik Oynanış Partikülleri (CPU SIMD / Arena)**
   - Tulpar'ın `core/math/random.hpp` (xorshift32) deterministik RNG'si ile çalışır.
   - Oynanışla doğrudan etkileşen efektler: Büyü mermileri (fireball), patlama şok dalgaları, karakter can/hasar efektleri.
   - Geri alma (Undo/Redo), Replay ve Rollback Netcode sistemleriyle bit-bit tam uyumludur.
   - O(1) swap-with-last ile bellek sıkı tutulur, sıfır heap tahsisatı yapılır.
2. **Katman 2: Yüksek Adetli GPU Çevre VFX (GPU Instanced Billboards & Compute)**
   - Mobil TBDR dostu instanced billboard çizimi.
   - 10.000+ kıvılcım, ortam tozu (dust motes), ateş alevleri, kar ve yağmur parçacıkları.
   - Vertex buffer oluşturmaya gerek kalmadan `gl_VertexIndex` (0..3) ile quad geometrisi shader içinde anlık üretilir (Sıfır vertex bellek tüketimi!).

### 8.2 Mobil TBDR Dostu Yumuşak Partikül (Soft Particles)
Geleneksel parçacıklar zeminle veya duvarla kesiştiğinde çirkin düz bir kesik çizgisi (hard edge artifact) oluşturur. Tulpar Soft Particle tekniği ile:
$$\alpha_{\text{soft}} = \text{clamp}\left(\frac{Z_{\text{sahne}} - Z_{\text{partik\xC3\xBCl}}}{\text{softness\_distance}}, 0.0, 1.0\right)$$
Derinlik tamponu ($Z$) okunarak parçacığın katı yüzeylere temas ettiği yerler pürüzsüzce şeffaflaşır.

### 8.3 Billboard Türleri ve Yönlenme Modları
- **Ekran Odaklı (Screen-Aligned Billboards):** Kameranın bakış açısını tam karşılayan küresel duman, ateş ve patlama parçacıkları.
- **Hız Odaklı Gerilmiş (Velocity-Stretched Billboards):** Parçacığın hız vektörü yönünde uzayan kıvılcımlar, lazerler, yağmur damlaları ve kan sıçramaları.
- **Zemin / Eksen Odaklı (Horizontal / Axis-Aligned Quads):** Zemin şok dalgaları, toz halkaları, büyü daireleri.

### 8.4 Ömür Boyu Renk & Boyut Eğrileri (Color & Size Over Life)
- `size_start` -> `size_end` (doğrusal veya üssel büyüme/küçülme).
- `color_start` -> `color_end` (RGBA renk ve alfa geçişi: alev sarıdan kızıla döner, duman şeffaflaşarak kaybolur).

### 8.5 Editör Sağ Tık Menüsünde Hazır VFX Önayarları
- 🔥 **Ateş & Köz (Fire & Embers):** Yukarı doğru ivmelenen alev küreleri ve etrafa saçılan köz parçacıkları.
- 💨 **Duman & Toz (Smoke & Dust):** Genişleyen, yavaşlayan ve şeffaflaşan hacimsel duman.
- ⚡ **Kıvılcım & Çarpışma (Sparks & Impact):** Yerçekimiyle yere çarpan gerilmiş parlak kıvılcımlar.
- 🌧️ **Yağmur & Kar (Precipitation):** Kamera çevresinde dönen hafif rüzgarlı yağış sistemi.
- 💥 **Patlama Şok Dalgası (Shockwave):** Hızla genişleyen zemin halkası ve toz bulutu.

---

**Özet Karar:** Skybox bir varlık değil, evrenin kendisidir. Işıklar ise motorun ruhudur. Bu mimari belge ile Tulpar Engine, amatörce bileşen ekleme yaklaşımından çıkarılıp Unreal Engine 5 ve Frostbite kalitesinde profesyonel bir aydınlatma, atmosfer ve görsel efekt (VFX) ekosistemine kavuşturulmaktadır.

