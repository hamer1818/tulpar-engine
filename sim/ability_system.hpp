#pragma once

#include "sim/ecs.hpp"
#include <cstdint>

namespace tulpar::engine::sim {

// -----------------------------------------------------------------------------
// GAS (Gameplay Ability System) ve Envanter Bilesenleri
// Tum veri deterministik, sabit boyutlu (POD) olmalidir.
// -----------------------------------------------------------------------------

struct Health {
  float current;
  float max;
};

struct Armor {
  float physical;
  float magical;
};

// Yetenek Tanimi (Veri Odakli)
struct Ability {
  uint32_t id;         // Yetenek tipi (0=Yakin, 1=Uzak, 2=Buyu vb.)
  float base_damage;   // Temel hasar
  float range;         // Etki menzili
  float cooldown_max;  // Bekleme suresi kapasitesi (saniye)
};

// Bekleme Suresi Durumu (Cooldown)
struct Cooldown {
  float remaining;     // 0 ise yetenek hazir
};

// Basit Eşya (Item) tanimi
struct Item {
  uint32_t item_id;
  uint32_t quantity;
};

// Envanter - Statik kapasiteli (rollback guvenligi icin heap tahsisi yasak)
constexpr uint32_t kMaxInventorySlots = 16;
struct Inventory {
  Item slots[kMaxInventorySlots];
  uint32_t count;
};

// -----------------------------------------------------------------------------
// Sistem API
// -----------------------------------------------------------------------------

// ECS'e bilesenleri kaydeder
void register_ability_components();

// Her kare guncellenmesi gereken sistem (cooldown dusurme vb.)
void update_ability_system(World &w, float dt);

// Hasar uygulama yardimci fonksiyonu
void apply_damage(World &w, Entity target, float amount, bool is_magical);

// Yetenek kullanimi denemesi
bool try_cast_ability(World &w, Entity caster, Entity target);

// Envantere esya ekleme
bool add_item_to_inventory(World &w, Entity e, uint32_t item_id, uint32_t qty);

} // namespace tulpar::engine::sim
