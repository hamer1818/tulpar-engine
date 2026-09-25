// L6 CONTENT — Sahne veri modeli + deterministik metin format (.sahne) +
// islem gunlugu (geri al / yinele). PLAN L7 "The Truth": editorun tamami
// tek bir veri modelinin ustune oturur; her degisiklik bir islemdir.
//
// Sahne dosyasi = yazar formati (L6 icerik boru hatti, offline). Runtime bunu
// yorumlamaz (PLAN §6 "sahne bir blob + kod"); derlenmis blob sonraki dilim.
// Ayirma yok: sabit kapasiteli diziler, dosya okuma icin cagiranin arenasi.
// Format ASCII, satir tabanli, deterministik: ayni SceneDesc -> ayni bayt.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "sim/physics.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kSceneVersion = 1;
constexpr uint32_t kSceneMaxEntities = 256;
constexpr uint32_t kSceneMaxAssets = 16;
constexpr uint32_t kSceneNameLen = 32;   // NUL dahil
constexpr uint32_t kScenePathLen = 128;  // NUL dahil
// Betik alani bir AD degil YOLDUR: editor tarayicisi IKI kokten toplar (sahne
// dizini + deponun `tulpar/` agaci) ve ikisi de ozyinelemelidir, yani deger
// "tulpar/examples/engine_arena.tpr" gibi dizinli gelir.
//
// kSceneNameLen (32) YETMIYOR — olculdu 2026-09-22, depodaki bes .tpr'nin
// koke goreli uzunluklari: 17 / 32 / 34 / 35 / 35. Ucu sigmiyor.
//
// Ayri sabit, kSceneNameLen'i buyutmek DEGIL: o yol `name` ve `audio_clip`
// alanlarini da genisletir, blob metin tablosunu varlik basina ~96 bayt sisirir
// ve satir ici yeniden adlandirmayi (editor_widgets.hpp HierarchyRename) 127
// karakterlik adlara acardi. Ad 32'de yetiyor.
constexpr uint32_t kSceneScriptLen = 128;  // NUL dahil
// Denetci secicisi (editor_widgets.hpp `prop_asset`) satirlari `char[128]`
// olarak aliyor. Genislikler ayrilirsa secilen yol SESSIZCE kirpilir.
static_assert(kSceneScriptLen == kScenePathLen,
              "betik alani secici satir genisligiyle ayni olmali");
// Agac derinligi TAVANI (kok = 0). Tavan OLMAK ZORUNDA: ebeveyn zinciri veri
// dosyasindan gelir, yani dusmanca/bozuk girdi olabilir; ozyineleme yok, her
// yurume bu sayida adimda durur. Asilmasi sessiz kirpma DEGIL, hatadir.
constexpr uint32_t kSceneMaxDepth = 16;

enum SceneComponentBits : uint32_t {
  kSceneModel  = 1u << 0, // glTF model (kaynak indeksi + renk)
  kSceneAnim   = 1u << 1, // model klibi (iskeletli)
  kSceneLight  = 1u << 2, // nokta isik
  kSceneBody   = 1u << 3, // fizik govdesi (kutu / kure)
  kSceneCamera = 1u << 4, // kamera (fov, yakin, uzak)
  kSceneAudio  = 1u << 5, // ses kaynagi (klip, ses, perde, dongu, uzamsal)
  kSceneScript = 1u << 6, // tulpar betik bileseni (.tpr)
  // --- PR #331: prosedurel / arkaplan bilesenleri -------------------------
  // Hepsi kSceneScript'in (1<<6) USTUNDE duruyor; ayristiricinin "bu satiri
  // gordum mu" sentinel bitleri bu yuzden 1<<24'e tasindi (scene.cpp) —
  // eskiden 1<<8..1<<12'deydiler ve buradaki yeni bitlerle CAKISIYOR olurdu.
  kSceneCharacter = 1u << 7,  // karakter kontrolcusu (kapsul)
  kSceneParticle  = 1u << 8,  // partikul yayici (VFX)
  kSceneTerrain   = 1u << 9,  // yukseklik haritasi (arazi)
  kSceneVoxel     = 1u << 10, // voxel grid (greedy mesh)
  kSceneWater     = 1u << 11, // okyanus/su (gerstner)
  kSceneWind      = 1u << 12, // ruzgar alani
  kSceneNavAgent  = 1u << 13, // yapay zeka ajani (navmesh)
  kSceneJoint     = 1u << 14, // fizik eklemi (hinge, vb.)
  kSceneSkybox    = 1u << 15, // PBR gokyuzu kutusu
  kSceneRefProbe  = 1u << 16, // Yansima sondasi (IBL)
  kSceneReverb    = 1u << 17, // Ses yanki alani
  // --- GAS (Yetenek ve Envanter Sistemi) ---------------------------------
  kSceneHealth    = 1u << 18, // Can ve zirh verisi
  kSceneAbility   = 1u << 19, // Büyü / yetenek tanımları
  kSceneInventory = 1u << 20, // Envanter kapasitesi ve başlangıç eşyaları
};
// Bilesen bitlerinin TAMAMINI kapsayan maske (bit 0..20). Ayristirici, satir
// anahtarlarini tek bir `seen` maskesinde biriktirip sonunda BUNUNLA maskeler;
// eskiden yerinde duran `0xFFu` sabiti yeni bitleri SESSIZCE kirpardi (varlik
// diske yazilir, geri okunurken bileseni kaybolurdu). Yeni bir bit eklenince
// burasi da buyumeli.
constexpr uint32_t kSceneComponentMask = 0x001FFFFFu;
enum class SceneShape : uint32_t { Box = 0, Sphere = 1 };
// Nokta: kSceneLight'in standart omni turu (yaricapli, konum onemli).
// Yonlu: entity-bazli yon gostergesi (gizmo gunes-oku cizer).
// Spot: koni odakli isik (ic/dis aci).
// Rect: LTC dikdortgen alan isigi.
// Capsule: silindirik tup / cizgi isigi.
// Disk: dairesel alan isigi.
enum class SceneLightType : uint32_t {
  Point       = 0,
  Directional = 1,
  Spot        = 2,
  Rect        = 3,
  Capsule     = 4,
  Disk        = 5
};
// Varlik bayraklari — EDITOR gorunumu, oyun icerigi DEGIL: `.sahneb` derleyicisi
// bunlara bakmaz (gizli bir varlik yine de blob'a girer), yalniz editor panelleri
// okur. Sifir varsayilan ve dosyaya YAZILMAZ; boylece bayraksiz sahnelerin metni
// Faz E2 oncesiyle bayt bayt aynidir.
enum SceneEntityFlags : uint32_t {
  kSceneHidden = 1u << 0, // editorde gizli (cizilmez, secilemez)
  kSceneLocked = 1u << 1, // kilitli (gizmo/surukleme degistiremez)
};

// --- Nesne OZELLIKLERI (E3) --------------------------------------------------
// Tasarimcinin varlik basina verdigi degerler: `can = 250`, `hiz = 5`, devriye
// noktalari. Betik ayni kodu on dusmana verir, her biri kendi degerini okur.
//
// YALNIZ USTUNE YAZILANLAR saklanir. Varsayilan betigin KODUNDA yasar (E5
// editoru onu oradan tarar); burada bir ozellik yoksa "betigin varsayilani"
// demektir. Varsayilani buraya kopyalamak, betik degisince eski degerin
// sessizce kalmasi demek olurdu.
//
// Ozellikler BILESEN BITINDEN BAGIMSIZ veridir (Tuzaklar 8x'in istisnasi,
// bilincli): varliga aittirler, betik bilesenine degil. Betik bileseni
// kaldirilsa da dosyaya yazilir ve esitlikte karsilastirilir; bileseni geri
// eklemek degerleri geri getirir. Bilesene bagli olsalardi "betigi kaldir ->
// kaydet -> geri ekle" tasarimcinin 16 degerini sessizce silerdi.
//
// DIKKAT: burada `kScene... = 1u << N` biciminde sabit YAZMA — tools/
// scene_check.py o bicimi BILESEN BITI sayar ve YAZ/OKU/ESITLIK arar.
constexpr uint32_t kSceneMaxProps = 16;
constexpr uint32_t kScenePropNameLen = 24; // NUL dahil: ad en cok 23 karakter
// Tur kodlari; 0 bilerek bos (sifirlanmis bir SceneProp gecerli bir tur
// tasimasin). Metin anahtarlari: ozellik_sayi / _tam / _bayrak / _nokta.
enum ScenePropType : uint32_t {
  kScenePropSayi = 1,   // float, v[0]
  kScenePropTam = 2,    // tamsayi, v[0]; |v| <= 2^24 (float'ta TAM temsil edilir)
  kScenePropBayrak = 3, // v[0] = 0 | 1 (metinde hayir | evet)
  kScenePropNokta = 4,  // v[0..2] = VARLIGA GORE YEREL ofset (scene_prop_point_world)
};
// `tam` float'ta tasinir: 2^24'e kadar her tamsayi tam temsil edilir, otesi
// yuvarlanirdi. Tavan ayristiricida VE scene_prop_set'te uygulanir.
constexpr float kScenePropTamMax = 16777216.0f;
struct SceneProp {
  char name[kScenePropNameLen]; // [a-z0-9_]{1,23}, NUL'dan sonrasi SIFIR (bayt karsilastirmasi)
  uint32_t type;                // ScenePropType
  float v[3];                   // tura gore kullanilmayanlar SIFIR (scene_prop_set kanonik yazar)
};
static_assert(sizeof(SceneProp) == 40, "SceneProp 40 bayt: ad 24 + tur 4 + deger 12");

struct SceneEntity {
  char name[kSceneNameLen];
  Vec3 pos{0, 0, 0}, rot_deg{0, 0, 0}, scale{1, 1, 1}; // YEREL (ebeveyne gore)
  // Sahne agaci: ebeveynin varlik DIZINI, -1 = kok. Dizin ileriyi de
  // gosterebilir (ebeveyn-once siralama SART DEGIL); tutarliligi
  // scene_tree_validate saglar. Varlik silinince/eklenince kaydirilir
  // (SceneDesc::remove_entity / insert_entity).
  int32_t parent = -1;
  uint32_t flags = 0; // SceneEntityFlags
  uint32_t components = 0;
  // model
  int32_t asset = -1;
  Vec3 tint{1, 1, 1}; // yazar rengi (sRGB)
  // Prosedurel ilkel yuvasi (content/primitives.hpp): -1 = yok, varlik glTF
  // kaynagindan cizilir. >= 0 ise SceneRuntime kaynak yerine bu mesh'i cizer,
  // yani `asset` -1 olabilir. `.sahne` dosyasina YALNIZ >= 0 iken yazilir —
  // boylece ilkelsiz sahnelerin metni bayt bayt eskisiyle ayni kalir.
  int32_t primitive = -1;
  // Varsayilanlar renderer::PbrParams ile AYNI olmali: glTF'te varsayilan
  // puruzluluk 1.0'dir ve motor da oyle kabul eder. Sahne 0.5 verirse ayni
  // nesne kaynagina gore (glTF mi ilkel mi) FARKLI parlaklikta gorunur.
  float metallic = 0.0f, roughness = 1.0f, reflectance = 0.5f;
  Vec3 emissive{0, 0, 0};
  float emissive_strength = 1.0f;
  // animasyon
  uint32_t clip = 0;
  float phase = 0, speed = 1;
  // isik
  Vec3 light_color{1, 1, 1};
  float light_intensity = 1, light_radius = 5;
  SceneLightType light_type = SceneLightType::Point;
  float light_spot_inner = 25.0f; // ic koni acisi (derece)
  float light_spot_outer = 40.0f; // dis koni acisi (derece)
  float light_width = 1.0f;       // alan isik genisligi / tup uzunlugu (metre)
  float light_height = 0.5f;      // alan isik yuksekligi (metre)
  bool  light_cast_shadow = true; // golge doksun mu
  bool  light_godray = false;     // bu isik huzme / godray sacsin mi
  float light_godray_intensity = 1.0f; // huzme carpan gucu

  // govde
  SceneShape shape = SceneShape::Box;
  Vec3 half{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  bool dynamic = false;
  // TETIK hacmi: carpisma tepkisi yok, icine giren/cikan govde bildirilir
  // (sim::Physics::add_sensor_*). Dinamik OLAMAZ — ayristirici reddeder.
  bool body_sensor = false;
  // kamera
  float cam_fov = 60.0f;
  float cam_near = 0.1f, cam_far = 200.0f;
  // ses
  char audio_clip[kSceneNameLen] = {0};
  float audio_volume = 1.0f, audio_pitch = 1.0f;
  bool audio_loop = false, audio_spatial = true;
  // betik
  char script_file[kSceneScriptLen] = {0};
  bool script_enabled = true;
  // --- PR #331 bilesenleri ------------------------------------------------
  // Hepsi kendi bilesen bitine baglidir: bit yoksa alanlar VERI DEGILDIR
  // (dosyaya yazilmaz, scene_entity_equal karsilastirmaz) — kSceneModel/
  // kSceneBody ile ayni sozlesme.
  // karakter (kSceneCharacter)
  float char_radius = 0.5f, char_height = 1.0f;
  float char_mass = 70.0f, char_max_slope = 45.0f;
  // partikul (kSceneParticle)
  float particle_spawn_rate = 10.0f; // saniyede partikul
  float particle_lifetime_min = 1.0f, particle_lifetime_max = 2.0f;
  float particle_size_start = 0.2f, particle_size_end = 0.0f;
  Vec3 particle_velocity{0, 2.0f, 0};
  Vec3 particle_jitter{1.0f, 0.5f, 1.0f};
  Vec3 particle_color_start{1.0f, 0.6f, 0.1f}; // baslangic rengi (ates/turuncu)
  Vec3 particle_color_end{0.2f, 0.2f, 0.2f};   // bitis rengi (duman/gri)
  float particle_gravity = -2.0f;              // yercekimi ivmesi (m/s^2)
  uint32_t particle_billboard_type = 0;        // 0: Screen-aligned, 1: Stretched/Velocity, 2: Horizontal
  float particle_curl_strength = 0.0f;         // curl noise turbulans siddeti (m/s^2)
  float particle_curl_freq = 1.0f;             // turbulans frekansi (1/m)
  float particle_drag = 0.0f;                  // stokes aerodinamik hava direnci
  bool particle_collision = false;             // zemin carpisma/sekme aktif
  float particle_bounce = 0.6f;                // carpisma sekme katsayisi (restitution)
  uint32_t particle_sub_on_death = 0;          // olum aninda alt parcacik patlama sayisi
  bool particle_ribbon = false;                // serit / kuyruk izi
  // yapay zeka (kSceneNavAgent)
  Vec3 ai_target{0, 0, 0};
  float ai_speed = 3.0f;
  float ai_turn_speed = 120.0f;
  // fizik eklemi (kSceneJoint)
  int32_t joint_target = -1; // baglanilan diger varligin indeksi
  Vec3 joint_axis{0, 1, 0};  // donus veya hareket ekseni (yerel)
  float joint_limit_min = -45.0f, joint_limit_max = 45.0f;
  float joint_motor_speed = 0.0f; // >0 ise motor aktif
  // arazi (kSceneTerrain). DIKKAT: width/height DUNYA olcusu degil, izgara
  // HUCRE SAYISIDIR (content::HeightmapConfig::width/height'a dogrudan
  // tamsayiya cevrilerek gider); dunya boyu = (N-1) * terrain_cell.
  float terrain_width = 64.0f, terrain_height = 64.0f;
  float terrain_cell = 1.0f, terrain_amp = 20.0f, terrain_freq = 0.02f;
  int32_t terrain_octaves = 5;
  uint32_t terrain_seed = 0;
  // IBL & yansima sondasi (kSceneRefProbe)
  float ref_probe_radius = 10.0f;
  float ref_probe_intensity = 1.0f;
  // yanki alani (kSceneReverb)
  float reverb_decay = 1.5f; // saniye cinsinden yanki sonumlenme suresi
  float reverb_room_size = 0.8f;
  // su / gerstner dalgasi (kSceneWater)
  float wave_length = 10.0f, wave_amplitude = 0.5f;
  float wave_steepness = 0.3f, wave_speed = 1.0f;
  Vec2 wave_direction{1.0f, 0.0f};
  // ruzgar (kSceneWind)
  Vec2 wind_direction{1.0f, 0.0f};
  float wind_strength = 1.0f, wind_gustiness = 0.5f;
  float wind_gust_freq = 0.3f;
  uint32_t wind_seed = 0;
  // voksel (kSceneVoxel)
  uint32_t voxel_size_x = 16, voxel_size_y = 16, voxel_size_z = 16;
  float voxel_cell = 1.0f;
  // gokyuzu (kSceneSkybox). Panelde sabit bir dugme vardi, ARKASINDA ALAN DA
  // YOKTU: bileseni eklemek ve HDRI secmek hicbir sey kaydetmiyordu. Bilesen
  // biti "gokyuzu var mi"yi, bu alan HANGI gokyuzu oldugunu tasir; bos metin
  // = motorun gomulu varsayilan gokyuzu.
  char skybox_asset[kScenePathLen] = {0}; // HDRI / kup haritasi dosyasi
  // GAS & Envanter (kSceneHealth, kSceneAbility, kSceneInventory)
  float health_max = 100.0f;
  float health_current = 100.0f;
  uint32_t ability_id = 0;
  float ability_damage = 10.0f;
  float ability_range = 5.0f;
  float ability_cooldown = 1.0f;
  // Nesne ozellikleri (E3): ADA GORE SIRALI, yalniz ilk prop_count tanesi
  // veri. `= {}`: varsayilan kurulumda da sifir — coklu duzenleme ve gunluk
  // baytlara bakiyor, kalinti bayt "degisti" diye okunurdu.
  SceneProp props[kSceneMaxProps] = {};
  uint32_t prop_count = 0;
};
// BUTCE KAPISI, yerlesim sozlesmesi DEGIL. Olculdu 2026-09-25, x86_64 GCC
// 16.2.1 (Linux): E3 oncesi 824 B, ozelliklerle 1468 B (+16 x 40 + 4). En genis
// hizalama 4 bayt (float/int32; bool 1), yani AArch64 / MinGW / Android ayni sayi.
// NEDEN: SceneEntity cogalir — SceneDesc'te 256 kez (213 144 -> 378 008 B),
// gunlukte islem basina 2 kez x 256 islem (SceneOp 2104 -> 3392 B, yani
// 526 -> 848 KB), editor durumunda 3 x 256 kez (surukleme once/sonra + pano).
// Buyume bilincli olmali: bu sayi degisince arena payini (editorun "sistem
// arenasi" satiri) ve yigin cercevelerini (CMake 128 KB kapisi) yeniden olc.
// Tuzaklar 8aw: sizeof alan SIRASI degisimini gormez; burada gormesi
// gerekmiyor, ikili yerlesime dayanan tuketici yok (.sahneb kendi bicimi).
static_assert(sizeof(SceneEntity) == 1468, "SceneEntity boyutu degisti: arena/yigin butcesini yeniden olc (scene.hpp)");
// Veri modeli esitligi: yalniz mevcut bilesenlerin alanlari (dosyaya yazilanlar)
// + ozellikler (bilesenden bagimsiz, yukarida).
bool scene_entity_equal(const SceneEntity &a, const SceneEntity &b);

// --- Ozellik yardimcilari (E3) -----------------------------------------------
// Ad kurali: [a-z0-9_], 1..23 karakter. Buyuk harf/Turkce karakter yok: ad
// Tulpar kodunda bir anahtar olarak gececek ve dosya ASCII.
bool scene_prop_name_ok(const char *name);
// Ada gore arama; yoksa nullptr.
const SceneProp *scene_prop_find(const SceneEntity &e, const char *name);
// Ekle ya da ustune yaz (tur degisebilir). Sirali ekleme: dizi hep ada gore
// sirali kalir. REDDEDER (false, HICBIR SEYI degistirmez): ad gecersiz, tur
// 1..4 disi, deger sonlu degil, `tam` tamsayi degil ya da |v| > 2^24, ya da
// ad yeni ve yer yok (kSceneMaxProps). Degeri KANONIK yazar: bayrak 0|1,
// tam'da -0 -> 0, kullanilmayan v bilesenleri ve adin kuyrugu sifir.
bool scene_prop_set(SceneEntity &e, const char *name, uint32_t type, const float v[3]);
// Ada gore sil; yoksa false.
bool scene_prop_remove(SceneEntity &e, const char *name);
// Iki ozellik ayni mi: ad, tur ve TURUN KULLANDIGI degerler (bit-tam).
bool scene_prop_equal(const SceneProp &a, const SceneProp &b);

// Dunya ayarlari (varlik disi): gunes/ortam isigi, golge hacmi, yazar kamerasi.
// Ayri struct: editorde tek islem olarak gunluge girer (SceneOp::World).
struct SceneWorld {
  Vec3 sun_dir{0.5f, 1.0f, 0.35f};
  Vec3 ambient{0.16f, 0.17f, 0.2f};
  float sun_diffuse = 0.85f;
  Vec3 shadow_center{0, 1.0f, -1.0f};
  float shadow_radius = 17.0f, shadow_depth = 70.0f;
  Vec3 cam_target{0, 1.0f, -3.0f};
  float cam_yaw = 0.7f, cam_pitch = 0.45f, cam_radius = 26.0f;
  // Mobil uyumlu Isik Huzmeleri (God Rays / Crepuscular Rays)
  bool  godrays_enabled = false;
  float godray_density = 0.8f;   // huzme ornekleme yogunlugu
  float godray_weight = 0.5f;    // huzme ornek agirligi
  float godray_decay = 0.95f;    // sönümleme katsayisi
  float godray_exposure = 0.3f;  // genel parlaklik
  // Atmosfer & 24 Saat Gunes Dongusu
  float time_of_day = 14.0f;     // 14:00 (saat)
  float sky_turbidity = 2.5f;    // atmosfer bulanikligi
  // --- 16 Sis Türü ve Atmosferik Hacimler (docs/SIS_VE_HACIMSEL_ATMOSFER_MIMARISI.md)
  bool     fog_enabled = false;
  uint32_t fog_type = 3;              // 0: Doğrusal, 1: Üstel, 2: Exp², 3: Yükseklik Sisi (Inigo Quilez), 4: Çift Kademeli UE5, 5: Toksik/Mistik
  float    fog_density = 0.015f;      // Temel sis yoğunluğu
  float    fog_start = 5.0f;          // Doğrusal başlangıç mesafesi
  float    fog_end = 120.0f;          // Doğrusal bitiş mesafesi
  float    fog_height_falloff = 0.08f;// Yükseklik sönümlenme katsayısı lambda
  float    fog_base_height = 0.0f;    // Sis taban zemin kotu y0
  Vec3     fog_color{0.7f, 0.76f, 0.84f}; // Atmosferik sis rengi
  float    fog_scattering = 0.5f;     // Güneş ışığı saçılım çarpanı (Mie)
};

bool scene_world_equal(const SceneWorld &a, const SceneWorld &b); // bit-tam

struct SceneDesc : SceneWorld {
  const SceneWorld &world() const { return *this; }
  void set_world(const SceneWorld &w) { static_cast<SceneWorld &>(*this) = w; }
  char assets[kSceneMaxAssets][kScenePathLen];
  uint32_t asset_count = 0;
  SceneEntity entities[kSceneMaxEntities];
  uint32_t entity_count = 0;

  int32_t add_asset(const char *path); // varsa mevcut indeks; sigmazsa -1
  int32_t find_entity(const char *name) const;
  // Ekleme. MEVCUT varliklarin `parent >= at` olanlari +1 kaydirilir; `e.parent`
  // ise EKLEMEDEN SONRAKI indeks uzayinda yorumlanir (kaydirilmaz) — Remove
  // isleminin geri alinmasi tam da bunu ister.
  // SONA ekleme (at == entity_count) hicbir seyi kaydirmaz: ayristirma sirasinda
  // henuz olusmamis bir varliga bakan ILERI ebeveyn referanslari bozulmasin.
  bool insert_entity(uint32_t at, const SceneEntity &e); // at <= entity_count
  // Silme, AGACI TUTARLI birakir (secim burada; bkz. scene.cpp):
  //   1. silinen dugumun cocuklari BUYUKBABAYA baglanir (silinen kok ise kok
  //      olurlar) — alt agac SESSIZCE yok olmaz, kullanici gordugu varliklari
  //      kaybetmez; alt agaci da silmek isteyen cagiran once onlari siler.
  //   2. `parent > at` olan her ebeveyn -1 kaydirilir (diziler sikisti).
  bool remove_entity(uint32_t at);
};

// SceneDesc'i YERINDE varsayilana dondurur (`d = SceneDesc{}` ile ayni sonuc).
// NEDEN AYRI: `d = SceneDesc{}` Clang'da ~370 KB'lik bir GECICIYI yigina kurup
// kopyaliyor; GCC ayni satiri yerinde kuruyor. Olculdu 2026-09-25: Clang 22.1
// Release'te scene_parse cercevesi 380 552 B, GCC 16.2'de CMake'in 128 KB
// cerceve kapisinin altinda. Yani hata yalniz macOS (AppleClang) / Android'de
// gorunurdu. Yerinde kurulum (placement new) yeni uyeleri de kendiliginden
// kapsar; elle alan alan sifirlamak bir uyeyi unuturdu.
void scene_desc_reset(SceneDesc &d);

struct SceneError {
  char msg[160];
  uint32_t line = 0; // 0 = satirsiz (dosya acilamadi vb.)
};

// Metin -> SceneDesc. out tam yeniden kurulur. Hata: err (satir numarali), false.
bool scene_parse(const char *text, size_t len, SceneDesc *out, SceneError *err);
// SceneDesc -> metin (NUL sonlu). Donus: gereken uzunluk (NUL haric; snprintf
// gibi, cap asilsa da). Ayni desc her zaman ayni baytlari verir.
size_t scene_write(const SceneDesc &d, char *buf, size_t cap);
bool scene_load(Arena &scratch, const char *path, SceneDesc *out, SceneError *err);
bool scene_save(Arena &scratch, const SceneDesc &d, const char *path, SceneError *err);
// Sahne dosyasinin dizini ("a/b/c.sahne" -> "a/b"); dizin yoksa ".".
void scene_dir_of(const char *path, char *out, size_t cap);

// Varlik donusumu: T * Rz * Ry * Rx * S (ImGuizmo ayristirmasiyla ayni sira;
// kapisi test_editor'da). Donus Euler derece. Bu YEREL donusumdur — ebeveyn
// zinciri KATILMAZ (dunya icin scene_entity_world_matrix).
Mat4 scene_entity_matrix(const SceneEntity &e);
Quat scene_entity_rotation(const SceneEntity &e);

// --- Sahne agaci (Faz E2) ----------------------------------------------------
// Agac gecerli mi: her ebeveyn indeksi sinir icinde, kendine bakan yok, dongu
// yok, derinlik <= kSceneMaxDepth. Bozuksa false ve *bad_index = ilk bozuk
// varlik. Ayirma yok, ozyineleme yok (her yurume tavanda durur).
bool scene_tree_validate(const SceneDesc &d, uint32_t *bad_index);
// Varligin kok'e uzakligi (kok = 0). Bozuk/derin zincir: kSceneMaxDepth doner
// (tavanda durur, asla donguye girmez).
uint32_t scene_tree_depth(const SceneDesc &d, uint32_t i);
// DUNYA donusumu: kok'ten asagi `M_kok * ... * M_i`. Her varligin kendi sirasi
// T*Rz*Ry*Rx*S olarak kalir. Kok varlikta sonuc scene_entity_matrix ile
// BIT-TAMDIR (erken donus) — duzlestirilmis sahne ile karsilastirma kapisi
// buna dayanir.
Mat4 scene_entity_world_matrix(const SceneDesc &d, uint32_t i);
// Dunya donusu: zincirdeki kuaterniyonlarin carpimi. Kok: scene_entity_rotation
// ile bit-tam.
Quat scene_entity_world_rotation(const SceneDesc &d, uint32_t i);
// Dunya olcegi: zincirdeki olceklerin bilesen carpimi. ⚠ SINIR: ebeveynde hem
// donus hem esit-olmayan olcek varsa gercek dunya donusumu EGIKTIR (shear) ve
// tek bir olcek vektorune sigmaz; bu durumda deger bir YAKLASIMDIR. Cizim
// matrisi (scene_entity_world_matrix) her durumda tamdir; yaklasim yalniz
// rijit govde / GI gibi T-R-S isteyen tuketicileri ilgilendirir.
Vec3 scene_entity_world_scale(const SceneDesc &d, uint32_t i);
// `nokta` ozelliginin DUNYA konumu: varligin dunya konumu + dunya donusu x
// yerel ofset. OLCEK ETKILEMEZ (bilincli): devriye noktasi "2 metre ileride"
// demek; varligi 3 kat buyutmek noktayi 6 metreye itmemeli. Nokta varlikla
// DONER ve TASINIR (ebeveyn zinciri dahil), yani prefab/kopya baska yere
// konunca noktalari da onunla gider. Gecersiz indeks: out = local.
void scene_prop_point_world(const SceneDesc &d, uint32_t i, const float local[3], float out[3]);
// scene_prop_point_world'un TERSI (E6, editorde noktayi gorunumde surukleme):
// yerel = ters(dunya donusu) x (dunya - varligin dunya konumu). Ayni iki
// kaynak (dunya matrisinin oteleme sutunu + kuaterniyon zinciri), olcek YOK —
// yani world(local(p)) == p (float yuvarlamasi icinde) ve varlik 3 kat
// buyukken surukleme ofseti 3'e BOLMEZ. Gecersiz indeks: out = world.
void scene_prop_point_local(const SceneDesc &d, uint32_t i, const float world[3], float out_local[3]);
// Belirlenimli on-sirali gezinti: kokler indeks sirasinda, her dugumun
// cocuklari indeks sirasinda. Donus: dugum sayisi (cap asilsa da dogru sayar,
// yalniz ilk cap tanesi yazilir). Panelin cizdigi sira budur.
uint32_t scene_tree_order(const SceneDesc &d, int32_t *out, uint32_t cap);
// Yeniden ebeveynleme: `child`'in DUNYA donusumu KORUNUR — yeni yerel
// pos/rot_deg/scale, `inverse(dunya(new_parent)) * dunya(child)` matrisinin
// T*Rz*Ry*Rx*S ayristirmasidir. Reddeder (ve HICBIR SEYI degistirmez):
// sinir disi indeks, kendine ebeveyn, dongu (yeni ebeveyn cocugun altindaysa),
// tavan asimi. ⚠ AYRISTIRMA SINIRI: ayna (negatif determinant) tek eksene — X —
// yuklenir; egik (shear) bir matris T*R*S ile temsil edilemez, o durumda dunya
// donusumu TAM korunmaz (ebeveynde donus + esit olmayan olcek birlikteyse).
bool scene_reparent(SceneDesc &d, uint32_t child, int32_t new_parent);
// Ayni hesap, UYGULAMADAN: sonucu *out'a yazar (gunluge tek islem olarak
// girmek icin; bkz. SceneHistory::reparent).
bool scene_reparent_entity(const SceneDesc &d, uint32_t child, int32_t new_parent, SceneEntity *out);
// Gizmo DUNYA uzayinda calisir (ImGuizmo'ya dunya matrisi verilir); sonucu
// varligin YEREL alanlarina yazmadan once bundan gecirmek ZORUNLU, yoksa
// cocuk varlik ebeveyn donusumunu iki kez yer. Kok varlikta `world` aynen doner.
Mat4 scene_world_to_local_matrix(const SceneDesc &d, uint32_t i, const Mat4 &world);

// Secim: isin–AABB. Yerel sinir = model sinirlari (varsa) ∪ govde ∪ isaret
// kutusu (bos/isik varligi 0.3). Dunya AABB yerel kutunun 8 kosesinden.
struct SceneBounds {
  Vec3 lo, hi;
};
SceneBounds scene_entity_local_bounds(const SceneEntity &e, const SceneBounds *model /* null = model yok */);
SceneBounds scene_world_bounds(const SceneBounds &local, const Mat4 &m);
// Slab testi; t >= 0 en yakin giris (isin icindeyse 0). dir normalize olmali.
bool scene_ray_aabb(Vec3 origin, Vec3 dir, const SceneBounds &b, float *t);
// En yakin vurusun indeksi, yoksa -1. bounds[n] dunya uzayinda.
int32_t scene_pick(const SceneBounds *bounds, uint32_t n, Vec3 origin, Vec3 dir, float *t_out);

// Fizik: govde bilesenli varliklari dunyaya koyar; ids[entity_count] doldurur
// (govdesizler gecersiz). Donus: eklenen govde sayisi. Govde DUNYA donusumuyle
// kurulur (konum/donus/olcek zincirden) — cocuk govde gorundugu yerde dogar,
// yerel ofsetinde degil.
uint32_t scene_spawn_bodies(const SceneDesc &d, sim::Physics &ph, sim::BodyId *ids);
void scene_remove_bodies(sim::Physics &ph, sim::BodyId *ids, uint32_t n);
// Dinamik govdenin sim'deki yeri: T(sim) * R(sim) * S(yazar).
Mat4 scene_body_matrix(const SceneEntity &e, const sim::Physics &ph, sim::BodyId id);

// Govdeler + KARAKTERLER (kSceneCharacter) — editorun oynatma kipi. SceneRuntime
// ile AYNI kural: karakterli varligin govde bileseni DOGURULMAZ (karakter onun
// yerini alir; editorun "Karakter Kontrolcusu" hazir nesnesi ikisini birden
// koyuyor, ikisi dogsaydi karakter kendi kutusuyla ic ice dogardi). Kapsul
// varligin yazar konumuna ORTALI (Jolt'un ayak tabanindan yarim boy asagi).
// `chars` null ise yalniz govdeler, eski davranis (scene_spawn_bodies).
struct SceneLiveStats {
  uint32_t bodies = 0;
  uint32_t characters = 0;
  uint32_t characters_failed = 0; // boy <= 2*yaricap ya da havuz dolu — sessiz degil, cagiran soyler
  uint32_t bodies_replaced = 0;   // karakter yuzunden dogurulmayan govde
};
SceneLiveStats scene_spawn_live(const SceneDesc &d, sim::Physics &ph, sim::BodyId *ids, sim::CharacterId *chars);
void scene_remove_live(sim::Physics &ph, sim::BodyId *ids, sim::CharacterId *chars, uint32_t n);
// Karakterli varligin sim'deki yeri: yazar donusumu (donus + olcek) korunur,
// yalniz ORTA nokta karakterden (ayak + yarim boy).
Mat4 scene_character_matrix(const SceneDesc &d, uint32_t i, const sim::Physics &ph, sim::CharacterId c);

// Islem gunlugu: her degisiklik once/sonra kopyasiyla kaydedilir. Yeni islem
// yinele kuyrugunu siler; kapasite dolunca en eski dusuruIur.
struct SceneOp {
  enum Kind : uint32_t { Set = 0, Add = 1, Remove = 2, World = 3, Asset = 4 };
  Kind kind;
  uint32_t index;
  SceneEntity before, after;
  SceneWorld world_before, world_after; // yalniz World
  char asset_path[kScenePathLen];       // yalniz Asset: tabloya SONA eklenen kaynak
  // Yalniz Remove: silinen dugumun cocuklarinin SILINMEDEN ONCEKI indeksleri.
  // Silme onlari buyukbabaya bagladigi icin sonradan bulunamazlar (gercek
  // buyukbaba cocuklariyla karisirlar) — geri alma bit-tam olsun diye 32 bayt
  // bit kumesi olarak saklanir (dizi kopyasi degil).
  uint32_t child_mask[(kSceneMaxEntities + 31) / 32];
};
class SceneHistory {
public:
  bool init(Arena &arena, uint32_t capacity);
  // Uygular ve kaydeder. Set: before/after esitse kaydetmez (false).
  bool set_entity(SceneDesc &d, uint32_t i, const SceneEntity &after);
  bool add_entity(SceneDesc &d, const SceneEntity &e); // sona
  bool remove_entity(SceneDesc &d, uint32_t i);
  // Yeniden ebeveynleme TEK islemdir: scene_reparent_entity yalniz `child`
  // varliginin alanlarini (parent + yerel donusum) degistirdigi icin mevcut
  // Set islemine oturur — geri alma bayt-tamdir, yeni bir islem turu yok.
  // Donus: gunluge islem girdi mi (ayni ebeveyn / gecersiz istek: false).
  bool reparent(SceneDesc &d, uint32_t child, int32_t new_parent);
  bool set_world(SceneDesc &d, const SceneWorld &after); // esitse kaydetmez (false)
  // Kaynak tablosuna ekle, GERI ALINABILIR. Kaynak zaten varsa islem YOK (false),
  // *index mevcut indeks; yeni ise sona eklenir ve kaydedilir (true). Sigmazsa
  // *index = -1, false. Eskiden tablo gunlugun DISINDA buyuyordu: kaynakla
  // varlik ekleyip Ctrl+Z yapinca varlik gidiyor, sahnede kimsenin kullanmadigi
  // bir `kaynak` satiri kaliyordu (penceresiz "kaynak tarayici" kapisi bunu,
  // kaynaksiz bir sahne kaynakli bir dizinde acilinca yakaladi).
  // Geri alma yalniz SON kaynagi kaldirir: sonra eklenen her sey (ona
  // basvuran varliklar dahil) LIFO geregi ondan once geri alinmis olur.
  bool add_asset(SceneDesc &d, const char *path, int32_t *index);
  bool undo(SceneDesc &d);
  bool redo(SceneDesc &d);
  uint32_t undo_count() const { return cursor_; }
  uint32_t redo_count() const { return count_ - cursor_; }
  void clear() { count_ = cursor_ = 0; }

private:
  bool push(const SceneOp &op);
  SceneOp *ops_ = nullptr;
  uint32_t cap_ = 0, count_ = 0, cursor_ = 0;
};

} // namespace tulpar::engine::content
