# Tuzaklar — Tulpar Engine

Bu dosya motorun C++ çekirdeğinde tekrar tekrar düşülen hata sınıflarını indeksler.
**Bir şey kırıldığında ilk bakılacak yer burasıdır.**

> **Depo notu.** Bu bölüm TulparLang tek-depo döneminde
> `docs/mindmap/Tuzaklar.md` dosyasının 8. bölümüydü. Motor ayrı depoya taşınırken
> (2026-09-20) buraya alındı; dildeki tuzaklar (1–7. bölümler) TulparLang deposunda
> kaldı. Numaralandırma (8a, 8b, …) tarihsel atıflar kırılmasın diye korundu —
> `CLAUDE.md` ve kaynak yorumları "Tuzaklar 8q", "Tuzaklar 8ap" diye atıf yapıyor.

## 8. Motor çekirdeği — C++ tuzakları

### 8a. Park etmiş fiber'lar havuzu tüketince sistem KİLİTLENİR
8 fiber, 32 ebeveyn iş; her ebeveyn 32 çocuk üretip bekliyor. İlk 8 ebeveyn fiber'ları
alıp park etti; çocuklara fiber kalmadı; çocuklar koşmadan ebeveynler uyanamıyor. Sonuç:
16 thread %1548 CPU, 33 dakika, çıktı yok (stdout tam tamponluydu; hangi testin
asılı kaldığı da görünmüyordu — `setvbuf(_IOLBF)` + her testten önce `RUN` satırı).
**Kural:** fiber yoksa iş worker'ın kendi yığınında **satır içi** koşar ve onun `wait()`i
park edemediği için kuyruğa **yardım eder** (iç içe). Havuz boyutu yine init'te (A2),
açlık **sayılır** (`fiber_starved`, `jobs_inline`). Test: `jobs_fiber_pool_starvation_recovers`.

### 8b. `new`/`delete` çifti ELENİR — ayırma kapısı yanlış geçer
C++14 allocation elision: GCC/Clang -O2+ gözlemlenmeyen `new`/`delete` çiftini siler.
`int *p = new int(1); delete p;` sayaçta görünmedi ve **A2 kapısının pozitif kontrolü**
"ayırma yakalandı" yerine "0" dedi — yani kapı çalışıyor sanılırken hiçbir şey ölçmüyordu
([[#1k. Nöbetçi KEŞFETTİĞİNİ sayıyor, ÖLÇTÜĞÜNÜ değil — 50 dedi, 49 ölçtü]] ailesi).
**Kural:** ayırma sayan testte işaretçi **kaçmalı**: `test::escape(p)` (`asm volatile`
engeli). Enjeksiyonun kendisi de bu engeli kullanır.

### 8c. GCC sabit null dereference'ı SİLER — çökme kobayı çökmez
`volatile int *p = nullptr; *p = 42;` -O3'te çökmeden döndü (çıkış kodu 4). Derleyici sabit
null dereference'ı UB sayıp yazmayı kaldırdı; `volatile` pointee'yi korumadı. Crash reporter
testi "rapor yok" diye düştü ve ilk teşhis işleyiciye gitti — suçlu kobaydı.
**Kural:** çökme kobayı adresi derleyicinin **göremediği** yerden okur (`volatile` global).
Yan bulgu: fiber içindeki çökme testinde `js.shutdown()` çağrılmayınca çıkışta worker'lar
serbest bırakılmış arenaya dokunup **başka** bir SIGSEGV üretti; rapor "çıkışta `_dl_fini`"
gösterdi — testin kendi kurulumu ikinci bir çökme üretmişti ([[#7a. Kendi testin yanlışsa, güvenle yanlış sonuç yayınlarsın]]).

### 8d. İkinci mimari, birinci mimarinin göremediğini bulur (libm ulp, FMA)
`math_quat` testi 180°'lik quaternion'a `slerp` uyguluyordu. 180°'nin **iki eşit-kısa yolu**
var; hangisinin seçileceğine `dot(a,b)`'nin işareti karar veriyor ve `dot = c² − s²`,
`sin(π/4)` ile `cos(π/4)`'ün float'ta **son ulp'ta** eşit olup olmamasına bağlı. x86_64
glibc: eşit, dot = 0, yol A, yerelde 10/10 yeşil. macOS arm64 (Apple libm + FMA
birleştirme): dot < 0, yol B, 90° ters, CI kırmızı. Hata `slerp`'te değil, **belirsiz
girdiyi sınayan testte**; 90°/45°'ye alındı.

**Kural:** matematik testinde girdi tekil/belirsiz noktada olmasın (antipodal quaternion,
dejenere üçgen, tam 0 determinant); sonuç tanımlı ama seçim platformun libm'ine kalıyorsa
test "bazen düşen" sınıfına girer ([[#1p. "Bazen düşen" kapı gürültü değil MAKİNE SINIFI olabilir — [makine] satırıyla eşle]]).
Ve simülasyon determinizmi için aynı ders (PLAN.md REV 8): aynı mimari + aynı libm + aynı
FP bayrakları dışında bit eşitliği yok. macOS arm64 CI'ı bu yüzden **ikinci mimari** olarak
değerli: fiber geçişini de, libm farkını da o buldu.

### 8e. Kapsama beklentisi kağıt üstünde hesaplanırken NDC genişliği 1 sanıldı
İlk piksel testi "üçgen ekranın %36'sı" bekliyordu; ölçüm %17.6 dedi. NDC −1..1 arası 2
birim: taban 1.2 → %60, yükseklik 1.2 → %60, alan ½·0.6·0.6 = **%18**. Beklenti yanlıştı,
renderer doğruydu. **Kural:** ölçüm iddiaya uymadığında önce iddiayı hesapla; sayı test
koduna yorumla birlikte girer ki bir sonraki okuyan aynı hatayı yapmasın.

### 8f. "Kare içinde 0 ayırma" kapısı SÜRÜCÜYÜ de sayar
Global `operator new` sayacı süreçteki herkesi sayar: NVIDIA 0/kare, **MoltenVK 28/kare**
(Metal nesneleri), lavapipe kurulumda (LLVM JIT). İlk yazım tek adımlı çizime `== 0` dedi;
lavapipe'ta düştü, MoltenVK'da düştü, NVIDIA'da geçti — yani yerelde yeşil, iki CI
sürücüsünde kırmızı ([[#1p. "Bazen düşen" kapı gürültü değil MAKİNE SINIFI olabilir — [makine] satırıyla eşle]]
ailesi, bu kez sürücü sınıfı). **Kural:** iddiayı ikiye ayır. (a) *bizim kod* kare içinde
ayırmaz → sürücüsüz harness'ta 0 (Faz 0 kapısı, kesin). (b) *sürücünün* kare ayırması cihaz
verisidir → ölçülür, basılır, **iddia edilmez** — ilk yazım "kararlı" (büyüme yok) diyordu, lavapipe
`[425 113 113 112 114]` verdi ve 114 > 113 ile düştü: LLVM JIT arka plan thread'leri bir-iki ayırma
oynatıyor. Sızıntı testi sürücüsüz yolda yapılır. Kurulum (pipeline, image, JIT) kareden ayrılır; kurulum
ayırması serbest ve bilgi.

### 8g. GCC'nin geçirdiği şablon başlığını Clang reddeder — push'tan önce clang sözdizimi
`ecs.hpp`'de `World::each` şablonu, sınıftan **sonra** tanımlanan `Archetype`'ı kullanıyordu.
GCC bağımlı olmayan adları örnekleme anına erteledi ve geçti (yerel 41/41); macOS CI (clang)
tanım anında çözdü: `subscript of pointer to incomplete type 'Archetype'`. Bir CI turu gitti.
**Kural:** yapılar şablonu kullanan sınıftan önce tanımlanır; push'tan önce
`tools/clang_syntax_check.sh` (clang++ varsa tüm engine kaynaklarını `-fsyntax-only`
ile geçirir, third_party hariç). İkinci mimari/ikinci derleyici CI'ı burada da işini yaptı
([[#8d. İkinci mimari, birinci mimarinin göremediğini bulur (libm ulp, FMA)]]).

### 8h. Üçüncü parti job'lar fiber yığınını taşırır — bekçi sayfa yakaladı
Jolt'un çarpışma job'ları (`PhysicsSystem::ProcessBodyPair`) büyük yerel yapılarla çalışır;
64 KB fiber yığınında **bekçi sayfaya** çarptı ve SIGSEGV verdi (gdb: 4 thread aynı fonksiyonda).
Ölçüm: 64 KB çöker, 128 KB geçer; varsayılan 256 KB. Bekçi sayfa olmasaydı komşu fiber'ın
yığını sessizce ezilir, "bazen" bozulan fizik olurdu. **Kural:** üçüncü parti kodu fiber'da
koşturmadan önce yığın ihtiyacını **ölç** (env ile boyut tara), bekçi sayfayı asla kaldırma;
ağır işler için ayrı "büyük yığın" havuzu adayı. Sızma değil, sınır: Tuzaklar 8a'nın kardeşi.

### 8i. Auto-merge ilk yeşilde birleştirir — sonraki push KAPALI PR'a gider ve kaybolur
#319 için bir düzeltme daha push'landı (MoltenVK doğrudan yükleme + CI ICD yolları). Bir önceki
commit'in koşumu o sırada yeşile dönmüştü; auto-merge PR'ı **hemen** birleştirdi. Sonraki push
kapalı PR'ın dalına indi, hiçbir yere girmedi, hiçbir uyarı yoktu. Fark edilmesi: sonraki PR'da
macOS "Vulkan cihazı yok" dedi; main'de `moltenvk_direct` yoktu, dalda vardı.
**Kural:** PR yeşile döndükten sonra o dala push yapma; yeni değişiklik = yeni dal, yeni PR.
Birleşme sonrası `git diff origin/main origin/<dal> --stat` **boş** olmalı (#318'de yapıldı, #319'da
atlandı). Squash merge'te `git log main..dal` her zaman dolu görünür, kanıt **dosya farkı**dır.

### 8j. "Cross-platform deterministic" define'ı tek başına yetmez — FMA birleştirmesi derleyicinin
Jolt `JPH_CROSS_PLATFORM_DETERMINISTIC` ile x86 ↔ ARM bit eşitliği vaat eder; bizim vendored derlemede
arm64 CI farklı özet verdi (`5dc4…` vs `4087…`). İki sebep, ikisi de bizim: (1) sahne kurulumunda
`axis_angle` → `sinf/cosf` → libm (8d'nin aynısı, fizik sahnesinde); (2) AArch64'te GCC/Clang
`a*b+c`'yi varsayılan olarak FMA'ya **birleştirir**, x86_64'te `-mfma` olmadığı için birleştirmez —
aynı kaynak, farklı yuvarlama. Jolt'un resmi `Build/CMakeLists.txt`'i `-ffp-contract=off` koyuyor;
upstream cmake'i atıp glob ile derleyince bayrak da gitti. **Kural:** üçüncü parti kütüphaneyi kendi
CMake'inle derliyorsan upstream'in bayraklarını **oku ve taşı** (özellikle FP); belirlenimlilik
iddiası ikinci mimaride altın özetle sınanmadan kabul edilmez; giriş verisi libm'den geçmemeli.

### 8k. Vulkan'da y ters çevrilmiş projeksiyon + GL alışkanlığı `CLOCKWISE` = zemin kaybolur
`Mat4::perspective` Vulkan NDC için y'yi ters çevirir. GL tarzı (çevirmeyen) projeksiyonda
dünya-CCW üçgen framebuffer'da CW görünür ve `VK_FRONT_FACE_CLOCKWISE` doğrudur; y'yi ters
çevirince sarım **geri** CCW olur. "Y'yi çevirdim, sarım da dönmüştür" diye CW koyunca tek yüzlü
zemin kayboldu, küpler iç yüzleriyle karanlık çizildi (normal ışığa ters → yalnız ambient) —
sahne "çalışıyor ama karanlık" göründü. **Sarım testi:** headless karede tek yüzlü zemin var mı.
Kural: ters çevrilmiş projeksiyon + `COUNTER_CLOCKWISE`; ikisini aynı yerde belgele.

### 8l. Swapchain kare yuvası ile renderer kare yuvası ayrı sayılırsa GPU'nun okuduğu UBO'ya yazılır
Renderer `begin_frame(frame_i % 2)` ile UBO yuvasını, swapchain kendi `frame_` sayacıyla fence
yuvasını seçiyordu. Acquire başarısız olunca (OUT_OF_DATE, yeniden boyutlandırma) swapchain
sayacı durur, uygulama sayacı ilerler → iki yuva ayrışır, fence beklenen yuva ile yazılan yuva
farklı olur. **Kural:** kare yuvası tek kaynaktan gelir (`FrameContext::frame_index`), renderer
onu alır. Ayrıca fence'i **gönderimden hemen önce** sıfırla: acquire'da sıfırlayıp gönderemeyen
kod bir sonraki `vkWaitForFences`'i sonsuza kadar takar.

### 8m. SUBOPTIMAL'i "yeniden yarat" saymak Android'de kareyi 3 katina cikarir
Telefonda demo 20 fps kosuyordu; alt zamanlayicilar toplami 6 ms iken kare 51 ms'ti — yani sure
**olculmeyen** yerdeydi. Sebep: `vkQueuePresentKHR` her kare `VK_SUBOPTIMAL_KHR` donuyordu ve kod
onu `needs_recreate` sayip **her karede swapchain'i yeniden kuruyordu** (~45 ms). Android'de
SUBOPTIMAL kalicidir: swapchain'in `preTransform`'u yuzeyin `currentTransform`'undan farkliysa
(biz IDENTITY istiyorduk, panel dikey oldugu icin yuzey ROTATE_90 istiyordu) her kare boyle doner ve
**yeniden yaratmak bunu duzeltmez**. Kurallar: (1) yalniz `OUT_OF_DATE` yeniden yaratma sebebidir,
SUBOPTIMAL **sayilir ve raporlanir**; (2) dogru cozum **on-dondurme**: `preTransform = currentTransform`,
90/270'te goruntu olcusu devrik, projeksiyon clip uzayinda dondurulur (kompozitorun tam ekran
dondurme gecisi de kalkar); (3) genel ders: **alt zamanlarin toplami ust zamani tutmuyorsa olculmeyen
bir is vardir** — once o bosluga zamanlayici koy, tahmin etme. Olculdu: 20 fps -> 59.9 fps.

### 8n. `adb shell`den kosan ikili GPU'yu GORMEZ — olcum APK surecinde yapilir
Telefona `adb push` edilen `engine_tests`/`engine_demo` calisti ama `vkEnumeratePhysicalDevices`
**0 cihaz** dondurdu (`vkCreateInstance` basariliydi, loader 1.1, `VK_KHR_android_surface` vardi).
`/dev/mali0` shell kullanicisina rw gorunuyor; engel SELinux/HAL tarafinda, uygulama surec baglami
gerekiyor. Yani "telefonda kostu, GPU yok" sonucu **cihaz hakkinda degil, kosum baglami hakkindadir**.
Cozum: NativeActivity host (`app/android_main.cpp`, `libtulparengine.so`) — testler de demo da
APK **surecinde** kosar (`debug.tulpar.mode` ozelligi secer), stdout boruyla logcat'e ve
`files/engine_log.txt`'ye gider. Ek tuzaklar: Huawei'de `run-as` calismiyor ("/data has wrong owner")
→ cikti **harici** dizine (`/sdcard/Android/data/<pkg>/files`) yazilir ve `adb pull` ile alinir;
logcat halkasi dakikalar icinde tasar, **dosya asil kaynak, logcat yedek**.

### 8o. Plan "zorunlu" dediyse bile gercek cihaz vermeyebilir — kapiyi rapora cevir
Plan L2 "zorunlu feature" listesi (`descriptorIndexing`, `timelineSemaphore`, `bufferDeviceAddress`)
ilk gercek cihazda (Mali-G72, Vulkan **1.1**, 2018 surucusu) **ucu de yoktu** ve `Device::init`
cihazi reddediyordu: motor telefonda hic acilmiyordu. Bunlar Vulkan 1.2 cekirdegi; 1.1 cihazda
uzanti bicimleri de yok. Duzeltme: `require_mandatory` varsayilan **false**, eksikler
`DeviceCaps::missing_mandatory` ile **raporlanir** ve test `[bilgi]` satiri basar (kapi degil, cihaz
verisi). Ders: "baseline sartimiz" cumlesi de bir hipotezdir; ilk cihaz onu curutebilir. Kapiyi
silme — rapora cevir ve eksik yol yedegini yaz.

### 8p. Bump ayirici + pencere omurlu kaynak = her yeniden boyutlandirmada sizinti
`Device::allocate` blok ayiricidir (64 MB blok, bump, **geri vermez**) — sahne omurlu kaynaklar icin
dogru, ucuz ve belirlenimli. Ama swapchain derinlik goruntusunun omru **pencereye** baglidir: her
yeniden boyutlandirma/dondurme yeni bir derinlik ayirir ve eskisi blokta gomulu kalir. 2159x1080 D32
= ~9 MB; birkac dondurme bir bloku, birkac blok yuz MB'lari yer. Kural: **omru farkli olan kaynak,
ayirma stratejisi de farkli olmali** — pencereye bagli olanlar `allocate_dedicated`/`free_dedicated`
ile. Testin pozitif kontrolu sart: ayni donguyu blok ayiriciyla kosup **buyudugunu** gosteremiyorsan
test bir sey olcmuyor olabilir.

### 8q. Boru hattinin `depthBias` birimi SURUCUYE baglidir — Mali'de golgeyi tamamen sildi
Golge haritasi akne'sine karsi standart recete `VkPipelineRasterizationStateCreateInfo::depthBias`
(constant 1.25 / slope 2.0). NVIDIA'da dogru gorundu; **Mali-G72'de golge hic cikmadi**. Sebep:
`depthBiasConstantFactor` "en kucuk cozulebilir derinlik farki r" cinsindendir ve **r
implementation-defined**'dir (D16 gibi sabit noktali formatlarda surucuye gore degisir). Mali'de r
buyuk cikinca tum golge yuzeyi isik tarafina itildi (asiri peter-panning) ve sahne golgesiz kaldi —
hicbir hata, hicbir uyari, yalniz "golge yok". **Kural:** egilim cihazdan bagimsiz birimde olsun —
golge aramasini DUNYA uzayinda normal boyunca kaydir (`shadow_params.w` metre) + derinlik uzayinda
kucuk sabit. `depthBias` kullanma. Genel ders: bir gorsel ozelligin "calistigi" yalniz gelistiricinin
GPU'sunda dogrulanmissa **dogrulanmamistir**; ikinci saticinin GPU'su sart.
**Kapi:** `renderer_shadow_map_actually_darkens` — golge ACIK/KAPALI iki kareyi karsilastirir ve
koyulasan piksel sayar; kendi **negatif kontrolu** var (6 m kaydirma → golge kacar, koyulasan 0),
yani ariza moduna duyarli oldugu gosteriliyor. Masaustu ve telefon ayni sayiyi verdi (2433 piksel).

### 8r. 2B arayuz framebuffer uzayinda cizilirse on-dondurmede 90 derece yatar; atlas tasarsa "font yok"
Iki cihaz-ozel tuzak, ikisi de yalniz telefonda gorundu. (1) HUD framebuffer piksel uzayinda
ciziliyordu; Android on-dondurmede framebuffer dikeydir (1080x2159), gorunen ekran yatay — metin
ekranin sol kenarinda 90 derece yatik cikti. Kural: UI **mantiksal** (gorunen) uzayda cizilir ve 3B
projeksiyonla **ayni aci** kadar dondurulur (`ui.vert` push sabiti); dokunmatik koordinatlar da o
uzaydadir. (2) Font atlasi 28 px x 2x oversample x 213 glif 512x512'ye sigmadi; `stbtt_PackFontRanges`
0 dondurdu, `Font::load` false dondu ve demo "font yok" dedi — masaustunde 14 px'te siginca gorulmedi.
Kural: sigmazsa atlasi buyut (2048'e kadar) ve sebebi bas; "yukleme basarisiz"i dosya yoklugu sanma.

### 8s. Dogrulama katmani "ETKIN" ama mesaj kanali yok = sahte yesil; Mali linter iki gercek ihlal buldu
Telefonda (Huawei P20 Pro, Android 10) Khronos dogrulama katmani APK'nin lib dizininden yuklendi,
`caps.validation_layer=true` yazdi, "0 hata" dedi — ama hicbir mesaj gelmiyordu: `VK_EXT_debug_utils`'i
ICD/yukleyici `vkEnumerateInstanceExtensionProperties(nullptr)` listesinde vermiyor, uzantiyi **katman**
saglar ve o liste yalniz katman adiyla sorgulaninca gorunur. Messenger yaratilmadi, "0 hata" olcum degildi.
Yakalayan sey pozitif kontroldu: LOD kirpan sampler Arm uyarisi vermeliydi, 0 -> 0 kaldi. Kural: (1) katman
varsa uzantiyi katmanin kendi listesinden de ara; (2) `caps.debug_messenger` yoksa dogrulama testleri
GECMEZ (0 hata iddiasi yok); (3) her "0 uyari" kapisinin yaninda uyariyi kasten tetikleyen kontrol olsun.
Ayrica linterin ilk kosumu iki gercek Mali ihlali buldu: her iki sampler `maxLod`'u kirpiyordu
(`BestPractices-Arm-vkCreateSampler-lod-clamping`; kural `maxLod = VK_LOD_CLAMP_NONE`, mip araligini
image view sinirlar). PerfDoc arsivlenmis; ardili bu katmanin `validate_best_practices_arm` ayari
(`rhi/device.cpp`, `VK_EXT_layer_settings` pNext ile). Masaustunde katman `~/.local/share/vulkan/explicit_layer.d`
altinda (LunarG SDK'dan yalniz katman); telefona `tools/fetch_vvl_android.sh` + `android_run.sh tests`.
Ayni turda iki ek bulgu: (a) `compositeAlpha=OPAQUE` Huawei yuzeyinde desteklenmiyor (yalniz INHERIT) — yillarca
tanimsiz davranisla calisirdi; yuzeyin `supportedCompositeAlpha`'sindan sec. (b) Katmanin
`sparse-index-buffer` taramasi alt-ayirmali tamponun **blok basini** okur (offset yok, VVL issue 45): "%0.00"
sahte pozitif. Kural: katmanin dogru olcemedigi seyi kendin olc (`Renderer::sparse_mesh_count`) ve dususu
kimlik adiyla, gerekcesiyle yap; "Arm uyarilarini yok say" gibi genel filtre asla.

### 8t. Tracy: bos iz dort ayri sebepten gelir — port kacirma, dinamik srcloc, kesik baglam, NO_EXIT
Tracy istemcisi motora baglaninca (ENGINE_TRACY) yakalama uc saat boyunca "0 bolge" verdi; her seferinde sebep
farkliydi. (1) Onceki telefon kosumundan kalan `adb forward tcp:8086` masaustunde 8086'yi tutuyordu:
`tracy-capture 127.0.0.1` sessizce telefona (kapali uygulamaya) baglandi, iz 400 bayt. Kural: yakalamadan once
`ss -ltnp | grep 8086` — kim dinliyor? Betik denetler ve forward'i sonda kaldirir. (2) Dinamik kaynak konumu
(`___tracy_alloc_srcloc_name`) ile acilan bolgeler yakalanir (5994) ama `tracy-csvexport` istatistiginde
gorunmez; ad basina statik `___tracy_source_location_data` tablosu kullan. (3) `TracyCZoneCtx` yalniz `id` +
`active` olarak saklanip `zone_end`'de yeniden kurulunca `TRACY_ON_DEMAND` altindaki `connectionId` alani
kaybolur ve `___tracy_emit_zone_end` sessizce doner: bolgeler acilir, hicbiri kapanmaz. Baglami ham bayt olarak
butunuyle sakla (`memcpy`, sizeof static_assert). (4) `TRACY_NO_EXIT` sunucu yoksa cikista SONSUZA dek bekler
(engine_tests asili kaldi); kullanma, yakalama penceresini kosumun icinde tut. Ayrica: `pkill -f <ad>` kendi
kabuk komut satirini da eslestirir ve tool cagrisini oldurur (cikis 144); `pkill -x` kullan.

### 8u. `alloc_array_zeroed<T>` kurucu calistirmaz: `-1` varsayilani sessizce 0 olur
`ModelMesh::skin = -1` (yok) varsayilaniyla eklendi; `out->meshes = arena.alloc_array_zeroed<ModelMesh>(n)` bellegi
sifirlar, kurucuyu CAGIRMAZ — her mesh "skin 0" oldu, dama kupu icin `cgltf_accessor_read_uint(nullptr)` cokme.
Testler 67/67'den "COKME testi: content_gltf_loads_checker_cube" a dustu; yeni test gecerken eski test cokuyordu.
Kural: zeroed dizilerde "yok" anlami 0 olsun (indeks+1 sakla) ya da alani acikca yaz; `alloc_array_zeroed` ile
"varsayilan uye degeri" birlikte kullanilmaz. Ayni sinif: `ModelClip::skin = -1`, `ModelMaterial::image = -1`.

### 8v. AGDK Swappy ilk sunumda asilir: sebep `SwappyVk_setQueueFamilyIndex` eksikligi, sinif yukleyici hatasi DEGIL
Ilk teshis (emulator, 2026-09-14) logcat'teki `SwappyDisplayManager ... InMemoryDexClassLoader ... couldn't find
"libtulparengine.so"` satirina takildi ve "API >= 30 Java simi" diye yazildi; telefon (Android 10) ayni sekilde asilinca
varsayim coktu. A/B (2026-09-15): gomulu dex'i APK'ya koyup (`classes.dex`, hasCode=true) hata susturuldu ama asilma
surdu; dex'siz + `SwappyVk_setQueueFamilyIndex(dev, queue, aile)` init'ten once → 59.8 fps. Aile bildirilmeyince 3
sunum "tamamlanir" (SwappyVk_queuePresent doner), sonra ana thread `binder_ioctl_write_read` beklemesinde kalir —
goruntu hic ekrana ulasmaz, acquire donmez, ekran siyah. Kural: (1) Swappy'de kuyruk ailesi ZORUNLU (basliktaki
"needs to know" ciddiye alinir), (2) "init basarili" hicbir sey demek degil — kare sayaci ilerlemeli, (3) logcat'teki
ilk kirmizi satir kok neden olmayabilir; A/B ile ayir. Bekci: 15 s ilerleme yoksa host /proc durumunu basar ve cikar
(siyah ekranda 300 s bekleme yok). Swappy acikken sunum yolunda kare basina 7 `operator new` var (olculdu).

### 8w. CI macOS'un GPU'su sanal ("Apple Paravirtual device"): piksel kapilari orada olculmez
PR #321'in ilk tam macOS kosumunda (daha once engine_tests fizikten sonra cokuyordu; bu kosumda cokme yok,
sebebi bilinmiyor) 6 renderer/content testi dustu: golge koyulasmadi, nokta isik kirmizi piksel vermedi, dama
dokusu keskin gecis vermedi, LOD silueti, iskeletli boru hic cizilmedi, sRGB yedek yolu farkli — hepsi piksel
sonucu. Ayni testler RTX (Linux), lavapipe (CI Linux), Mali (telefon) ve gfxstream (emulator) ile gecer. Karar:
`test::gpu_is_virtual` ile bu cihazda piksel kapilari GORUNUR ATLANDI; CPU tarafi (analitik skinning, meshopt
istatistigi, KTX2 cozumu) kosmaya devam eder. Gercek bir Mac'te MoltenVK sonucu ayri konu (olculmedi).


### 8x. Bilesen bitleri kapaliyken alanlar veri degildir: esitlik/no-op tespiti bilesene gore
`SceneEntity` tum bilesenlerin alanlarini tasir (model, animasyon, isik, govde); dosyaya yalniz biti acik olanlar
yazilir. Ilk `scene_entity_equal` her alani karsilastirdi: testin kopyalanan `e`'sinde kalan `phase`/`asset`
(bileseni kapali) yaz→oku sonrasi varsayilana dondu, gidis-donus kapisi 3 varlikta dustu — metin baytlari ise
AYNIYDI. Ayni hata gunlukte no-op tespitini de bozar (bilesen kapali alan degisince "islem" kaydedilir). Kural:
esitlik = dosyaya giden alanlar; bit kapaliysa alan yok sayilir. Kapi: `scene_text_roundtrip_is_deterministic`.

### 8y. fish kabugunda `set -- $x` bash degil: deney degiskenleri bos kalir, A/B ikisi de varsayilanla kosar
Bash tool bu makinede fish acar. `for exp in "A VAR=0" "B VAR2=0"; do set -- $exp; kv=$2` fish'te bolme yapmaz:
`$kv` bos, iki "farkli" kosum da ayni yapilandirmayla gecti ve sonuc "ikisi de calisiyor" gibi gorundu (2026-09-15,
Swappy A/B). Kural: cok adimli/degiskenli deneyler `bash betik.sh` dosyasiyla kosulur; kosumun basinda etkin
degiskenler loga basilir (`[android] swappy DENEY: ...` satiri gibi) — cikti tarafinda dogrulanmayan deney yok sayilir.

### 8z. Huawei'de `abort()`/tombstone logcat crash tamponuna dusmez; takilan thread'i surec icinden teshis et
Bekci `abort()` etti, surec oldu ama `logcat -b crash` 0 satir, "F DEBUG" yok; boru uzerinden yazilan son satirlar da
surecle birlikte kayboldu (stdout okuyucu thread'i olur). Sonraki kosumun `logcat -c`'si onceki kaniti da sildi.
Kural: (1) teshis surec icinden: `/proc/self/task/<tid>/{stat,wchan,syscall}` + SIGUSR1 ile `crash_capture_frames`
(kesilemez beklemede yanit vermez, wchan yine konusur), sonra `usleep` + `_exit` (boru bosalsin), (2) kosum betigi
surec olunce beklemeyi keser (`pidof`), (3) kanit iceren logcat'i bir sonraki kosumdan ONCE dosyaya cek.

### 8aa. Renderer istatistiği kayıtta sayılır: `record`tan önce okunan `draws` önceki karenindir
`Renderer::draw()` kuyruğa ekler, `stats_.draws` ancak `record()`ta atanır. Blob runtime testi `rt.draw()`un hemen
ardından `ren.stats().draws == 6` bekledi, 0 gördü — "runtime hiç çizmiyor" sanıldı; çizim kuyruktaydı, sayaç henüz
güncellenmemişti (2026-09-15). Kural: istatistik kayıt/submit **sonrasında** okunur; "0 çizim" görünce önce sayacın
ne zaman güncellendiğine bak, kuyruğun boş olduğuna değil. Kapı: `scene_runtime_draws_blob_entities_offscreen`
(kayıt sonrası sayı + piksel farkı; boş-boş 0 kontrolü).

### 8ab. Ön-döndürme unutulunca 3B yan yatar ama HUD DÜZGÜN görünür — ekran görüntüsü yanıltır
Tulpar köprüsünün ilk emülatör koşumunda zemin neredeyse dikeydi, küre elipse dönmüştü; **HUD yazısı ise
tertemiz ve düz** duruyordu. Sebep: projeksiyon fiziksel (döndürülmüş) framebuffer oranıyla kuruldu ve
`swap.rotation_radians()` clip uzayında uygulanmadı — oysa `ui_begin` dönüşü zaten parametre olarak alıyor,
o yüzden arayüz doğru çıktı. Kural: Android'de en-boy oranı **logical_extent**'ten, projeksiyon
`Mat4::rotate({0,0,1}, rotation)` ile çarpılır (demo_app bunu yapıyordu, köprüye taşınmadı). "Arayüz düzgünse
render de düzgündür" çıkarımı YANLIŞ; iki yol dönüşü ayrı alıyor. Tuzaklar 8k/8r'nin köprüdeki tekrarı.

### 8ac. Tulpar'da bit kaydırma ve onaltılık literal yok; `log` doğal logaritmadır
Köprü sarmalayıcısı ilk sürümde `(r << 24) | ...` ve `0xE63946FF` yazdı: **ikisi de lexer'da yok**, gömülü
kütüphane sessizce ayrıştırılamadı ve hata "senin dosyanda 44. satır" diye değil, `(stdin):44` diye çıktı
(gömülü kaynak). Ayrıca `log("...")` yazınca "fonksiyon bulunamadı" değil, **matematik `log`una** çakışma
alınır — sarmalayıcı `logla()` oldu. Kural: yeni bir `lib/*.tpr` yazarken renk paketlemeyi `tame`'in
`rgb()`'sinden (çarpma), ad seçimini ise typeinfer builtin tablosundan doğrula.

### 8ad. Girdi cihazı yokken erken dönen doğrulayıcı, yanlış tuş adını SESSİZ yutar
`teng_key_down` önce `if (!g->in) return 0;` yapıyordu: headless'ta (ve Android'de klavye yokken) yanlış
yazılmış `"SPCAE"` hiç şikâyet etmeden hep false döndü — "tuş çalışmıyor" hatası saatlerce sürebilirdi.
Kural: **ad/arg doğrulaması önce, cihaz kontrolü sonra**; doğrulama hatası her kipte loglanır. Kapı:
`bridge_runs_a_scripted_game_headless` geçerli adın sayaç artırmadığını (kontrol) ve geçersizin artırdığını ölçer.

### 8ae. `install_run.sh` paket adını sabit tutuyordu — her yeni oyunda "Activity does not exist"
`tulpar build --target=android` paket adını **çıktı adından** türetiyor (`dev.tulparlang.<ad>`), oysa koşum
betiği `dev.tulparlang.game` diye başlatmayı deniyordu; APK kuruluyor, `am start` "Error type 3" veriyordu.
Betik artık paketi APK'nın yanındaki staging manifest'inden (ya da `TULPAR_ANDROID_PKG` / aapt2) okuyor ve
başlarken basıyor. Kural: kurulum ile başlatma arasındaki kimlik **tek yerden** gelmeli.

### 8af. Profiler çalışma tamponu kare kapasitesinin İKİ KATI olmalı; azı 300. karede süreci abort eder
`Profiler::frame_stats(scratch, count)` tamponun ilk yarısını örnek, ikinci yarısını sıralama alanı olarak
kullanır ve `scratch.size() >= count*2` assert'ler. Köprü 600 kapasiteyle 600'lük tampon verdi: 300 kareye
kadar sorunsuz, sonrasında **kapanışta** `ENGINE_ASSERT` → SIGABRT. Sinsi tarafı: masaüstü koşumlarım 120–200
kareydi, hiç düşmedi; emülatörde 1984 kare koşan oyun **kapanışta çöktü ve ben fark etmedim**, çünkü logda
"kapanis" satırının yokluğuna değil, hata satırının varlığına bakmıştım. Aynı hata `editor_app.cpp`'de de
duruyordu (3 karelik headless koşum yüzünden hiç patlamamıştı). Kural: tampon = 2 × `frame_capacity`; ve
"bitiş satırı YOK" da bir hata işaretidir, sessiz başarı sayılmaz. Kapı: köprü testi 365 kare koşar.

### 8ag. Android varlıkları ALT DİZİNE montaj edilir ama `AAssetManager_openDir` alt dizin adı vermez
`tulpar build --target=android` varlık dizinini kaynak yoluyla montaj ediyor
(`TULPAR_ANDROID_ASSETS=examples/assets` → APK'da `assets/examples/assets/...`), böylece masaüstündeki göreli
yol cihazda da tutuyor. Ama native `AAssetManager_openDir(mgr, "")` yalnız **o dizindeki dosyaları** listeler,
alt dizin adlarını vermez — kök taraması hiçbir şey bulamaz ve oyun "varlık yok" der. Dizin adlarını yalnız
Java tarafındaki `AssetManager.list()` veriyor. Köprü host'u JNI ile özyinelemeli geziyor (dizin = list()
boş değil), `mkdir` + çıkarma yapıyor ve sayıyı logluyor (`varlik 21 dosya, 7 dizin`).

### 8ah. Gölge atlasında iki tuzak: komşu kademeye taşan PCF ve kenetlenmemiş kutunun "yürüyen" gölgesi
Kademeler tek dokuda yan yana durunca 3x3 PCF tile sınırında **komşu kademenin** derinliğini okur ve orada
ince bir yanlış gölge şeridi çıkar; çözüm örneklemeyi tile'ın bir texel içinde tutmak (`inset`). İkincisi:
kademe kutusunun merkezi odakla (kamera) birlikte sürekli kayarsa gölge kenarları her karede yarım texel
oynar ve statik sahnede bile "yürür"; çözüm merkezi **ışık uzayında texel katına yuvarlamak**
(`cascade_matrix`). İkisi de açılışta görünmez, hareket edince ortaya çıkar — bu yüzden tek kare ekran
görüntüsü bu sınıfı doğrulamaz.

### 8ai. Son işlem yolu kendi temizleme rengini kullanır: bloom'u açınca gökyüzü siyaha döner
Post açıkken sahne artık çağıranın hedefine değil **iç HDR hedefine** çiziliyor ve o hedef `post_clear` ile
temizleniyor (varsayılan siyah). Bloom A/B ölçümünde 112 bin piksel fark çıktı; bakınca farkın çoğu hale değil
**arka plandı** — post kapalıyken mavi-gri olan gökyüzü açıkken simsiyahtı. İç hedefin temizleme rengi, post
kapalıyken kullanılan hedefin rengiyle eşitlenince fark 26 bine indi ve geriye yalnız gerçek hale kaldı.
Kural: yeni bir geçiş zinciri eklerken **temizleme/clear değerleri de sözleşmenin parçasıdır**; A/B ölçümünde
"fark var" yetmez, farkın NEREDE olduğuna bak.

### 8aj. Bayat arşiv denetimi TEK sembol ailesine bakıyordu: "temiz" derken web hedefi tamamen kırıktı
`tests/dist_archive_audit.py` tam olarak "arşiv bayat mı" sorusunu eyleme çevirmek için yazılmıştı, ama yalnız
`aot_tm_*` (tame) tablosuna bakıyordu. Ölçüldü (2026-09-15): denetim **"dist arsiv denetimi temiz"** dedi, aynı
anda `tulpar build --target=web` **her** oyunda `undefined symbol: aot_intern_string` ile düşüyordu — çekirdek
runtime sembolü tablonun dışındaydı. Aynı kör nokta Android'de de vardı: `aot_http_request` yoktu, yani skor
tablosu kullanan her Android derlemesi link'te ölecekti. Kural: denetim, **codegen'in adıyla bildirdiği** sembol
kümesini (LLVMAddFunction literalleri + tablolar) hedefin arşiv kümesine karşı denetlemeli; `nm` çıktısında
**tür harfi U olan satır TANIMSIZ demektir**, onu "var" saymak denetimi sahte yeşile çevirir. Hedefte bilerek
olmayan aileler (async, TLS) sebebiyle listelenir, sessizce yok sayılmaz.

### 8ak. wasm32'de işaretçi 4 bayt: codegen'in sabitlediği nesne başlığı 32 değil 20 bayt
AOT, dizi erişiminin hızlı yolunu satır içi GEP ile yapıyor ve `ObjArray` düzenini kendi kuruyordu —
başlık dolgusu **sabit 28 bayt** yazılmıştı, yani 64-bit varsayımı. wasm32'de `Obj` 20 bayt (işaretçi 4), bu
yüzden runtime'ın `static_assert`'leri web derlemesini kırıyor, `wasm/dist` tazelenemiyor ve hedef sessizce
çürüyordu. Üstelik `backend->target_web` **`llvm_init_types`'tan SONRA** atanıyordu: tip gövdesi kurulurken
bayrak hep 0 görünüyordu. Kural: hedefe bağlı her düzen kararı, bayrağın **kurulduğundan emin olunan** noktadan
sonra alınır; runtime'daki düzen kilidi de iki işaretçi boyutunu ayrı ayrı sabitler, tek bir 64-bit iddiası
yazmak 32-bit hedefi kapatır.

### 8al. Her GPU kapısı kendi `VkInstance`'ını açarsa, SONRAKİ kapılar sessizce ATLANDI'ya düşer
Ölçüldü (2026-09-15, bu makine): her `vkCreateInstance` NVIDIA ICD'sini `dlopen`'lıyor, `libnvidia-tls.so`
initial-exec TLS istiyor ve glibc'nin "static TLS surplus" alanı dlopen/dlclose döngülerinde **geri
verilmiyor**. Süreçte belli sayıda instance'tan sonra yükleyici `cannot allocate memory in static TLS block`
→ `Found no drivers!` diyor ve `vkCreateInstance` `VK_ERROR_INCOMPATIBLE_DRIVER` dönüyor. Sonuç sinsi: yeni
bir GPU kapısı EKLEMEK, kendisi geçerken **sonradan koşan başkalarının** kapılarını (editör ImGui, ışık
gizmosu, köprü) görünür `skip`'e düşürüyor — takım yeşil kalıyor ama kapı sayısı sessizce eriyor. Kural: yeni
GPU kapıları **cihazı/instance'ı paylaşsın**; ayrı instance yalnız doğrulama sayaçlarını kirletmemek gibi
gerçek bir sebep varsa açılsın. Belirti: "geçen test sayısı aynı ama ATLANDI arttı".

**Mekanizma ölçüldü (2026-09-16):** sebep Vulkan değil, **glibc'nin statik-TLS fazlası**. NVIDIA ICD'si
`dlopen` edildiğinde aldığı statik-TLS bloğunu kapanışta geri vermiyor; her yeni `VkInstance` bir tur daha
tüketiyor ve süreç sınıra dayandığında sonraki `dlopen` başarısız oluyor — üst katmana "Vulkan cihazı yok"
diye görünüyor. Yani hata, onu tetikleyen kapıda değil **ondan sonrakilerde** patlıyor; yeni bir kapı
eklemek, kendisi geçerken başkalarının kapısını eritiyor. Somut vaka: beş yeni PBR kapısı kendi
instance'ını açtı ve **25 kapı** sessizce ATLANDI'ya düştü (149/149/0 → 159/1/25). Düzeltme: beş kapı tek
bir paylaşılan cihazı kullanıyor ve cihaz süreç boyunca yaşıyor. 8an yüzünden paylaşılan cihaz **kendi
`VkApi` tablosunu** kullanmalı — aynı dosyadaki başka kapılar kendi cihazlarını açıp kapatıyorsa ortak
tabloyu ezerler.

Denetlenebilir kural: `engine_tests` özeti **atlanan sayısını da basıyor**; GPU'su olan bir makinede o sayı
**0 olmalı**. "X passed" tek başına yeşil sayılmaz.

### 8am. Türetilmiş ama DİSKTE DURAN dosya bayatlayınca kapı sessizce "atlandı"ya düşer
`examples/assets/arena.sahneb` türetilmiş (gitignore'lu) bir dosya; sahne blob formatı **sürüm 2**'ye
çıkınca diskteki kopya v1 kaldı. Motor onu doğru biçimde reddediyordu, ama `tests/engine_bridge.test.tpr`'nin
sahne kapısı "dosya açılamadı → görünür atlama" yoluna düşüyor ve suite **yeşil** kalıyordu: kapı vardı, bir
şey ölçmüyordu. Kural: türetilmiş girdi kullanan kapı, dosyayı **kendisi üretmeli** (test derleme adımını
çağırmalı) ya da bulamadığında ATLAMAK yerine KIRMIZI olmalı. Aynı sınıf: `wasm/dist` ve `android/dist`
arşivleri (Tuzaklar 8aj). Genel kural: "atlandı" sayısı sessizce artıyorsa, kapılar erimiş demektir.

### 8an. Aynı `VkApi` tablosuyla ikinci cihaz açmak, paylaşılan cihazın giriş noktalarını EZER
`VkApi` cihaz düzeyindeki fonksiyon işaretçilerini tek tabloda tutuyor. İki testin aynı tabloyla iki ayrı
`VkDevice` açması, ikinci `init` sırasında tablodaki adresleri ikinci cihazınkilerle **değiştiriyor**; ikinci
cihaz kapanınca ilk cihaz üstünden yapılan sonraki çağrı geçersiz adrese atlıyor (ölçüldü: `offscreen_create`
içinde SIGSEGV). Kural: paylaşılan cihaz varken ayrı bir cihaz açman gerekiyorsa **ayrı bir `VkApi` tablosu**
kullan. Bu, 8al'in (her kapının kendi instance'ını açması) ikizi: biri sessiz atlama, bu ise çökme üretir.

### 8ao. Tazelik denetimi yanlış BİRİMİ karşılaştırınca her şeyi "bayat" ilan eder — ve inandırıcı görünür
`tests/paket_boyut_audit.py`'ye SPIR-V tazelik denetimi eklerken üretilmiş `*_spv.h` başlıklarını **bayt**
dizisi sanıp `0x[0-9a-f]{1,2}` ile taradım; başlıklar aslında 32-bit **kelime** tutuyor (`0x%08x`). Regex hiç
eşleşme bulmadı, denetim 21 shader'ın 21'ini birden "BAYAT — GPU eski shader'i kosturur" diye bildirdi. Çıktı
tamamen ikna edici: her satır gerçek bir bayt sayısı veriyordu (`0 != 1640 bayt`). Tuzağın asıl yüzü sessiz
yeşilin **aynadaki hali**: yeni bir kapının ilk koşusu KIRMIZI olduğunda, refleks "demek ki gerçekten bozuk"
olur ve insan kaynağı düzeltmeye girişir — oysa bozuk olan ölçüm. Kural: yeni bir kapının ilk sonucu
**makullük sınavından** geçmeli. "Hepsi bozuk" (21/21) ile "hiçbiri bozuk değil" aynı şüpheyi hak eder;
ikisi de tipik olarak ölçümün hiçbir şeye bakmadığı anlamına gelir. Somut kontrol: denetimin gördüğü ham
veriyi bir kez yazdır (kaç kelime okundu?) — sıfır okuyorsan karşılaştırma değil ayrıştırma bozuktur.

### 8ap. Aynı `LLVMModule`'ü İKİ hedef için emit etmek — ikinci hedef sessizce bozuk kod alır
Android hedefi tek modülden iki ABI üretiyor: önce `arm64-v8a`, sonra `x86_64`. `LLVMTargetMachineEmitToFile`
**saf bir okuma değildir**: CodeGen boru hattı modülü YERİNDE değiştiren IR geçişleri içerir
(`PreISelIntrinsicLowering`, `AtomicExpand`, `ExpandLargeFpConvert`, `SelectOptimize`, …) ve `LLVMSetModuleDataLayout`
veri yerleşimini de hedefe göre damgalar. Yani ilk emit'ten sonra elde kalan şey ön-uç IR'i değil, **o hedefe
göre alçaltılmış IR**'dir; ikinci emit onun üstüne biner.

**Ölçülen sonuç (2026-09-15, emülatör):** `examples/engine_aksiyon.tpr` ilk karede SIGSEGV veriyordu. Motorun
kendi çökme raporu faili tam yerinden söyledi: `t_menu_ciz.f+576`. Disassembly: 16 bayt hizalı `movapd`,
8 mod 16 olan `0x48(%rsp)` yuvasına yazıyordu. Masaüstü ikilisinde aynı fonksiyon `0x40(%rsp)` (hizalı)
kullanıyor. Kanıt tek komutla kapandı: aynı optimize IR `llc -mtriple=x86_64-linux-android34
-relocation-model=pic` ile **tek başına** derlendiğinde doğru yuvayı (`0x40`) üretti — fark yalnızca
"bu modül daha önce başka bir hedef için emit edildi mi" idi. Düzeltme: her ABI kendi `LLVMCloneModule`
kopyasından üretiliyor (`emit_object_with_triple(..., clone_module)`).

**Neden bu kadar sinsi — üç ayrı maskeleme birden:**
1. **İLK ABI doğru üretilir.** arm64 (gerçek telefon) kusursuz çalışır; yalnız ikinci sırada üretilen x86_64
   (emülatör) bozulur. İnsanın refleksi "emülatör işte" olur ve hata emülatörde aranır.
2. **`fault_addr: 0x0` null gibi görünür.** Hizalama hatası (GP fault) SIGSEGV'yi `si_addr = 0` ile verir;
   yığın izi olmayan bir null dereference avına çıkılır. Ayırt edici işaret: faulting komut `rsp`-göreli
   bir `movapd`/`movaps` ise sorun eksik bellek değil, YANLIŞ HİZADIR.
3. **Link ve derleme yeşildir.** `-Wl,--no-undefined` dahil her şey geçer; hata yalnız o kod yolu ilk kez
   çalıştığında görünür — burada ana menünün ilk çizimi, yani kurulumun tamamı loglandıktan SONRA.

**Kural:** bir `LLVMModule`'den birden fazla hedef için nesne üretiyorsan **her hedef için klonla**. Aynı
kural başka bir yerde daha geçerli: `llvm_backend_optimize` zaten bu yüzden `LLVMCloneModule` üstünde
deneme yapıyor. Genel biçimi: *"aynı IR'i iki kez tüketmek" bir varsayımdır, ve LLVM'de yanlıştır.*

### 8aq. Var olan bir sembolün İMZA değişikliği, "sembol var mı" denetiminden YEŞİL geçer
`aot_input()` sıfır argümanlıydı; `input("You: ")` istemi sessizce düşüyordu. Düzeltme sembolü
`aot_input(VMValue)` yapmak — **yeni sembol değil, var olanın imzası**. `wasm/dist` ve `android/dist`
altındaki ön derlenmiş arşivler eski imzayı taşır ve `tests/dist_archive_audit.py` sembolün **varlığını**
sınar: sembol hâlâ "var" göründüğü için denetim temiz der. Ölçüldü: bayat arşivle web derlemesi
`wasm-ld: warning: function signature mismatch: aot_input — defined as (i32,i32)->void in <obj>, as
(i32)->void in libtulpar_runtime_web.a` diyor ama **link yine de tutuyor** ve `.html` üretiliyor; hata
ancak tarayıcıda, o çağrıya gelindiğinde ortaya çıkıyor.

Kural: yeni builtin **eklemek** ile var olanın imzasını **değiştirmek** ayrı risk sınıflarıdır. İkincisi
arşiv yenilemesiyle **aynı turda** yapılır; ayrı turda yapılırsa ortada hiçbir kırmızı olmadan bozuk bir
ağaç kalır. Denetim tarafındaki karşılığı `check_archive_freshness()`: sembol denetimi "ne **eksik**"
der, tazelik denetimi "ne **bayat**" der — bu sınıfı yalnız ikincisi görür. Kaynak listesi elle yazılmaz,
sürücünün kendi `warn_if_prebuilt_archive_stale` listesinden okunur ki sürücüyle denetim ayrışamasın;
desen tutmazsa denetim "temiz" demek yerine **kapsamını kaybettiğini** söyleyip kırmızı olur.

### 8ar. `rm -rf <ortak dizin>` komşunun türetilmiş çıktısını da siler — ve bunu kimse görmez
`android/build_tame_android.sh` her koşumda `rm -rf "dist/$abi"` yapıyordu. O dizin yalnız kendisinin
değil: `tools/build_bridge_android.sh`'ın ürettiği **11 motor arşivi** de orada yaşıyor. Betiği
koşturmak motorun bütün arşivlerini siliyor, `import "engine"` eden her Android derlemesi link'te ölüyor
ve **masaüstünde hiçbir şey kızarmıyor** — çünkü masaüstü o arşivlere hiç bakmıyor. Betik artık yalnız
kendi çıktılarını siliyor (`rm -rf "$OBJ"` + kendi iki `.a`'sı).

Genel biçim: bir dizin **paylaşılıyorsa** temizlik `rm -rf <dizin>` değil, **ürettiğin dosyaların adıyla**
yapılır. Belirtisi sinsi çünkü hasar, betiği koşturan kişinin ilgilenmediği bir hedefte ortaya çıkıyor:
tame'i yeniden kuran kişi motorun bozulduğunu göremez. `tests/dist_archive_audit.py` bunu "arşiv YOK"
diye yakalar — ama ancak koşturulursa; `build.sh suites` içinde olmasının değeri tam olarak budur.

### 8as. `vkGetDeviceProcAddr`'in verdiği yordam CİHAZA ÖZGÜDÜR — tek global "gerçek işaretçi" çökertir
PSO önbelleğini bütün pipeline kurulumlarına bağlamak için `VkApi` tablosuna bir ara yordam (thunk)
takıldı. İlk yazımı **tek global** bir "gerçek `vkCreateGraphicsPipelines`" işaretçisi tutuyordu. Oysa
`vkGetDeviceProcAddr`'in döndürdüğü adres o **cihaza** aittir: kendi cihazını açıp kapatan bir test, globali
kendi (artık ölü) cihazının yordamıyla değiştiriyor, ardından paylaşılan cihaz üstündeki ilk pipeline
kurulumu **SIGSEGV** veriyordu (ölçüldü: `render_graph_gpu_cull_indirect_matches_cpu_path`).

Çözüm: her kanca yuvası **kendi** gerçek işaretçisini tutar, thunk gelen `VkDevice`'a göre seçer; hiçbir
yuvaya uymayan bir çağrı **çökmez** — hata döner ve sayılır (`pso_unrouted_calls()`, kapı 0 bekler). O
sayaç olmasaydı yanlış yönlendirme sessiz kalırdı.

Bu, 8an'in (aynı `VkApi` tablosuyla ikinci cihaz açmak giriş noktalarını ezer) aynı ailesi ve genel biçimi
şu: **Vulkan'da cihaz düzeyindeki hiçbir fonksiyon işaretçisi süreç genelinde geçerli değildir.** Bir
işaretçiyi global tutuyorsan, onu hangi cihazdan aldığını da tutmak zorundasın.

### 8at. Aralanmamış A/B süreç ölçümü makine yükü kaymasını "kazanç" gibi gösterir
PSO önbelleğinin uçtan uca kazancı önce SOĞUK→SICAK sırasıyla ölçüldü ve **21 ms kazanç** çıktı. Ölçüm
A/B/**C** olarak aralanınca (soğuk / sıcak / bozuk-önbellek, üçü dönüşümlü) fark **sıfırlandı**: 203.3 /
201.0 / 200.2 ms medyan. Yani 21 ms, önbelleğin değil makinenin o sırada boşalmasının eseriydi. Aynı
ölçümün mutlak değeri yüke aşırı duyarlı: derleme CPU'yu doldururken pipeline kurulumu 98–120 ms, boş
makinede ~11 ms.

Kural: bir iyileştirmenin A/B'si **dönüşümlü** koşulur ve mümkünse **zamanlamadan bağımsız** bir kanıtla
desteklenir. Burada asıl kanıt süre değil, **önbellek büyümesi** oldu: soğuk koşumda dosya 0 → 203 249 B
büyüyor, sıcak koşumda **+0 B** — yani 15 varyantın hepsi diskten isabet ediyor. Bu sayı makine yükünden
etkilenmez. Ölçemediğin şeyi iddia etme: süreçler arası uçtan uca kazanç bu masaüstünde **ölçülemedi** ve
öyle kaydedildi.

### 8au. Android'de `shutdown` çoğu zaman HİÇ koşmaz — kapanışta yazılan şey hiç yazılmaz
Kalıcı PSO önbelleği `Device::shutdown()` içinde diske yazılıyordu; masaüstünde kusursuz çalışıyor.
Emülatörde ölçüldü: uygulama iki kez açılıp kapandıktan sonra bile log hâlâ **"dosya yok (ilk çalıştırma)"**
diyordu. Sebep: Android'de uygulamalar temiz kapanmaz — sistem (ya da `am force-stop`, ya da kullanıcının
uygulamayı kapatması) **süreci öldürür**, `shutdown` hiç çağrılmaz. Yani önbellek, tam da en çok ihtiyaç
duyduğu platformda **asla oluşmuyordu** ve bunu hiçbir şey kızartmıyordu: oyun her açılışta çalışıyor,
sadece bütün boru hatlarını yeniden kuruyor.

Düzeltme: kurulum biter bitmez yaz (o noktada ön ısınma bitmiş, bilinen bütün varyantlar kurulu), kapanıştaki
yazma masaüstü için yedek kalsın. Ölçülen sonuç (emülatör, temiz kurulum → öldür → yeniden aç):
soğuk **6.5 ms** / 168 495 B yazıldı, sıcak **1.1 ms** — 5.9x, ve bu kazanç süreçler arası, yani gerçek.
(Aynı kazanç masaüstünde **ölçülemedi**; bkz. 8at. Ölçümün doğru platformda yapılması gerekiyordu.)

Genel biçim: **mobilde "çıkışta yap" diye bir kanca yoktur.** Kalıcı olması gereken her şey (kayıt dosyası,
önbellek, telemetri) üretildiği anda ya da bir yaşam döngüsü duraklamasında yazılır. Bir masaüstü kapanış
yolunun çalıştığını görmek, mobilde çalıştığına dair hiçbir kanıt değildir.

### 8av. Ölçülen değerden türetilen eşik, mevcut yanlışı KUTSAR
`tests/paket_boyut_audit.py`'ye eşikleri koyarken kural şuydu: "ölçülenin ~2 katı — amaç bir gün büyüdü
demek değil, bir anda ZIPLADI demek". APK için ölçülen 71 MB'dı, eşik 96 MB kondu ve denetim **yeşil**
verdi. Eşik doğru çalışıyordu; yanlış olan **71 MB'ın kendisiydi**: linkten çıkan `.so` hiç
striplenmiyordu (arm64 36.3 MB, x86_64 33.8 MB — tamamı sembol ve hata ayıklama bilgisi). Strip'ten sonra
5.4 / 5.7 MB, APK **71 MB → 11.6 MB** (6.1x).

Tuzağın biçimi: bir eşiği "bugünkü ölçüm + pay" diye koymak, bugünkü değeri **normal ilan eder**. Denetim
o andan sonra yalnızca *değişimi* görür, *yanlışlığı* değil — ve sayı ne kadar büyükse, ona eklenen pay da
o kadar büyük olur, yani hata büyüdükçe denetim gevşer. Kural: bir eşik koyarken "bu sayı **olması
gereken** sayı mı?" diye ayrıca sor. Cevabı bilmiyorsan eşiği koy ama **yanına sorusunu da yaz**; yoksa
altı ay sonra kimse o 96'nın nereden geldiğini sorgulamaz.

İkinci yarısı da kayda değer: çıplak strip **teşhis yeteneğini öldürür**. Bu depoda `t_menu_ciz.f+576`
satırı Android x86_64 kod üretimindeki hizalama hatasını tam yerinden gösterdi (8ap); stripli bir `.so`'da
o ad yoktur. Doğru çözüm ikisinden birini seçmek değil: striplenmemiş kopya `<stage>/symbols/<abi>/`
altında saklanıyor, `android/symbolize.sh` adresi geri çözüyor, ve denetim **her ikisini birden** şart
koşuyor — stripsiz `.so` da kırmızı, sembol kopyası olmayan stripli `.so` da kırmızı. İkinci kontrol
olmasaydı "strip et, sembolleri at" yolu paketi küçültüp denetimi yeşil bırakır, kaybı ancak bir sahada
çökme anında fark ederdik.

### 8aw. `static_assert(sizeof(...))` yerleşimin yalnız YARISINI görür — alan sırası değişimi ondan geçer
CPU-GPU arayüzünde bir shader bloğunun std140/std430 yerleşimi ile C++ struct'ının bayt yerleşimi
**sessizce ayrışabilir**: ne derleyici, ne linker, ne Vulkan doğrulama katmanı bunu söyler. GPU başka bir
ofsetten okur, görüntü "biraz yanlış" olur. Bu depoda tam bu sınıfın bir örneği yaşandı (Frame UBO'su için
elle yazılan `static_assert` 80 bekliyordu, gerçek 96'ydı — 6 vec4).

Asıl bulgu: `static_assert(sizeof(X) == N)` bu sınıfın **yarısını** yakalar. Ölçüldü (2026-09-16):
`MaterialUbo`'nun iki alanı yer değiştirildi — boyut aynı kaldı, depodaki `static_assert(sizeof(...) == 64)`
**hâlâ geçti**, ama GPU'nun okuduğu `pbr` alanı artık `emissive`'in baytlarını okuyordu. İddia edilmedi,
**derleyiciye sorduruldu**: bozuk tanım + depodaki assert ayrı bir TU'da derlendi ve geçti.

`tools/layout_audit.py` + `tools/spirv_reflect.py` bunu alan alan denetliyor (ofset / boyut / dizi
adımı / matris adımı). Üç tasarım kararı, hepsi bir kör noktayı kapatıyor:
1. **Yerleşim SPIR-V'den okunuyor, GLSL metninden değil.** GLSL'den std140 kurallarını yeniden
   hesaplasaydık denetim, denetlediği şeyin *aynı varsayımını tekrarlardı* — bu deponun
   "tekrarlanan varsayım kendini gizler" sınıfı. `OpMemberDecorate Offset` glslc'nin gerçekten ürettiği
   sayıdır.
2. **C++ yerleşimi derleyiciye sorduruluyor** (struct başlıktan olduğu gibi alınıp geçici bir TU'ya
   konuyor, `&üye - &nesne` / `sizeof` / `alignof` ölçülüyor) — elle hesaplanan tek bir sayı yok.
3. **Kapsam sessizce daralamaz:** eşleşmeyen her blok ya gerekçesiyle `KAPSAM_DISI` sözlüğünde kayıtlı,
   ya KIRMIZI. Aksi halde "eşleştiremedim, o hâlde temiz" yolu açık kalırdı.

Operasyonel not: **`glslc -O` `OpName`/`OpMemberName`'i siler** — depodaki SPIR-V'de üye adı yoktur
(`m0, m1, …`). SPIR-V üzerinden iş yapacak her araç bunu bilmeli; adlar GLSL kaynağından gelmek zorunda,
ve GLSL üye sayısı SPIR-V üye sayısıyla tutmazsa ad eşlemesi sessizce kaymasın diye KIRMIZI olmalı.

### 8ax. Vendor kütüphanesinde de 8u var: cgltf `texture_view.scale` malzeme düzeyinde varsayılansız
`cgltf_parse_json_texture_view` `scale` alanını **yalnız o JSON nesnesi varsa** 1'e kuruyor; malzeme
düzeyinde bir varsayılan yok. Yani bir glTF malzemesinde `normalTexture` yazılı DEĞİLSE
`material.normal_texture.scale` **sıfır** kalır. Koşulsuz okuyan bir içe aktarıcı bütün normal haritalarını
sıfır ölçekle uygular — yani **hepsini düzleştirir**, ve hiçbir şey kızarmaz: görüntü "biraz yanlış" olur.

Bu, `alloc_array_zeroed` yapıcı çalıştırmaz (8u) kuralının **başkasının kodundaki** hâli: sıfırdan farklı
her varsayılan, onu yazan katman tarafından açıkça atanmalı ve **sen o katman değilsen kontrol etmelisin**.
Doğru kalıp: alanı okumadan önce ilgili bloğun varlığını (`has_*` / işaretçi) sor. Aynı sınıf bizim
tarafımızda da vardı: `ModelImage::srgb = true` varsayılanı `alloc_array_zeroed` sonrası uygulanmıyordu
(sıfır = doğrusal), güvenli varsayılan açıkça sRGB'ye kuruldu.

Genel kural: **bir vendor struct'ını memset'lenmiş/zeroed bellekten okuyorsan, onun varsayılanları senin
varsayılanların değildir.**

### 8ay. Türetilmiş çıktı önbelleğinin anahtarı, çıktıyı belirleyen HER girdiyi içermeli
`engine_texpack`'e `--tur albedo|orm|normal` eklendi (renk uzayı ve ASTC kipi buna göre değişiyor). Önbellek
anahtarı önce yalnız kaynak PNG'nin özetiydi: aynı PNG'yi **önce albedo sonra normal** paketlemek **aynı
anahtarı** üretiyor ve ikinci çağrı birincinin ürününü geri veriyordu — yani normal haritası istediğin yerde
sRGB kodlanmış bir albedo alıyordun. Derleme yeşil, dosya yerinde, içerik yanlış.

`--tur` anahtara eklendi ve `kTexpackVersion` 1→2'ye çıkarıldı (eski girdiler geçersiz sayılsın diye).
Kural: bir önbellek anahtarı "girdi dosyası"nı değil, **çıktıyı belirleyen bütün parametre kümesini**
özetlemeli — bayraklar, sürüm, profil, hedef. Eksik bir parametre, önbelleği sessiz bir yanlış-sonuç
üreticisine çevirir. İlgili: [[8am]] (türetilmiş dosya bayatlayınca kapı sessizce atlanır).

### 8az. Ölçüm, ölçtüğü şeyin temsil ettiği durumda yapılmalı — ASTC MAP_NORMAL örneği
ASTC'nin normal-harita kipini (`ASTCENC_FLG_MAP_NORMAL`) değerlendirmek için üretilen ilk test kaynağında
tümsek kenarlarında z ≈ 0.199 vardı ve ölçüm MAP_NORMAL'in **kaybettiğini** söylüyordu (açısal hata 1.460
vs düz kodlamada 1.363). Sebep kipin kötülüğü değil: iki kanaldan z'yi yeniden kurmak z küçükken **kötü
koşullu** bir işlem, yani hata z→0'da patlıyor. Kenar gerçekçi bir eğime (z = 0.436) çekilince MAP_NORMAL
her blok boyunda kazanıyor.

Ders: bir kodlama/sıkıştırma kararını, **üretimde karşılaşacağın veri dağılımında** ölç. Uç bir örnekte
yapılan ölçüm doğru sayıyı verir ama **yanlış kararı** destekler. Kararın kapsamını da yaz: bu karar "çok
sıyırtma açılı" normal haritaları için geçerli değildir.

### 8ba. Token enum'unu ORTADAN genişletmek, önceden derlenmiş arşivlere sızar
Bit işleçleri eklenirken yeni token'lar enum'un **sonuna** kondu, ortasına değil. Sebep somut:
`vm_binary_op` (`src/vm/runtime_bindings.cpp`) işleci **ham `int` enum değeri** olarak alıyor ve o fonksiyon
`wasm/dist/` + `android/dist/` altındaki **önceden derlenmiş** arşivlerde de duruyor. Ortadan bir değer
eklemek bütün sonraki değerleri kaydırır: masaüstü (kaynaktan derlenen) yeşil kalır, web ve Android ise
**sessizce yanlış işlemi** yapar — `+` yerine `-`, `<` yerine `<=` gibi. Hiçbir sembol eksilmediği için
sembol denetimi de görmez (bu, 8aq'nun kardeşi: ABI yalnız isimlerden ibaret değildir).

Kural: **arşiv sınırını geçen hiçbir sayısal sabit ortadan genişletilmez.** Enum'a ekleme sona yapılır; bir
sıralamayı gerçekten değiştirmen gerekiyorsa arşivler aynı değişiklikte yeniden üretilir.

### 8bb. Yeni anahtar kelime, çalışan bir kodda geçen bir ADI çalar
`const` eklenirken Türkçe eşi olarak `sabit` de denendi ve paket **anında** düştü:
`tests/engine_bridge.test.tpr` içinde `int sabit = -1;` diye bir değişken vardı. Aynı sınıf daha önce
`move` ve `don` (=`return`) ile yaşandı — `bool don = ...` yazan bir modül sessizce kırılıyordu.

Kural: yeni bir anahtar kelime eklemeden önce **depoyu tara**:
`grep -rn --include='*.tpr' '\bKELIME\b' .` — `examples/`, `lib/`, `tests/`, `packages/` dahil. Türkçe
sözcükler burada özellikle riskli, çünkü değişken adları da Türkçe. `sabit` bu yüzden alınmadı; dilde
yalnız `const` var.

### 8bc. Biçimlendiricinin çıktısı DERLENMEYİ bırakabiliyordu ve hiçbir şey sormuyordu
`tulpar fmt` bilinmeyen bir işleci karakter karakter boşluklayınca geçerli kaynağı bozuyor. Ölçülen iki
vaka: yeni bit işleçleri olmadan `a << 2` → `a < < 2` ve `a <<= 1` → `a < <= 1`; ve **önceden var olan**
bir hata, `1.5e-8` → `1.5e - 8` (üs işareti ikili işleç sanılıyordu). İkincisi uzun süredir oradaydı:
`tests/scientific_notation.test.tpr` biçimlendirildiğinde **20 ayrıştırma hatası** veriyordu ve kimse
sormuyordu — çünkü biçimlendirici yalnız *idempotans* için denetleniyordu, "çıktısı hâlâ derleniyor mu"
diye değil. İdempotans, bozuk bir çıktı için de sağlanabilir: bozuk metni ikinci kez biçimlendirmek aynı
bozuk metni verir.

Kapı `tests/fmt_audit.py`: her `.tpr` biçimlendirilir, sonuç **typecheck'ten geçirilir** ve hata sayısı
biçimlendirme öncesine göre ARTMAMALIDIR; ayrıca idempotans denetlenir. Kendi pozitif kontrolü var.

### 8bd. Pozitif kontrolün kendisi boş olabilir — ölçütü, hedeflediği hatanın BOZDUĞUNDAN emin seç
Navmesh ajan kapısında "yolların ajan başına kopyalandığını" şu ölçütle kanıtladığımı sanıyordum: karşılıklı
iki ajan bırakılır, **birbirlerine yaklaşmalılar**. Kopyalamayı kasten bozdum (bütün ajanlar aynı yuvayı
paylaşsın) — **kapı yine geçti**. Sebep: bozuk hâlde iki ajan da yolu bulamayıp (0,0,0)'a doğru yürüyordu,
yani yine yaklaşıyorlardı. "Yaklaştılar" ölçütü, doğru davranış dışında en az bir yanlış dünya tarafından
da sağlanıyordu.

Düzeltme ölçütü sertleştirmekti: **her ajan KENDİ hedefine varmalı** (ikisi yer değiştirmeli). Bu, paylaşılan
yuvayla sağlanamaz. Kontrol enjekte edilince kapı kırmızıya döndü (`expected 10 got -0.27`).

Kural: bir kontrol yazarken "bu ölçüt, hedeflediğim hata dışında hangi yanlış dünyalarda da sağlanır?"
diye sor. Cevap "hiçbiri" değilse ölçüt zayıftır. İlgili: 8ao (bir kapının ilk sonucu makullük ister).

### 8be. Kontrol "ateşlemedi" demeden önce ikiliye ULAŞTIĞINI doğrula (`lib/*.tpr` yeniden yapılandırma ister)
Yukarıdaki kontrolü ilk denediğimde kapı geçti ve "ölçüt zayıf" sonucuna atladım — **yanlış teşhis**.
Gerçek sebep: kontrolü `lib/engine.tpr` içine yazmıştım ve yalnız `cmake --build --target tulpar`
koşturmuştum. Gömülü stdlib `configure_file()` ile üretiliyor, yani **yeniden yapılandırma olmadan
`src/embedded_libs.h` tazelenmiyor**: ikili hâlâ ESKİ kütüphaneyi taşıyordu. Kontrol koda hiç girmemişti.

Teşhis tek komut: `grep -c "<kontrol metni>" src/embedded_libs.h`. 0 ise ölçtüğün şey eski kopyadır.
Doğru sıra: `cmake -S . -B build-linux && cmake --build build-linux --target tulpar`.

Genel kural: bir pozitif kontrol beklendiği gibi kırmızıya dönmüyorsa **önce kontrolün derlenmiş ürüne
girdiğini kanıtla**, sonra ölçütü sorgula. İki farklı arıza aynı belirtiyi veriyor.

### 8bf. Şeker açılan yol denetleniyor diye DÜĞÜM yolu da denetleniyor sanma
`x &= 2.5` ayrıştırıcıda `x = x & 2.5` olarak şeker açılıyor, BinaryOp yolundan geçiyor ve `[typecheck]`
uyarısı alıyor. `a[0] &= 2.5` ise şeker açılmıyor — `CompoundAssign` düğümü olarak kalıyor ve typeinfer o
düğümü **hiç ziyaret etmiyordu**: sessizce geçiyordu (ölçüldü 2026-09-16). Aynı dilde aynı işlecin iki
yazımı farklı şey söylüyordu.

Bir özelliğin hem "şeker" hem "düğüm" yolu varsa denetim İKİSİNE de bağlanmalı; şeker yolundaki denetim
düğüm yolunu kapsamaz. Kapı: `tests/gramer_bosluklari.test.tpr` içinde her iki yazım için ret kontrolü.

### 8bg. Bozuk bir değerden okunan 0, geçerli boş durum gibi görünebilir
`int[] a;` (başlatıcısız) dizi DEĞİL bir değer üretiyordu. `len(a)` **0 dönüyordu** — yani "boş dizi" gibi
görünüyordu — ama `a[0] = 1` ve `push(a, 1)` çalışma zamanında "geçersiz hedef" ile düşüyordu. Derleyici
kabul ediyor, typecheck susuyor, hata en geç noktada ve en anlamsız mesajla çıkıyordu.

`len()`'in 0 dönmesi buradaki asıl tuzak: sağlıklı bir "boş dizi" ile bozuk bir değeri AYIRT EDİLEMEZ
kılıyordu. Bir sondanın 0 dönmesi "geçerli ve boş" demek zorunda değil; "okunamadı" da 0 döndürebilir.
Düzeltme (2026-09-16): başlatıcısız dizi bildirimi artık başlatıcı sentezliyor — `int[] a;` → `[]`,
`int[4] a;` → `[0,0,0,0]`. Böylece `T[N]`'deki N ilk kez bir şey ifade ediyor.

### 8bh. Normal haritasının mip'i BLIT ile üretilemez — ortalama normalin boyu 1 değildir
Donanım blit'i doğrusal süzüyor, yani dört komşu normalin **bileşenlerini** ortalıyor. İki komşu normal
birbirine ters eğimliyse ortalama vektörün boyu 1 değil ~0 olur; encode edilince (0.5, 0.5, ~1) yani
**düz yüzey** çıkar. Görünen sonuç: uzaktaki yüzey sessizce düzleşir, ışık "yassılaşır". Hiçbir şey
kızarmaz, hiçbir doğrulama katmanı konuşmaz — yalnızca görüntü yanlıştır.

Ölçüldü (2026-09-16): birbirine ters eğimli (±0.8) dama deseninde normalleştirmeyen ortalamanın boyu
**0.60**; küçültmeden sonra yeniden normalleştirince **1.0000**. Çözüm normal haritalarını CPU'da
mip'leyip hazır seviye olarak yüklemek (`content::build_normal_mips` → `create_texture_levels`).

İki ayrı tuzak daha var: (1) **ORM bu işlemi ALMAMALI** — pürüzlülük/metaliklik birer skalerdir,
normalleştirmek onları bozar; bayrak renk uzayından ayrı tutuluyor. (2) Fonksiyonu doğrudan ölçen bir kapı
YETMEZ: yükleme yolu onu hiç çağırmazsa görüntü eski davranışta kalır ve kapı yine yeşil olur. O yüzden
`UploadedModel::normal_mip_textures` sayılıyor ve GPU kapısı `== 1` diye bakıyor.

### 8bi. Doğru bir optimizasyon ölçülebilir hiçbir kazanç vermeyebilir — sayıyı yaz, iddiayı yazma
`expr_is_int` bit işleçlerini tanımıyordu, yani `a[i] = x & maske` kutusuz dizi yolunu kaybediyordu.
Tanıtmak doğruydu (kanıt kuralının kendisinde boşluktu) ama **hız kazancı ölçülmedi**: 4096 elemanlı
dizide 20 000 tur, eski ikili 30 ms, yeni ikili 30 ms. LLVM her iki yolu da aynı şekilde indirgiyor.

İlk ölçümüm daha da yanıltıcıydı: ifadede döngü değişkeni olmayan bir ad (`n`) kullanmıştım, o yüzden
`expr_is_int` iki sürümde de false dönüyordu — yani kıyas hiçbir şeyi ayırt etmiyordu. Kıyas kurarken
"bu iki yol gerçekten farklı kodu mu çalıştırıyor?" sorusu, sonucu okumaktan önce gelir.

Değişiklik tutarlılık için durdu, hız için değil, ve kaynak yorumu bunu böyle söylüyor. Ölçmeden
"hızlandırdık" yazmak, bu depoda ölçmeden "düzelttik" yazmakla aynı sınıf.

### 8bj. "Bit bit aynı" bir kapı ölçütü olamaz — sürücünün optimize edicisi bizim sözleşmemiz değil
`renderer_normal_map_tilts_lighting` şunu istiyordu: normal ölçeği 0'ken kare, normal dokusu hiç
olmayan referansa **bit bit** dönmeli (`md == 0 && ad == 0.0`). NVIDIA'da dönüyordu, lavapipe'ta (CI Linux)
dönmüyordu ve kapı orada kırmızıydı.

Sebep şu: iki kare **iki farklı shader dalından** geliyor — biri "normal dokusu yok", öteki "normal dokusu
var, ölçek 0". Aritmetik olarak aynı sonucu verirler ama **metin olarak farklıdırlar**. İkisini aynı koda
indirgemek sürücü derleyicisinin işidir; yaptığı da garanti değildir, yapmaya devam edeceği de. Yani kapı
bizim renderer'ımızı değil sürücünün optimize edicisini sınıyordu.

Yerine geçen ölçüt ayrımı koruyor ve sürücüden bağımsız: ölçek 0'ın artığı düz haritanınkinden **büyük
olamaz** ve ürün sinyalinden en az 20 kat küçük olmalı. Ayırt etme gücü ölçüldü — ölçek okunmuyormuş gibi
enjekte edince artık 0'dan **18 501 piksele** (ortalama 5.53) fırladı ve kapı kırmızıya döndü. Bit eşitlik
hâlâ **raporlanıyor** ("bit bit eşit: EVET/hayır") ama hüküm değil: sayı var, iddia yok.

Genel kural: bir kapı yalnızca **bizim ürettiğimiz** şeyi ölçmeli. Sürücünün/derleyicinin iki eşdeğer
ifadeyi aynı koda indirgeyip indirgemediği bizim ürünümüz değil. İlgili: 8be (kopyalanan kontrol, korumasız).

### 8bk. Aynı sınıf kontrol üç yere kopyalanınca koruması geride kalır
CI Linux'ta dört Mali kapısı birden kırmızı döndü. Üçü yeniydi ve pozitif kontrolü
(`renderer_mali_best_practices_gate`'teki "LOD kırpan sampler Arm uyarısı vermeli") **kopyalamıştı** — ama
orijinaldeki **korumayı** kopyalamamıştı: katman Arm kurallarını tanımıyorsa kapı ölçemez ve görünür
atlanır. CI'daki apt katmanı (Ubuntu 24.04, VVL 1.3.275) tam olarak bu durumda.

İki ders. (1) Kopyalanan kontrol, korumasıyla birlikte kopyalanmalı — yoksa "aynı kontrol" değil, yarısıdır.
(2) Bu sınıfın kalıcı çözümü kopya değil **ortak fonksiyon**: sonda artık `test::arm_rules_missing()`
(gövdesi `test_main.cpp`), dördü de onu çağırıyor, bir daha ayrışamaz.

Ayrıca dikkat: o kapılardaki ÜRÜN iddiası (`bp_arm_effective == 0`) katman kuralları tanımıyorken **boşa
geçer** — 0 uyarı her zaman 0'dır. Kapıyı ayakta tutan tek şey kontroldür; o yüzden kontrolün sonucu
`CHECK` değil **atlama** olmalı.

### 8bl. "Son render'ın tepesi" + gerçek zamandan hızlı çeken cihaz = bazen düşen ses kapısı
`audio_default_device_opens` 0,3 s'lik bir klip çalıp 200 ms sonra `MixerStats::peak`'e bakıyordu.
`peak` **son render çağrısının** tepesidir, kümülatif değil. Yani "ölçüm anında klip hâlâ çalıyor mu"
sorusu, cihazın ne kadar önden tampon doldurduğuna bağlanıyordu.

CI macOS'un sanal ses cihazı gerçek zamandan hızlı çekiyor: 200 ms uykuda **33 callback × 480 = 15 840
kare** (= 330 ms ses) render etti, 0,3 s'lik klip bitti, son render sessizdi → tepe **0,00000**, kapı
kırmızı. Bir önceki koşumda aynı kapı 22 callback (220 ms) ile tepe 0,00050 verip geçmişti — klasik
"bazen düşen", ama sebebi gürültü değil **yarış**.

Yerelde birebir üretildi: klibi 0,01 s yapıp döngüsüz çalınca tepe 0,00000 ve kapı kırmızı; aynı klip
**döngülü** çalınca tepe 0,00050 ve yeşil. Düzeltme döngülü çalmak — son render her zaman sinyal taşır,
cihazın hızı ölçümü etkilemez.

Ders: bir kapı "şu an" okunan bir değere bakıyorsa, o değerin **ne kadar süre geçerli kaldığını** sor.
"Bazen düşüyor" demeden önce yarışı yerelde üretmeye çalış — burada üç dakika sürdü.

### 8bm. Zamanlama eşiğini ÖLÇÜLEN birime bağlamak, kapıyı yük altında SERTLEŞTİRİR
`gather` eşzamanlılık kapısı üç kez yanlış yazıldı, üçü de macOS/arm64 CI'da düştü. Üçüncüsü
(`esz < birim * 2`) şu modeldeydi: birim = uyku + bir çağrı ek yükü, gather ≈ 1 birim. **Yanlış**:
gather **üç** çağrı ek yükü öder (+ kendi kurulumu). 20 ms uykuda ek yük 11 ms olunca — sinyalin yarısı
kadar — model kırıldı (birim=31 gather=82 eşik=62).

İki ayrı hata vardı. (1) Ölçülen süre ek yükle **aynı büyüklük mertebesindeydi**; çözüm eşikle oynamak
değil uykuyu 20 ms'den 120 ms'ye çıkarmak — ek yük sinyalin %55'inden %9'una düştü. (2) Eşik `birim`e
bağlıydı; gerçek tasarruf (2 uyku) yükle **değişmez** ama `birim` yükle **büyür**, yani eşiği birime
bağlamak kapıyı yük altında sertleştiriyordu — tam ters yön. Eşik artık nominal uykuya bağlı.

Ayrıca seri kol artık **varsayılmıyor, ölçülüyor**: bir daha düştüğünde "gather gerçekten seri miydi"
sorusu tahminle değil sayıyla cevaplanır. O koşumda gather aslında seriden hızlıydı (82 < ~93) — yani
eşzamanlılık çalışıyordu, ölçüt bozuktu; seri kol ölçülseydi bu ilk bakışta görülürdü.

### 8bn. İş sistemi EN SONDA kapanırsa, alt sistemler yıkılırken worker'lar hâlâ çalışıyordur
Üç giriş noktasında da (`teng_shutdown`, `demo_app`, `editor_app`) `jobs.shutdown()` **en sonda**ydı:
fizik, renderer ve Vulkan cihazı yok edilirken worker thread'leri hâlâ canlıydı. Jolt'un iş uyarlayıcısı
(`FiberJoltJobs`) bizim kuyruğa **çıplak `Job*`** itiyor ve o işaretçiler `FiberJoltJobs::jobs_` havuzunu
gösteriyor; `Physics::shutdown()` ise `delete impl_->jobs` ile o havuzu yok ediyor. Bir worker o sırada
kuyrukta kalmış bir girdiyi çekerse çöp bir işaretçiyi çağırır.

Ölçüldü (CI macOS/arm64, 2026-09-16): `thread: tulpar-job`, SIGSEGV, `fault_addr 0x8bc94512aa864210` —
**null değil, çöp**; null olsaydı sıradan bir deref hatası derdik. Dört koşumun ikisinde düştü, ikisinde
geçti: yarış. Yığın izi **iki çerçeveydi**, çünkü fiber yığını çözücüyü kesiyor — yani bu sınıfın izi
doğal olarak fakir, teşhis buna hazır olmalı.

Düzeltme sıra: `jobs.shutdown()` artık `vkDeviceWaitIdle`'dan hemen sonra, fizikten **önce**. O çağrı
worker'ları JOIN eder ve hiçbir fiber'in park halinde kalmadığını `ENGINE_ASSERT` ile doğrular, yani
sonrası tek thread'lidir. Kapanış yolunda iş ÜRETEN kimse yok (yıkım yalnız nesne serbest bırakıyor).

Üstüne nöbetçi: `~FiberJoltJobs` kuyrukta/çalışmakta iş varsa `ENGINE_ASSERT_MSG` ile **abort** eder.
Sessiz UAF yerine tam yerinde, adıyla patlar. Ateşlediği doğrulandı (sayacı elle bozunca çıkış 134 ve
"1 is hala kuyrukta/calisiyor"). `ENGINE_ASSERT` bu depoda Release'te de AÇIK — o yüzden sahada da geçerli.

**Genel kural:** bir alt sistem başka bir alt sisteme ham işaretçi veriyorsa, alan taraf VERENDEN önce
susturulmalı. "En sonda kapat" sezgisi burada tam tersi.

**Devamı — sözleşmeyi önce fazla dar yazdım (2026-09-17).** Yıkımda "hiç iş kalmamalı" diye assert koydum;
CI macOS onu **276 iş** ile düşürdü. Ama birikinti başlı başına hata değil: Jolt'un bariyeri beklerken
işleri kendi thread'inde de koşuyor, bizim kuyruk girdileri bayat ama refli kalıyor. Tehlike o girdilerin
**varlığı** değil, havuz öldükten sonra bir worker'ın onları **çekmesi**.

Doğru sözleşme ikili: iş sistemi **koşuyorsa** birikinti tükenene kadar bekle (worker'lar boşaltır);
**durmuşsa** girdiler atıldır ve beklemek kilitlenme olurdu. İkisi de yıkıcının içinde, yani doğruluk artık
çağıranın kapanış **sırasına bağlı değil** — sıra düzeltmesi ikinci hat olarak duruyor. Bekleme sınırlı:
sonsuz sessiz bekleme CI'da en kötü sonuç, sınıra dayanırsak adıyla patlıyoruz.

Bir de ölçü notu: birikinti **yerelde hiç üremedi** — 15 worker'da 0, zorla 2 worker'da bile 0. macOS/arm64
koşucusunda 276. "Yerelde üretemedim" bir düzeltmeyi geçersiz kılmaz ama sözleşmeyi ölçüyle değil
**muhakemeyle** yazdığını bilerek yazmayı gerektirir.

### 8bo. Kurulu sandığın sürücü hiç kurulmamıştı — "atlandı" sayısını iki platform arasında karşılaştır

macOS CI ayağı eklenince ilk koşumda kırmızı geldi: `MoltenVK_icd.json YOK`. Sebep, devralınan
reçetenin ICD json'ını `share/vulkan/icd.d/` altında aramasıydı. Homebrew formülü onu oraya
**hiç koymuyor** — son satırı `(prefix/"etc/vulkan").install "MoltenVK/icd" => "icd.d"`, yani
json `<keg>/etc/vulkan/icd.d/` altında (ölçüldü 2026-09-20, molten-vk 1.4.2).

Asıl tuzak yolun yanlış olması değil, **yanlışlığın görünmemesiydi**. Aynı reçete TulparLang'in
macOS işinde aylarca koştu; orada kurulum yumuşaktı (`|| echo "::warning::"`) ve ICD denetimi
yoktu. Sonuç zinciri: `VK_ICD_FILENAMES` var olmayan bir dosyayı gösterdi → loader hiçbir ICD
bulamadı → Vulkan cihazı yok → bütün RHI/renderer/içerik kapıları `ATLANDI` → iş **yeşil**.
"397 passed, 0 failed" satırı doğruydu ve GPU yolu hakkında hiçbir şey söylemiyordu.

Bunu görünür kılan tek şey **iki platformun atlama sayısını yan yana koymak** (aynı koşum,
2026-09-17):

```
macOS : 397 passed, 0 failed, 36 atlandi
linux : 397 passed, 0 failed,  7 atlandi
```

29 kapılık fark tek başına teşhisti. Tek platforma bakarken 36 sayısı masum görünür; ikisini
karşılaştırınca "bu platformda bir aile hiç koşmuyor" diye okunur.

Kural: bir sürücü/araç **kurulduğu iddia edilen** bir platformda, kurulumun başarısızlığı
uyarı değil **hata** olmalı ve yol bulunamadığında iş kırmızıya dönmeli. Ayrıca yolu sabit
yazma — ara ve bulamazsan `find` ile keg'in içini dök; formül yolu sürüm arası değiştirir.
Bkz. [[8s]] (debug messenger'sız "etkin" katman da aynı sınıf: yeşil ama ölçmüyor).

### 8bp. Bir baytlık taşma yerelde sessiz, CI'da SIGABRT — "yerelde geçti" bir kanıt değil

Birleştirilen editör işi yerelde **485/485, 0 düştü** verdi. Aynı ağaç Ubuntu CI'da çöktü:

```
RUN  multiedit_string_field_is_copied_whole
*** buffer overflow detected ***: terminated
COKME testi: multiedit_string_field_is_copied_whole (SIGABRT)
```

Sebep tek baytlıktı. Alan `char audio_clip[kSceneNameLen]` ve o sabitin yorumu
`// NUL dahil` diyor, yani kapasite 32 **bayt**, 32 karakter değil. Test
`"hedefin_kendi_uzun_dosya_adi.wav"` yazıyordu: 32 karakter + NUL = **33 bayt**.

Asıl tuzak taşmanın kendisi değil, **nerede göründüğü**. glibc'nin
`_FORTIFY_SOURCE` denetimi `strcpy`'ın hedef boyutunu derleyici çıkarabildiğinde
devreye giriyor; bu da derleyici sürümüne, optimizasyon düzeyine ve dağıtımın
varsayılan sertleştirmesine bağlı. Geliştirme makinesinde (CachyOS, GCC 16)
sessizce geçti, Ubuntu koşucusunda süreci öldürdü. Ve öldürdüğü için **özet
satırı hiç basılmadı** — paket "0 failed" bile diyemedi.

İki sonuç:

1. **"Yerelde 485/485 geçti" bellek güvenliği hakkında hiçbir şey söylemez.**
   Sabit boyutlu alanlara yazan kod için ölçüm ya sanitizer'la ya da gerçekten
   farklı bir dağıtımda yapılır.
2. **Uzunluk kısıtı çalışma zamanına bırakılmaz.** Düzeltme literali kısaltmakla
   bitmiyor; sınır artık derleme zamanında bağlı:

```cpp
static constexpr char kHedefAd[] = "hedefin_kendi_cok_uzun_adii.wav";
static_assert(sizeof kHedefAd <= content::kSceneNameLen, "...");
static_assert(sizeof kHedefAd > sizeof "cok_daha_uzun_bir_ad.wav", "...");
```

İkinci `static_assert` testin niyetini de kilitliyor: hedefin adı kaynaktan
**uzun** kalmalı, yoksa "kopya bütün mü" sorusu ölçülmez olur. Kapının boş
olmadığı pozitif kontrolle doğrulandı — eski literal `static assertion failed`
veriyor. Bir sonraki sefere çökme değil, derleme hatası olacak.

### 8bq. MinGW python'u stdout'a CRLF basar — yol adının içine giren CR dosyayı "yok" gösterir

Paketleme kapısı Linux ve macOS'ta yeşilken Windows'ta düştü:

```
##[error]kod 'assets/fonts/DejaVuSans.ttf
' varligini istiyor ama kaynak agacinda YOK.
```

Mesajın ortasındaki satır kayması hatanın **kendisiydi**: yol `DejaVuSans.ttf\r`
olmuştu. MSYS2/MinGW python'u `stdout`'u **metin kipinde** açıyor ve her `\n`'i
`\r\n` yapıyor; liste bash'e boru ile geçtiği için `read` satır sonundaki `\r`'yi
yolun parçası sayıyor, `[ -e ... ]` de var olan dosyayı bulamıyor.

Tuzağın asıl sinsiliği: dosya **oradaydı**. Kapı doğru çalışıyordu, yanlış olan
girdisiydi — ve hata mesajı CR'yi bastığı için ekranda "yol doğru görünüyor".

İki katmanlı savunma:

```bash
python3 - "$kok" <<'PY' | tr -d '\r'      # ikinci savunma: baska python da dusurmesin
...
try: sys.stdout.reconfigure(newline="\n") # kok sebep: cikti platformdan bagimsiz
except AttributeError: pass
```

Pozitif kontrolle doğrulandı: CRLF beslenince düzeltmesiz yol `$'\r'` taşıyor ve
dosya bulunamıyor; `tr -d '\r'` sonrası bulunuyor.

Genel kural: **python → bash boru hattı platformlar arası bir sınırdır.** Linux'ta
`\n`, MinGW'de `\r\n`. Liste taşıyan her boru ya kaynakta newline'ı sabitlemeli ya
da tüketicide `\r` süzmeli. `.gitattributes` bunu çözmez — sorun dosyalarda değil,
çalışma zamanındaki stdout çevirisinde.

### 8br. ImGui hatayı BASAR ve devam eder — basmak kapı değildir

`engine_editor` v0.1.0'da her karede şunu yazıyordu:

```
[01944] [imgui-error] In window 'Debug##Default': Calling End() too many times!
```

Sebep: #332 birleştirilirken dört maket panel (Sequencer, Arazi Fırçası, Girdi
Yöneticisi, Profiler) kaldırıldı ama birinin `ImGui::End()`'i ağaçta kaldı.
`Begin`siz `End`, ImGui yığınında bir seviye aşağı iner ve örtük
`Debug##Default` penceresini kapatmaya çalışır.

**Asıl tuzak hatanın kendisi değil, hiçbir şeyin kızarmamasıydı.** ImGui
"kurtarılabilir kullanıcı hatası"nı `stdout`'a basıp devam eder; ne derleme ne
test ne CI bunu görür. Editör testlerinin hepsi yeşildi, paket koşumu yeşildi,
ve hata o hâliyle **yayınlanmış bir sürüme** girdi. Ben bu satırı kendi
penceresiz koşumumda görüp geçmiştim — çıktıya bakmak, kapı kurmanın yerini
tutmuyor.

İki katmanlı kapı kuruldu (`ImGuiContext::ErrorCallback` bir sayaca bağlı):

* `engine_editor --headless` sayaç sıfır değilse **çıkış 1** döner.
* `editor_probe_render` HER sondada sayacı sıfırlar ve sonunda denetler — yani
  editör arayüzüne dokunan **her test** bu sınıfın kapısı olur.

Pozitif kontrolle doğrulandı: artık `End()` geri konunca penceresiz koşum
çıkış 1 / 5 hata (kare başına bir) veriyor, geri alınca 0.

İkinci, aynı ailedeki hata: konsol paneli `if (show_console) ImGui::End();`
diye korunuyordu, ama `&show_console` `p_open` olarak veriliyor — kullanıcı
pencerenin X'ine bastığında `Begin` bayrağı false yapar ve `End` o karede
**atlanır** (bu sefer ters yönde dengesizlik). Kural: `Begin` çağrıldıysa `End`
şarttır, dönüş değerinden ve bayrağın sonraki hâlinden **bağımsız** — bayrağı
`Begin`den önce oku.

### 8bs. Aynı süreçte tekrar tekrar `VkInstance` açmak glibc'nin static TLS havuzunu tüketir — hata "sürücü yok" gibi görünür

`tests/editor_probe.cpp` her sondada bir `rhi::Device` (dolayısıyla bir
`VkInstance`) açıp kapatıyor. Yükleyici `vkCreateInstance`'ta ICD'yi `dlopen`,
`vkDestroyInstance`'ta `dlclose` ediyor. NVIDIA ICD'si `libnvidia-tls.so`'yu
çekiyor ve o kütüphane **initial-exec TLS** kullanıyor: glibc'nin **sabit**
"static TLS surplus" havuzundan yer istiyor. glibc bu havuzu `dlclose`'da ancak
LIFO sırada geri alabiliyor, pratikte alamıyor.

Ölçüldü (2026-09-20, RTX 5080 / NVIDIA 615.71.09): `engine_tests editor`
**23 sonda** başarılı, **24.'sü** (dizin `#23`) düşüyor. Yükleyicinin dediği:

```
libnvidia-tls.so.615.71.09: cannot allocate memory in static TLS block
loader_icd_scan: Failed loading library associated with ICD JSON libGLX_nvidia.so.0
vkCreateInstance: Found no drivers!   -> VK_ERROR_INCOMPATIBLE_DRIVER
```

Tuzağın sinsiliği **hatanın adında**: `VK_ERROR_INCOMPATIBLE_DRIVER`, yani
"sürücü yok". Makinede çalışan bir GPU var; tükenen şey **süreç içi** bir
kaynak. Aynı nedenle **tam suite yeşildi**: orada başka bir test ICD'yi ayakta
tutuyordu ve tavan hiç görünmüyordu — yani "filtreli koşum kırmızı, tam koşum
yeşil" burada bir sıralama tuhaflığı değil, **ölçüm farkıydı**.

**Çözüm:** ICD'yi hiç `dlclose` ettirme. Süreç ömrü boyunca yaşayan tek bir
çıplak `VkInstance` (`pin_icd_once()`), yükleyicinin ICD'yi elinde tutmasını
sağlar; static TLS bir kez harcanır. Düzeltmeden sonra aynı koşumda **72
sondanın 72'si** açılıyor. Bağımsız pozitif kontrol: `GLIBC_TUNABLES=
glibc.rtld.optional_static_tls=262144` de aynı koşumu yeşile çeviriyor — yani
daralan kaynak gerçekten oydu.

**Genel kural:** `dlopen`/`dlclose` çevrimi ücretsiz değildir. Bir testin aynı
süreçte N kez açıp kapattığı her sürücü/eklenti için "N büyürse ne tükenir?"
sorusu sorulmalı; ve "sürücü yok" diyen bir hata, **sürücünün yokluğunun
kanıtı değildir**.

### 8bt. `CHECK` DÖNMEZ — düşen bir sonda kırmızı testi çekirdek dökümüne çevirir, üstelik "atlandı" diye yalan söyler

İki hata bir arada duruyordu:

1. `tests/editor_probe.cpp` hem `vk_api_load()` hem `dev.init()` düştüğünde
   `ProbeStatus::NoVulkan` dönüyordu. Çağrı yerleri de `if (st == NoVulkan)
   { skip("Vulkan yok"); return; }` yazıyordu. Sonuç: çalışan bir GPU'da
   `ATLANDI: Vulkan yok` — **gerçek bir hata, iyi huylu bir ortam atlaması
   gibi görünüyordu** (bkz. 8br).
2. `CHECK(editor_probe_render(p) == ProbeStatus::Ok);` başarısızlığı **kaydeder
   ama dönmez**. Sonda düştüğünde `p.pixels` `nullptr`'dır ve hemen ardından
   gelen `keep()`/`memcpy` SIGSEGV atar: `engine_tests editor` 139 ile ölüyor,
   sebep ekrandan siliniyor ve **koşumun geri kalanı hiç koşmuyordu**.

**Çözüm:** durumlar ayrıldı — `NoVulkan` (yükleyici yok), `NoDevice` (bu
süreçte hiç cihaz açılamadı → görünür atlama), `Exhausted` (önce açıldı, sonra
açılamaz oldu → **KIRMIZI**, çünkü bu ortam eksikliği değil tavandır), `Fail`.
Çağrı yerleri `PROBE_OR_RETURN(p)` / `probe_not_ok(st, p, __FILE__, __LINE__)`
ile "raporla ve DÖN" kalıbına geçti; `keep()` içine de bir ağ kondu (pikselsiz
sondada çekirdek dökümü değil kırmızı).

Pozitif kontrol kalıcı: `TULPAR_ENGINE_PROBE_LIMIT=n` n. sondadan sonrasını
kasıtlı düşürür (`n=0` → `NoDevice`/atlama, `n>0` → `Exhausted`/kırmızı).
Enjeksiyonsuz "artık çökmüyor" cümlesi ölçülmemiş bir iddiadır.

**Genel kural:** bir kapının ardından gelen kod o kapının sonucuna bağlıysa
kapı **dönmelidir**. Kırmızı bir test kırmızı kalmalı; çekirdek dökümü hem
sebebi hem de geri kalan testleri yutar.

### 8bu. Loader'ı atlayan yedek, katmanı da atlar — ve sessiz olduğu için "katman kurulu değil" gibi görünür

macOS CI'da beş doğrulama kapısı `ATLANDI: VK_LAYER_KHRONOS_validation yok`
diyordu. Paket **kuruluydu**, `VK_LAYER_PATH` **verilmişti**, `vulkaninfo`
katmanı **listeliyordu**. Yine de yoktu.

Sebep `rhi/vk_api.cpp` + `rhi/device.cpp`: loader MoltenVK ICD'si üzerinden
instance kuramayınca motor sessizce `vk_api_load_moltenvk_direct()`'e düşüp
loader'ı `dlclose` ediyor ve `libMoltenVK.dylib`'i doğrudan açıyor.
**Katmanlar bir loader mekanizmasıdır** — o yol seçildiğinde katman var
olamaz, `VK_LAYER_PATH` süreçte olmayan bir loader'a sesleniyor.

Asıl tuzak yedek değil, **sessizliği**. Log'da tek fark "katman yok" satırıydı,
ve o satır iki bambaşka durumu örtüyordu:

* katman kurulu değil → **ortam eksiği**, dürüst atlama
* loader atlandı → **motorun kendi yolu**, katman kurulmuş olsa bile ölçüm yok

Düzeltme üç parçalı: yedek artık `[rhi] loader ATLANDI` basıyor,
`DeviceCaps::loader_bypassed` bunu taşıyor, ve beş atlama mesajı sebebi
ayırıyor. CI kapısı da iddiasını değiştirdi — **"katman koşmalı" değil,
"ya katman koşar ya motor loader'ı atladığını söyler"**. Sessiz bozulma
(ikisi de yok) hâlâ kırmızı; ama bugün imkânsız olan talep edilmiyor.

Genel kural: **bir yedeğe düşmek ölçülebilir bir yolu ölçülemez bir yolla
takas etmektir.** Takas sessizse, sonraki her "yeşil" o takasın üzerine
kurulur. Yedek kendini bildirmeli ve bildirdiği şey `Caps`'e girmeli ki
kapılar onu okuyabilsin.

### 8bv. `ldd` `dlopen`'lanan kütüphaneyi GÖREMEZ — paket eksik çıktı ve kapı yeşil geçti

Kullanıcı yayınlanmış **v0.1.0** Windows zip'ini indirdi, `engine_editor.exe`'ye çift
tıkladı, program açılmadı. Wine altında aynı ikili tek satır basıyor:

```
pencere: GLFW yok (glfw3.dll): masaustu pencere acilamaz
```

Sebep paketleyicideydi. `tools/package.sh` Windows DLL'lerini `ldd` çıktısından
süzüyordu ve **`ldd` tanımı gereği yalnız ithalat tablosunu okur.** GLFW ise link
edilmez, `platform/window.cpp` içinde `dl_open("glfw3.dll")` ile *çalışma anında*
yüklenir — ithalat tablosunda hiç yoktur. Üç MinGW DLL'i pakete girdi, GLFW girmedi.
Paket kapısı da "en az bir `.dll` var mı" diye baktığı için **yeşil geçti**: kapı
gerçekten var olan bir şeyi ölçüyordu, ama eksik olan şeyi değil.

Üç ayrı ders var, üçü de ayrı ayrı tekrarlanabilir:

1. **İki bağımlılık sınıfı vardır ve araçları farklıdır.** LINK edilenler ithalat
   tablosundan (`ldd`/`objdump -p`) bulunur; `dlopen` edilenler **yalnız kaynaktan**
   bulunur. Bir aracın gördüğü şeyi "bağımlılıkların hepsi" sanmak, aracın kör
   noktasını sessiz bir boşluğa çevirir.
2. **İkinci liste kayar.** Çözüm `package.sh`'a elle bir `glfw3.dll` satırı eklemek
   değildi: liste artık kaynaktaki her `dl_open(...)` çağrısından türetiliyor ve
   politikayı da kaynaktan okuyor (`// PAKET: gomulu|sistem|turetilmis`, ad dizisinin
   üstünde). **İşaretsiz bir `dl_open` çağrısı HATADIR** — yeni bir çalışma zamanı
   bağımlılığı eklendiğinde karar vermeye zorlar, varsayılana düşmez.
3. **Konsol uygulaması hemen çıkarsa görünmez.** Hata stderr'e basılıyordu; çift
   tıklayan kullanıcıda konsol programla birlikte kapandığı için ekranda *hiçbir şey*
   olmuyordu. Artık aynı satır `<ikili dizini>/engine_hata.log` dosyasına da yazılıyor
   (`platform/startup_report.cpp`) ve pakette `BASLAT-*.bat` başlatıcıları var —
   çıkış kodu 0 değilse `pause` ile bekliyorlar. Hata **susturulmuyor**, görünür hale
   getiriliyor.

Neyin paketlendiği de bir karar: **GLFW gömülü** (Windows/macOS'ta sistemde yoktur;
Linux'ta sistemdeki kazanır, paketteki yedektir — ikili `<exe dizini>/<ad>` yolunu
*mutlak* olarak `dlopen` ettiği için rpath gerekmez), **Vulkan loader sistem**
(loader yalnız yönlendiricidir, çizen ICD'dir ve loader'ı da sürücü kurulumu getirir;
yanımızda taşımak sürücüsüz makinede hiçbir şeyi çalıştırmaz, sürücülü makinede ise
daha yeni olan sistem loader'ını gölgeler).

Pozitif kontrol (kapının gerçekten ölçtüğünün kanıtı): yeni kapı **yayınlanmış v0.1.0
paketine** doğrultulduğunda kırmızı döner — `EKSIK dlopen kutuphanesi
[platform/window.cpp:64]: adaylarin hicbiri pakette yok -> glfw3.dll libglfw3.dll
glfw.dll`. Eski kapı aynı pakete "var *.dll (3 adet)" diyordu.

### 8bw. Sayaç ilerliyor, iş yapılmıyor — editörün F5'i fiziği hiç adımlamıyordu

**Belirti** (2026-09-24, kullanıcı): editörde F5 → "ekranda hiçbir hareketlenme yok".
Durum çubuğunda `tick` artıyordu, "Durdur" düğmesi görünüyordu, duraklatma kapısı
yeşildi.

**Sebep:** editör `DemoScene`'i demo içeriği OLMADAN kuruyor (`init(..., with_content
= false)`) ve `init` o dalda zamanlayıcıyı kurmadan dönüyordu. `DemoScene::tick` ise
her şeyi zamanlayıcıya bırakıyordu: `sched_.run` sıfır aşamalı bir zamanlayıcıyla
**hiçbir şey yapmadan** döndü. Fizik adımı (`demo_sys_phys`) yalnız demo içeriğiyle
kayıtlıydı. Editörün tick sayacı ise döngüde AYRI artıyordu — yani "oynatma çalışıyor"
izlenimini veren tek şey kendi sayacıydı. Hata motor deposuna taşınırken (#4)
girmişti ve F5'e karakter eklenene kadar (#40) kimse fark etmedi, çünkü...

**...kapı SAYACI ölçüyordu:** penceresiz "duraklatma kapısı" `tick_i`'nin duraklamada
durup F10'da bir arttığını doğruluyordu. Sayaç fizikten bağımsız arttığı için kapı,
fizik hiç koşmazken yeşil yandı.

**Düzeltme:** editör kipinde `tick` fiziği doğrudan adımlar. Kapı artık bir dinamik
gövdenin **konumunu** da ölçüyor (duraklamada y sabit, tek adımda değişiyor) ve yeni bir
"durdur" kapısı var: oynarken gövde yazar konumundan ayrıldı mı, durdurunca yazar
konumuna döndü mü, yeniden oynatınca baştan mı başladı.

**Pozitif kontrol:** yeni kapı eski `tick` ile (fizik dalı kapatılarak) **kırmızı**:
`govde kup_dusen y 6.0000 -> 6.0000 (sabit evet) -> 6.0000 (adimda degisti HAYIR) HATA`.
Eski kapı aynı durumda "OK" diyordu.

**Ders:** bir sayaç, ölçülmek istenen işin YAN ÜRÜNÜ değilse kanıt değildir. "Kaç kez
çağrıldı" yerine "çağrı dünyada neyi değiştirdi"yi ölç — burada: gövdenin y'si.

### 8bx. Ebeveyni öldürmek çocuğu öldürmez — "Durdur" oyun penceresini kapatmıyordu

**Belirti** (2026-09-24, ölçüldü): editörün "Oyunu çalıştır"ı (Ctrl+F5) durdurulunca
Konsol "oyun durduruluyor" diyor, süreç bitiyor, ama oyun penceresi açık kalıyor.

**Sebep:** editör `tulpar oyun.tpr`'yi başlatıyor; `tulpar` oyunu derleyip **kendi
çocuğu** olarak koşturuyor (`/tmp/.tulpar_run.<pid>`). `process_kill` yalnız doğrudan
çocuğa, yani derleyiciye SIGTERM gönderiyordu. Ölçüm: derleyicinin pid'ine SIGTERM →
derleyici öldü, oyun init'e devredildi (ebeveyn 1942) ve çalışmaya devam etti. Editör
"süreç bitti" gördü, çünkü izlediği süreç gerçekten bitmişti. Windows'ta
`TerminateProcess` aynı şekilde yalnız `tulpar.exe`'yi alır.

**Düzeltme:** `platform::process_start` her (ayrık olmayan) çocuğu bir **ağacın** kökü
yapar ve `process_kill` ağacı alır. POSIX'te çocuk kendi süreç grubunun lideri
(`setpgid`), kill gruba (`kill(-pid)`) gider. Windows'ta çocuk askıda başlatılır, bir
iş nesnesine (Job, `KILL_ON_JOB_CLOSE`) alınır, sonra yürütülür; kill işi sonlandırır.
Bedeli: terminaldeki Ctrl+C artık oyuna gitmez (başka grup). Editörün gömülü oyunu
(F5) bunu kalp atışıyla kapatır: editör susarsa oyun 5 s içinde kendini kapatır.

**Pozitif kontrol:** `process_kill_takes_the_whole_tree`. Kobay çocuk, derleyicinin
yaptığı gibi **aynı grupta** bir torun başlatır. Grup kill'i kapatılınca kapı kırmızı:
`torun ...: kill oncesi canli, cocuk cikis 143, torun HALA CALISIYOR (5000 ms)`.

**Ders:** "süreç bitti" izlenen sürecin bittiğini söyler, işin bittiğini değil. Başka
bir programı başlatan bir program başlatıyorsan, öldürdüğün şey o programın kendisi
değil, **ağacı** olmalı.

### 8by. Kare ortasında bırakılan ImGui dokusu — cihaz kaybı (`VK_ERROR_DEVICE_LOST`)

**Belirti** (2026-09-24, penceresiz kapı): gömülü oynatma kapısı OK dedi, bir kare
sonra editör `kare: gonderim: vkWaitForFences (VK_ERROR_DEVICE_LOST)` ile düştü.

**Sebep:** Oyun sekmesinin dokusu (`EditorGameView`), oyun kapanınca **kare ortasında**,
yani panel çizildikten sonra bırakılıyordu. O karenin ImGui çizim listesi dokunun
descriptor'ına zaten başvurmuştu. Liste kaydedilince GPU yok edilmiş bir görünümü
örnekledi. Aynı tehlike ilk karede de vardı: doku ilk kare gelince, panelden sonra
kuruluyordu. Ölçü değişseydi eskisini yine kare ortasında yıkacaktı.

**Düzeltme:** doku yalnız **kare başında** kurulur ve bırakılır (poll'dan hemen sonra,
`ui.begin_frame`'den önce). O an bu karenin hiçbir çizimi kurulmamıştır. Kare içindeki
yol (`oyun_kare_hazirla`) yalnız kopyalar, hiçbir şey ayırmaz ya da yıkmaz.

**Ders:** ImGui'ye verilen bir dokunun ömrü **çizim listesinin** ömrüdür, panel
kodunun değil. "Artık gösterilmiyor" bir sonraki kareden itibaren doğrudur; bu karenin
listesi onu hâlâ tutuyor.

### 8bz. Oyun çalışıyor, harita görünmüyor — depo ayrılırken kaynaklar geride kaldı

**Belirti** (2026-09-24, kullanıcı): editörde `salon1.sahne` açılıp F5'le oynatılınca
"haritayı göremiyorum, yalnız düşman küpler var". Zemin, duvarlar ve sütunlar yok, ama
oyuncu duvarlardan geçemiyor.

**Sebep:** motor TulparLang'dan ayrılırken (#1) `examples/assets/` altından yalnız `.sahne`
dosyaları taşındı. Sahnelerin kaynağı `checker_cube.gltf` ve arena oyununun
`sesler/altin.wav`'ı derleyici deposunda kaldı. Dört örnek sahnenin (arena, salon1,
salon2, sıcak_küçük) bütün geometrisi o tek modeli kullanıyor. Model yüklenemeyince
çalışma zamanı o varlıkları **hiç çizmedi**, gövdeleri ise doğdu. Çarpışma çalıştığı için
oyun "çalışıyor" göründü. Üç şey hatayı gizledi:
- Editör modelsiz gövdeyi gri çarpışma kutusu olarak çiziyor. Harita editörde yerinde
  görünüyordu.
- Köprünün `HATA sahne: 1 kaynak yuklenemedi` satırı editörün Konsol'una **bilgi**
  rengiyle düşüyordu, hata sayacı 0 kaldı.
- Editörün penceresiz "kaynak tarayıcı" kapısı örnek dizinde glTF olmadığı için ATLANDI
  diyordu. O kapı da kaynaksız sahnede Ctrl+Z'nin geride kimsenin kullanmadığı bir
  `kaynak` satırı bıraktığını (tablo günlüğün dışında büyüyordu) hiç ölçmemişti.

**Düzeltme:** kaynaklar geri geldi. `checker_cube.gltf`'yi `tools/make_test_gltf.py` artık
iki yere birden yazıyor, kopya elle tutulmuyor. (Aynı gün: örneklerin kopyası merkez
pivotlu `dama_kup.gltf` oldu, bkz. 8ca.) Oyunun çıktı satırlarının düzeyi satırın
kendisinden okunuyor (HATA kırmızı, sayaca girer). Kaynak tablosuna ekleme günlüğe girdi
(`SceneHistory::add_asset`): geri al, sahneyi bayt bayt eklemeden önceki hâline döndürüyor.

**Pozitif kontrol:** `editor_game_example_scenes_and_games_find_their_assets` kaynaklar
geri gelmeden kırmızı: 4 sahnede `checker_cube.gltf yok`, 2 oyunda
`examples/assets/sesler/altin.wav yok`. Kaynak tarayıcı kapısı eski ekleme yoluyla
`betik_dagitimi.sahne`'de `geri al -> baslangic baytlari HAYIR HATA` dedi.

**Ders:** bir depoyu bölerken taşınan dosyanın **bağımlılıklarını** da say: sahne bir
dosyadır ama kaynak tablosu başka dosyalara işaret eder. "Yüklendi" demek "çizildi"
demek değildir. Ölçü, kaynak sayacı (`kaynak 0/1`) olmalıydı.

### 8ca. Pivot kayması — karakterler "yerin içinde", duvarlar havada

**Belirti** (2026-09-24, kullanıcı; 8bz'nin düzeltmesinden hemen sonra): harita görünür
oldu, ama "yerin içine girmiş ana karakter ve düşman karakterler".

**Sebep:** sahneler gövdeyi varlığın konumunda **merkezli** kurar (`govde kutu 0.5 0.5 0.5`,
ölçekle çarpılır). Model ise kendi pivotuyla çizilir. `checker_cube.gltf`'in düğümü y'de
+0.5 ötelenmiş: test ve demo için küp y=0'da yere otursun diye. Sahnede her model
çarpışma kutusunun `ölçek.y × 0.5` **üstünde** çizildi. Zeminin görünen yüzü fiziğinkinden
0.5 m yukarıdaydı ve karakterler fiziğin zemininde, yani görünen zeminin 0.5 m içinde
duruyordu. Duvarlar 1.25 m, sütunlar 1.5 m havadaydı. Ölçüldü: model ve kutu gövdeli 32
varlığın 32'si kayıktı. Harita görünmezken (8bz) bu kayma da görünmüyordu: tek hata
ötekini örtüyordu.

**Düzeltme:** örnek sahneler aynı mesh ve dokuyla ama **merkez pivotlu** bir küp kullanıyor
(`dama_kup.gltf`). Onu da `tools/make_test_gltf.py` üretiyor. Test küpü (`tests/assets`)
değişmedi, testleri ve demo ona göre kurulu.

**Pozitif kontrol:** `editor_game_example_scene_models_sit_on_their_colliders`. Model ve
kutu gövdeli her varlıkta modelin sınır kutusu, varlık uzayında gövdenin kutusuyla
örtüşmeli. Eski küple kırmızı: `32 tanesinde ... kayik, en kotu 1.50 m:
salon1.sahne/sutun_kb (model merkezi y +0.50, olcek y 3.00)`. Yeni küple 0.
`engine_aksiyon.tpr` 3200 karelik belirlenimli özeti bayt bayt aynı kaldı: fizik hiç
değişmedi, yalnız görüntü fiziğe oturdu.

**Ders:** görünen şey ile çarpışan şey iki ayrı veridir ve ancak **ikisi birlikte**
ölçülürse örtüştükleri bilinir. Bir modelin pivotu, onu kullanan her sahnenin sözleşmesidir.

### 8cb. Doğrulama kapısı kullanıcının dosyasını, kendi bozduğu sahneden yeniden yazıyordu

**Belirti** (2026-09-24, bir ajanın ölçümüyle): editörün derlediği `salon1.sahneb`
8112 bayt ve 27 navmesh poligonuydu. `engine_sahnec`'in aynı `.sahne`'den derlediği ise
8160 bayt ve 28 poligon. Döküm farkı gösterdi: diskteki blob'da zemin (0, -0.5, 0) yerine
(2.81, -1.53, 0.56) konumundaydı. `betik_dagitimi.sahneb` de aynı şekilde bozuktu. Oyunu
doğrudan çalıştıran biri zemini bir metre aşağıda görürdü. Editörün F5'i sahneyi her seferinde
yeniden derlediği için editörde hiçbir şey görünmüyordu.

**Sebep:** penceresiz kapılar sırayla aynı sahne üzerinde çalışıyor ve hepsi yaptığını geri
almıyor. Gizmo sürükleme kapısı varlık 0'ı sürükleyip bırakıyor. Gömülü oynatma kapısı (#43)
sonra F5'i çağırıyor. F5 bellekteki sahneyi, yani **kapıların değiştirdiği** sahneyi, oyunun
okuduğu diskteki `.sahneb`'e derledi. Blob türetilmiş ve gitignore'lu olduğu için hiçbir git
farkı bunu göstermedi.

**Düzeltme:** gömülü oynatma kapısı başlamadan önce sahneyi dosyadan yeniden okuyor. Artık
editörün derlediği blob ile `engine_sahnec`'inki arasında yalnız bilinçli fark kalıyor:
editör kaynak ölçümü yapmıyor. Navmesh aynı: 28 poligon, 4344 bayt. Bozuk blob'lar yeniden
derlendi.

**Ders:** bir kapı kullanıcının dosyasına yazıyorsa girdisi kullanıcının verisi olmalı,
önceki kapıların artığı değil. "Türetilmiş dosya" demek "önemsiz dosya" demek değil: oyun onu
okuyor.

### 8cc. Çarpışma halkasının SIRASI koşumdan koşuma değişir — kümesi değişmez

**Belirti** (2026-09-24, kodla betik bağlama işi sırasında, masaüstü 16 iş parçacığı): 48
küre aynı adımda zemine düşüyor, oyun kuyruğu indis sırasıyla okuyor
(`carpisma_a(i)`/`carpisma_b(i)`). 8 koşumda **8 farklı** sıra; aynı listeler sıralanınca
**1** küme. Olayların kendisi belirlenimli, sırası değil. Hiçbir kapı kızarmıyordu: iki
koşumun `[kapi]` satırı yalnız sıraya bağlı mantık varsa ayrışır (iki düşman aynı karede
oyuncuya çarpar ve "ilk işlenen öldürdü"; bir çarpışma kancası ötekinin varlığını siler).

**Sebep:** `ContactRing::OnContactAdded` Jolt'un iş parçacıklarından çağrılıyor ve yuvayı
`fetch_add` ile alıyor: halka olayların **geliş** sırası. Tetik olayları bu yüzden
`teng_frame_end`'de (adım, sensör, diğer) ile sıralanıyordu; temaslar hiç sıralanmıyordu.
`JPH_CROSS_PLATFORM_DETERMINISTIC` simülasyonu belirlenimli yapar, geri çağrım sırasını değil.

**Düzeltme (kodla bağlanan betikler):** `bound_fire_carpisma` kanca çağrılarını toplar ve
(yuva, karşı gövde, nokta, şiddet) ile sıralayıp öyle çağırır; `olay` yine halka indisi.
Ölçüldü: aynı 48 küreye `betik_ata(k, "sonda")` ile bağlı `sonda_carpisma` kancasının çağrı
izi (sıra özeti) 8 koşumda **1** farklı, aynı karelerin halka izi **8/8** farklı.

**Bilerek değişmeyen:** sahne `<ad>_carpisma` kancaları ve kuyruğu indis sırasıyla okuyan oyun
kodu hâlâ halka sırasını görür (sahne kancalarının davranışı o işte değişmesin istendi).
Onları da sıralamak ayrı bir davranış değişikliği. O güne kadar **kural:** halkayı sıraya bağlı
mantıkla okuma; gerekiyorsa önce kendin sırala (ör. köprü id'siyle).

**Pozitif kontrol:** `bridge_runs_a_scripted_game_headless` 10d — 32 küre aynı adımda iner,
bağlama sırası yuva sırasının TERSİ; kancalar yuva sırasıyla gelmeli (`yuva sirasini bozan 0`).
Aynı karelerin halka sırası bilgi satırında basılır. Koşumlar arası ölçüm tek süreçte
yapılamadığı için sonda Tulpar'da: 48 `kure` + `betik_ata`, kanca içinde
`iz = (iz * 31 + yuva + 1) % 1000000007`, ana döngüde halka için aynısı; 8 kez koştur,
farklı izleri say.

**Ders:** "iş parçacığından yazılan halka" belirlenimli bir simülasyonun içinde bile
belirlenimli değildir. Sıra bir sözleşmeyse onu tüketici **kurar** (sıralar); üreticinin
geliş sırasına güvenen her okuyucu bu tuzağa açıktır.

### 8cd. `AllocGate` "kare içi new 0" diyordu, süreç kare başına 330 KB büyüyordu — sürücü belleği bizim sayacımızdan geçmez

**Belirti** (2026-09-25, RTX 5080, sürücü 615.71.09, Linux; doğrulama katmanı kurulu
değil): `engine_demo --headless N --scene tulpar/examples/assets/salon1.sahneb` kare başına
~327 KB büyüyordu — tepe RSS 300 karede 345 MB, 1500 karede 738 MB. Büyüme
`/proc/<pid>/smaps`'ta `/dev/nvidiactl` eşlemelerindeydi (4 s'de 64 eşleme / 123 MB → 20 s'de
150 / 300 MB); `[heap]` ve anonim bellek düzdü. Köprüde (`TULPAR_ENGINE_HEADLESS`) aynı sınıf:
boş oyun döngüsü 62 KB/kare, 50 kutu 138, 200 kutu 360 KB/kare — çizim başına ~1.5 KB. Demo
aynı satırda `kare ici new (en cok) 0` basıyordu ve hiçbir kapı kırmızı değildi.

**Sebep:** `Device::begin_one_shot()` her çağrıda `vkAllocateCommandBuffers` yapıyor,
`end_one_shot_and_wait()` tamponu hiç bırakmıyordu (`vkFreeCommandBuffers` yok, havuz hiç
sıfırlanmıyor). Penceresiz her kare `offscreen_render_custom` ile bir tek seferlik tampon
kaydediyor; kare başına bir kare dolusu komut (gölge kademeleri + ana geçiş; çizim başına
bağlama, push constant, draw) sürücünün komut belleğinde kalıyordu. Sayaçla ölçüldü: 294
kararlı karede `CommandBuffer +294/-0`, başka hiçbir tür sıfırdan farklı değil. Yükleme
yolları da (`create_mesh`, `update_mesh_vertices`, `create_texture*`, `read_motion`) çağrı
başına bir tampon sızdırıyordu — bu kısım pencereli kipte de geçerliydi.

**Neden görünmedi — iki maskeleme:**
1. AllocGate *bizim* `operator new`'umuzu sayar. Sürücü belleğini kendi `mmap`/ioctl'üyle
   alır, bizim ayırıcımıza hiç uğramaz. "Kare içinde 0 ayırma" yalnız CPU yığınımız için
   doğruydu.
2. Pencereli yol (`Swapchain::acquire`) ön-ayrılmış iki tamponu `vkResetCommandBuffer` ile
   yeniden kullanıyor, yani geliştiricinin pencerede baktığı yol temizdi. Sızan yol
   penceresiz doğrulama, `engine_tests` ve **editörün F5'i** (gömülü oyun: köprü headless
   yoldan çizip kareyi kanala yazıyor; oynandığı sürece büyür).

**Düzeltme:** tek seferlik tamponlar `init_device`'ta bir kez ayrılıyor
(`Device::kOneShotSlots = 4`), her kullanımda `vkResetCommandBuffer` + yeniden kayıt. İç içe
kullanım kapasiteye kadar çalışıyor; yuva kalmazsa `VK_NULL_HANDLE` dönüyor ve
`one_shot_exhausted()` sayıyor (sessiz büyüme yok). Bekleme zaman aşımına uğrarsa yuva meşgul
kalıyor: GPU'da bekleyen tamponu yeniden kaydetmek tanımsız davranış olurdu. Sonra, aynı
komut: RSS 4 s'den 20 s'ye 248 820 kB sabit, nvidiactl 37 eşleme / 77 MB sabit. Köprü, 200
kutu: 6–18 s arası 288 308 kB sabit. Görüntü özeti değişmedi (`a32dcdbd870d8766`).

**Kapı:** `rhi/vk_api.cpp` `vk_counters_*`. VkApi tablosundaki bütün `vkCreate*`/`vkAllocate*`
ve `vkDestroy*`/`vkFree*` giriş noktaları ince bir ara yordamla sarılıyor ve cihaz başına
sayılıyor (8an gereği her yuva kendi gerçek yordamlarını tutar). `Device::init_device` takar;
takılamazsa cihaz açılmaz. `tests/test_vk_steady.cpp`: 8 kare ısınma + 32 kare, varsayılan ve
post+huzme yolunda her türün kurma **ve** bırakma farkı 0 olmalı. Pozitif kontrol aynı
ölçümle kasıtlı olarak kare başına bırakılmayan bir komut tamponu ve bir fence kur-yık çifti
yapıyor; kapı `CommandBuffer +32/-0 Fence +32/-32` diye tam sayıyor. Eski `begin_one_shot`
geri konunca kapı iki yolda da kırmızı döndü (`CommandBuffer +32/-0`). Çalışma anında
görünür hali: demo satırında `kare ici vk kurma (en cok) N`, köprü kapanışında
`kapanis (vk nesne, kare 6..N)`.

**Kör noktalar (bilerek söyleniyor):** ImGui (editör) Vulkan'ı kendi tablosundan
(`vkGetInstanceProcAddr`) çağırıyor; sayaç onu görmez. Sürücünün `vkCreate*`'e bağlı olmayan
iç havuz büyümesi de sayılmaz. Kapı *bizim kodun* ne yaptığını ölçer (kesin). Sürücünün ne
yaptığı RSS/smaps ile ölçülür ve iddia edilmez.

**Ders:** "kare içinde 0 ayırma" derken hangi ayırıcıyı kastettiğini söyle. Bir sayaç yalnız
kendi geçtiği yolu görür; sahibi başkası olan bellek (sürücü, GPU, dosya eşlemesi) için ya
ayrı bir sayaç ya da doğrudan süreç ölçümü (RSS, smaps) gerekir. Penceresiz doğrulama yolu da
pencereli yolun "aynısı" değildir: kaynak yaşam döngüleri farklıysa o farkı ayrıca kapıla.
