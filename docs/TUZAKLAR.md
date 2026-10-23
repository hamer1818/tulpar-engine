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

> **Güncel (2026-09-27):** başlığın ilk yarısı ARTIK DOĞRU DEĞİL — `<<`, `>>`, `&`, `|`, `^`, `~` ve
> `0x`/`0b` literalleri TulparLang #323'ten (a025b44) beri var (ölçüldü: `(200 << 24) | 0xE63946FF`
> derleniyor). Kalan ders geçerli: gömülü kütüphanedeki hata `(stdin):N` diye çıkar, ve `log`
> matematik fonksiyonudur.
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

> **Not (2026-09-27):** `tools/layout_audit.py` hiçbir otomasyona (CMake kapıları, CI, `derle.sh`)
> bağlı değil — elle koşulur. Ve başka bir `glslc` sürümüyle koşulunca, yerleşim sonucu "0
> UYUŞMAZLIK" iken `*_spv.h` tazelik alt denetimi 23 shader'ın 23'ünü "BAYAT" diye KIRMIZI basıp
> çıkış 1 veriyordu (üreteç kimliği farkı, bkz. `shader_check.py`). Artık üreteç tutmazsa bayt
> karşılaştırması KOŞMADI deyip araç gerektirmeyen özet kapısına düşüyor.

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

> **Güncel (2026-09-27, TulparLang #350):** artık `cmake --build` YETER — her gömülü `lib/*.tpr`
> `CMAKE_CONFIGURE_DEPENDS`'te, değişince cmake kendiliğinden yeniden koşuyor. Derleyici deposunun
> `build.sh suites`'i ilk adımda `tests/gomulu_stdlib_tazelik.py` ile ikilinin bu ağacın lib
> dosyalarını AYNEN taşıdığını ölçüyor. Ders (kontrolün ürüne girdiğini kanıtla) aynen geçerli.

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

**Aynı sınıf, üçüncü ayırıcı: dilin çalışma zamanı** (2026-09-25, RTX 5080 + Ryzen 7
9800X3D). Betik kancaları `TengScriptVm::call(ad, ...)` ile çağrılıyordu; Tulpar tarafı
(`eng_script_call`) adı HER çağrıda bir `ObjString`'e kopyalıyor ve o dizgi hiç
sıfırlanmayan AOT dizgi arenasında kalıyordu. 200 boş `guncelle` kancası: kare başına
**+14.4 KB** kalıcı (çağrı başına 72 bayt), `AllocGate` ve Vulkan sayacı ikisi de 0 —
bellek dilin `malloc`'undan geçiyordu. Kare belleği (8ch) kare İÇİNDEKİ kancanın dizgisini
kare sonunda geri sarıyor; kare dışındaki kanca (yüklemede `baslat`) ve `TULPAR_KARE_BELLEK=0`
için kalıcıydı. Düzeltme: kanca yüklemede bir kez çözülüyor (`resolve`), kare içinde adsız
çağrılıyor (`invoke`); aynı ölçümde (kare belleği kapalı) kancaya düşen büyüme 0.
Ölçü `tools/kanca_olcumu.py`. **Ölçümün kendi tuzağı:** eğim 300. kareden başlatılınca
kancasız koşum da +1.4 KB/kare "büyüyordu" — köprü profiler'inin 600 karelik halkası ilk
dokunuşta RSS'e giriyor (kare 500..3000 smaps farkı yalnız +192 kB, anonim bellek düz).
Isınma 700 kare ve **kancasız kontrol koşumu** olmadan kalan küçük bir eğim "hâlâ
sızıyor" diye okunurdu.

### 8ce. Derleyici bayrağı bir derleyicide hata, ötekinde "bilinmeyen uyarı" — kapı yalnız bir platformda ölçer

**Belirti** (2026-09-25, E3 nesne özellikleri): `SceneDesc` 213 KB'tan 378 KB'a büyüyecekti ve
editörün "sahne aç" lambdası onu **yığına** koyuyordu (`content::SceneDesc nd;`). Linux'un 8 MB
ana yığını bunu sindirir; Windows'un 1 MB'lık yığını iki tanesini üst üste görünce çöker — ve
yalnız o kullanıcıda. Kapı olarak `-Werror=frame-larger-than=131072` eklendi. GCC 16.2'de
200 KB'lık çerçeve **hata** verdi. Clang 22.1'de aynı bayrak `warning: unknown warning option
'-Werror=frame-larger-than=131072'` diyip **hiçbir şey ölçmedi**: macOS ayağı (AppleClang)
yeşil kalırdı. Clang'ın yazımı (`-Wframe-larger-than=N -Werror=frame-larger-than`) ise GCC'de
"böyle bir seçenek yok" diye **reddediliyor**.

**Düzeltme:** CMake derleyiciye göre yazımı seçer ve yapılandırmada **pozitif kontrol** koşar:
seçilen bayraklarla 200 KB'lık çerçeve DERLENMEMELİ, 1 KB'lık derlenmeli; ikisinden biri
tutmazsa `FATAL_ERROR`. Ölçüldü: Clang'a GCC yazımı zorlanınca yapılandırma "200 KB'lık çerçeve
DERLENDİ" diye durdu; GCC'ye Clang yazımı zorlanınca "1 KB'lık çerçeve DERLENMEDİ" diye durdu.

**Kapının ilk yakaladıkları** (E3 öncesi kod, GCC 16.2 Release): editör `nd` lambdası 213 392 B,
"yeni sahne" lambdası 213 152 B, `editor_run` 342 672 B (lambdalar içine gömülünce),
`rhi/device.cpp` uzantı listesi 134 704 B (512 × `VkExtensionProperties`), beş test
`SceneDesc`'i yığında tutuyordu, bir test iki dosya diyaloğu bağlamını (141 344 B). Hepsi
statik yapıldı. `SceneDesc` çıkınca `editor_run` 131 520 B'da kaldı: altı adet 16 KB'lık
yordamsal doku tamponu aynı blokta, derleyici yuvalarını paylaştıramıyor (yerel değişkenler
`-fdump-tree-optimized` ile tek tek sayıldı) — onlar da statik.

**Ders:** `-Werror=<x>` yazan bir kapı, **her** CI derleyicisinde bir kez kırmızıya
düşürülmeden kurulmuş sayılmaz. "Bilinmeyen uyarı seçeneği" bir uyarıdır, hata değil: bayrak
sessizce hiçbir şey yapmaz ve derleme yeşil geçer.

### 8cf. Satır başını kalıpla arayan kapı, kalıba uymayan satırı GÖRMEZ — ve görmediğini söylemez

**Belirti** (2026-09-25): `tools/scene_check.py` yazıcı satırlarını `o.puts("  kw ")` kalıbıyla
arıyordu. Tırnakla başlayan operandlı satırlar (`o.puts("  betik \"")`, `o.puts("  ses \"")`)
kalıba uymuyor, `o.puts(lt)` gibi değişkenle kapanan satır ise kapanmıyor ve bir sonrakiyle
birleşiyordu. Ölçüldü, aynı (E3 öncesi) `scene.cpp` üzerinde: eski kapı `write_entity`'de 35
satır görüyordu, doğrusu 38 — `betik`, `ses`, `spot_koni` **hiç** ölçülmüyordu; `isik`
`spot_koni`'yi yutup 9 jeton sayılıyordu (gerçeği 7). Üçü de ayrıştırıcıyla uyuşuyordu: kapı
şansla yeşildi. E3'ün `ozellik_*` satırları da tırnaklı ad taşıyor.

**Düzeltme:** kapı artık yazıcının çıktı akışını **benzetiyor** (dizgi kaçışları çözülür, tırnak
açık/kapalı bir durum, değişken gövdedeki atamalarından çözülür) ve satırları ayrıştırıcının
`split()` kuralıyla jetonluyor. Çözülemeyen ifade tırnak dışındaysa KIRMIZI (sessizce 0 jeton
saymak kapıyı kör ederdi). `--oz-sinama` modu CMake'te her derlemede önce koşar: tırnaklı
operandlı bozuk fikstür (yazıcı 2 jeton, ayrıştırıcı `n != 3`) yakalanmalı, doğrusu geçmeli,
aynı satır eski kalıpla eşleşmemeli (sınamanın yeni bir şey ölçtüğünün kanıtı).

**Ders:** "0 uyuşmazlık" bir ölçüm değil, ölçülen satır sayısıyla birlikte okunur. Kapı
kaç şey gördüğünü basmalı ve gördüklerinin kümesi elle doğrulanmalı.

### 8cg. Yükleme ortasında çağrılan kanca, kendisinden SONRA kurulan veriyi "yok" görür — hata değil, varsayılan

**Belirti** (2026-09-25, E4): `teng_scene_load` betiklerin `<ad>_baslat` kancalarını sahne
kurulumunun **ortasında** çağırıyor; navmesh sorgu nesnesi ise kanca döngüsünün **sonunda**
kuruluyordu. Navmesh'li bir sahnede `baslat` içinde `nav_var()` **false** döndü, yüklemeden hemen
sonra **true** (ölçüldü: sabit zeminli iki varlıklı sahne, motoru tanıyan derleyiciyle). Hata
sayacı artmadı — "navmesh yok, düz yola düş" dalı meşru bir dal, betik sessizce yanlış yola
girer. `nav_yol` çağıran betik ise yanlış sebepli bir HATA görürdü ("sahnede navmesh yok").
Aynı sınıf E4'ün kendi tasarımında da vardı: varlık başına özellik aralığı kancalardan sonra
kurulsaydı `baslat`taki her `ozellik_*` **varsayılanı** okurdu — ki "özellik yok" da hata değil,
olağan durum. Sorunun çekirdeği bu: sorgu "henüz hazır değil" ile "yok"u AYIRMIYOR ve ikincisi
geçerli bir cevap.

**Düzeltme:** kancaların okuyabileceği her şey (özellik aralığı, navmesh) kanca döngüsünden
**önce** kuruluyor; `teng_scene_load`'da ikisinin yanında sebep yazıyor. Kapılar: C++
`bridge_runs_a_scripted_game_headless` 4.8 — sahte VM'in `baslat`ı `can` (250) ve
`teng_nav_ok()` (1) okuyor; Tulpar `run_ozellik` — iki muhafızın gerçek `muhafiz_baslat`ı can
toplamı 350 ve navmesh 2/2. Pozitif kontroller (mutant, 2026-09-25): aralık kurulumu kanca
döngüsünün ardına taşınınca 4.8 `can = -1` ile kırmızı; navmesh eski yerine (döngünün sonuna)
geri konunca 4.8 `nav_ok = 0` ile kırmızı.

**Ders:** yükleme sırasında dışarıya (betiğe, geri çağrıma) açılan her nokta bir **sıra
sözleşmesidir**: o noktadan sonra kurulan her şey, çağrılan kod için "yok"tur. "Yok" geçerli bir
cevapsa (varsayılan, düz yol, boş liste) bu sessiz bir bozulmadır. Kancayı çağırmadan önce
kancanın okuyabileceği her yüzeyi kur; olmuyorsa sorgu "hazır değil" diye HATA versin.

### 8ch. Tulpar çalışma zamanı kareyi hiç geri vermiyordu — ve geri sarım açılınca üç kalıp sessizce bozuluyor

**Belirti** (2026-09-25, RTX 5080 masaüstü, Linux; #52 ve TulparLang #345 sonrası):
`engine_aksiyon` 20000 kare penceresiz koşarken RSS 25. saniyede 334 416 kB, 285. saniyede
383 272 kB — ~188 KB/s. Kare cinsinden (yeni ikili, kare belleği kapalı:
`TULPAR_KARE_BELLEK=0`, 1000. kareden 12000. kareye): **2522 KB / 1000 kare**, saatte ~550–680 MB.
C++ tarafı düzdü: 8cd'nin Vulkan nesne sayacı 20000 karede toplam 8 kurma (bölüm 2 yüklemesi),
kare başına büyüyen tür yok. Hiçbir kapı kırmızı değildi.

**Sebep:** TulparLang'ın AOT çalışma zamanı metni (`t"..."`, `+`, `toString`, `tm_make_str` —
motorun döndürdüğü her metin ve **her kanca çağrısında** kurulan fonksiyon adı) 1 MB'lık bir bump
arenasından (`aot_arena_alloc`), dizi/nesne literallerini malloc'tan ayırır. Arena yalnız
`arena_save`/`arena_restore` ile geri sarılır; dizi/nesne yalnız bir arena noktası AÇIKKEN
"bölge"ye kaydedilir ve o noktayla birlikte serbest kalır. wings istek başına, tame `run()` kare
başına bunu yapıyor; motor oyunları (`engine.tpr` `kare_basla`/`kare_bitir`) yapmıyordu.

**Düzeltme:** `kare_basla` `arena_save`, `kare_bitir` önce `eng_frame_end` (kancalar onun içinde
koşar) sonra `arena_drop`. Kare **başına** nokta, tame'in "bir kez save, her kare restore"u
değil: o modelde iki kare ARASINDA üretilen değer (test koşucusu, döngü dışı kod) bir sonraki
restore'da siliniyor — sonda: `str s = "arakare-1007"; kare(); print(s)` → `COPCOPCOP0`. Sonra:
3200 karede 0, 12000 karede 5, 20000 karede 2 KB/1000 kare (19000 karede +40 KB; dış örnekleyici
10. saniyeden 285.'ye 325 948 → 325 996 kB).

**Sessiz bozulma — geri sarım üç kalıbı kırar** (sondalar: kare içinde yaz → geri sar →
arenayı çöple ez → oku; TulparLang 3eee948). **Güncel (2026-09-28):** 1. ve 2. TulparLang #347
(v3.15.5) ile derleyicide DÜZELDİ ve orada `tests/arena_kalicilik.test.tpr` ile kilitli; 3. hâlâ
açık (TulparLang envanteri K131). Aşağıdaki metin tarihsel teşhis olarak duruyor:
1. **Global metne bileşik atama** `g_metin += x`. Derleyicinin global kalıcılaştırması
   (`llvm_backend.cpp` "assign.autopersist") yalnız DÜZ atamada; `AST_COMPOUND_ASSIGN` onu
   atlıyor. Sonda: "1", "12", "123" beklenirken `COPCOPCOP0`, `COPCOPCOP02`, `COPCOPCOP023`.
   `g = g + x` doğru.
2. **Tipli struct dizisini kare içinde yeniden atamak** (`Dusman[] g; ... g = [];`) ya da kare
   içinde kurulmuş struct dizisini kalıcı bir kaba koymak. `aot_sarr_new` diziyi bölgeye
   kaydediyor, `aot_persist` ise yalnız STRING/ARRAY/OBJECT'i derin kopyalıyor, struct dizisini
   skaler gibi AYNEN döndürüyor → global, kare sonunda `free` edilmiş başlığa bağlı kalıyor.
   Sonda: sonraki okuma "indeksleme hedefi bir struct dizisi degil" (çökmezse kullanımdan sonra
   serbest bellek). `engine_aksiyon` `dusmanlari_bosalt()` tam bunu yapıyordu — bölüm 2 geçişi
   (k713) ve yeniden başlatma KARE İÇİNDE; yuvaları yeniden kullanacak şekilde düzeltildi.
3. **Döngü dışında bildirilmiş yerele** kare içinde metin/dizi/nesne atayıp sonraki karede
   okumak: yazma bariyeri yalnız global'i ve kalıcı kabı görür. Sonda: metin çöp, dizi boş,
   nesne "get islemi icin gecersiz hedef".
Güvenli olanlar (aynı sondalar): global'e düz atama (derin kopya), global dizi/nesne öğesi ve
push (bariyer), `g[i] += s`, skaler/enum/`Vec3` globaller, kareden önce kurulmuş tipli struct
dizisine push/öğe/alan yazma, kareden önce kurulmuş yerel diziye push. Tam tablo `docs/KOPRU.md` §3.1.
Bozulma değil ama aynı aile: global'e her düz atama bir KALICI malloc kopyadır ve kare belleği
onu geri vermez — her karede atanan global metin sızar (`engine.tpr` `ui_pencere` başlığı her
karede kopyalıyordu; artık yalnız değişince).

**Kapı:** `engine_aksiyon.tpr` `[kapi] bellek:` — `bellek_kb()` (`eng_rss_kb` →
`platform::os_resident_bytes`) 1000. karede ve son karede; eğim > 600 KB/1000 kare ya da geri
sarılan kare ≠ koşan kare → BAŞARISIZ. Pozitif kontrol `TULPAR_KARE_BELLEK=0`: 3200 karede 1805,
12000 karede 2524 KB/1000 kare → BAŞARISIZ. Ölçü aletinin kendi kapısı
`platform_resident_bytes_counts_touched_pages_only`: 64 MB rezerv +0 kB, dokununca +65 536 kB,
bırakınca −65 536 kB; statm'in 1. alanını (sanal boyut) okuyan sürüm aynı testte kırmızı
(rezerv +65 536, dokunma +0). Tulpar kapısı (`engine_bridge.test.tpr` "kare bellegi"): 8 kare ×
~4 MB metin → +0 kB, aynı iş kare DIŞINDA +34 204 kB.

**Kör noktalar (bilerek söyleniyor):** ~~1 ve 2 derleyicide düzelmeli (bileşik atamaya
kalıcılaştırma, `aot_persist`'e struct dizisi)~~ — ikisi de TulparLang #347'de düzeldi (yukarıdaki
"Güncel" notu); `tools/motor_derleyici.sh --dogrula` ikisini de sondayla sınıyor (üçüncü ve
dördüncü dil sondası, #72) ve #347 öncesi bir derleyiciyi reddediyor. 3. kalıp (döngü dışında
bildirilmiş yerel) hâlâ açık, TulparLang K131. RSS kapısı
yalnız `engine_aksiyon`'un yolunu ölçer; diğer örnekler ve `davranis/` betikleri elle denetlendi
(yalnız skaler global, kareden önce kurulmuş diziler). CI `.tpr` koşturmuyor: kapı yereldir.

**Ders:** Bir geri sarım (arena, havuz sıfırlama) eklemek "bu değer kalıcı mı" sorusunu her
atamada sorulur hale getirir. Güvenli kalıpları SONDAYLA say, varsayma: derleyicinin "otomatik
kalıcılaştırması" yalnız gördüğü atama biçimlerini kapsıyor ve kapsamadığını söylemiyor.

### 8ci. İki damgayı karşılaştıran kapı damganın DEĞERİNİ ölçmez — Windows'ta `dosya_zamani()` negatifti

**Belirti** (2026-09-25, E5; hata hesaplandı ve Linux'ta benzetildi, düzeltme Windows CI'da
ölçüldü): `teng_file_mtime` / `dosya_zamani` sözleşmesi "saniye (epoch)". Windows dalı
`GetFileAttributesExA`'nın FILETIME'ını `(int64_t)(ft * 100)` ile ns'ye çeviriyordu. FILETIME
1601-01-01'den 100 ns sayar (Microsoft tanımı); 2026'daki bir dosya için bu 1601'den
13 434 793 600 s = 1.34·10¹⁹ ns — `int64` tavanının (9.22·10¹⁸) **üstü**. İşaretliye çevrim
sarıyor: dönen değer ≈ **−5.01·10⁹ s** (Unix devrinde 1811 yılı). Tek kapı
(`engine_bridge.test.tpr` "kopya dosya damgasını ilerletmeli") iki damgayı **karşılaştırıyor**:
sarma o aralıkta tekdüze kaldığı için sıra doğru, üç platform yeşil. Damgayı saatle ya da başka
bir makinenin damgasıyla karşılaştıran bir betik yalnız Windows'ta sessizce yanlış karar verirdi.
Yardımcı, editör de kullanabilsin diye köprüden `platform/fs.hpp`'ye taşınırken bulundu.

**Düzeltme:** `platform::fs_file_stamp` her platformda Unix devrinden ns verir (Windows:
`(ft − 116 444 736 000 000 000) × 100`, 2026 için 1.79·10¹⁸ — sığar); köprünün `file_stamp_pub`'ı
ona devreder. Kapı MUTLAK değeri ölçer: `editor_props_cache_rescans_on_change_and_throttles_disk`
dosyanın mtime'ını `utime` ile 1 700 000 000 s'ye ayarlar ve damganın **tam** 1.7·10¹⁸ ns olmasını
ister. Ölçüldü (Windows x86_64 MSYS2 CI, 2026-09-25): taze dosya 1 790 321 424 166 334 500 ns
(Unix devri, 100 ns adımlı), ayarlanan 1 700 000 000 000 000 000 ns TAM. Pozitif kontrol (mutant,
Linux, 2026-09-25): POSIX dalına 1601 kayması eklenince damga −5 011 949 563 967 458 915 ns
okundu — yukarıdaki hesapla aynı sarma — ve kapı KIRMIZI.

**Ders:** "değişti mi" kapısı "değer ne" sözleşmesini ölçmez. Bir API mutlak bir birim/devir vaat
ediyorsa kapısı en az bir **bilinen girdiyi** mutlak olarak karşılaştırmalı; iki çıktıyı
birbiriyle karşılaştırmak her ortak kaymayı (devir, birim, sarma) gizler.

### 8cj. `pipefail` + `| grep -q`: erken çıkan tüketici boru hattını "yok"a çevirir

**Belirti** (2026-09-25): `tools/motor_derleyici.sh`'ye eklenen soru — "TulparLang
kopyası `aot_func_lookup` içeriyor mu" — fonksiyonu İÇEREN kopyayı da reddetti
(`git show HEAD:src/vm/runtime_bindings.cpp | grep -q 'aot_func_lookup(const char'`).

**Sebep:** `grep -q` ilk eşleşmede çıkar. Yazan taraf (`git show`, `ldconfig -p`) kalan
çıktıyı yazarken SIGPIPE alır ve 141 ile ölür; `set -o pipefail` boru hattının durumunu
141 yapar, `if` "yok" der. Yalnız çıktı boru arabelleğini (Linux 64 KB) aşıyor ve eşleşme
erken geliyorsa olur — yani girdiye göre değişir, küçük bir denemede görünmez.

**Aynı sınıf, fark edilmemiş hali:** `derle.sh`'nin `kutuphane_var`'ı
`ldconfig -p | grep -q -- "$ad"` yapıyordu (bu makinede 288 KB çıktı). Ölçüldü:
libvulkan, libglfw, libc.so, libzstd için **hep 141** — ldconfig dalı ölüydü. Rapor doğru
görünüyordu çünkü altındaki glob yedeği (`/usr/lib`, `/usr/lib64`, ...) kütüphaneleri
buluyordu; kütüphanesi yalnız ldconfig'in bildiği bir yerde olan dağıtımda "eksik" derdi.

**Düzeltme:** `grep KALIP >/dev/null` (girdinin tamamını okur, SIGPIPE yok). İki yerde de.
**Pozitif kontrol:** `bash -c 'set -o pipefail; ldconfig -p | grep -q libvulkan; echo $?'`
→ 141, aynısı `grep ... >/dev/null` ile → 0; `motor_derleyici.sh`'nin sorusu eski
TulparLang (3eee948) ile kırmızı ("TulparLang'ı güncelleyin"), yenisiyle yeşil ölçüldü.
`derle.sh --sadece-denetle` raporu düzeltmeden önce ve sonra bayt bayt aynı (bu makinede
yedek zaten buluyordu).

**Ders:** `pipefail`'li bir betikte boru hattının sonunda erken çıkan bir tüketici
(`grep -q`, `head -1`, `sed q`) koşul olarak kullanılamaz: sonucu tüketicinin değil
yazanın çıkış kodu belirler. Koşul gerekiyorsa tüketici girdiyi sonuna kadar okumalı ya da
çıktı önce bir değişkene alınmalı.

### 8ck. Denetçi her kare `edit_before`ı tazeliyor — gizmo sürüklemesi onu "başlangıç" sanıyordu

**Belirti** (2026-09-25, E6; penceresiz editör, RTX 5080, `ozellik.sahne`): yeni `nokta
surukleme kapisi` ilk koşumda noktanın yerel değerini tam beklenen kadar değiştirdi ama
günlüğe **0 işlem** yazdı — geri al hiçbir şeyi geri getirmedi. Aynı kapının KONTROLÜ (kip
kapalıyken iki seçili varlığı gizmoyla sürüklemek) ikinci, eski bir sessiz bozulmayı gösterdi:
yan varlık bırakışta **başlangıcına döndü** (ana seçiliye göre sapma 0.529 m) ve günlüğe 2 değil
1 işlem girdi. Çoklu seçimde gizmo sürüklemesi yalnız ana seçiliyi taşıyordu.

**Sebep:** `EditorState::edit_before` iki sahibin ortak alanı. `track_edit` (denetçinin her
sürekli widget'ı çağırır) widget etkin DEĞİLKEN her kare `edit_before = e` yapar — "etkinleşirse
elimde son hal olsun". Denetçi gizmodan ÖNCE çizilir ve gizmo sürüklerken hiçbir widget etkin
değildir; yani `edit_before` her kare **sürüklenmiş hale** yenilenir. (1) Nokta sürüklemesi
bırakışta `edit_before`ı "önce" diye günlüğe verdi: önce == sonra, `set_entity` işlem yazmadı.
(2) Grup sürüklemesi deltayı `e.pos - edit_before.pos` ile alıyordu: yalnız SON karenin artımı.
Bırakış karesinde ImGuizmo yine `changed` döndürüyor (gizmo sondası: k17 `using=0 changed=1`),
artım 0 ve yan varlıklar `drag_before + 0`a — başlangıca — dönüyor; `selection_commit`
değişmeyeni yazmaz. Yakalayan kapı yoktu: çoklu seçim kapısı grubu `selection_translate` ile
taşıyor, gizmoyla değil; gizmo sürükleme kapısı tek varlık sürüklüyor.

**Düzeltme:** Sürüklemenin tabanı, sürükleme BAŞINDA alınan ve kimsenin yenilemediği kopya:
nokta için `pe_drag_before`, grup için ana seçilinin `drag_before` girdisi. Kapı: `nokta
surukleme kapisi` (kare 51–59) — kontrol sürüklemesinde yan varlık ana seçiliyle AYNI deltayı
almalı (sapma < 1e-5), günlükte 2 işlem / 1 grup; nokta sürüklemesinde TAM 1 işlem / 1 grup ve
geri al sahne baytlarını geri getirmeli. Ölçüldü (RTX 5080, 2026-09-25, `ozellik.sahne`):
düzeltmeden önce nokta 0 işlem, yan sapma 0.529 m / 1 işlem (kapı KIRMIZI); sonra nokta 1 işlem /
1 grup, yan sapma 0 / 2 işlem / 1 grup; `salon1.sahne` ve `tests/assets/editor.sahne` aynı.

**Ders:** "başlangıç kopyası" ile "son bilinen hal" aynı alanda tutulamaz. Bir anlık görüntüyü
her kare tazeleyen bir yol varsa, o alanı "sürükleme başı" diye okuyan her yol yalnız son
karenin farkını görür — ve tek karelik ya da tek varlıklı bir sürüklemeyi ölçen kapı bunu
göremez. Sürükleme tabanı, sürüklemenin sahibi olan yolda ve başka kimsenin yazmadığı bir
alanda tutulur.

### 8cl. ASCII yolda A-API ile W-API aynı sonucu verir — Türkçe kullanıcı adında ayrışır ve ASCII geçici dizinde koşan kapı farkı HİÇ görmez

**Bulgu** (2026-09-25, editör içi güncelleyici çekirdeği, kod okuması; ölçümü Windows CI'da
Türkçe harfli fikstür yapar): güncelleyici kullanıcının indirdiği paketi yerinde değiştirir —
`C:\Users\Çağrı\Downloads\tulpar-engine-...\`. Motorun Windows yolları bugüne kadar A-API'lerle
(`GetModuleFileNameA` → `platform::exe_dir()`, `CreateFileA`, `MoveFileExA`, `fopen`) çalışıyor:
yol baytları **sistem kod sayfasında** (Türkçe Windows'ta 1254) taşınıyor ve kendi içinde
tutarlı. Güncelleyici ise yolu UTF-8 kabul edip UTF-16'ya çevirerek W-API'ye veriyor. İkisi
karışınca (`exe_dir()`'in ACP baytlarını UTF-8 sanıp `MultiByteToWideChar(CP_UTF8, ...)`)
dönüşüm ya reddedilir ya başka harfler üretir ve kurulum "yok" görünür — **yalnız** adında
ASCII dışı harf olan makinede. İngilizce Windows'ta (ACP 1252) `ğ`/`ı` hiç temsil edilemez;
A-API orada zaten `?` görür. Alt süreç argümanı da aynı sınıfa girer: System32 `curl.exe`/
`tar.exe` argv'yi ACP'den geçirebilir.

**Neden sessiz:** CI koşucusunun ve geliştiricinin geçici dizini ASCII (`C:\Users\RUNNER~1\...`,
`/tmp`). ASCII'de CP_ACP == CP_UTF8, A-API == W-API: bütün kapılar yeşil, fark hiçbir koşumda
görünmez. Hata yalnız kullanıcının makinesinde ve güncelleme anında çıkar.

**Düzeltme:** `platform/fs_ops.{hpp,cpp}` — her yol UTF-8, Windows'ta UTF-16'ya çevrilip W-API
(uzun yolda `\\?\` öneki); kurulum dizini `fs_exe_dir_utf8()` (`GetModuleFileNameW`) ile,
`exe_dir()` ile DEĞİL. curl/tar'a yalnız **göreli ASCII** argüman gider, çalışma dizini
`CreateProcessW`'ye UTF-16 olarak verilir. Kapı: `tests/test_updater.cpp` kurulum dizinini
bilerek `kurulum-Çağrı` adıyla kurar (uçtan uca, geri alma, `fs_move`); Windows CI ayağı W
yolunu gerçekten koşturur.

**Ders:** bir yol kodlaması hatasını yakalayacak kapının girdisi o kodlamanın AYRIŞTIĞI
karakterleri içermeli. ASCII fikstür, kodlama farkı olan iki yolu da "doğru" ölçer. Aynı
süreçte iki kodlama (ACP ile çalışan eski yol, UTF-8 bekleyen yeni yol) varsa sınır açıkça
çizilir: yeni kod eski yardımcının çıktısını (`exe_dir`) kullanmaz.

### 8cm. "Sıfır istek" kapısı, ölçtüğü özellik KAPALIYKEN kendiliğinden geçer — kaynak derlemesinde güncelleyici hep kapalı

**Bulgu** (2026-09-25, editör içi güncelleme arayüzü, `app/editor_update`): kapının iddiası
"penceresiz editör güncelleyiciye hiç istek göndermez (sayaç 0)". CI ve geliştirici editörü
kaynaktan derler: `build_version()` boş → güncelleyici **Disabled** → hiçbir yol istek
göndermez; penceresiz koruması silinse bile sayaç 0 kalır. Kapı yeşil, ölçtüğü şey yok. Aynı
sınıf fikstürde de yaşandı: çekirdek manifest biçimini sıkılaştırınca (`SURUM.txt`
listelenmeli, satırlar bayt sıralı) arayüz testinin sahte paketi geçersiz oldu ve güncelleyici
sessizce Disabled'a düştü — "penceresiz: istek 0" yine geçti. Yakalayan yalnız testin ayrıca
sorduğu `durum != Disabled` satırıydı.

**Neden sessiz:** "0" hem "koruma çalıştı" hem "korunacak iş hiç yoktu" demektir; sayaç ikisini
ayırmaz. Kaynak derlemesi CI'ın TEK yapılandırması olduğu için ikinci anlam her koşumda doğrudur.

**Düzeltme:** (1) kapı önce özelliğin AÇIK olduğunu iddia eder (`durum != Disabled`, karar metni
"penceresiz" — "kapalı" değil); (2) `engine_tests` penceresiz editörü **etkin** bir fikstürle
(sahte paket dizini + sürüm ezmesi, `TULPAR_GUNCELLEME_DIZIN`/`_SURUM`) koşturur; (3) editörün
kendi kapısı reddi ayrıca sayar: menü yolu penceresiz kipte Disabled denetiminden ÖNCE
reddedilir, `red 1` kaynak derlemesinde de ölçülür; (4) pozitif kontrol aynı yapılandırmanın
pencerelisi: aynı sayaç 1 olur ve `file://` fikstür Available döner.

**Ders:** "X hiç olmadı" iddiası, X'in OLABİLDİĞİ bir yapılandırmada ölçülür ve kapı o
yapılandırmada olduğunu kendisi doğrular. Sıfır sayan her kapının yanında "burada sıfırdan
fazlası mümkündü" satırı durur.

### 8cn. `Surum: minor` PR açıklamasında yazıldı, squash commit'ine hiç gelmedi — iki özellik sürümü sessizce yama çıktı

**Bulgu** (2026-09-25, editör içi güncelleme, #66): PR açıklamasında tek satır `Surum: minor`
vardı, `otomatik-surum.yml` yine **yama** çıkardı: v0.2.0 yerine v0.1.47. Geriye bakınca #56
(kare belleği, `Surum: minor`) da aynı yoldan v0.1.37 olmuştu. İş akışının kendi yorumu
"squash merge PR gövdesini commit gövdesine koyduğu için PR'da yazmak yeterli" diyordu; bu
depoda **yanlış**: `squash_merge_commit_message = COMMIT_MESSAGES` — squash gövdesi dal
commit'lerinin mesajlarıdır, PR açıklaması oraya hiç girmez.

**Neden sessiz:** "seviye bulunamadı" ile "seviye istenmedi" aynı sonucu verir (yama) ve her
iki durumda da sürüm çıkar, Release yayınlanır, iş yeşildir. İş özetindeki `artis: patch`
satırı doğruydu ama kimse "minor istemiştim" diye karşılaştırmadı.

**Düzeltme:** iş akışı birleşen PR'ı commit'ten API ile bulur (`commits/<sha>/pulls`, main'e
birleşmiş olan), açıklamasını da okur; PR ile commit mesajından BÜYÜK seviye kazanır. İş
özetinde seviyenin **kaynağı** yazılır (`minor (PR #66 açıklaması)`). API okunamazsa uyarı +
özet satırı, sessiz düşüş yok. Pozitif kontrol (yerel, 2026-09-25, iş akışı adımının kendisi
gerçek PR'larla): #66 → minor, #56 → minor (ikisi de eskiden yama), #65/#63 → yama, geçersiz
belirteç → uyarı + commit mesajı.

**Ders:** bir yapılandırma satırının "nereye gittiği" varsayılmaz, okunduğu yerde ölçülür.
Depo ayarı (squash mesajı) değişebilen bir girdi; ayara değil, verinin asıl kaynağına (PR) bak.

### 8co. Eklenti bildirimi C imzasından kayar — derleyici statik arşivde tip GÖREMEZ

**Sınıf** (2026-10-02, TulparLang K303 ile doğdu): motor Tulpar'a bir yerel eklenti olarak
bağlanıyor; derleyici `tulpar-ext.json`'daki tiplerle `teng_*`'i **doğrudan** çağırıyor. Bildirim
`i32` derken C fonksiyonu `double` alıyorsa (ya da `int` dönüşü `int64_t` okunursa) hiçbir şey
hata vermez: link geçer (sembol var), çağrı yanlış yazmaçtan okur, sonuç çöptür. Eski VMValue
bindingleri C++ derleyicisinden geçtiği için örtük çevrimle "kendiliğinden" uyuyordu; o güvence
eklenti yolunda yok.

**Kapı:** `bridge/tulpar_abi.cpp` — bildirimle aynı kaynaktan (SPEC) üretilen her satırı
`teng_*`'in gerçek bildirimine **tipli işlev işaretçisi** olarak atar; C++'ta farklı işlev
işaretçisi tipleri arasında örtük çevrim yok, kayma = derleme hatası. Tablo `engine_tests`'e
bağlı (sembol yoksa link hatası). Bildirimin kendisi `engine_bindings_check` ile SPEC'e karşı
taze tutulur. **Pozitif kontrol:** CMake yapılandırması aynı dosyayı `TULPAR_ABI_KILIDI_BOZ` ile
(teng_init'e `double` parametreli bir işaretçi) derlemeyi dener ve DÜŞMESİNİ bekler; derlenirse
yapılandırma durur.

**Ders:** derleyicinin göremediği bir sözleşmeyi (statik arşivin C tipleri), onu gören tek
yerde — sözleşmenin C tarafının derlendiği yerde — kilitle. "Link geçti" tip uyumu demek değil.

### 8cp. Derleme zamanı yolu kurulu pakette yok — kapı derleyen makinede koştuğu için onu yine bulur

**Sınıf** (2026-10-02, ölçüldü): `ENGINE_SOURCE_DIR` bir **derleme zamanı** mutlak yolu ve
köprünün varlık aramasının son basamağı (`platform::asset_path`: exe dizini → cwd → kaynak
ağacı). Kurulu bir motor paketinin editörü F5'te oyunu `tulpar`ın geçici dizininde derleyip
proje kökünde koşturuyor: exe dizininde de cwd'de de `assets/fonts` yok, HUD fontu **yalnız**
kaynak ağacından bulunuyordu. Kullanıcının makinesinde o ağaç yok → `UYARI font bulunamadi`,
`eng_text` sessizce çizmez. Aynı kosu CI'da ve geliştirici makinesinde **yeşil** — çünkü ikisi
de paketi derleyen makine, kaynak ağacı orada duruyor. Yani "paketten koştu" diyen bir kapı,
paketin değil depo ağacının varlığını ölçebilir (`TULPAR_ENGINE_LOG=3` ile görülen satır:
`font adayi 1/3: <depo>/assets/fonts/DejaVuSans.ttf -> YUKLENDI`).

**Düzeltme:** eklenti paketi fontu kendi içinde taşır (`tulpar-ext/assets/fonts/`, CMake ve
`tools/package.sh`), köprü `TULPAR_EXT_PATH` girdilerinde onu arar (editör o değişkeni her
zaman verir). **Kapı:** `tools/package.sh` paketlenmiş eklentiyle bir örnek oyunu depo
dışındaki geçici dizinde koşturur ve yüklenen fontun yolunun **paketin içinde** olduğunu
denetler — kaynak ağacı duruyor olsa bile. Arşivlerin paketten linklendiğini pozitif kontrol
gösterir: bir arşivi silinmiş kopyayla aynı oyun düşmeli, link hatası arşivi adıyla söylemeli.

**Ders:** "kaynak ağacı olmadan çalışıyor" iddiası, kaynak ağacının durduğu makinede ancak
**hangi yolun kullanıldığı** ölçülerek doğrulanır; çıkış kodu 0 bunu söylemez.

### 8cq. Yarım kurulum geri alınmaz — kapanış `!inited` görüp döner, her yeni deneme hepsini yeniden alır

**Sınıf** (2026-10-05, Windows CI'da görüldü, yerelde ölçüldü): `teng_init` ilk adımlarda 512 MB
arena rezervi alır, iş sistemini kurar (çekirdek−1 worker) ve Vulkan yükleyicisini açar; sonra
`vkCreateInstance` `VK_ERROR_INCOMPATIBLE_DRIVER` ile düşer (Windows runner: `vulkan-1.dll` var,
ICD yok). Eski kod o noktada düz `return 0` yapıyordu: arena, **çalışan** worker'lar ve açık
yükleyici oldukları gibi kalıyordu. `teng_shutdown` ise `!inited` görüp hemen dönüyordu — yani
toplayan kimse yoktu. Tek denemede yalnız RSS büyür (CI: 4,7 → 11,2 MB) ve göze batmaz; aynı
süreçte yeniden deneyen bir oyun (ör. "GPU'yu tekrar dene" düğmesi) her denemede hepsini yeniden
alır. Ölçüldü (RTX 5080 / Linux, 16 çekirdek, düzeltmesiz kod): 8 düşen kurulum → sanal
**+5316 MB**, thread **+120**, RSS **+33,8 MB**.

İkinci, bağlı sınıf: `vk_api_load` `TULPAR_ENGINE_NO_VULKAN` anahtarına "yükleyici zaten açık"
erken dönüşünden **sonra** bakıyordu. Köprü başarılı bir oturumdan sonra yükleyiciyi açık tuttuğu
için aynı süreçte sonradan verilen anahtar sessizce yok sayılıyor, ölçülmek istenen "yükleyici
yok" yolu yerine kurulum başarıyordu.

**Düzeltme:** `teng_init`'in arena rezervinden sonraki **her** başarısız çıkışı
`init_geri_al(b, aşama)`'dan geçer: `teng_shutdown`'un sırasıyla iş sistemi → fizik → renderer →
hedefler → cihaz → pencere → yükleyici → profiler → arena. Alt sistemlerin `shutdown`'u kurulmamış
halde güvenli olduğu için hangi aşamada düşüldüğü ayrıştırılmaz. Kaynakta denetim: düzenleme
betiği arena rezervinden sonra çıplak `return 0` kalmadığını doğruladı; yeni bir çıkış eklerken de
`return init_geri_al(...)` yazılır. Anahtar artık erken dönüşten önce okunur.

**Kapı:** `tests/test_bridge.cpp` `bridge_failed_init_without_loader_leaves_no_residue` (anahtarla
yükleyici kapalı) ve `bridge_failed_init_without_icd_leaves_no_residue` (yükleyici var,
`VK_DRIVER_FILES`/`VK_ICD_FILENAMES` olmayan bir dosyaya → Windows CI'nın gerçek yolu): ısınma
turundan sonra 8 düşen kurulum + kapanış; sanal boyut (`platform::os_virtual_bytes`), RSS ve thread
sayısı (`platform::os_thread_count`) değişmemeli (thread için eşik "deneme başına < 1": tam
koşumda önceki testlerin sürücü thread'leri ±1 oynuyor, CI lavapipe'ta ölçüldü), hata metni beklenen
aşamayı söylemeli. macOS'ta ICD senaryosu görünür atlanır: ICD gizlenince `rhi/device.cpp`'nin yedeği
MoltenVK'yi doğrudan yükler ve kurulum başarır — o platformda "yükleyici var, ICD yok" yolu yok.

**Bağlı sınıf (aynı gün kapandı, [[#8ct. "Kapat" bir bayrak indirir, yeniden açmayı bilmez — ikinci oturum bayat durumla kurulur]]):**
*başarılı* bir oturumdan sonra aynı süreçte ikinci bir başarılı `teng_init` + `teng_shutdown` çöküyordu
(CI macOS 2026-10-05, SIGSEGV): `teng_shutdown` `Bridge`'i sıfırlamıyor ve arenayı bırakmıyordu. Tulpar
tarafı (düşen kurulum): `tulpar/tests/engine_gpusuz.test.tpr` "yeniden deneme" (8 deneme, RSS eşiği 2048 KB).
**Pozitif kontrol** (aynı testler, düzeltmesiz köprü, 2026-10-05): C++ iki senaryoda da sanal
+5316 MB, thread +120, RSS +33,8 MB → 3 kontrol kırmızı; Tulpar `Fail: 1`, 8 denemede RSS +36172 KB.
Düzeltmeyle: C++ +0 MB / +0 thread / +0–8 KB, Tulpar +8 KB.

**Ders:** "kurulum false döner" sözleşmesi, "ve aldığını geri bırakır" demiyorsa yarıdadır.
Kapanış kurulmamış durumda hiçbir şey yapmıyorsa, kısmi kurulumu toplayan yer başarısız yolun
kendisi olmak zorunda — ve bu, tek denemede değil **N denemede** ölçülür.

### 8cr. Yazılım ICD'si "kuruldu ve cihaz görünüyor" — ama bellek bırakırken yığını bozuyor; kapılar şans eseri geçer

**Sınıf** (2026-10-05, CI windows-latest'te ölçüldü): Windows CI'ya yazılım Vulkan ICD'si
aranırken MSYS2'nin `mingw-w64-x86_64-mesa` (26.2.4) paketindeki lavapipe kuruldu. Loader cihazı
gördü (`llvmpipe (LLVM 22.1.8, 256 bits)`, Vulkan 1.4.354), doğrulama katmanı yüklendi,
`rhi_loader_and_device_caps` geçti, iki pozitif kontrol de geçti. Tam `engine_tests` ise
**çıktısız**, çıkış 127 ile öldü (`cmd` üzerinden: `-1073740940` = 0xC0000374,
STATUS_HEAP_CORRUPTION). Sonraki koşumlarda başka yerlerde öldü: gömülü oyun çocuk süreçleri
920 kareyi doğru renklerle çizip **çıkışta**, `engine_editor` penceresiz kapısı ve
`bridge_runs_a_scripted_game_headless`. Yani lavapipe üzerinde "geçen" kapılar da güvenilir
değildi: bozulma sonradan, rastgele bir `free`'de patlıyor.

**Teşhis yolu** (her adım bir sonrakinin ön koşulu):
1. Harness'ın `setvbuf(stdout, _IOLBF)`'i MSVCRT'de **tam tampon**: fail-fast ölümde son ~4 KB
   kayboluyordu, ölen testin adı bile görünmüyordu → `RUN` satırı elle boşaltılıyor
   (`tests/test_main.cpp`). msys2 kabuğunun `bash -e`'si de "Özet satırı YOK" teşhisini yutuyordu
   → test adımı `set +e`.
2. Tam sayfa yığını (IFEO `GlobalFlag=0x02000000`, `PageHeapFlags=3`) + `cdb`: `VERIFIER STOP 10:
   corrupted start stamp`, `msvcrt!free ← vulkan_lvp+0x223921 ← vulkan_lvp+0x2b6b50 ←
   ImGui_ImplVulkan_UpdateTexture+0x89b`. Release yapısında DWARF yok; `nm -n` ile en yakın alt
   sembol bulundu, `objdump -d` dönüş adresinin `call *vkFreeMemory`'nin arkası olduğunu gösterdi.
   Sayfa yığınında bir YAZMA taşması anında erişim ihlali verirdi, öyle olmadı.
3. **Motordan bağımsız asgari yeniden üretim** (`tools/vk_bellek_sonda.py`, ctypes: ayır → eşle
   → yaz → kaldır → `vkFreeMemory`): aynı yığın, `_ctypes ← vulkan_lvp ← msvcrt!free`, PageHeap'siz
   de çıkış 127. Aynı sonda SwiftShader'da (Chrome 154) ve RTX 5080'de temiz.

**Karar:** Windows CI'nın ICD'si **SwiftShader** (runner'daki Chrome'un `vk_swiftshader.dll`'i,
Vulkan 1.3). `tools/windows_vulkan_icd.sh kur` bellek sondasını **her koşumda** koşturur;
seçilen sürücü bu sınıfı taşıyorsa CI orada, nedeniyle kırmızı olur. Hatanın mesa'da olduğu
**ölçüldü, iddia edilmedi**: kaynak bu ortamdan okunamadı (freedesktop GitLab erişilemedi);
ölçülen, çökmenin sürücünün `free`'sinde ve motorsuz kalıpta olduğu. Upstream bildirimi dış depo
işidir. Sürüm işi (`release.yml` Windows) aynı gün aynı betiklerle tam kipe geçti (#86):
pozitif kontroller, test kapısı ve Tulpar köprüsü `tools/windows_kapilar.sh`'te tek yerde, iki iş
akışı onu çağırır. ICD bulunamazsa adım kırmızı — ölçüldü: arama yolu bozuk geçici dalda
`workflow_dispatch` provası ICD adımında düştü, sonraki adımlar koşmadı (run 37314080867).

Yan bulgu, aynı yoldan: Windows loader'ı **yönetici** süreçte `VK_DRIVER_FILES` /
`VK_ICD_FILENAMES` / `VK_ADD_LAYER_PATH` / `VK_LAYER_PATH`'i yok sayar (`VK_LOADER_DEBUG`: arama
konumları boş, yalnız HKLM). CI runner'ı yönetici olduğu için ICD ve katman kayıt defterine
yazılır, katman gizleme pozitif kontrolü de kayıt girdisini geçici siler. Env ile ICD gizleyen
testler (8cq'nun ICD senaryosu) orada görünür atlanır: önce ham bir `vkCreateInstance` ile
gizlemenin tutup tutmadığı sorulur.

**Ders:** "Sürücü kuruldu, cihaz listelendi, bir test geçti" bir sürücünün doğru çalıştığını
söylemez. Yazılım ICD'si seçilirken onu **motordan bağımsız** en basit yaşam döngüsüyle
(ayır/bırak) yokla; heap bozulması gibi gecikmeli bir hata, üstteki her kapıyı rastgele yeşil ya
da kırmızı yapar.

### 8cs. İki uç arasındaki fark, sınırlı bir basamağı "eğim" sanar — ve pozitif kontrol sessizce eskir

**Sınıf** (2026-10-05, CI'da ölçüldü): `engine_aksiyon.tpr`'ın bellek kapısı RSS'i pencerenin iki
ucunda okuyup farkı kare sayısına bölüyordu. CI'ın llvmpipe'ında (mesa 25.2.8, LLVM 20.1.2) her
koşumda bir kez, aynı büyüklükte (~26–28 MB) bir sürücü basamağı oluşuyor, sonrası düz. Basamağın
zamanı ise rastgele: 16 paralel koşumda k1200 ile k6000 arası, 200 karelik 4–7 aralığa yayılarak.
Basamak pencereye düştüğünde kapı "bellek büyüyor" dedi (28 CI koşumunda 3 kez: 2936 / 3583 / 4134
KB/1000). Kırmızı koşumların sonu geçenlerle aynı platoda (194–195 MB) bitiyordu, yani sızıntı yoktu.
Önceki düzeltme ("ısınma k1100'de biter, pencereyi k2400'e kaydır") tek bir gözleme dayanıyordu ve
basamak bazen daha geç geliyordu.

İkinci, bağlı bulgu: kapının belgelenmiş pozitif kontrolü (`TULPAR_KARE_BELLEK=0`, 2026-09-25'te
1805–2522 KB/1000) TulparLang v3.39.0'da RTX 5080'de yalnız **203** KB/1000 sızdırıyordu, yani sınırın
(600) altında. Kontrol çoktan bir şey göstermez olmuştu ve bunu söyleyen yoktu, çünkü CI'da
koşturulmuyordu.

**Düzeltme:** ölçü 200 karelik aralık artışlarının **alt çeyreği** (25. yüzdelik). Sızıntı her
aralıkta artar, basamak aralıkların en çok ~%65'inde: 14 normal koşumda (üç farklı pencere) Q1 = 0,
istisnasız. Medyan yetmedi: 11 aralıklık pencerede 2 koşumda 2260 / 5140. Pozitif kontrol
**enjekte** ve **her koşumda** (`tulpar_dogrula.sh`): `TULPAR_AKSIYON_SIZINTI_B=2048` her karede
~2 KB'lık yeni bir dizgiyi kareler arası yaşayan bir global'e bırakır; kapı kırmızı olmalı
(llvmpipe Q1 1140, RTX 5080 Q1 1240). Ölçü aletinin kendi sınırı da ölçüldü ve yazıldı: 1 KB/kare'yi
yığının boş alanı yutuyor (RTX 5080'de Q1 20). Ölçüm aracı: `TULPAR_AKSIYON_BELLEK_IZ=N`, her N karede
RSS. Eski llvmpipe'a özel 4600 karelik ikinci koşum kalktı.

**Ders:** iki uçtan okunan eğim, eğrinin şeklini görmez. Önce eğriyi çiz, sonra şekle dayanıklı bir
istatistik seç. Pozitif kontrolü ise belgede değil, **her koşumda** koştur: dilin ya da sürücünün
değişmesi onu sessizce etkisiz bırakabilir.

**Ek (2026-10-27, CI'da ölçüldü — aynı sınıf, ikinci kapı):** aynı ölçüyü (200 karelik ardışık aralık,
alt çeyrek) devralan `engine_savas.tpr`'ın enjekte sızıntı kontrolü CI'ın lavapipe'ında (4 vCPU)
**yakalamadı** (iki koşumda alt çeyrek -600 / -2800, sınır 600); kendi PR'ının koşumunda yalnız +1080
ile, şans eseri geçmişti. Eğri (`TULPAR_ENGINE_SAVAS_BELLEK_IZ=100`): sızıntı düzgün +2.4 KB/kare, ama
savaşta RSS olaylarla (birlik/mermi ölümü, sürücünün bıraktığı tamponlar) 200 karede 0.5–1 MB iniyor,
temiz koşumda da aynı yerlerde. 2 KB/kare aralık başına yalnız +400 KB ekliyor, alt çeyrek inişlere
düşüyor. RTX 5080'de iniş yok, kontrol orada hep geçiyordu — yani yerel yeşil hiçbir şey söylemedi.
**Düzeltme:** 100 karede örnek, **600 karelik kayan pencere** artışlarının alt çeyreği; iniş genliği
aynı kalır, sızıntının katkısı 1.2 MB olur. Sınır (600) ve iddia (2 KB/kare yakalanır) aynı. Ölçüldü
2026-10-27, 5'er koşum, temiz / sızıntılı: Ubuntu 24.04 lavapipe (mesa 25.2.8, CI'ınki) 4 çekirdek
-706..-580 / 1460..1740; RTX 5080 6..26 / 2086..2146. Eski ölçü aynı lavapipe'ta sızıntıda 5/5 -120..-60.
**Ders:** bir pozitif kontrolün kenar payını kapının koştuğu **en gürültülü** platformda ölç; geçtiği tek
koşum onun ölçtüğünü göstermez.

### 8ct. "Kapat" bir bayrak indirir, yeniden açmayı bilmez — ikinci oturum bayat durumla kurulur

**Sınıf** (2026-10-05, CI macOS'ta görüldü, yerelde ölçüldü): `teng_shutdown` alt sistemleri kapatıp
yalnız `inited = false` diyordu. 512 MB arena rezervi, profiler dizileri ve `Bridge`'in **bütün** alanları
(varlıklar, sahne, betik havuzları, modeller, kare/zaman sayaçları) olduğu gibi kalıyordu. Aynı süreçte
ikinci `teng_init` "kurulu değil" görüp devam ediyor, eskisinin üstüne yeni arena rezerv ediyor ve
bayat tablolarla kuruluyordu. Ölçüldü (RTX 5080 / Linux, düzeltmesiz köprü): ikinci oturum açılır açılmaz
varlık sayısı 3 (önceki oturumun), kare 32, önceki oturumun id'si **canlı**; ikinci kapanış fizikten
silinmiş gövdeleri yeniden silmeye çalışıp **SIGSEGV**. Tulpar tarafında aynı: ikinci `motor_kapat`
süreci 139 ile düşürdü; ilk oturumdan sonra sanal boyut 711 yerine 1351 MB (arena kalmış).

Kim kullanıyor? Editörün F5'i **kullanmıyor**: F5 → Durdur → F5 her seferinde yeni bir oyun süreci
başlatır (`app/editor_game.hpp`), yani kullanıcı bu hatayı editörde görmezdi. Yol: oyunun kendi
"motoru kapat / yeniden aç" akışı ve Android'de süreç canlıyken etkinliğin yeniden yaratılması
(`android_main` yeniden çağrılır, `main()` yeniden koşar). **Emülatörde ölçüldü (2026-10-06):** Android
emülatörü API 31 x86_64, `-gpu swiftshader_indirect` (konuk Vulkan 1.1, gfxstream → SwiftShader);
`motor_ac` → 60 kare → `motor_kapat` yapan bir Tulpar oyunu (`tulpar build --target=android --ext`), APK
`am start` ile üç kez başlatıldı: üçü de **aynı PID**'de (farklı thread'ler), her oturum varlık 0 / kare 0,
önceki oturumun id'si (dosyada taşındı) ölü, `hata=0`, kapanışta canlı vk 5 / 5 / 5, RSS 116 → 119 → 119
MB, çökme yok. Bulunan tek bayatlık: log satırının kare öneki `g_log`'da (Bridge'in dışında) ve
yalnız kare sonunda eşitleniyordu — ikinci ve üçüncü oturumun kurulum satırı `k60 bilgi kurulum`
basıyordu. Kapanış artık onu sıfırlıyor; `bridge_second_session_*` kapısı kurulum satırını
yakalayıp `k0` arıyor (düzeltmesiz: 5 kontrol KIRMIZI, `k32`). Hata/uyarı sayaçları bilerek
sıfırlanmıyor (kapanıştan sonra okunuyor — `test_game_channel`). Fiziksel cihazda (Mali) ölçülmedi.

**Düzeltme:** kapanışın sonu oturumun geri kalanını bırakır (profiler, kare arenası, `sys.release()`)
ve `Bridge`'i yıkıp statik deposunda yeniden kurar: kapanıştan sonraki durum "hiç kurulmamış" ile
aynı. Korunan iki şey: Vulkan yükleyicisi (`b.api`, başarılı yolda açık kalır — bir sonraki
`vk_api_load` aynı kütüphaneyi bulur) ve varlık **nesilleri** (kapanışta canlı yuvanın nesli bir
artırılır: önceki oturumun id'si yeni oturumdaki varlığa denk gelmez, ölü id sözleşmesi korunur).
Kurulum öncesi ayarlar (`eng_bloom`, `eng_set_headless`, kamera, tema) yeni oturuma taşınmaz.
Kapanış raporuna yeni satır: `kapanis (vk oturum): N kurma, cihaz yikilmadan once canli K havuzsuz nesne`
— renderer ve hedefler yıkıldıktan sonra yalnız cihazın kendi nesneleri kalır (RTX 5080: 5), oturumdan
oturuma değişmemeli. Ölçü aletleri ABI'de: `eng_vk_live`, `eng_virtual_mb`, `eng_thread_count`.

**Kapı:** `tests/test_bridge.cpp` `bridge_second_session_in_same_process_starts_clean` — ısınma +
5 oturum; her oturumda varlık 0 / kare 0 başlangıcı, bir kare offscreen çizilip PPM okunur (sahne-boş
37402 px, ilk oturumun karesinden fark 0 px — RTX 5080; CI macOS'un sanal GPU'sunda, Apple
Paravirtual, sahne-boş 0 px ölçüldü ve piksel bloğu görünür ATLANDI), eski id ölü, hata sayacı sabit, kapanışta
canlı vk eşit; süreç ölçüleri **oturum başına artışların en küçüğü** olarak: sanal < 256 MB, RSS
< 4 MB, thread toplamı < 5. Tulpar: `tulpar/tests/engine_iki_oturum.test.tpr` (ısınma + 4,
`tools/tulpar_dogrula.sh --tam`). Neden uç fark değil (8cs dersi): ikinci oturumda **tek seferlik**
~+17–21 MB RSS ve +86..277 MB sanal ölçüldü, sonra düz (0–4 KB/oturum). Kaynağı glibc yığını:
`mallinfo2` "kullanımda" 0,76 → 0,88 MB iken "arena" 17 → 40 MB — boşaltılmış ama sisteme
verilmemiş tepe noktası; sanaldaki ara sıra +64 MB basamaklar thread başına malloc arenası rezervi
(RSS'e yansımıyor). Açık kalan küçük artış: "kullanımda" oturum başına ~+8 KB (10 oturumda 0,88 →
0,93 MB) — sürücü/yükleyici mi bizim mi, ayrıştırılmadı.
İlk eşik ("en çok bir oturum +4 MB RSS aşar") CI lavapipe'ta düştü: orada RSS her oturumda büyüyor
(C++: +1,2..2,9 MB/oturum; Tulpar: +1,7..6,7 MB), canlı vk nesnesi sabit (4). Aynı kod RTX 5080'de
0–4 KB, MoltenVK'de 80–272 KB/oturum büyüdüğü için bu **sürücü davranışı** olarak ölçülüp basılır,
iddia edilmez (llvmpipe her cihazda shader'ları LLVM ile yeniden derliyor; kaynağı ayrıştırılmadı).
Bu yüzden ölçü en küçük artış: sürücünün ara sıra büyümesi onu oynatmaz, oturum başına sabit bir
kaçak (arena, worker) her oturumda görünür.
**Pozitif kontrol** (aynı gün, aynı makine): düzeltmesiz köprüde C++ kapısı ikinci oturumda 5 kontrol
KIRMIZI + SIGSEGV, Tulpar testi çıkış 139 (özet yok → `tulpar_dogrula` DÜŞTÜ). Eşiklerin kendisi:
teste oturum başına 512 MB kaçak rezerv enjekte edilince her oturum +512..576 MB sanal (en küçük artış 512 MB ≥ 256) → KIRMIZI. (İlk denenen
kontrol — yalnız `sys.release()`'ı çıkarmak — kaçak üretmedi: `~Bridge()` içindeki `~SystemArena`
arenayı zaten bırakıyor. Kontrol, kapının ölçtüğünü değil düzeltmenin fazlalığını göstermiş olurdu.)

**Ders:** "kapat" sözleşmesi "ve yeniden açılabilir" demiyorsa, kapanış bir bayrak indirmekten ibaret
kalır. Yeniden kurulabilirlik tek oturumda değil **N oturumda** ve oturum başına artışla ölçülür; ilk
iki oturum arasındaki tek seferlik basamak (yığın tepe noktası) kaçak değildir, her oturumda tekrarlanan
artış kaçaktır.


**Fiziksel cihazda ölçüldü (Huawei P20 Pro CLT-L09, Mali-G72, Android 10, 2026-10-07):**
`tools/android_iki_oturum.sh` 5/5 ve 3/3 tur aynı PID'de, her oturum temiz (varlık 0, kare 0, önceki id
ölü, hata 0), kurulum satırları `k0`, p50 16,7 ms (60 Hz). Cihaz iki şey daha gösterdi:
- **Kapanış raporunun sayaçları süreç toplamıydı.** `g_log` süreç boyu yaşıyor, `Bridge` her oturumda
  sıfırlanıyor; rapor `g_log.errors/warnings`'i doğrudan basıyordu. Pencere gelmeyen 5 oturumun raporu
  "uyari 1, 2, 3, 4, 5" dedi. Artık rapor oturumun sayısını basıyor (`oturum_hata0/uyari0` tabanı);
  `teng_error_count()` süreç toplamı olarak kaldı (testler fark ölçüyor). Kapı:
  `bridge_shutdown_report_counts_this_session_only` (düzeltmesiz kodda "hata 1" — kırmızı).
- **Ölçüm düzeneği yalan söyleyebiliyordu:** EMUI'de logcat ana tamponu 256 KiB ve sistem çok konuşuyor;
  sonda `logcat -d` ile okunan 5 turluk koşumda tulpar satırlarının hepsi düşmüştü ("temiz oturum 0/5").
  Betik artık logu akış olarak dosyaya alıyor. Ekran kapalıyken etkinlik pencere almıyor, köprü 10 sn
  bekleyip headless kipe düşüyor; betik ekranı uyandırıyor ve pencere alamayan oturumu not olarak basıyor
  (pencereli yol ölçülmedi demek).

**Bağlı bayatlık: ortam değişkeni sürece aittir** (2026-10-09, emülatör Pixel_10_Pro_XL x86_64): `android_main`
`debug.tulpar.env`'deki her `AD=değer`'i `setenv` ile koyuyordu ama önceki oturumun koyduğunu hiç silmiyordu.
Etkinlik aynı süreçte yeniden yaratılınca (PID 25416, 18:07 ve 18:17) özellik yalnız `TULPAR_KKS_OTOPILOT=1`
iken 18:07'nin `TULPAR_KKS_TEST=h:1-16:8`'i yaşadı: "sonsuz otopilot" diye açılan oturum hikâye taramasını
x8'de koştu (logda `[kapi] hikaye test: kisim 1-16 kat 8`). Düzeltme (`bridge/android_host.cpp`): koyulan adlar
statik bir tamponda tutulur, her girişin başında `unsetenv` edilip logda `onceki oturumdan X silindi` basılır.
Cihazda pozitif kontrolü (iki ardışık `am start`, ikincisinde eski ad yok) bu düzeltmeyle henüz koşulmadı.

### 8cu. Yalnız Android'de derlenen liste masaüstü kapısından geçmez — ses biçimi "destekleniyor" ama telefona hiç gitmiyor

**Belirti** (2026-10-07, ilk gerçek oyun "Küpler ile Kürelerin Savaşı", Huawei P20 Pro / Mali-G72 /
Android 10): oyunun dört `muzik_*.mp3` dosyası masaüstünde çalıyor, telefonda çalmıyordu; log sessizdi —
oyun `file_exists` ile sorduğu için "dosya yok" deyip müziği atlıyordu. Cihaz satırları: `android: varlik
17 dosya` (APK'da 21 dosya vardı) ve `ses: 0 dosya, 9 sentetik ton`.

**Sebep:** Android host'u APK varlıklarını ilk açılışta dosya sistemine çıkarırken bir **uzantı
süzgeci** kullanıyor (Huawei'de kök listesi sistem kaplamalarını da getirdiği için, 8ag). Listede
`.wav` ve `.ogg` vardı, `.mp3` ve `.flac` **yoktu** — oysa kod çözücü (miniaudio,
`audio/miniaudio_impl.c`) WAV/MP3/FLAC çözer, Ogg çözmez. Liste `bridge/android_host.cpp`'nin içindeydi
ve o dosya yalnız Android derlemesinde derleniyor: masaüstündeki hiçbir kapı onu görmüyordu.

**Düzeltme:** liste `bridge/asset_filter.hpp`'ye taşındı (başlık-yalnız, iki tarafta aynı kod), `.mp3` ve
`.flac` eklendi, karşılaştırma büyük/küçük harf duyarsız (`MUZIK.MP3`). Kapı masaüstünde:
`bridge_android_asset_filter_keeps_every_decodable_audio_format` — çözülebilen her ses biçimi ve oyunun
kullandığı varlık türleri çıkarılır; KONTROL: uzantısız ad, `.tmp`, `.tpr`, `x.mp`, `a.mp3.bak` elenir.
**Pozitif kontrol:** `.mp3`/`.flac` listeden çıkarılınca kapı 4 kontrolle KIRMIZI (6/10). **Cihazda**
(aynı gün, aynı APK içeriği): eski süzgeçle `varlik 17 dosya`, `ses: 0 dosya`; yeni süzgeçle `varlik 21
dosya`, `ses: 4 dosya`.

**Ders:** platforma özgü dosyada duran bir **tablo** (uzantı, anahtar kodu, yetenek listesi) o
platformun kod yolu kadar görünmezdir. Tabloyu ortak bir başlığa al ve masaüstü kapısıyla ölç; ölçtüğü
şeyi de başka bir tabloya (burada kod çözücünün biçimleri) bağla ki biri büyüyünce öteki unutulmasın.

### 8cv. "Çağrı başına" ortalaması kare başına sabit bedeli böler — ve telefonda kare içi döngü soğuk önbellekle koşar

**Belirti** (2026-10-07, ilk gerçek oyun, Huawei P20 Pro / Kirin 970 / Mali-G72 / Android 10): kapanış
satırı `kare icinde 101149 kanca cagrisi, 671.25 ms, cagri basina 6636.2 ns`; masaüstünde aynı oyun
397 ns. "Kanca dağıtımı telefonda 16 kat yavaş" diye okundu (Geri bildirim #14).

**Sebep, iki katlı:** (1) Sayı, kanca aşamasının **duvar saati / çağrı**. Oyun kare başına ~4 çağrı
yapıyordu; aşamanın kare başına sabit bedeli (döngüler, çarpışma halkası, ilk çağrının soğuk kodu) dört
çağrıya bölününce "çağrı başına" şişti. (2) Telefonda aşama **soğuk önbellekle** koşuyor: vsync beklemesi
ve çizim, kareler arasında L1/L2'yi boşaltıyor (cihazda: aynı 200 çağrı sıcak döngüde 7.7 µs, kare içinde
75–80 µs; vsync'siz/penceresiz koşum 3–4 kat iyi). Soğuk önbellekte, masaüstünde **ölçülemeyen** bir
tasarım bedeli belirdi: `guncelle` dağıtımı her kare bütün varlık yuvalarını (100 baytlık `Ent`) ve bütün
sahne varlıklarını geziyordu — bağlı olmayan her yuva bir soğuk satır, yuva başına ~200 ns. 497 yuvalı,
30 bağlı varlıklı sahnede aşama kare başına 85–105 µs (masaüstünde 1 µs).

**Düzeltme:** yuva/sahne sıralı dağıtım listeleri (bağla/çöz yalnız kirli bayrağı kurar, liste aşamanın
başında kurulur; çağrı anında eski taramanın koşullarıyla doğrulanır), sıcak alanlar ilk satırda; kapanış
satırına **kare başına** süre. Telefonda F oturumu 85–105 → 32–45 µs/kare; masaüstünde değişmedi ya da
iyileşti (KOPRU §7.9 tablosu). **Pozitif kontrol** (`bridge_runs_a_scripted_game_headless`): listenin
sıralaması kaldırılınca bağlama sırası tersten yapılan üç varlığın `guncelle` sırası YANLIŞ (kapı kırmızı);
sahne listesi kurulmayınca sahne `guncelle`leri hiç gelmez (3 kontrol kırmızı).

**Ders:** (a) Ortalamayı paydasıyla yaz: "çağrı başına" yanına "kare başına" — seyrek olayda biri
ötekini yalan söyletir. (b) "Bütün yuvaları gez, işaretliyi seç" masaüstünde bedava görünür (96 MB L3,
sıcak), telefonda her kare soğuk satır başına yüz nanosaniyeler. Kare içinde seyrek bir alt kümeyi
dolaşan döngü, alt kümeyi **liste** olarak tutsun; bedeli ancak cihazda ölç (`tools/android_kanca_olcumu.sh`).
Dikkat: kazanç taşınabilir — taramayı bırakan aşamanın soğuk satırlarını sonraki tam tarama (animasyon,
çizim) öder; toplamı da ölç (burada `teng_frame_end` CPU zamanı gürültü düzeyinde değişti).

### 8cw. Metin biçimi kapısı yeşil, blob alanı taşımıyor — ve köprü sistemi hiç adımlamıyor

**Belirti** (2026-10-07, ilk gerçek oyun "Küpler ile Kürelerin Savaşı", Geri bildirim #3): `.sahne`deki
meşale yayıcıları (`partikul_renk 1 0.75 0.2 0.9 0.2 0.05`) editörde turuncu alev, derlenmiş oyunda
**hiçbir şey** değildi. Üç ayrı sessiz kayıp üst üste binmişti:

1. **Blob alanı taşımıyordu.** `SceneBlobParticle` (v6, 48 bayt) yalnız doğum hızı/ömür/boyut/hız/dağılım
   taşıyordu; renk, yerçekimi, türbülans, sürtünme, çarpışma, alt yayıcı, şekil, şerit (11 alan) yazılıp
   **ayrıştırılıyordu** ama blob'a girmiyordu. `tools/scene_check.py` yazıcı↔ayrıştırıcı uyumunu ölçüyordu,
   blob'u hiç görmüyordu: kapı yeşildi.
2. **Köprü `SceneRuntime::update`'i hiç çağırmıyordu.** Yayıcılar `engine_demo`da doğuyordu, `eng_scene_load`
   ile yüklenen sahnede doğmuyordu — örnek sahnelerin hiçbirinde parçacık olmadığı için kimse görmedi.
3. **Doğum hızı yanlıştı.** Runtime her çağrıda `Rng(++kare)` kuruyordu; xorshift32'nin küçük tohumdan ilk
   çıkışı ~tohum/16384 (tohum 1 → 6.3e-5), yani "kesir olasılığı" testi binlerce kare hep tuttu: 14/s
   isteyen yayıcı 60/s doğurdu (RTX 5080, aynı gün: ~10 yerine 43 canlı parçacık).

**Düzeltme:** blob **v9** — `SceneBlobParticle` 112 bayt, bütün alanlar; çevirim tek yolda
(`scene_blob_particle_of` → `scene_particle_emitter`) ve **editör de aynı fonksiyonları çağırıyor** (editörde
görülen = oyunda çıkan; editörün alt yayıcısı da artık gerçekten doğuyor, eskiden `spawn_on_death_count` 0
kalıyordu). Köprü yayıcıları **sim adımıyla** (sabit adım) güncelliyor. Üreteç tek ve kalıcı (init'te sabit
tohum). Şerit isteyen yayıcı runtime'da çizilmiyor: sayılır, köprü `UYARI` basar.

**Kapı:** `scene_check.py` "kısmi taşıma" — bir bileşen grubunun (alan öneki) tek bir alanı bile blob
derleyicisinde geçiyorsa, yazıcının yazdığı bütün alanları da geçmeli ya da gerekçesiyle muaf yazılmalı
(muaf sayısı her koşuda basılır; bugün 7 ışık alanı: spot konisi, alan ışığı, gölge, godray — runtime
nokta ışığı çiziyor). **Pozitif kontrol:** kapı `origin/main` ağacında 11 alanla KIRMIZI; `--oz-sinama`
4 fikstür. Testler: `scene_blob_v9_particle_fields_reach_runtime` (metin → blob → doğan parçacığın rengi/
yerçekimi/türbülansı; alt yayıcılı tepe 54 / alt yayıcısız 16; belirlenim), `scene_runtime_particle_rate_matches_authored`
(ort. 10.04 canlı, beklenen 9.80; KONTROL eski yöntem 41.54), `bridge_scene_particles_spawn_with_authored_color`
(yeşil yayıcı: 60 çizim, 472 yeşil piksel; KONTROL hız 0: 0/0; düzeltmesiz köprüde 0/0 → KIRMIZI).

**Ders:** "yazılır ↔ okunur" kapısı formatın **bir** sınırını ölçer. Veri her sınırı geçerken (metin →
model → blob → runtime → köprü) ayrı ölçülmeli; ve bir sistemin var olması, kare döngüsünün onu
çağırdığı anlamına gelmez — uçtan uca kapı (köprü + piksel) ister.

### 8cx. Sıfırla doldurulmuş tutamaç dizisi "geçerli" — eksik LOD ilk mesh'le çizilir, yalnız ekranın kenarında

**Belirti (2026-10-08, ilk gerçek oyun, Huawei P20 Pro):** kullanıcı "küreler bazı alanlarda küp gibi,
küplerin içinde başka küp var; ekranın ortasına doğru kayboluyor", "lazer kenarda duvara dönüşüyor",
"kenardaki birliklerin rengi değişiyor" dedi. Hepsi ekranın sol/sağ kenarına yakın.

**Sebep:** `content::upload_model` LOD tutamaç dizisini `alloc_array_zeroed` ile ayırıyordu ve yalnız
üretilen seviyeleri yazıyordu. `MeshHandle` geçersizi `0xFFFFFFFF`; **sıfır geçerli bir id** — renderer'ın
ilk oluşturduğu mesh. `draw_model` uzaklığa göre (20/45 m) LOD seçip "seviye yoksa bir alta in" diyordu ama
`valid()` sıfır tutamaç için `true` döndüğü için inmedi: LOD'u üretilmemiş her düşük poligonlu model (12
üçgenli sopa, kalkan, lazer) kameraya 20 m'den uzakta mesh #0 ile — oyunda ilk yüklenen model, kafa küpü —
çizildi. 2:1 ekranda kameranın merkezde baktığı birlik ~18 m, kenardaki ~24 m uzakta: eşik tam ekranın
kenarlarına düşüyordu. İzole sondada ölçüldü (RTX 5080, aynı gün): x = ±18 m'deki 1 × 0.12 × 0.12 sopa 0.76 m
küp olarak çizildi; kamera 18 m kayınca aynı nesne merkezde ince sopa, yeni kenardaki nesne küp.

**Düzeltme:** tutamaçlar ayrılınca `MeshHandle{}` (geçersiz) ile doldurulur. **Kapı:**
`content_missing_lod_falls_back_to_lod0` — 12 üçgenli küp (LOD üretilmez) 100 m'den LOD0 seçer ve tutamaçları
geçersiz; düzeltmesiz kodda kırmızı (ölçüldü: geçersiz 0/2, seçim 0/0/1). **Pozitif kontrol:** LOD'lu küre
aynı uzaklıktan LOD2 seçer.

**Ders:** "geçersiz" değeri sıfır olmayan bir tutamacı **sıfırlayan** ayırıcıyla doldurmak, ilk kaynağa
sessizce işaret eden geçerli tutamaçlar üretir. Ve uzaklığa bağlı bir seçim hatası ekranın **konumuna**
bağlı görünür: "kenarda bozuluyor" belirtisi geometrik değil, seçimdir.

### 8cy. Öklid uzaklığına göre LOD ekranın x'ine bağlıdır — geniş ekranda kenar, aynı derinlikteki merkezden "uzak"

**Belirti (2026-10-08, Huawei P20 Pro 2240x1080):** 8cx düzeltildikten sonra da aynı derinlikteki birlikler
ekranın ortasında LOD0, sol/sağ kenarda LOD1 çiziliyordu; kamera yana kayınca seviye nesneyle değil ekranla
birlikte "yürüyordu".

**Sebep:** `content::draw_model` seviyeyi kamera → model kökü **öklid** uzaklığıyla seçiyordu (20 / 45 m).
Ekrandaki boy ise `r * proj[1][1] / w`'dir ve `w` **görüş derinliğidir**: aynı derinlikteki iki özdeş model
ekranın her yerinde aynı boyda görünür, ama kenardakinin öklid uzaklığı yanal kayma kadar büyüktür. 2:1
ekranda, köprü kamerası (π/3.5) ile 18 m derinlikte merkez 18.00 m, kenarın %95'i 24.81 m — eşik (20 m)
tam ekranın içine düşüyor (`content_lod_uses_view_depth_not_screen_x`'in bastığı sayılar, RTX 5080 değil
saf CPU hesabı, 2026-10-08).

**Düzeltme:** ölçüt `|dot(p - göz, ileri)| * depth_scale` (`ModelLod::set_view`, `content::model_lod_level`).
Eşiklerin anlamı **ekranın ortasında** aynen korunur (orada derinlik = uzaklık); `depth_scale = tan(fov/2) /
tan(π/3.5 / 2)` köprü/demo varsayılan FOV'unda tam 1, FOV daraltılınca (yakınlaştırma) < 1 → daha ince LOD —
yani ölçüt yansıtılan boyla orantılı. Model yarıçapı ölçüte girmez (eşikler model başına anlam
değiştirmesin). Kamera arkası mutlak değer: yalnız gölgeye giren uzak verici LOD0'a düşüp gölge geçişini
pahalılaştırmasın. `camera_forward = {0,0,0}` eski öklid yolu. Köprü (varlık modelleri + sahne blob'u),
demo ve editör görüş derinliğine geçti.

**Kapı:** `content_lod_uses_view_depth_not_screen_x` — aynı derinlikte merkez ve kenar (NDC x 0.95, gerçek
projeksiyon matrisiyle; `w` eşitliği ölçülür) aynı seviyeyi alır; 30 m LOD1, 50 m LOD2, FOV yarıya inince
30 m LOD0. **Pozitif kontrol:** aynı ikili öklid ölçütüyle ayrışır (merkez 0, kenar 1) — düzeltmesiz kodda
kapı kırmızı.

**Ders:** "uzaklık" ekran uzayında ölçülen bir şeyin vekiliyse, vekilin dünya uzayındaki tanımı ekranın
şeklini taşır. Geniş ekran bu farkı büyütür; 4:3'te göze batmayan hata 2:1'de kenarı keser.

### 8cz. Sabit gölge eğilimi kademeli haritada her kademeye yanlış — dış kademe akne, iç kademe kaçma

**Belirti:** CSM'den (3 kademe) sonra eğilim (`shadow_bias` 0.0008, `shadow_normal_offset` 0.06 m) bütün
kademelerde **aynı** dünya değeriydi. Akne ve kaçma (peter-panning) texel'in **dünya ölçüsüne** orantılıdır;
kademeler arası texel oranı 3 × 3 = 9. Köprü varsayılanında (R 17, 3 × 1024) texel 0.0037 / 0.0111 /
0.0332 m: normal kaydırması iç kademede **16 texel** (gölge temas noktasından kopar), dış kademede 1.8
texel (3x3 PCF'in ±1 texel'i ile akne sınırında).

**Düzeltme:** eğilim ve normal kaydırması kademe başına `texel_i / texel_ref` ile çarpılır; `texel_ref` =
hacmin tamamını `shadow_bias_ref_tile` (varsayılan 2048) texel'lik **tek** haritayla kaplayan kurulumunki —
değerlerin ayarlandığı CSM öncesi 1 × 2048 harita. 1 × 2048'de çarpan tam 1 (eski davranış), 3 × 1024'te
2/9 / 2/3 / 2. `mesh.frag` çarpanı kademe matrisinden türetir (ortografik kutunun satır 0 boyu tam 1/r,
look_at dönmesi birim); `shadow_params.x` artık `1/texel_ref` (eskiden kullanılmayan `1/boyut`).
`shadow_bias_ref_tile = 0` ölçeksiz eski yol. UBO yerleşimi değişmedi.

**Kapılar:** `renderer_shadow_bias_scales_with_cascade_texel` (CPU: çarpan kesin artar ve texel oranına
eşit; 1 × 2048 → 1; shader türetmesi gerçek ışık matrisinde CPU çarpanıyla 9e-8 bağıl farkla aynı;
**pozitif kontrol** `ref_tile = 0` "kesin artar"ı geçemez). `renderer_shadow_bias_outer_cascade_has_no_acne`
(piksel: yalnız zemin, R 45, 3 × 256; gölge açık/kapalı farkında koyulaşan her piksel aknedir — ölçekli 7
piksel %0.02, **pozitif kontrol** sabit eğilim 24586 piksel %60; RTX 5080, 2026-10-08).
`renderer_shadow_map_actually_darkens`'in negatif kontrolü artık **en iç** kademede 6 m kaydırır (nominal
27 m): düz 6 m iç kademede 1.3 m'ye inince gölge kaçmıyordu (963 / 2397 koyu, kontrol kırmızıydı).

**Kalan:** kademeler arası harmanlama yok — sınırda texel boyu birden 3 kat değişir; maliyeti (bantta ikinci
9 dokunuşlu PCF) Mali'de ölçülmeden eklenmedi.

**Ders:** dünya biriminde tek bir eğilim, çözünürlüğü değişen bir haritada bir **texel sayısı** değil; ayarı
hangi texel'de yapıldıysa orada doğrudur. Ölçeklemeyi parametrenin tanımına yaz, yoksa kademe/tile değişikliği
eğilimi sessizce bozar.

### 8da. İskeletli model klip atanmadan çizilmez — ama "çizildi" sayılır, ve kapı boş ekranı "statik" sanar

**Belirti:** `content::draw_model` iskeletli mesh'i yalnız bir **poz** verilince çiziyordu (`pose == nullptr`
→ atla). Köprüde `eng_spawn_model` + klip atamamak = model **görünmez**; hata yok, uyarı yok, LOD sayacı
"LOD0'da çizildi" diye artıyor. Köprü kapısının animasyon bölümü (`bridge_runs_a_scripted_game_headless`
§10) "statik kare" ile "pozlu kare"yi karşılaştırıp farkın > 100 piksel olmasını istiyordu — statik kare
aslında **boş ekrandı**, fark modelin kendisiydi; kapı "animasyon pozu değiştiriyor" değil "model bir kez
çizildi" ölçüyordu. Küp-küre oyununun yüzlü karakterleri (30 iskeletli glTF) eklenirken çıktı.

**Düzeltme:** pozu olmayan iskeletli mesh **bind pozunda** çizilir: skin matrisi = model[j] · ters_bind[j]
ve bind pozunda model[j] tam ters_bind'in tersidir, yani her eklem için birim matris (statik
`kBindPose[kModelMaxJoints]`, kopyası yok). Animasyonlu yol değişmedi.

**Kapı:** §10'a BOŞ kare eklendi (model kurulmadan önce): `statik(bind)-boş > 100` ve `pozlu-boş > 100`;
eski kodda `statik(bind)-boş = 0` → kapı **kırmızı** (pozitif kontrol, 2026-10-08 RTX 5080: düzeltmeyle
2678 / 2514 piksel, düzeltmesiz 0 / 2514). Karakter tarafı:
`content_skinned_character_face_clips` — 17 eklemli iç içe iskelet, 8 klip, ölçek izleri; X gözleri `bekle`de
ölçek 0 ile tek noktaya çöker, `ol`da yayılır (kontrol: göz beyazında ters sonuç).

**Ders:** "çizilemedi"yi "çizildi" sayacına yazan dal, sessiz görünmezliğin ta kendisi. Piksel kapısının
karşılaştırdığı iki karenin İKİSİNDE de nesnenin göründüğünü ayrıca ölç — farkın kaynağı "nesne yok" olabilir.

### 8db. Yeniden kullanılan PCM tamponu, karıştırıcı onu okurken yazılır — "durdur" komutu henüz tüketilmedi

**Belirti (tasarım aşamasında, hikâye modu M3b, 2026-10-09):** döngü müzikleri iki **sabit** tampona
(müzik yuvası) çözülüyor ki arena her parçada ~23 MB büyümesin. Saf yol "yuvayı çalan sesi durdur, sonra
yeni parçayı yaz" — ama `Mixer::stop` yalnız komut halkasına yazar; ses iş parçacığı onu **bir sonraki**
callback'in başında tüketir. O an süren callback tamponu hâlâ okuyordur: oyun iş parçacığı aynı belleğe yeni
parçayı yazarken yarım eski / yarım yeni örnek çalınır (çıtırtı) ya da tampon sınırında eski uzunluk okunur.
Hiçbir kapı bunu görmez: yarış nadirdir ve NULL arka uçta duyulmaz.

**Düzeltme:** `teng_music_slot_load` (1) yuvanın klibini çalan her sesi durdurur (köprünün ses kaydı artık
klip dizinini de tutar), (2) karıştırıcının callback sayacı durdurmadan **sonra** okunan değerden bir
ilerleyene kadar bekler — o an komuttan önce başlamış callback bitmiştir, sonrakiler sesi görmez —, (3)
yazma boyunca klibi boş (`frames 0`) tutar: o aralıkta çalma isteği HATA ile reddedilir. Cihaz duraklıyken
(arka plan) callback yoktur, okuyan da yoktur: beklenmez. 500 ms içinde callback gelmezse yazılır ve
**sayılır** (`kapanis (muzik yuvasi) ... zaman asimi N`).

**Kapı:** `bridge_music_slot_reuses_buffer_and_rejects_long_track` (NULL arka uç): yuva **çalarken** 20 kez
yeniden yüklenir; her turdan sonra çalan parçanın tepesi yeni parçanınki (0.6 / 0.3, yanlış 0), arena
değişmez (60182104 B üç ölçümde), bekleme 20, zaman aşımı 0. KONTROL: 65 s'lik parça `-2` + HATA +
"uzun red 1"; boş yuva çalınamaz.

**Ders:** kilitsiz komut halkasında "gönderdim" ile "karşı taraf gördü" ayrı olaylardır. Paylaşılan belleği
yeniden yazmadan önce karşı tarafın komutu **tükettiğini** gösteren bir sayaç (burada callback sayacı) bekle.

### 8dc. Silinemeyen havuzdan her yüklemede yeniden ayırmak — 5. bölüm geçişinde sahne sessizce görünmez

**Belirti (M5 ölçümü, 2026-10-09):** hikâye modu kısım geçişinde diyar sahnesi değiştirecek; önce "yükle/boşalt
sızdırıyor mu" ölçüldü (`olcum_sahne.tpr`, kup-kure `savas.sahneb`, 20 tur). RSS yükleme başına ~720 KB —
eşiğin çok altında. Ama kapanış satırının `malzeme` sayısı 1 yüklemede 195, 3'te 387, 5'te **512** (renderer
`max_materials` tavanı). `SceneRuntime::init` her çizim kaydına `create_material` çağırıyordu; renderer'da
malzeme silme yok. Tavan dolunca `create_material` geçersiz tutamaç döner, `draw()` geçersiz malzemeli varlığı
**atlar**: zemin, kaleler, kayalar çizilmez — hata yok, uyarı yok, `hata=0`. Mesh ve doku tarafı bu yüzden
yıllardır korunuyordu (ilkel mesh tablosu renderer başına bir kez), malzeme unutulmuştu.

**Düzeltme:** `SceneRuntime` malzeme havuzunu **sahneler arası kalıcı** tutar (`mat_pool_`): yeniden yüklemede
aynı tutamaçlar yeni PBR değeriyle (`set_material_pbr`, yalnız UBO; doku/descriptor aynı — hepsi varsayılan doku
+ beyaz) kullanılır, havuz en büyük sahnenin çizim sayısı kadar büyür. `set_material_pbr` reddederse (bayat
tutamaç) yenisi kurulur. Kurulamayan malzeme artık **sayılır** ve köprü sahne yüklerken HATA basar
("o varliklar CIZILMEZ"); kapanışta `kapanis (sahne malzemesi): N kuruldu, M yeniden kullanildi`.
Kalan: yeniden kullanımın UBO yazımı uçuştaki karenin okumasıyla örtüşebilir (tek karelik ton farkı,
geçiş anında; ölçülmedi, bilinçli).

**Kapı:** `bridge_scene_reload_reuses_materials` — 40 ilkelli üretilmiş sahne 8 kez yükle/boşalt: malzeme
43 = tek yüklemeninki (havuzsuz 323 olurdu), 40 kuruldu, 280 yeniden kullanıldı. Oyunda (21 yükleme) 99 sabit.

**Ders:** "yükle/boşalt"ın bedeli RSS değil **en küçük havuzun tavanıdır**. Yeniden yüklenebilir her kaynağın
sayacını kapanış satırına koy ve N kez yükleyip sayacın düz kaldığını ölç; dolan havuzun "atla" dalı ayrıca
HATA saymalı.

### 8dd. GPU'ya giden veri kalıcı arenada kalır — 75 modelin 34 MB'ı kimsenin okumadığı CPU kopyası

**Belirti (yayın adımı 3, 2026-10-13, RTX 5080 penceresiz):** Küp-Küre'nin 75 modeli (13 MB glTF) kapanış
satırında `arena 34449 KB toplam (model basina ~459 KB)`. `gltf_load` her şeyi köprünün kalıcı arenasına
(`b.sys`) koyuyordu: çözülmüş RGBA pikseller, vertex/indeks/LOD dizileri, ve klip **sıkıştırılmadan önceki
ham izleri** (eklem x örnek x 40 B; 20 klipli karakterde asıl bedel). `upload_model` bunları GPU'ya kopyaladıktan
sonra köprüde hiçbir yol okumuyor (çizim tutamaçla, poz sıkıştırılmış klip + iskeletle) ama bump arenada tek tek
bırakılamazlar. Mali gibi birleşik bellekte bu, aynı verinin ikinci kopyası demek.

**Düzeltme:** köprü modeli önce `b.sys`'ten bir kez oyulan 32 MB'lık **yükleme arenasına** (`model_tmp`,
`ReturnNull`) yükleyip GPU'ya gönderir; `content::model_compact` çizim/poz için gerekeni (örnekler, mesh'in
malzeme/iskelet alanı, malzemeler, eklemler + ters bind, tek blok sıkıştırılmış klip) kalıcı arenaya **derin**
kopyalar, yükleme arenası geri sarılır. Kopyada CPU geometri/piksel işaretçileri `nullptr` (sayılar kalır) —
`upload_model`'e yeniden verilemez. Yükleme arenasına sığmayan model eski yoldan yüklenir ve **sayılır**
(`sigmayan N`). Oyunda: kalıcı 34449 KB -> 4159 KB, yükleme arenasının tepesi 676 KB; süreç RSS'i ~31 MB düştü.

**Kapı:** `content_model_compact_pose_bit_identical` — karakter_kup (8 klip): yükleme arenası geri sarılıp 0xCD
ile ezildikten sonra 40 pozun skin matrisleri ezilmeden öncekiyle bit bit aynı; kopyanın yükleme arenasını
gösteren işaretçisi 0. POZİTİF KONTROL: düz struct kopyası aynı denetimde 16 işaretçiyle yakalanıyor.

**Ders:** "yükleme anında, kabul" denen her ayırmanın **ömrünü** sor: GPU'ya kopyalanan, sıkıştırılmadan önceki
ya da yalnız kurulumda okunan veri kalıcı arenaya değil geri sarılan bir arenaya gider. Kapanış satırı kalıcı
bedeli ve bırakılanı ayrı yazsın.

### 8de. targetSdk 36'da geri tuşu NativeActivity'ye hiç gelmez — "oyuna bağlandı" logu basılır, uygulama kapanır

**Belirti (2026-10-16, Pixel 10 Pro XL emülatörü, API 37):** oyun ekranında sistem geri tuşu oyunu ana
menüye döndürmek yerine **uygulamayı kapattı**. Kapanış satırı `geri tusu: 0 basis, oyuna bagli evet`, hemen
önünde `host kapatma istedi`. Yani köprü geri tuşunu oyuna bağladığını sanıyordu, ama hiçbir basış ulaşmadı.

**Sebep:** TulparLang'in manifesti targetSdk 34'ten 36'ya çıktı (Play şartı, yayın adımı 2). API 36 hedefinde
öngörülü geri (predictive back) varsayılan açık: sistem `KEYCODE_BACK`'i uygulamaya **göndermez**, geri
hareketini `OnBackInvokedDispatcher`'a verir; kayıtlı geri çağrı yoksa etkinliği bitirir. `android_host.cpp`
geri tuşunu `AKEYCODE_BACK` girdi olayından yakalıyor — o olay artık hiç gelmiyor. NativeActivity'de Java kodu
yok (`hasCode=false`), geri çağrı kaydedilemez.

**Düzeltme:** derleyicinin manifestinde `<application android:enableOnBackInvokedCallback="false">`
(TulparLang `yerel/aab` 722f2807). Düzeltilmiş APK'da geri tuşu haritadan ana menüye döndü, etkinlik açık kaldı
(debug ve yayın APK'sı, aynı emülatör). Google bu muafiyeti ileriki bir hedef API'de kaldırabilir: o gün
geri tuşu JNI ile `OnBackInvokedCallback` kaydı ister.

**Ders:** hedef API yükseltmesi davranış değiştirir, derleme hatası vermez. "Bağlandı" logu bağlantıyı
değil niyeti söyler: geri tuşu gibi platform olaylarının kapanış sayacı (`0 basis`) cihaz testinde okunmalı.
Hedef API her yükseldiğinde geri tuşu, yön ve kenardan kenara ekran cihazda elle denenir.

### 8df. Ölçeği "doğuşta küçük kur, karede büyüt" kalıbıyla kurulan varlık, büyütme yolunu atlarsa sessizce görünmez kalır

**Belirti (2026-10-16, kullanıcının telefonu; masaüstünde yeniden üretildi, RTX 5080, h:4-4:4):** hikâye
modunun ilk boss'u Koca Yumru sahada yoktu: üstte boss can çubuğu azalıyor, cephede havada yalnız bir can
çubuğu asılı, birlikler "boşluğa" vuruyordu. Log temiz: model 57 `k_boss_yumru.gltf` yüklenmiş (1 mesh, 1
doku, 20 klip), `BOSS: Koca Yumru sahada`, hata 0 uyarı 0. Topuz, Paşa ve Kazan da aynıydı; Diken/Golem
evrelerini kendi `model_koy`'uyla yeniden kurduğu için görünüyordu.

**Sebep (oyun):** `hk_birlik_uret` her görseli `olcek_ver(gorsel, 0.1 * boy)` ile kurar; doğuş büyümesi
(`dogus_t`, 0.32 sn'de 2 × boy'a) `hk_birlik_gorsel`'de koşar. Boss'u `boss.tpr` sürer ve `hk_govde_sur`
boss için `hk_birlik_gorsel`'i bilerek atlar (yön/klip `boss_gorsel`'de) — büyüme de onunla birlikte atlandı.
Boss bütün savaş boyunca 0.1 × boy'da (20 kat küçük) çizildi. Motor açısından her şey doğruydu: varlık canlı,
görünür, çizim sayısına giriyor. İlk şüpheli olan yükleme sonrası kalıcı kopya (8dd) temize çıktı: aynı
yoldan gelen iskeletli birlikler doğru boyda çiziliyordu ve `model_compact` sınırları (`bounds_*`) aynen taşır.

**Düzeltme:** büyüme `hk_dogus_buyu`'ya ayrıldı, `boss_gorsel` de çağırır. Motor tarafı **ölçüm**:
`eng_drawn_height(id)` (Tulpar `cizilen_boy`) çizim yolundaki hesabın aynısını döner — model için ölçek ×
model sınır kutusu yüksekliği, gizli varlıkta 0. Oyun her boss için doğuştan sonraki en küçük
`cizilen_boy / boy`'u basar (`[kapi] boss gorunum`).

**Kapı:** köprü `bridge_drawn_height_follows_scale_and_visibility` (iskeletli + klipli model: ölçek 2.0'da
4.000 m, 0.1'de 0.200 m — oran 0.050 = POZİTİF KONTROL, ölçek gerçekten ölçülüyor; gizli 0; ölü id HATA).
Oyun: `tulpar_dogrula.sh --hikaye` beş boss'un `min_oran >= 1.0`'ını ister (ölçüldü 2.28..2.99); POZİTİF
KONTROL `TULPAR_KKS_BOSS_BUYUME_YOK=1` → Koca Yumru 0.11.

**Ders:** "kur, sonra her karede düzelt" kalıbında düzeltme bir **yol**dur; varlığı o yoldan çıkaran her özel
durum (boss, araba, pilon) düzeltmeyi de çıkarır. "Yüklendi, doğdu, görünür, hata yok" görünürlüğü kanıtlamaz:
çizilen boyu ölç.

### 8dg. Fizik dünyası bölüm geçişinde boşaltılır ama sıfırlanmaz — bölüm N'in sonucu önceki bölümlere bağlı

**Sınıf** (2026-10-18, Küp-Küre hikâye taraması, RTX 5080 / Linux): `eng_scene_unload` gövdeleri
fizikten çıkarıyor, Jolt `PhysicsSystem`'i ise yerinde kalıyordu. Aynı gövdeler aynı sırayla yeniden
eklenince bile Jolt kimliği (indeks + yuva başına sıra no) **geçmişe** bağlı: boş yuva listesinin sırası
ve her yuvanın kaç kez kullanıldığı. Köprü çarpışma ve tetik olaylarını bu kimliğe göre sıralıyor
(belirlenim için); motorun `FixedStep` birikimi de bölümden bölüme taşınıyordu (ağır çekim artığı).
Sonuç: `h:1-16:4` taramasında 1.3 tick 4420'de, tek başına koşulan 1.3'ten ayrıştı (oyun tarafındaki
otopilot sızıntısı kapatıldıktan sonra). Kapılar yeşildi, çünkü her kapı **tek bir** sırayı ölçüyordu.

**Düzeltme:** `teng_physics_reset` / `fizik_sifirla()` (sahne boşken, gövdeli varlık yokken): Jolt
dünyası aynı yapılandırmayla yeniden kurulur, temas/tetik halkaları ve sabit adım birikimi sıfırlanır.
Çarpışma grubu tablosu, yerçekimi, zaman ölçeği ve varlık nesilleri korunur. Koşul tutmazsa HATA + 0
döner ve hiçbir şey değişmez. Kapı: `physics_reset_world_makes_history_irrelevant`. Geçmiş + sıfırlama
ile taze dünyanın kimlik ve durum özeti bit-tam aynı çıkıyor. **Pozitif kontrol** (aynı geçmiş,
sıfırlamasız): kimlik özeti farklı, durum özeti de farklı (ölçüldü). Yani geçmiş Jolt'un çözüm sırasını
da değiştiriyor. Oyun tarafı: `tools/tulpar_dogrula.sh --hikaye` 5j, her kısım tek başına ve taramada
bayt bayt aynı satırı vermeli (`TULPAR_KKS_SIZINTI=fizik|oto` pozitif kontrolleri). Köprü yarısı
(A13, 2026-10-19): `bridge_physics_reset_refuses_live_bodies_and_restores_fresh_world` (ret yolları +
taze oturumla bit-tam aynı bölüm; sıfırlamasız kontrol 20 yerine 23 temas).

**Ders:** "boşalt + yeniden yükle" ile "hiç oynanmamış" aynı durum değildir. Belirlenim iddiası tek
koşumun iki tekrarıyla ölçülürse geçmişe bağımlılık görünmez. Aynı içeriği **farklı geçmişle**
koşturup karşılaştırmak gerekir.

### 8dh. Önbellek anahtarı sabit tampona `snprintf` ile kesilir — "aynı yol aynı tutamaç" uzun yolda sessizce bozulur

**Sınıf** (2026-10-19, A13 incelemesi, RTX 5080 / Linux): `teng_image_load` aynı yolu ikinci kez
yüklememek için yuvadaki `char path[128]` ile `strcmp` yapıyordu; yuvaya yol `snprintf` ile yazılıyordu.
127 bayttan uzun yol kesilerek saklanıyor, sonraki çağrının tam yolu kesik anahtarla hiç eşleşmiyor,
her çağrı YENİ yuva alıyor ve 32'lik resim tavanı doluyordu. Hata yok, uyarı yok; tavan dolana kadar
her şey çalışıyor görünüyor. Ölçüldü: scratchpad `TMPDIR`'de (~125 baytlık yol)
`bridge_ui_image_draws_srgb_png_like_rect` DÜŞTÜ, kısa `TMPDIR`'de geçti — testin geçmesi yolun
kısalığına bağlıydı.

**Düzeltme:** yuva yolu 512 bayt; sığmayan yol HATA + -1 (sayılır, kesilip yüklenmez). Kapı:
`bridge_image_long_path_dedups_and_rejects_overflow` (306 baytlık yol aynı tutamaç, 706 bayt ret;
pozitif kontrol: aynı dosyanın kısa yolu farklı anahtar).

**Ders:** sabit tampona yazılan bir dizgi **anahtar** olarak kullanılıyorsa kesme sessiz bozulmadır;
yazmadan önce uzunluğu denetle ve sığmayanı reddet. Test geçici dizinleri kısa olduğu sürece kapı bunu
göremez — uzun yolu testin kendisi kurmalı.

### 8dj. Fizik kare süresiyle adımlanır, oyun tik'i ayrı biriktiricide — iki cihazda "tik/fizik gruplaması" ayrışır

**Belirti (N2b, 2026-10-23, iki süreç loopback):** kilit adımlı ağ düellosu (iki eş aynı komutları aynı tik'te
uyguluyor) yüzlerce özet eşitken bir anda ayrıştı. Hibrit sim birlik konumunu ve isabetleri fizikten okuyor;
fizik `frame_end`'de kare süresiyle, oyun tik'leri kendi biriktiricisiyle ilerliyor. Bir eşte kare "2 tik, sonra
2 fizik adımı", öbüründe "1+1, 1+1" olunca ikinci tik farklı konum okur. Bekleme karesi (rakip komutu gelmedi,
0 tik) fiziği yine adımlıyordu.

**Çözüm:** `eng_physics_steps(n)` (elle kip): oyun her kare attığı tik kadar fizik adımı ister; bekleme karesinde 0.
Aynı sınıfın ikinci yüzü oyunda: kare başına çağrılan betik kancaları (`enkaz_guncelle(id, dt)`) bekleme
karesinde de `dt` alıyordu — enkaz ömrü kare süresiyle dolup parça iki eşte farklı tik'te silindi (tik 11220 /
21060'ta ayrışma). Ağ maçında kancanın süresi tik saatinden (`n × D_TIK`).

**Genel kural:** kilit adımlı simde sonucu etkileyen HER şey tik sayacından ilerlemeli; kare süresi (`dt`, kanca
`dt`'si, zaman ölçeği) yalnız sunuma. Ham `teng_collision_*` sırası da iş parçacığına bağlı (yalnız betik
kancaları sıralı); özet/karar ham sıraya dayanmamalı (bridge_physics_reset kapısı bu yüzden dalgalıydı).

### 8dk. Ekranı aynalamak üçgen sarımını ters çevirir; ayna kameradan "taklit" edilirse sahnenin arkası görünür

**Belirti (N2b, 2026-10-23):** ağ 1v1'de katılan oyuncu kendi kalesini solda görsün diye kamera şeridin öbür
yanına alındı. Bu ayna değil, 180 derece dönüştür. Sahne tek yönden bakılmak üzere kurulmuştu ve arka plan dağları
kadrajın ortasına geldi. Işık arkadan vurdu, karakterler sırtını gösterdi.

**Kök:** gerçek ayna bir yansımadır (determinant −1). Görünüm ya da projeksiyon matrisine `S(-1,1,1)` girince
ekrandaki üçgenlerin sarımı CCW'den CW'ye döner. Arka yüz ayıklamalı boru hattı (`frontFace = CCW`) artık ÖN
yüzleri atar. Tek yüzlü zemin kaybolur, kürelerin iç yüzü görünür. Hata vermez, yalnızca "garip" bir kare çıkar.
Vulkan negatif viewport genişliğine izin vermez, dinamik `frontFace` ise 1.3 / EXT ister. Mali-G72'de buna
güvenilemez.

**Çözüm:** `Renderer::set_mirror_x` aynayı view'a koyar. Android ön-döndürmesi ve jitter clip uzayında kalır,
`camera_world` aynı göz konumunu verir. Sahne boru hatlarının CW ikizleri ilk açılışta kurulur. Ekran ışını ve
izdüşüm aynı aynayı köprünün `cam_frame`'inden alır: sağ vektör ters, tek kaynak. Kapı
`bridge_camera_mirror_flips_scene_not_hud_pick_consistent`: aynalı kare, aynasız karenin çevriğidir. Pozitif
kontrolü `TULPAR_ENGINE_AYNA_SARIM_ESKI=1` (sarım düz) ile fark 0.296 çıkar.

**Genel kural:** yansıma içeren her dönüşüm (ayna, negatif ölçek) sarımı ve sağ el kuralını ters çevirir. Böyle
bir dönüşüm ekleyen kod, ayıklama yönünü ve ekran↔dünya eşlemesini AYNI yerde çevirmelidir.
