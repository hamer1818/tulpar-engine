#include "sim/ability_system.hpp"

namespace tulpar::engine::sim {

void register_ability_components() {
  component_id<Health>("Health");
  component_id<Armor>("Armor");
  component_id<Ability>("Ability");
  component_id<Cooldown>("Cooldown");
  component_id<Inventory>("Inventory");
}

void update_ability_system(World &w, float dt) {
  // Cooldown dusurme sistemi
  w.each(mask_of(component_id<Cooldown>()), [dt](ChunkView v) {
    Cooldown *cooldowns = v.col<Cooldown>();
    for (uint32_t i = 0; i < v.count; i++) {
      if (cooldowns[i].remaining > 0.0f) {
        cooldowns[i].remaining -= dt;
        if (cooldowns[i].remaining < 0.0f) {
          cooldowns[i].remaining = 0.0f;
        }
      }
    }
  });
}

void apply_damage(World &w, Entity target, float amount, bool is_magical) {
  if (!w.alive(target)) return;
  
  Health *h = w.get<Health>(target);
  if (!h) return;
  
  Armor *a = w.get<Armor>(target);
  float mitigation = 0.0f;
  
  if (a) {
    mitigation = is_magical ? a->magical : a->physical;
  }
  
  float actual_damage = amount - mitigation;
  if (actual_damage < 0.0f) actual_damage = 0.0f;
  
  h->current -= actual_damage;
  if (h->current < 0.0f) h->current = 0.0f;
}

bool try_cast_ability(World &w, Entity caster, Entity target) {
  if (!w.alive(caster) || !w.alive(target)) return false;
  
  Ability *ab = w.get<Ability>(caster);
  Cooldown *cd = w.get<Cooldown>(caster);
  
  if (!ab) return false;
  
  // Cooldown kontrolu
  if (cd && cd->remaining > 0.0f) {
    return false; // Yetenek henüz hazir degil
  }
  
  // Hasar uygulama
  apply_damage(w, target, ab->base_damage, (ab->id == 2)); // Ornek: id==2 buyudur
  
  // Cooldown baslat
  if (cd) {
    cd->remaining = ab->cooldown_max;
  }
  
  return true;
}

bool add_item_to_inventory(World &w, Entity e, uint32_t item_id, uint32_t qty) {
  if (!w.alive(e)) return false;
  
  Inventory *inv = w.get<Inventory>(e);
  if (!inv) return false;
  
  // Mevcut esya var mi kontrol et
  for (uint32_t i = 0; i < inv->count; i++) {
    if (inv->slots[i].item_id == item_id) {
      inv->slots[i].quantity += qty;
      return true;
    }
  }
  
  // Yeni bosluga ekle
  if (inv->count < kMaxInventorySlots) {
    inv->slots[inv->count].item_id = item_id;
    inv->slots[inv->count].quantity = qty;
    inv->count++;
    return true;
  }
  
  return false; // Envanter dolu
}

} // namespace tulpar::engine::sim
