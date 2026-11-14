// L6 CONTENT — bellekteki model: glTF'ten (ya da ileride kendi pack
// formatindan) okunmus, Arena'da sabit kapasiteli diziler. Renderer'a
// yuklenmek uzere; sahne akisi yok (bolum = resident set, PLAN §8/5).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/animation.hpp"

namespace tulpar::engine::content {

struct ClusterDag; // content/cluster_dag.hpp (Faz 9: kume DAG'i; ileri bildirim)

struct ModelImage {
  uint32_t width = 0, height = 0;
  uint8_t *rgba = nullptr; // Arena'da, width*height*4
  // RENK UZAYI KULLANIMDAN turetilir, dosyadan degil: glTF goruntuleri renk
  // uzayi tasimaz — baseColor/emissive sRGB, metallicRoughness/normal/occlusion
  // DOGRUSAL veridir (spec). Yanlis uzay hicbir seyi kizartmaz, yalnizca
  // puruzlulugu/normali sessizce egriltir; kapi: content_gltf_texture_colorspace.
  bool srgb = true;
  // NORMAL HARITASI MI: mip zinciri BLIT ile uretilirse dogrusal suzme
  // komsu normalleri ORTALAR ve ortalama vektorun boyu 1'den KUCUK olur —
  // uzaktaki yuzey sessizce duzlesir (isik "yassilasir"). Tek dogrusu
  // kucultmeden sonra YENIDEN NORMALLESTIRMEK; o yuzden bu goruntuler
  // CPU'da mip'lenip hazir seviye olarak yukleniyor. ORM bu bayragi
  // ALMAZ: puruzluluk/metaliklik birer skaler, normallestirilmeleri
  // anlamsiz olurdu.
  bool normal_map = false;
};
struct ModelMaterial {
  int32_t image = -1; // -1: dokusuz (beyaz)
  Vec3 base_color{1, 1, 1};
  // glTF metallic-roughness. Varsayilanlar glTF 2.0 spesifikasyonunun kendi
  // varsayilanlari DEGIL (spec metallic=1, roughness=1 der); burada
  // metallic=0/roughness=1 = Lambert'e en yakin nokta secildi, cunku PBR yolu
  // motorda opsiyonel ve `has_pbr` yanlisken eski goruntu korunmali.
  float metallic = 0.0f;
  float roughness = 1.0f;
  Vec3 emissive{0, 0, 0}; // glTF emissive_factor (dogrusal)
  bool has_pbr = false;   // dosyada pbr_metallic_roughness blogu VAR miydi
  // Doku basina degisen kanallar (-1 = yok, o kanal yalniz carpandan gelir).
  // ORM = glTF metallicRoughnessTexture: G ROUGHNESS, B METALLIC (spec);
  // occlusionTexture AYNI goruntuyu gosteriyorsa R kanali occlusion olur ve
  // ekstra sampler'a gerek kalmaz (yayginlasmis "ORM" paketlemesi).
  int32_t orm_image = -1;
  int32_t normal_image = -1;
  int32_t emissive_image = -1;
  float normal_scale = 1.0f;       // glTF normalTexture.scale
  float occlusion_strength = 0.0f; // glTF occlusionTexture.strength; 0 = occlusion yok
  // occlusionTexture ORM'den FARKLI bir goruntuyu gosteriyor. Yuklemede
  // BIRLESTIRILIR (2026-11-04): ORM'in G/B'si + occlusion'in R'si yeni bir
  // goruntuye yazilir, orm_image ona doner — dorduncu sampler YOK (TBDR'da
  // degmez), occlusion yine ORM.R'den okunur. Birlestirilemediyse (PBR'siz
  // malzeme, goruntu siniri) true KALIR ve sayilir (Model::occlusion_unmerged).
  bool occlusion_separate = false;
};
constexpr uint32_t kModelMaxLods = 2; // LOD1, LOD2 (LOD0 = indices)
constexpr uint32_t kModelMaxSkins = 4;
constexpr uint32_t kModelMaxJoints = 128; // vertex eklem indeksi u8

// Iskelet (glTF skin): eklemler ebeveyn-once sirada (sim::to_model kurali),
// glTF eklem sirasindan yeniden siralanmis; vertex eklem indeksleri de oyle.
struct ModelSkin {
  sim::Joint *joints = nullptr;   // [joint_count] (Arena)
  uint32_t joint_count = 0;
  Mat4 *inverse_bind = nullptr;   // [joint_count]
  char name[64] = {0};
};
// Klip: glTF animasyonu sabit hizda ornekleyip ClipBuilder ile sikistirilmis.
struct ModelClip {
  char name[64] = {0};
  int32_t skin = -1;              // hangi iskelet
  float duration = 0;             // saniye
  const sim::ClipHeader *clip = nullptr;
  sim::ClipBuildStats stats{};
};
struct ModelMesh {
  renderer::Vertex *verts = nullptr;
  uint32_t vertex_count = 0;
  uint32_t *indices = nullptr;
  uint32_t index_count = 0;
  int32_t material = -1;
  // meshoptimizer ile uretilen ayrik LOD'lar (ayni vertex tamponu, kisa indeks
  // listesi). lod_index_count 0 = o seviye yok (sadelestirme hedefe ulasamadi).
  uint32_t *lod_indices[kModelMaxLods] = {};
  uint32_t lod_index_count[kModelMaxLods] = {};
  float lod_error[kModelMaxLods] = {}; // bagil (mesh olcegine gore)
  // A7: LOD indekslerinin GERCEKTEN kullandigi (tekil) kose sayisi. LOD ayni vertex tamponunu paylasir, yani vertex_count
  // LOD'da dusmez; iskeletli kose butcesi (RendererStats::skin_verts) bunu sayar.
  uint32_t lod_vertex_count[kModelMaxLods] = {};
  // Kume (cluster) DAG: GltfLimits::cluster_dag acikken kurulur (Faz 9).
  // Ayrik LOD'larin (lod_indices) YERINE GECMEZ, ustune gelir: LOD butun mesh'i
  // birden degistirir, DAG kume kume ve cut'i runtime secer.
  const ClusterDag *dag = nullptr;
  // Iskeletli: skin >= 0 ise skin_verts dolu (verts ile ayni sira/sayi; JOINTS_0
  // yeniden siralanmis, WEIGHTS_0 unorm16). Iskeletli mesh'te meshopt/LOD atlanir.
  int32_t skin = -1;
  renderer::SkinnedVertex *skin_verts = nullptr;
  // A5 (morph target): glTF POSITION farklari. `morph_deltas` = morph_count x vertex_count (hedef-major), taban `verts` KALIR (model_compact
  // bu mesh'te verts'i de saklar). Morph'lu mesh'te meshopt/LOD/DAG ATLANIR (kose sirasi deltalarla birlikte gider). Iskeletli mesh'te morph
  // bu surumde YOK (Model::morph_skipped sayar).
  Vec3 *morph_deltas = nullptr;
  uint32_t morph_count = 0;
};
constexpr uint32_t kModelMaxMorph = 8;          // mesh basina en cok morph hedefi (asan hedefler Model::morph_dropped'a sayilir)
constexpr uint32_t kModelMaxMorphVerts = 16384; // morph'lu mesh'in en cok kosesi (asani morph'suz yuklenir, sayilir)
// Yukleme sonrasi mesh optimizasyonu olcumu (meshoptimizer analyze*): sayilar
// ucgen agirlikli ortalama. ACMR = donusturulen vertex / ucgen (0.5 en iyi).
struct ModelOptStats {
  float acmr_before = 0, acmr_after = 0;
  float overdraw_before = 0, overdraw_after = 0;
  uint32_t vertices_before = 0, vertices_after = 0; // tekillestirme
  uint32_t triangles = 0;
};
struct ModelInstance {
  uint32_t mesh = 0;
  Mat4 world;
};
struct Model {
  ModelImage *images = nullptr;      uint32_t image_count = 0;
  ModelMaterial *materials = nullptr; uint32_t material_count = 0;
  ModelMesh *meshes = nullptr;       uint32_t mesh_count = 0;
  ModelInstance *instances = nullptr; uint32_t instance_count = 0;
  ModelSkin *skins = nullptr;        uint32_t skin_count = 0;
  ModelClip *clips = nullptr;        uint32_t clip_count = 0;
  // GltfLimits::max_clips'i asan animasyon sayisi (yuklenmedi; sessiz degil, sayilir).
  uint32_t clips_dropped = 0;
  // A5: morph hedefi tavani asan (yuklenmeyen) hedefler / kose tavanini asip morph'suz yuklenen mesh'ler / iskeletli oldugu icin morph'u atlanan mesh'ler.
  uint32_t morph_dropped = 0, morph_meshes_dropped = 0, morph_skipped = 0;
  Vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0}; // ornek uzayinda, tum instance'lar
  // M1 ice aktarma ayari (content/model_import.hpp): instance'larin ONUNE carpilan C ve C'den onceki ham sinir (editor ayari
  // degistirince eski C geri alinir). Ayarsiz glTF'te birim matris ve ham = bounds.
  Mat4 import_xform;
  Vec3 import_raw_min{0, 0, 0}, import_raw_max{0, 0, 0};
  // A7: model basina LOD esikleri (metre, .ayar `lod1`/`lod2`); 0 = cagiranin ModelLod esigi. draw_model uygular.
  float lod_distance1 = 0.0f, lod_distance2 = 0.0f;
  ModelOptStats opt;
  // Ayni goruntu hem renk (sRGB) hem veri (dogrusal) olarak kullanilmis:
  // glTF'te gecersiz. Renk kazanir, sayac artar — sessiz yanlis yok.
  uint32_t colorspace_conflicts = 0;
  // Ayri occlusionTexture birlestirmesi (ModelMaterial::occlusion_separate):
  // kac yeni ORM goruntusu uretildi / kac malzemede yapilamadi.
  uint32_t occlusion_merged = 0, occlusion_unmerged = 0;
  char error[160] = {0};
  char note[160] = {0};   // BAŞARILI içe aktarmada bilgi notu (örn. OBJ: doku haritası desteklenmedi); konsol özeti yazar
};

// GPU'ya yuklenmis hal: her mesh/malzeme icin renderer tutamaclari.
struct UploadedModel {
  renderer::MeshHandle *meshes = nullptr;
  renderer::MeshHandle *lod_meshes = nullptr; // mesh_count x kModelMaxLods; gecersiz = yok
  renderer::MaterialHandle *materials = nullptr;
  renderer::TextureHandle *textures = nullptr;
  uint32_t mesh_count = 0, material_count = 0, texture_count = 0;
  // Kac doku CPU'da yeniden normallestirilmis mip zinciriyle yuklendi.
  // Kapi bunu okuyor: `build_normal_mips` dogru calissa bile yukleme yolu
  // onu hic CAGIRMAZSA goruntu eski davranista kalirdi ve dogrudan
  // fonksiyonu olcen bir test bunu goremezdi.
  uint32_t normal_mip_textures = 0;
};

// Modeli renderer'a yukler (dokular + malzemeler + mesh'ler). Yukleme aninda.
bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out);

// YUKLEME SONRASI KALICI KOPYA (yayin adimi 3, 2026-10-13). gltf_load arenada
// GPU'ya gittikten sonra kimsenin okumadigi seyleri de birakir: cozulmus RGBA
// pikseller, vertex/indeks/LOD/DAG dizileri ve klip SIKISTIRILMADAN ONCEKI ham
// izler (eklem x ornek x 40 B; 20 klipli karakterde asil bedel bu). Bunlar
// gecici bir arenaya yuklenip upload edilince `model_compact` cizim ve poz
// icin GEREKENI (ornekler, mesh'in malzeme/iskelet alani, malzemeler, iskelet
// eklemleri + ters bind, sikistirilmis klip blogu) `keep`e DERIN kopyalar;
// gecici arena geri sarilabilir. dst'de verts/indices/skin_verts/lod_indices/
// dag/rgba nullptr'dir (sayilar bilgi icin kalir) — upload_model'e VERILEMEZ.
// draw_model / model_pose_evaluate src ile ayni sonucu verir (kapi:
// content_model_compact_pose_bit_identical). false: keep arenasi doldu.
bool model_compact(Arena &keep, const Model &src, Model *dst);
// UploadedModel tutamac dizilerinin `keep`e kopyasi (upload gecici arenayla yapildiysa).
bool uploaded_model_copy(Arena &keep, const UploadedModel &src, UploadedModel *dst);

// Normal haritasi icin CPU mip zinciri: her kucultmeden SONRA yeniden
// normallestirir (donanim blit'i bunu yapamaz — bkz. ModelImage::normal_map).
// data[0] goruntunun kendisi, geri kalan seviyeler arenada. levels seviye
// sayisi, sizes[] bayt cinsinden boyutlar. Disari aciktir cunku kapi onu
// dogrudan olcuyor (normallestirmeyen kontrolle karsilastirarak).
bool build_normal_mips(Arena &arena, const ModelImage &img, uint32_t levels,
                       uint8_t **data, uint32_t *sizes);
// Poz: iskelet basina skin matrisleri (draw_model iskeletli mesh'lere bunu verir).
struct ModelPose {
  const Mat4 *skin_mats[kModelMaxSkins] = {};
  uint32_t joint_count[kModelMaxSkins] = {};
};
// Model bütçesi (M1, docs/MODEL-ANIMASYON-PLANI.md): editör "kaynak yüklendi" satırı ve mobil uyarısı bunu kullanır.
// triangles = LOD0, TÜM örnekler (instance) toplamı; skinned_vertices = iskeletli mesh'lerin örnek başına köşe toplamı (kare başına
// iskeletli köşe bütçesinin birimi: P20 Pro ölçümü, ≲ 50K köşe/kare önerilir).
struct ModelBudget {
  uint32_t meshes = 0, instances = 0, triangles = 0, skinned_vertices = 0, joints = 0, clips = 0, clips_dropped = 0, images = 0;
};
ModelBudget model_budget(const Model &m);
constexpr uint32_t kMobileSkinnedVertexBudget = 50000;   // kare başına (P20 Pro / Mali-G72 ölçümü, 2026-11-13)
// Tek satır özet + uyarı: out'a yazar; dönüş: uyarı var mı (0/1). Sayılar ve cihaz notu Türkçe.
uint32_t model_budget_text(const Model &m, char *out, uint32_t cap);

// Klip degerlendirme calisma alani: ayirma yok, cagiranin (statik ya da arena).
struct PoseScratch {
  Vec3 t[kModelMaxJoints];
  Quat r[kModelMaxJoints];
  Vec3 s[kModelMaxJoints];
  Vec3 t2[kModelMaxJoints];   // A2: ikinci klip (karisim) yerel pozu
  Quat r2[kModelMaxJoints];
  Vec3 s2[kModelMaxJoints];
  Mat4 model[kModelMaxJoints];
  Mat4 skin[kModelMaxSkins][kModelMaxJoints];
};
// clip'i time_s'de (dongusel) ornekler, ilgili iskeletin skin matrislerini
// scratch.skin[skin]'e yazar ve pose'u gunceller. false: gecersiz klip.
bool model_pose_evaluate(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, ModelPose *pose);
// A2 (docs/MODEL-ANIMASYON-PLANI.md): iki klibi karistirir — w = 0 ise yalniz A, w = 1 ise yalniz B (bit esit: model_pose_evaluate ile).
// Eklem basina ceviri/olcek dogrusal, donus slerp (antipodal guvenli). Iki klip AYNI iskelete ait olmali; degilse false
// (cagiran tek klibe duser). Ayirma yok.
bool model_pose_blend(const Model &m, uint32_t clip_a, float time_a, uint32_t clip_b, float time_b, float w, PoseScratch &scratch,
                      ModelPose *pose);
// A3: N (<= 8) klibin AĞIRLIKLI karışımı (animatör çıktısı). Ağırlığı <= 0 olanlar atlanır; ağırlıklar toplamı 1 olmasa da normalleşir.
// Ötelenme/ölçek doğrusal, dönüş sıralı slerp (kümülatif ağırlıkla). Hepsi AYNI iskelete ait olmalı; değilse false. Tek girdi =
// model_pose_evaluate ile BİT EŞİT.
struct PoseMixEntry { uint32_t clip; float time; float weight; };
// lock_root (A4 kök hareketi): sonuçta eklem 0'ın YATAY (x, z) ötelenmesi bind değerine kilitlenir — hareket varlığa aktarıldığında görsel çift hareket olmasın.
bool model_pose_mix(const Model &m, const PoseMixEntry *e, uint32_t n, PoseScratch &scratch, ModelPose *pose, bool lock_root = false);
// Klibin t anındaki kök (eklem 0) YEREL yatay ötelenmesi (x, z): kök hareketi farkı için. false: geçersiz klip.
bool model_clip_root_xz(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, float out_xz[2]);

// LOD secimi: olcut d >= distance1 ise LOD1, d >= distance2 ise LOD2 (varsa).
// nullptr = hep LOD0.
//
// OLCUT GORUS DERINLIGIDIR, oklid uzakligi degil (Tuzaklar 8cy):
//   d = |dot(p - camera_pos, camera_forward)| * depth_scale
// Ekrandaki (yansitilan) boy r * proj[1][1] / derinlik'tir — yani ayni derinlikteki
// iki ozdes model ekranin ortasinda da kenarinda da AYNI boyda gorunur ve artik
// ayni LOD'u alir. Oklid uzakligi kenardakini daha uzak sayiyordu: 2:1 ekranda
// (P20 Pro 2240x1080) merkezde 18 m'deki birlik kenarda ~24 m oluyor ve 20 m esigi
// tam ekranin kenarina dusuyordu — LOD ekranin x'ine bagliydi.
//
// ESIKLERIN ANLAMI (eslem): ekranin ORTASINDA derinlik = uzaklik, yani referans
// FOV'da distance1/distance2 bugunku metreleriyle aynen gecerli. Kenarda ayni
// derinlik ayni seviyeyi verir (eskiden daha kaba seviye veriyordu).
// depth_scale = tan(fov/2) / tan(kModelLodRefFovY/2) = proj_ref[1][1] / proj[1][1]:
// kopru/demo varsayilan FOV'unda (pi/3.5) tam 1; FOV daraltilinca (yakinlastirma)
// model ekranda buyudugu icin olcut kuculur -> daha ince LOD. Model boyu olcute
// GIRMEZ: esikler bugun oldugu gibi mutlak metre (yaricapla olceklemek esiklerin
// anlamini model basina degistirirdi).
// Kamera ARKASI (yalniz golgeye giren ornek): mutlak deger — arkada 50 m'deki golge
// vericisi LOD0'a dusup golge gecisini pahalilastirmasin.
// camera_forward sifirsa ESKI oklid yolu (geriye uyum + kapinin pozitif kontrolu).
constexpr float kModelLodRefFovY = 3.14159265358979f / 3.5f;
struct ModelLod {
  Vec3 camera_pos{0, 0, 0};
  float distance1 = 20.0f;
  float distance2 = 45.0f;
  Vec3 camera_forward{0, 0, 0}; // birim bakis yonu; {0,0,0} = oklid uzakligi (eski yol)
  float depth_scale = 1.0f;     // tan(fov/2) / tan(kModelLodRefFovY/2)
  // Kameradan kurar: goz, hedef, dikey FOV (radyan). Goz = hedef ise oklid yolu.
  void set_view(Vec3 eye, Vec3 target, float fov_y);
};
// Saf secim (ayirma yok, renderer gerekmez): istenen seviye 0/1/2. O seviye
// uretilmemisse bir alta inmek draw_model'in isi (Tuzaklar 8cx).
// A7: modelin .ayar esikleri (Model::lod_distance1/2, 0 = yok) uygulanmis kopya; draw_model bunu kullanir.
ModelLod model_lod_effective(const Model &m, const ModelLod &lod);
uint32_t model_lod_level(const ModelLod &lod, Vec3 world_pos);
// Tum instance'lari cizer (model matrisi * instance.world). Secilen LOD sayilari
// (0/1/2) istege bagli raporlanir.
void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint = {1, 1, 1},
                const ModelLod *lod = nullptr, uint32_t lod_counts[kModelMaxLods + 1] = nullptr,
                const ModelPose *pose = nullptr);

} // namespace tulpar::engine::content
