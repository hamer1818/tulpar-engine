// L1 CORE — Statik Yansıma Motoru Birim Testleri (test_reflect.cpp)
// O3DE ve Prowl esinlenmeli statik alan ofsetleri, varsayılana sıfırlama
// ve bileşen veri kopyalama operasyonlarının bit düzeyinde doğruluğunu kanıtlar.

#include "content/reflect.hpp"
#include "content/scene.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::reflect;

ENGINE_TEST(reflect_component_metas_exist_and_match_bits) {
  const ComponentMeta *light_meta = find_component_meta(content::kSceneLight);
  CHECK(light_meta != nullptr);
  CHECK(light_meta->component_bit == content::kSceneLight);
  CHECK(light_meta->field_count > 0);

  const ComponentMeta *model_meta = find_component_meta(content::kSceneModel);
  CHECK(model_meta != nullptr);
  CHECK(model_meta->component_bit == content::kSceneModel);

  const ComponentMeta *body_meta = find_component_meta(content::kSceneBody);
  CHECK(body_meta != nullptr);
  CHECK(body_meta->component_bit == content::kSceneBody);

  const ComponentMeta *cam_meta = find_component_meta(content::kSceneCamera);
  CHECK(cam_meta != nullptr);
  CHECK(cam_meta->component_bit == content::kSceneCamera);

  const ComponentMeta *part_meta = find_component_meta(content::kSceneParticle);
  CHECK(part_meta != nullptr);
  CHECK(part_meta->component_bit == content::kSceneParticle);
  CHECK(part_meta->field_count > 5);

  const ComponentMeta *water_meta = find_component_meta(content::kSceneWater);
  CHECK(water_meta != nullptr);
  CHECK(water_meta->component_bit == content::kSceneWater);

  const ComponentMeta *terrain_meta = find_component_meta(content::kSceneTerrain);
  CHECK(terrain_meta != nullptr);
  CHECK(terrain_meta->component_bit == content::kSceneTerrain);

  const ComponentMeta *health_meta = find_component_meta(content::kSceneHealth);
  CHECK(health_meta != nullptr);
  CHECK(health_meta->component_bit == content::kSceneHealth);

  const ComponentMeta *ability_meta = find_component_meta(content::kSceneAbility);
  CHECK(ability_meta != nullptr);
  CHECK(ability_meta->component_bit == content::kSceneAbility);
}

ENGINE_TEST(reflect_field_offsets_and_sizes_match_scene_entity) {
  const ComponentMeta *light_meta = find_component_meta(content::kSceneLight);
  CHECK(light_meta != nullptr);

  // Işık alanlarının ofset ve boyut denetimi
  bool found_intensity = false;
  for (size_t i = 0; i < light_meta->field_count; i++) {
    const FieldMeta &f = light_meta->fields[i];
    if (std::strcmp(f.name, "Siddet") == 0) {
      found_intensity = true;
      CHECK(f.offset == offsetof(content::SceneEntity, light_intensity));
      CHECK(f.size == sizeof(float));
      CHECK(f.type == FieldType::Float);
    }
  }
  CHECK(found_intensity);

  // Su bileşeni Vec2 dalga yönü ofset denetimi
  const ComponentMeta *water_meta = find_component_meta(content::kSceneWater);
  CHECK(water_meta != nullptr);
  bool found_wave_dir = false;
  for (size_t i = 0; i < water_meta->field_count; i++) {
    const FieldMeta &f = water_meta->fields[i];
    if (std::strcmp(f.name, "Dalga Yonu") == 0) {
      found_wave_dir = true;
      CHECK(f.offset == offsetof(content::SceneEntity, wave_direction));
      CHECK(f.size == sizeof(Vec2));
      CHECK(f.type == FieldType::Vec2);
    }
  }
  CHECK(found_wave_dir);
}

ENGINE_TEST(reflect_reset_component_to_defaults) {
  content::SceneEntity e{};
  // Değerleri rastgele boz
  e.light_intensity = 999.0f;
  e.light_color = Vec3{0.1f, 0.2f, 0.3f};
  e.light_radius = 50.0f;

  reset_component_to_defaults(e, content::kSceneLight);

  // Varsayılanlara dönmeli
  CHECK(e.light_intensity == 10.0f);
  CHECK(e.light_color.x == 1.0f);
  CHECK(e.light_color.y == 1.0f);
  CHECK(e.light_color.z == 1.0f);
  CHECK(e.light_radius == 10.0f);

  // Su bileşeni sıfırlama denetimi
  e.wave_length = 999.0f;
  e.wave_direction = Vec2{0.0f, -1.0f};
  reset_component_to_defaults(e, content::kSceneWater);
  CHECK(e.wave_length == 10.0f);
  CHECK(e.wave_direction.x == 1.0f);
  CHECK(e.wave_direction.y == 0.0f);
}

ENGINE_TEST(reflect_copy_component_preserves_data) {
  content::SceneEntity src{};
  src.light_intensity = 42.5f;
  src.light_color = Vec3{0.9f, 0.5f, 0.1f};
  src.light_radius = 18.0f;
  src.light_godray = true;
  src.light_godray_intensity = 2.4f;

  content::SceneEntity dst{};
  dst.light_intensity = 1.0f;
  dst.light_color = Vec3{1.0f, 1.0f, 1.0f};

  copy_component_data(src, dst, content::kSceneLight);

  CHECK(dst.light_intensity == 42.5f);
  CHECK(dst.light_color.x == 0.9f);
  CHECK(dst.light_color.y == 0.5f);
  CHECK(dst.light_color.z == 0.1f);
  CHECK(dst.light_radius == 18.0f);
  CHECK(dst.light_godray == true);
  CHECK(dst.light_godray_intensity == 2.4f);

  // Parçacık bileşeni kopyalama denetimi
  src.particle_spawn_rate = 250.0f;
  src.particle_velocity = Vec3{0, 15.0f, 0};
  copy_component_data(src, dst, content::kSceneParticle);
  CHECK(dst.particle_spawn_rate == 250.0f);
  CHECK(dst.particle_velocity.y == 15.0f);
}
