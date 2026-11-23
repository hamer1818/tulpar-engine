#include "renderer/vsm_pages.hpp"

#include <cmath>

namespace tulpar::engine::renderer {

namespace {
uint32_t hash_key(VsmPageKey k) {
  uint32_t h = k.level * 0x9E3779B1u;
  h ^= (uint32_t)k.x * 0x85EBCA77u + (h << 6) + (h >> 2);
  h ^= (uint32_t)k.y * 0xC2B2AE3Du + (h << 6) + (h >> 2);
  return h;
}
bool same(VsmPageKey a, VsmPageKey b) { return a.level == b.level && a.x == b.x && a.y == b.y; }
} // namespace

void VsmPageCache::reset_light(Vec3 light_dir) {
  const Vec3 d = normalize(light_dir);
  const Vec3 up = std::fabs(d.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  lu_ = normalize(cross(up, d));
  lv_ = cross(d, lu_);
  if (!slots_) return; // init'ten once (taban yalniz)
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) slots_[i] = Slot{};
  for (uint32_t i = 0; i <= tab_mask_; i++) tab_slot_[i] = -1;
  free_n_ = cfg_.pool_pages;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) free_[i] = cfg_.pool_pages - 1 - i;
  tombstones_ = 0;
  resident_ = 0;
  render_n_ = 0;
}

bool VsmPageCache::init(Arena &arena, const VsmConfig &cfg, Vec3 light_dir) {
  cfg_ = cfg;
  if (cfg_.levels == 0 || cfg_.pool_pages == 0 || cfg_.base_page_world <= 0 || cfg_.level0_distance <= 0) return false;
  slots_ = nullptr;
  reset_light(light_dir); // yalniz taban (tablolar henuz yok)
  uint32_t cap = 1;
  while (cap < cfg_.pool_pages * 2) cap <<= 1;
  tab_mask_ = cap - 1;
  slots_ = arena.alloc_array_zeroed<Slot>(cfg_.pool_pages);
  free_ = arena.alloc_array<uint32_t>(cfg_.pool_pages);
  tab_slot_ = arena.alloc_array<int32_t>(cap);
  render_ = arena.alloc_array<VsmRender>(cfg_.max_render_per_frame);
  if (!slots_ || !free_ || !tab_slot_ || !render_) return false;
  for (uint32_t i = 0; i < cap; i++) tab_slot_[i] = -1;
  free_n_ = cfg_.pool_pages;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) free_[i] = cfg_.pool_pages - 1 - i; // 0 once cikar (belirlenimli)
  return true;
}

float VsmPageCache::page_world(uint32_t level) const { return cfg_.base_page_world * (float)(1u << level); }

uint32_t VsmPageCache::level_for(Vec3 world) const {
  const float dist = length(world - cam_);
  uint32_t l = 0;
  float lim = cfg_.level0_distance;
  while (dist > lim && l + 1 < cfg_.levels) { lim *= 2.0f; l++; }
  return l;
}

VsmPageKey VsmPageCache::key_for(Vec3 world, uint32_t level) const {
  const float pw = page_world(level);
  return VsmPageKey{level, (int32_t)std::floor(dot(world, lu_) / pw), (int32_t)std::floor(dot(world, lv_) / pw)};
}

int32_t VsmPageCache::find(VsmPageKey k) const {
  uint32_t i = hash_key(k) & tab_mask_;
  for (uint32_t probe = 0; probe <= tab_mask_; probe++, i = (i + 1) & tab_mask_) {
    const int32_t s = tab_slot_[i];
    if (s == -1) return -1;
    if (s >= 0 && same(slots_[s].key, k)) return (int32_t)i;
  }
  return -1;
}

void VsmPageCache::table_insert(VsmPageKey k, uint32_t slot) {
  uint32_t i = hash_key(k) & tab_mask_;
  while (tab_slot_[i] >= 0) i = (i + 1) & tab_mask_; // tablo havuzun 2 kati: her zaman yer var
  tab_slot_[i] = (int32_t)slot;
}

void VsmPageCache::table_erase(VsmPageKey k) {
  const int32_t i = find(k);
  if (i < 0) return;
  tab_slot_[i] = -2;
  // Mezar tasi birikirse bos (-1) yuva kalmaz ve her arama butun tabloyu tarar: esikte yerlesik yuvalardan yeniden kur.
  if (++tombstones_ > (tab_mask_ + 1) / 4) {
    for (uint32_t j = 0; j <= tab_mask_; j++) tab_slot_[j] = -1;
    for (uint32_t s = 0; s < cfg_.pool_pages; s++)
      if (slots_[s].used && !same(slots_[s].key, k)) table_insert(slots_[s].key, s);
    tombstones_ = 0;
  }
}

bool VsmPageCache::is_valid(VsmPageKey k) const {
  const int32_t i = find(k);
  return i >= 0 && !slots_[tab_slot_[i]].dirty;
}

void VsmPageCache::begin_frame(Vec3 camera) {
  cam_ = camera;
  frame_++;
  render_n_ = 0;
  stats_ = VsmStats{};
}

uint32_t VsmPageCache::evict_victim() {
  uint32_t best = ~0u, best_frame = ~0u;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) {
    const Slot &s = slots_[i];
    if (!s.used || s.last_used == frame_) continue; // bu karede istenen sayfa tahliye EDILMEZ
    if (s.last_used < best_frame) { best_frame = s.last_used; best = i; }
  }
  return best;
}

void VsmPageCache::push_render(VsmPageKey k, uint32_t slot) {
  if (render_n_ == cfg_.max_render_per_frame) { stats_.render_overflow++; return; }
  render_[render_n_++] = VsmRender{k, slot};
}

void VsmPageCache::request(Vec3 world) { request_key(key_for(world, level_for(world))); }

void VsmPageCache::request_key(VsmPageKey k) {
  const int32_t ti = find(k);
  if (ti >= 0) {
    Slot &s = slots_[tab_slot_[ti]];
    if (s.last_used == frame_) return; // ayni sayfa bu karede zaten islendi
    s.last_used = frame_;
    stats_.requested++;
    if (s.dirty) {
      if (render_n_ < cfg_.max_render_per_frame) s.dirty = 0; // listeye giremezse kirli kalir, sonraki kare
      stats_.redrawn++;
      push_render(k, (uint32_t)tab_slot_[ti]);
    } else {
      stats_.hits++;
    }
    return;
  }
  stats_.requested++;
  uint32_t slot;
  if (free_n_) {
    slot = free_[--free_n_];
  } else {
    slot = evict_victim();
    if (slot == ~0u) { stats_.exhausted++; return; }
    table_erase(slots_[slot].key);
    stats_.evicted++;
    resident_--;
  }
  Slot &s = slots_[slot];
  s.key = k;
  s.used = 1;
  s.last_used = frame_;
  s.dirty = render_n_ < cfg_.max_render_per_frame ? 0 : 1;
  table_insert(k, slot);
  resident_++;
  stats_.allocated++;
  push_render(k, slot);
}

uint32_t VsmPageCache::invalidate(Vec3 mn, Vec3 mx) {
  // AABB'nin 8 kosesinin isik-uzayi (u, v) araligi; her yerlesik sayfanin dikdortgeniyle kesisim.
  float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
  for (uint32_t c = 0; c < 8; c++) {
    const Vec3 p{(c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z};
    const float u = dot(p, lu_), v = dot(p, lv_);
    u0 = u < u0 ? u : u0; u1 = u > u1 ? u : u1;
    v0 = v < v0 ? v : v0; v1 = v > v1 ? v : v1;
  }
  uint32_t n = 0;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) {
    Slot &s = slots_[i];
    if (!s.used || s.dirty) continue;
    const float pw = page_world(s.key.level);
    const float pu0 = s.key.x * pw, pv0 = s.key.y * pw;
    if (pu0 <= u1 && pu0 + pw >= u0 && pv0 <= v1 && pv0 + pw >= v0) { s.dirty = 1; n++; }
  }
  return n;
}

void VsmPageCache::end_frame() {}

uint32_t vsm_mark_from_depth(VsmPageCache &cache, const float *depth, uint32_t w, uint32_t h, const Mat4 &inv, uint32_t stride) {
  if (!depth || !w || !h) return 0;
  if (stride == 0) stride = 1;
  uint32_t n = 0;
  for (uint32_t y = 0; y < h; y += stride)
    for (uint32_t x = 0; x < w; x += stride) {
      const float d = depth[(size_t)y * w + x];
      if (d >= 1.0f) continue;
      const Vec4 c = inv * Vec4{((float)x + 0.5f) / (float)w * 2.0f - 1.0f, ((float)y + 0.5f) / (float)h * 2.0f - 1.0f, d, 1.0f};
      if (c.w == 0.0f) continue;
      cache.request(Vec3{c.x / c.w, c.y / c.w, c.z / c.w});
      n++;
    }
  return n;
}

bool vsm_pack_key(VsmPageKey k, uint32_t *out) {
  if (k.level > kVsmKeyLevelMax || k.x < kVsmKeyCoordMin || k.x > kVsmKeyCoordMax || k.y < kVsmKeyCoordMin ||
      k.y > kVsmKeyCoordMax)
    return false;
  *out = (k.level << 28) | (((uint32_t)k.x & 0x3FFFu) << 14) | ((uint32_t)k.y & 0x3FFFu);
  return true;
}

VsmPageKey vsm_unpack_key(uint32_t p) {
  // 14 bitlik isaretli alanlar: ust bitleri isaretle doldur.
  auto sx = [](uint32_t v) { return (int32_t)(v ^ 0x2000u) - 0x2000; };
  return VsmPageKey{p >> 28, sx((p >> 14) & 0x3FFFu), sx(p & 0x3FFFu)};
}

uint32_t vsm_request_from_gpu(VsmPageCache &cache, const uint32_t *pages, uint32_t list_capacity) {
  if (!pages) return 0;
  const uint32_t n = pages[0] < list_capacity ? pages[0] : list_capacity;
  const uint32_t *keys = pages + sizeof(VsmRequestHeader) / 4;
  for (uint32_t i = 0; i < n; i++) cache.request_key(vsm_unpack_key(keys[i]));
  return n;
}

} // namespace tulpar::engine::renderer
