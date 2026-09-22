// L6 CONTENT — TulparEngine Statik Yansıma ve Bileşen Metaveri Sistemi (reflect.hpp)
//
// ESİNLENME VE KAYNAK:
// 1. O3DE (Open 3D Engine - AzCore/Serialization/SerializeContext.h & AzToolsFramework)
//    O3DE'nin SerializeContext / EditContext deseninden esinlenilmiştir; ancak O3DE'nin
//    ağır dinamik tip kayıtları (TypeInfo/RTTI) yerine Tulpar'ın C++17 statik
//    constexpr ve ofset tablosu yaklaşımı benimsenmiştir.
// 2. Prowl Game Engine (Prowl.Editor/GUI/PropertyEditors & EditorGUI.Row)
//    Prowl'un her bileşeni ve alanı bağımsız olarak denetleyen, minimum/maksimum sınırları,
//    adımları, ipuçlarını ve varsayılan sıfırlamayı taşıyan veri modeli esas alınmıştır.
// 3. EdenSpark / Dagor Engine (GaijinEntertainment/DagorEngine prog/gameLibs/ecs/)
//    daECS'nin sıfır sanal çağrı ve veri yönelimli archetype/ofset disiplini ile
//    uyumlu olarak, çalışma zamanında sıfır bellek ayırma (zero-alloc) garantisi sunar.
//
// SIFIR TAHSİS (ZERO-ALLOC) SÖZLEŞMESİ:
// Bu başlık altındaki tüm yapılar ROM/kod kesitinde constexpr/statik dizi olarak yaşar.
// Hiçbir heap ayırması (malloc/new/std::vector) içermez; faz0_gate'i ihlal etmez.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "content/scene.hpp"
#include "core/math/vec.hpp"

namespace tulpar::engine::reflect {

enum class FieldType : uint8_t {
  Float = 0,
  Int,
  UInt,
  Bool,
  Vec2,
  Vec3,
  Color3,
  String,
  Enum
};

struct FieldMeta {
  const char *name;         // Kullanıcıya görünen etiket ("Yoğunluk", "Işık Rengi")
  const char *prop_key;     // İkincil ImGui/Hash kimliği ("##intensity")
  FieldType type;           // Veri tipi
  size_t offset;            // SceneEntity içindeki byte ofseti (offsetof)
  size_t size;              // sizeof
  float min_val;            // Sürgülü giriş için min sınır
  float max_val;            // Sürgülü giriş için max sınır
  float step;               // Değişim adımı / sürükleme hassasiyeti
  const char *format;       // Sayı formatı ("%.2f", "%.2f m", "%.1f°")
  const char *tooltip;      // Fare üzerine geldiğinde gösterilecek açıklama
  const char *enum_names;   // Enum için '\0' ile ayrılmış isim dizisi
  float default_f32;        // Varsayılana sıfırlama için varsayılan float değeri
  Vec2 default_vec2{0,0};   // Varsayılan Vec2 değeri
  Vec3 default_vec3{0,0,0}; // Varsayılan Vec3 değeri
};

struct ComponentMeta {
  const char *name;         // Bileşen adı ("Işık", "Model", "Gövde", "Kamera")
  const char *icon;         // Unicode UTF-8 simge
  uint32_t component_bit;   // content::kSceneLight, kSceneModel vs.
  const FieldMeta *fields;  // Alana ait metaveri dizisi
  size_t field_count;       // Alan sayısı
};

// =============================================================================
// ALAN TANIMLARI TABLOLARI (STATİK / ROM)
// =============================================================================

// 1. TRANSFORM
inline constexpr FieldMeta kTransformFields[] = {
  {"Konum", "##pos", FieldType::Vec3, offsetof(content::SceneEntity, pos), sizeof(Vec3), -10000.0f, 10000.0f, 0.05f, "%.2f m", "Yerel konum (ebeveyne gore)", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Donus", "##rot", FieldType::Vec3, offsetof(content::SceneEntity, rot_deg), sizeof(Vec3), -360.0f, 360.0f, 0.5f, "%.1f deg", "Euler acisi derece (T*Rz*Ry*Rx*S)", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Olcek", "##scale", FieldType::Vec3, offsetof(content::SceneEntity, scale), sizeof(Vec3), 0.01f, 100.0f, 0.02f, "%.2f", "Yerel buyukluk carpani", nullptr, 1.0f, Vec2{0, 0}, Vec3{1, 1, 1}},
};

// 2. MODEL (kSceneModel)
inline constexpr FieldMeta kModelFields[] = {
  {"Renk", "##tint", FieldType::Color3, offsetof(content::SceneEntity, tint), sizeof(Vec3), 0.0f, 1.0f, 0.01f, nullptr, "Model temel renk tonlamasi (albedo)", nullptr, 1.0f, Vec2{0, 0}, Vec3{1, 1, 1}},
  {"Metalik", "##metallic", FieldType::Float, offsetof(content::SceneEntity, metallic), sizeof(float), 0.0f, 1.0f, 0.01f, "%.2f", "PBR metalik orani (0 = dielektrik, 1 = tam metal)", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Puruzluluk", "##roughness", FieldType::Float, offsetof(content::SceneEntity, roughness), sizeof(float), 0.04f, 1.0f, 0.01f, "%.2f", "Yuzey mikro-puruzlulugu (0.04 = ayna parlak, 1.0 = mat)", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yansitma", "##reflectance", FieldType::Float, offsetof(content::SceneEntity, reflectance), sizeof(float), 0.0f, 1.0f, 0.01f, "%.2f", "F0 Fresnel yansitma katsayisi", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Isima Rengi", "##emissive", FieldType::Color3, offsetof(content::SceneEntity, emissive), sizeof(Vec3), 0.0f, 1.0f, 0.01f, nullptr, "Kendiliginden isik yayan yuzey rengi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Isima Gucu", "##emissive_str", FieldType::Float, offsetof(content::SceneEntity, emissive_strength), sizeof(float), 0.0f, 100.0f, 0.1f, "%.1fx", "HDR isima carpani", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 3. ANİMASYON (kSceneAnim)
inline constexpr FieldMeta kAnimFields[] = {
  {"Klip Indeksi", "##anim_clip", FieldType::UInt, offsetof(content::SceneEntity, clip), sizeof(uint32_t), 0.0f, 64.0f, 1.0f, "%u", "Oynatilacak animasyon klip indeksi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Faz", "##anim_phase", FieldType::Float, offsetof(content::SceneEntity, phase), sizeof(float), 0.0f, 10.0f, 0.01f, "%.2f", "Animasyon baslangic fazi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hiz", "##anim_speed", FieldType::Float, offsetof(content::SceneEntity, speed), sizeof(float), 0.0f, 10.0f, 0.01f, "%.2fx", "Oynatma hizi carpani", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 4. IŞIK (kSceneLight)
inline constexpr FieldMeta kLightFields[] = {
  {"Tur", "##ltype", FieldType::Enum, offsetof(content::SceneEntity, light_type), sizeof(content::SceneLightType), 0.0f, 5.0f, 1.0f, nullptr, "Isik kaynaginin geometrik tipi", "Nokta (Point)\0Yonlu Gunes (Directional)\0Spot (Koni)\0Alan (Dikdortgen LTC)\0Tup (Kapsul)\0Disk\0\0", 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Renk", "##lcol", FieldType::Color3, offsetof(content::SceneEntity, light_color), sizeof(Vec3), 0.0f, 1.0f, 0.01f, nullptr, "Isik rengi (Lineer RGB)", nullptr, 1.0f, Vec2{0, 0}, Vec3{1, 1, 1}},
  {"Siddet", "##lintensity", FieldType::Float, offsetof(content::SceneEntity, light_intensity), sizeof(float), 0.0f, 500.0f, 0.05f, "%.2f Lux", "Isik akisi siddeti", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Etki Yaricapi", "##lrad", FieldType::Float, offsetof(content::SceneEntity, light_radius), sizeof(float), 0.1f, 200.0f, 0.1f, "%.2f m", "Isigin zayiflama yaricapi", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Spot Ic Aci", "##lspot_in", FieldType::Float, offsetof(content::SceneEntity, light_spot_inner), sizeof(float), 1.0f, 89.0f, 0.5f, "%.1f deg", "Spot isik ic koni acisi", nullptr, 25.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Spot Dis Aci", "##lspot_out", FieldType::Float, offsetof(content::SceneEntity, light_spot_outer), sizeof(float), 1.0f, 89.0f, 0.5f, "%.1f deg", "Spot isik dis koni acisi", nullptr, 40.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Alan Genisligi", "##lwidth", FieldType::Float, offsetof(content::SceneEntity, light_width), sizeof(float), 0.05f, 20.0f, 0.05f, "%.2f m", "Alan isigi genisligi veya tup boyu", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Alan Yuksekligi", "##lheight", FieldType::Float, offsetof(content::SceneEntity, light_height), sizeof(float), 0.05f, 20.0f, 0.05f, "%.2f m", "Alan isigi yuksekligi", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Golge Dok", "##lshadow", FieldType::Bool, offsetof(content::SceneEntity, light_cast_shadow), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Bu isik sahneye dinamik golge doksun mu?", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Isik Huzmesi", "##lgodray", FieldType::Bool, offsetof(content::SceneEntity, light_godray), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Volumetrik isik huzmeleri etkinlestirilsin mi?", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Huzme Gucu", "##lgodray_int", FieldType::Float, offsetof(content::SceneEntity, light_godray_intensity), sizeof(float), 0.1f, 10.0f, 0.05f, "%.2fx", "Isik huzmesinin sacilma yogunlugu carpani", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 5. GÖVDE (kSceneBody)
inline constexpr FieldMeta kBodyFields[] = {
  {"Sekil", "##bshape", FieldType::Enum, offsetof(content::SceneEntity, shape), sizeof(content::SceneShape), 0.0f, 1.0f, 1.0f, nullptr, "Fiziksel carpisma geometrisi", "Kutu\0Kure\0\0", 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yarim Kenar", "##bhalf", FieldType::Vec3, offsetof(content::SceneEntity, half), sizeof(Vec3), 0.01f, 50.0f, 0.02f, "%.2f m", "Kutu carpisan icin yarim kenar uzunluklari", nullptr, 0.5f, Vec2{0, 0}, Vec3{0.5f, 0.5f, 0.5f}},
  {"Yaricap", "##bradius", FieldType::Float, offsetof(content::SceneEntity, radius), sizeof(float), 0.01f, 50.0f, 0.02f, "%.2f m", "Kure carpisan yaricapi", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Dinamik", "##bdyn", FieldType::Bool, offsetof(content::SceneEntity, dynamic), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Isaretliyse yercekimi ve kuvvetlerden etkilenir", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 6. KAMERA (kSceneCamera)
inline constexpr FieldMeta kCameraFields[] = {
  {"Gorus Acisi (FOV)", "##cam_fov", FieldType::Float, offsetof(content::SceneEntity, cam_fov), sizeof(float), 10.0f, 140.0f, 0.5f, "%.1f deg", "Dikey gorus acisi derecesi", nullptr, 60.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yakin Duzlem", "##cam_near", FieldType::Float, offsetof(content::SceneEntity, cam_near), sizeof(float), 0.01f, 10.0f, 0.01f, "%.2f m", "Kamera yakin kirpma mesafesi", nullptr, 0.1f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Uzak Duzlem", "##cam_far", FieldType::Float, offsetof(content::SceneEntity, cam_far), sizeof(float), 1.0f, 10000.0f, 1.0f, "%.1f m", "Kamera uzak kirpma mesafesi", nullptr, 200.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 7. KARAKTER KONTROLCÜ (kSceneCharacter)
inline constexpr FieldMeta kCharacterFields[] = {
  {"Yaricap", "##char_rad", FieldType::Float, offsetof(content::SceneEntity, char_radius), sizeof(float), 0.05f, 5.0f, 0.01f, "%.2f m", "Kapsul yaricapi", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yukseklik", "##char_h", FieldType::Float, offsetof(content::SceneEntity, char_height), sizeof(float), 0.1f, 10.0f, 0.02f, "%.2f m", "Kapsul govde yuksekligi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Kutle", "##char_mass", FieldType::Float, offsetof(content::SceneEntity, char_mass), sizeof(float), 1.0f, 500.0f, 0.5f, "%.1f kg", "Karakter fiziksel kutlesi", nullptr, 70.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"En Dik Egim", "##char_slope", FieldType::Float, offsetof(content::SceneEntity, char_max_slope), sizeof(float), 0.0f, 89.0f, 0.5f, "%.1f deg", "Tirmanilabilecek maksimum egim acisi", nullptr, 45.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 8. SES (kSceneAudio)
inline constexpr FieldMeta kAudioFields[] = {
  {"Klip Dosyasi", "##audio_clip", FieldType::String, offsetof(content::SceneEntity, audio_clip), content::kSceneNameLen, 0.0f, 0.0f, 0.0f, nullptr, "Ses klip dosya adi veya yolu", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Ses Duzeyi", "##audio_vol", FieldType::Float, offsetof(content::SceneEntity, audio_volume), sizeof(float), 0.0f, 2.0f, 0.02f, "%.2f", "Ses yuksekligi kazanci", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Perde (Pitch)", "##audio_pitch", FieldType::Float, offsetof(content::SceneEntity, audio_pitch), sizeof(float), 0.2f, 3.0f, 0.02f, "%.2fx", "Ses frekans hizi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Dongu", "##audio_loop", FieldType::Bool, offsetof(content::SceneEntity, audio_loop), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Ses bittiginde bastan calsin mi?", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Uzamsal 3D", "##audio_spatial", FieldType::Bool, offsetof(content::SceneEntity, audio_spatial), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "3D konumsal ses zayiflamasi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 9. BETİK (kSceneScript)
inline constexpr FieldMeta kScriptFields[] = {
  // GENISLIK ALANIN KENDISINI IZLEMEK ZORUNDA: FieldMeta::size salt belge
  // degil, asagida `reset_component_to_defaults` icinde memset ve
  // `copy_component_data` icinde memcpy BOYUTU olarak kullaniliyor. Burasi
  // kSceneNameLen'de birakilsaydi "bileseni sifirla" ilk 32 bayti temizleyip
  // kuyrugu birakirdi; strcmp ve strlen bunu goremez ama coklu duzenleme
  // (editor_multiedit.cpp, sizeof kullaniyor) gorurdu — sessiz sapma.
  {"Betik Dosyasi", "##script_file", FieldType::String, offsetof(content::SceneEntity, script_file), content::kSceneScriptLen, 0.0f, 0.0f, 0.0f, nullptr, "Tulpar betik (.tpr) yolu", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Etkin", "##script_en", FieldType::Bool, offsetof(content::SceneEntity, script_enabled), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Betik calissin mi?", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 10. PARTİKÜL YAYICI (kSceneParticle)
inline constexpr FieldMeta kParticleFields[] = {
  {"Uretim Hizi", "##p_spawn", FieldType::Float, offsetof(content::SceneEntity, particle_spawn_rate), sizeof(float), 0.1f, 1000.0f, 1.0f, "%.1f /s", "Saniyede uretilen parcacik sayisi", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Min Omur", "##p_lmin", FieldType::Float, offsetof(content::SceneEntity, particle_lifetime_min), sizeof(float), 0.05f, 30.0f, 0.1f, "%.2f s", "Parcacik minimum yasama suresi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Maks Omur", "##p_lmax", FieldType::Float, offsetof(content::SceneEntity, particle_lifetime_max), sizeof(float), 0.05f, 30.0f, 0.1f, "%.2f s", "Parcacik maksimum yasama suresi", nullptr, 2.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Baslangic Boyutu", "##p_sstart", FieldType::Float, offsetof(content::SceneEntity, particle_size_start), sizeof(float), 0.01f, 10.0f, 0.02f, "%.2f m", "Uretim anindaki boyut", nullptr, 0.2f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Bitis Boyutu", "##p_send", FieldType::Float, offsetof(content::SceneEntity, particle_size_end), sizeof(float), 0.0f, 10.0f, 0.02f, "%.2f m", "Yokolus anindaki boyut", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hiz Vektoru", "##p_vel", FieldType::Vec3, offsetof(content::SceneEntity, particle_velocity), sizeof(Vec3), -50.0f, 50.0f, 0.1f, "%.1f m/s", "Ilk firlatma hizi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 2.0f, 0}},
  {"Rastgele Sapma", "##p_jit", FieldType::Vec3, offsetof(content::SceneEntity, particle_jitter), sizeof(Vec3), 0.0f, 20.0f, 0.05f, "%.2f", "Hiz vektoru uzerine rastgele gurultu", nullptr, 0.0f, Vec2{0, 0}, Vec3{1.0f, 0.5f, 1.0f}},
  {"Baslangic Rengi", "##p_cstart", FieldType::Color3, offsetof(content::SceneEntity, particle_color_start), sizeof(Vec3), 0.0f, 1.0f, 0.01f, nullptr, "Dogus rengi (RGB)", nullptr, 1.0f, Vec2{0, 0}, Vec3{1.0f, 0.6f, 0.1f}},
  {"Bitis Rengi", "##p_cend", FieldType::Color3, offsetof(content::SceneEntity, particle_color_end), sizeof(Vec3), 0.0f, 1.0f, 0.01f, nullptr, "Omur sonu rengi (RGB)", nullptr, 0.0f, Vec2{0, 0}, Vec3{0.2f, 0.2f, 0.2f}},
  {"Yercekimi", "##p_grav", FieldType::Float, offsetof(content::SceneEntity, particle_gravity), sizeof(float), -50.0f, 50.0f, 0.1f, "%.1f m/s2", "Parcaciklara uygulanan yercekimi ivmesi", nullptr, -2.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Parcacik Sekli", "##p_bb", FieldType::Enum, offsetof(content::SceneEntity, particle_billboard_type), sizeof(uint32_t), 0.0f, 7.0f, 1.0f, nullptr, "Gorsel yonelim ve sekil", "Kameraya Donuk Dortgen\0Hiza Gore Esneyen Igne\0Yatay Duzlem\0Yuvarlak 3B Kure\0Voksel 3B Kup\0" "3B Enerji Simiti (Torus)\0" "3B Koni (Cone)\0" "3B Silindir (Cylinder)\0\0", 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Turbulans Gucu", "##p_curl", FieldType::Float, offsetof(content::SceneEntity, particle_curl_strength), sizeof(float), 0.0f, 50.0f, 0.1f, "%.1f", "Curl noise akiskan turbulans ivmesi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Turbulans Frekansi", "##p_cfreq", FieldType::Float, offsetof(content::SceneEntity, particle_curl_freq), sizeof(float), 0.01f, 10.0f, 0.05f, "%.2f", "Uzamsal gurultu olcegi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hava Direnci", "##p_drag", FieldType::Float, offsetof(content::SceneEntity, particle_drag), sizeof(float), 0.0f, 10.0f, 0.01f, "%.2f", "Stokes aerodinamik surtunme", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Zemin Carpisma", "##p_coll", FieldType::Bool, offsetof(content::SceneEntity, particle_collision), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Zemin duzlemine carpip sekme", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Sekme Katsayisi", "##p_bnc", FieldType::Float, offsetof(content::SceneEntity, particle_bounce), sizeof(float), 0.0f, 1.0f, 0.05f, "%.2f", "Carpisma sonrasi enerji korunumu", nullptr, 0.6f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Olumde Alt-Parcacik", "##p_sub", FieldType::Int, offsetof(content::SceneEntity, particle_sub_on_death), sizeof(uint32_t), 0.0f, 50.0f, 1.0f, "%.0f", "Parcacik sonunde dogacak kivilcim sayisi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Kuyruk Izi", "##p_rib", FieldType::Bool, offsetof(content::SceneEntity, particle_ribbon), sizeof(bool), 0.0f, 1.0f, 1.0f, nullptr, "Serit seklinde kuyruk geometrisi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 11. ARAZİ (kSceneTerrain)
inline constexpr FieldMeta kTerrainFields[] = {
  {"Genislik", "##t_w", FieldType::Float, offsetof(content::SceneEntity, terrain_width), sizeof(float), 4.0f, 512.0f, 1.0f, "%.0f", "Izgara genislik hucre sayisi", nullptr, 64.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yukseklik", "##t_h", FieldType::Float, offsetof(content::SceneEntity, terrain_height), sizeof(float), 4.0f, 512.0f, 1.0f, "%.0f", "Izgara yukseklik hucre sayisi", nullptr, 64.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hucre Boyutu", "##t_cell", FieldType::Float, offsetof(content::SceneEntity, terrain_cell), sizeof(float), 0.1f, 10.0f, 0.1f, "%.2f m", "Her hucrenin dunya boyutu", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Genlik (Amp)", "##t_amp", FieldType::Float, offsetof(content::SceneEntity, terrain_amp), sizeof(float), 0.0f, 200.0f, 0.5f, "%.1f m", "Maksimum tepe yuksekligi", nullptr, 20.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Frekans", "##t_freq", FieldType::Float, offsetof(content::SceneEntity, terrain_freq), sizeof(float), 0.001f, 0.5f, 0.002f, "%.4f", "Perlin gurultu sikligi", nullptr, 0.02f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Oktav Sayisi", "##t_oct", FieldType::Int, offsetof(content::SceneEntity, terrain_octaves), sizeof(int32_t), 1.0f, 8.0f, 1.0f, "%d", "Fraktal gurultu katman sayisi", nullptr, 5.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Tohum (Seed)", "##t_seed", FieldType::UInt, offsetof(content::SceneEntity, terrain_seed), sizeof(uint32_t), 0.0f, 99999.0f, 1.0f, "%u", "Gurultu rastgelelik tohumu", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 12. SU / GERSTNER DALGASI (kSceneWater)
inline constexpr FieldMeta kWaterFields[] = {
  {"Dalga Boyu", "##w_len", FieldType::Float, offsetof(content::SceneEntity, wave_length), sizeof(float), 0.5f, 100.0f, 0.2f, "%.2f m", "Gerstner dalga boyu uzunlugu", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Genlik", "##w_amp", FieldType::Float, offsetof(content::SceneEntity, wave_amplitude), sizeof(float), 0.0f, 20.0f, 0.05f, "%.2f m", "Dalga tepe yuksekligi", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Diklik", "##w_steep", FieldType::Float, offsetof(content::SceneEntity, wave_steepness), sizeof(float), 0.0f, 1.0f, 0.02f, "%.2f", "Gerstner dalga keskinligi", nullptr, 0.3f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Dalga Hizi", "##w_spd", FieldType::Float, offsetof(content::SceneEntity, wave_speed), sizeof(float), 0.0f, 30.0f, 0.1f, "%.2f m/s", "Yayilma hizi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Dalga Yonu", "##w_dir", FieldType::Vec2, offsetof(content::SceneEntity, wave_direction), sizeof(Vec2), -1.0f, 1.0f, 0.05f, "%.2f", "2D yayilma yonu vektoru", nullptr, 0.0f, Vec2{1.0f, 0.0f}, Vec3{0, 0, 0}},
};

// 13. RÜZGAR (kSceneWind)
inline constexpr FieldMeta kWindFields[] = {
  {"Ruzgar Yonu", "##wind_dir", FieldType::Vec2, offsetof(content::SceneEntity, wind_direction), sizeof(Vec2), -1.0f, 1.0f, 0.05f, "%.2f", "2D ruzgar akis yonu", nullptr, 0.0f, Vec2{1.0f, 0.0f}, Vec3{0, 0, 0}},
  {"Ruzgar Gucu", "##wind_str", FieldType::Float, offsetof(content::SceneEntity, wind_strength), sizeof(float), 0.0f, 50.0f, 0.2f, "%.1f m/s", "Ortalama ruzgar hizi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Esinti Siddeti", "##wind_gust", FieldType::Float, offsetof(content::SceneEntity, wind_gustiness), sizeof(float), 0.0f, 2.0f, 0.05f, "%.2fx", "Ani esinti carpani", nullptr, 0.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Esinti Frekansi", "##wind_freq", FieldType::Float, offsetof(content::SceneEntity, wind_gust_freq), sizeof(float), 0.01f, 5.0f, 0.02f, "%.2f Hz", "Esintilerin degisim sikligi", nullptr, 0.3f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Tohum (Seed)", "##wind_seed", FieldType::UInt, offsetof(content::SceneEntity, wind_seed), sizeof(uint32_t), 0.0f, 99999.0f, 1.0f, "%u", "Ruzgar gurultu tohumu", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 14. VOKSEL (kSceneVoxel)
inline constexpr FieldMeta kVoxelFields[] = {
  {"Boyut X", "##vox_x", FieldType::UInt, offsetof(content::SceneEntity, voxel_size_x), sizeof(uint32_t), 1.0f, 128.0f, 1.0f, "%u", "Voksel izgara X genisligi", nullptr, 16.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Boyut Y", "##vox_y", FieldType::UInt, offsetof(content::SceneEntity, voxel_size_y), sizeof(uint32_t), 1.0f, 128.0f, 1.0f, "%u", "Voksel izgara Y yuksekligi", nullptr, 16.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Boyut Z", "##vox_z", FieldType::UInt, offsetof(content::SceneEntity, voxel_size_z), sizeof(uint32_t), 1.0f, 128.0f, 1.0f, "%u", "Voksel izgara Z derinligi", nullptr, 16.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Voksel Kenari", "##vox_cell", FieldType::Float, offsetof(content::SceneEntity, voxel_cell), sizeof(float), 0.05f, 5.0f, 0.05f, "%.2f m", "Bir voksel kupunun kenar boyutu", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 15. FİZİK EKLEMİ (kSceneJoint)
inline constexpr FieldMeta kJointFields[] = {
  {"Hedef Varlik", "##j_target", FieldType::Int, offsetof(content::SceneEntity, joint_target), sizeof(int32_t), -1.0f, 255.0f, 1.0f, "%d", "Baglanilan diger varligin indeksi (-1 = dunya)", nullptr, -1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Eklem Ekseni", "##j_axis", FieldType::Vec3, offsetof(content::SceneEntity, joint_axis), sizeof(Vec3), -1.0f, 1.0f, 0.05f, "%.2f", "Donus veya kayma ekseni", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 1.0f, 0}},
  {"Min Sinir", "##j_lmin", FieldType::Float, offsetof(content::SceneEntity, joint_limit_min), sizeof(float), -180.0f, 180.0f, 1.0f, "%.1f deg", "Minimum donus veya mesafe siniri", nullptr, -45.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Maks Sinir", "##j_lmax", FieldType::Float, offsetof(content::SceneEntity, joint_limit_max), sizeof(float), -180.0f, 180.0f, 1.0f, "%.1f deg", "Maksimum donus veya mesafe siniri", nullptr, 45.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Motor Hizi", "##j_spd", FieldType::Float, offsetof(content::SceneEntity, joint_motor_speed), sizeof(float), 0.0f, 100.0f, 0.5f, "%.1f deg/s", "Eklem motoru hedef hizi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 16. YANSIMA SONDASI (kSceneRefProbe)
inline constexpr FieldMeta kRefProbeFields[] = {
  {"Yansima Yaricapi", "##prb_rad", FieldType::Float, offsetof(content::SceneEntity, ref_probe_radius), sizeof(float), 0.5f, 100.0f, 0.2f, "%.1f m", "IBL yansima etki alani", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Yansima Siddeti", "##prb_int", FieldType::Float, offsetof(content::SceneEntity, ref_probe_intensity), sizeof(float), 0.0f, 10.0f, 0.05f, "%.2fx", "Ortam yansimasi parlaklik carpani", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 17. YANKI ALANI (kSceneReverb)
inline constexpr FieldMeta kReverbFields[] = {
  {"Sonumlenme Suresi", "##rev_dec", FieldType::Float, offsetof(content::SceneEntity, reverb_decay), sizeof(float), 0.1f, 10.0f, 0.1f, "%.2f s", "Yanki sonumlenme (RT60) suresi", nullptr, 1.5f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Oda Boyutu", "##rev_room", FieldType::Float, offsetof(content::SceneEntity, reverb_room_size), sizeof(float), 0.0f, 1.0f, 0.02f, "%.2f", "Hacim hissi carpani", nullptr, 0.8f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 18. CAN / SAĞLIK (kSceneHealth)
inline constexpr FieldMeta kHealthFields[] = {
  {"Maksimum Can", "##hp_max", FieldType::Float, offsetof(content::SceneEntity, health_max), sizeof(float), 1.0f, 100000.0f, 5.0f, "%.0f HP", "Karakter maksimum can puani", nullptr, 100.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Mevcut Can", "##hp_cur", FieldType::Float, offsetof(content::SceneEntity, health_current), sizeof(float), 0.0f, 100000.0f, 5.0f, "%.0f HP", "Karakter anlik can puani", nullptr, 100.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 19. YETENEK / GAS (kSceneAbility)
inline constexpr FieldMeta kAbilityFields[] = {
  {"Yetenek ID", "##ab_id", FieldType::UInt, offsetof(content::SceneEntity, ability_id), sizeof(uint32_t), 0.0f, 1000.0f, 1.0f, "%u", "GAS veri tabani yetenek tanim indeksi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hasar", "##ab_dmg", FieldType::Float, offsetof(content::SceneEntity, ability_damage), sizeof(float), 0.0f, 10000.0f, 1.0f, "%.1f", "Temel hasar degeri", nullptr, 10.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Menzil", "##ab_rng", FieldType::Float, offsetof(content::SceneEntity, ability_range), sizeof(float), 0.1f, 100.0f, 0.2f, "%.1f m", "Uygulama menzili", nullptr, 5.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Bekleme Suresi", "##ab_cd", FieldType::Float, offsetof(content::SceneEntity, ability_cooldown), sizeof(float), 0.0f, 60.0f, 0.1f, "%.1f s", "Iki kullanim arasi bekleme suresi", nullptr, 1.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// 20. YAPAY ZEKA AJANI (kSceneNavAgent)
inline constexpr FieldMeta kNavAgentFields[] = {
  {"Hedef Konum", "##ai_tgt", FieldType::Vec3, offsetof(content::SceneEntity, ai_target), sizeof(Vec3), -1000.0f, 1000.0f, 0.1f, "%.1f m", "Navmesh hedef varis noktasi", nullptr, 0.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Hareket Hizi", "##ai_spd", FieldType::Float, offsetof(content::SceneEntity, ai_speed), sizeof(float), 0.1f, 50.0f, 0.1f, "%.1f m/s", "Seyir hizi", nullptr, 3.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
  {"Donus Hizi", "##ai_turn", FieldType::Float, offsetof(content::SceneEntity, ai_turn_speed), sizeof(float), 10.0f, 720.0f, 5.0f, "%.0f deg/s", "Hedefe donus acisal hizi", nullptr, 120.0f, Vec2{0, 0}, Vec3{0, 0, 0}},
};

// =============================================================================
// BİLEŞEN METAVERİ KAYIT TABLOSU (METADATA REGISTRY)
// =============================================================================

inline const ComponentMeta kComponentMetas[] = {
  {"Model", "\xE2\x97\x86", content::kSceneModel, kModelFields, sizeof(kModelFields)/sizeof(FieldMeta)},
  {"Animasyon", "\xE2\x86\xBB", content::kSceneAnim, kAnimFields, sizeof(kAnimFields)/sizeof(FieldMeta)},
  {"Isik", "\xE2\x98\x80", content::kSceneLight, kLightFields, sizeof(kLightFields)/sizeof(FieldMeta)},
  {"Govde", "\xE2\x97\xBC", content::kSceneBody, kBodyFields, sizeof(kBodyFields)/sizeof(FieldMeta)},
  {"Kamera", "\xE2\x96\xA3", content::kSceneCamera, kCameraFields, sizeof(kCameraFields)/sizeof(FieldMeta)},
  {"Karakter", "\xE2\x8A\x99", content::kSceneCharacter, kCharacterFields, sizeof(kCharacterFields)/sizeof(FieldMeta)},
  {"Ses", "\xE2\x99\xAB", content::kSceneAudio, kAudioFields, sizeof(kAudioFields)/sizeof(FieldMeta)},
  {"Betik", "\xE2\x8C\xA8", content::kSceneScript, kScriptFields, sizeof(kScriptFields)/sizeof(FieldMeta)},
  {"Partikul", "\xE2\x9C\xA6", content::kSceneParticle, kParticleFields, sizeof(kParticleFields)/sizeof(FieldMeta)},
  {"Arazi", "\xE2\x96\xB2", content::kSceneTerrain, kTerrainFields, sizeof(kTerrainFields)/sizeof(FieldMeta)},
  {"Su", "\xE2\x89\x88", content::kSceneWater, kWaterFields, sizeof(kWaterFields)/sizeof(FieldMeta)},
  {"Ruzgar", "\xE2\x89\x8B", content::kSceneWind, kWindFields, sizeof(kWindFields)/sizeof(FieldMeta)},
  {"Voksel", "\xE2\x8A\x9E", content::kSceneVoxel, kVoxelFields, sizeof(kVoxelFields)/sizeof(FieldMeta)},
  {"Eklem", "\xE2\x98\x8D", content::kSceneJoint, kJointFields, sizeof(kJointFields)/sizeof(FieldMeta)},
  {"Yansima Sondasi", "\xE2\x97\x89", content::kSceneRefProbe, kRefProbeFields, sizeof(kRefProbeFields)/sizeof(FieldMeta)},
  {"Yanki Alani", "\xE2\x97\x8E", content::kSceneReverb, kReverbFields, sizeof(kReverbFields)/sizeof(FieldMeta)},
  {"Saglik", "\xE2\x99\xA5", content::kSceneHealth, kHealthFields, sizeof(kHealthFields)/sizeof(FieldMeta)},
  {"Yetenek", "\xE2\x9A\xA1", content::kSceneAbility, kAbilityFields, sizeof(kAbilityFields)/sizeof(FieldMeta)},
  {"NavAgent", "\xE2\x9E\xA4", content::kSceneNavAgent, kNavAgentFields, sizeof(kNavAgentFields)/sizeof(FieldMeta)},
  {"Gokyuzu", "\xE2\x98\xBC", content::kSceneSkybox, nullptr, 0},
  {"Envanter", "\xE2\x96\xA3", content::kSceneInventory, nullptr, 0},
};
inline constexpr size_t kComponentMetaCount = sizeof(kComponentMetas)/sizeof(ComponentMeta);

inline const ComponentMeta *find_component_meta(uint32_t comp_bit) {
  for (size_t i = 0; i < kComponentMetaCount; i++) {
    if (kComponentMetas[i].component_bit == comp_bit) return &kComponentMetas[i];
  }
  return nullptr;
}

// =============================================================================
// YARDIMCI FONKSİYONLAR: SIFIRLAMA, KOPYALAMA VE VERİ ERİŞİMİ
// =============================================================================

inline void reset_component_to_defaults(content::SceneEntity &e, uint32_t comp_bit) {
  const ComponentMeta *meta = find_component_meta(comp_bit);
  if (!meta || !meta->fields) return;
  uint8_t *base = reinterpret_cast<uint8_t*>(&e);

  for (size_t i = 0; i < meta->field_count; i++) {
    const FieldMeta &f = meta->fields[i];
    uint8_t *ptr = base + f.offset;
    switch (f.type) {
      case FieldType::Float:
        *reinterpret_cast<float*>(ptr) = f.default_f32;
        break;
      case FieldType::Int:
      case FieldType::Enum:
        *reinterpret_cast<int32_t*>(ptr) = static_cast<int32_t>(f.default_f32);
        break;
      case FieldType::UInt:
        *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(f.default_f32);
        break;
      case FieldType::Bool:
        *reinterpret_cast<bool*>(ptr) = (f.default_f32 > 0.5f);
        break;
      case FieldType::Vec2:
        *reinterpret_cast<Vec2*>(ptr) = f.default_vec2;
        break;
      case FieldType::Vec3:
      case FieldType::Color3:
        *reinterpret_cast<Vec3*>(ptr) = f.default_vec3;
        break;
      case FieldType::String:
        std::memset(ptr, 0, f.size);
        break;
      default:
        break;
    }
  }
}

inline void copy_component_data(const content::SceneEntity &src, content::SceneEntity &dst, uint32_t comp_bit) {
  const ComponentMeta *meta = find_component_meta(comp_bit);
  if (!meta || !meta->fields) return;
  const uint8_t *src_base = reinterpret_cast<const uint8_t*>(&src);
  uint8_t *dst_base = reinterpret_cast<uint8_t*>(&dst);

  for (size_t i = 0; i < meta->field_count; i++) {
    const FieldMeta &f = meta->fields[i];
    std::memcpy(dst_base + f.offset, src_base + f.offset, f.size);
  }
}

} // namespace tulpar::engine::reflect
