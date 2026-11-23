#include "renderer/vt_cache.hpp"

namespace tulpar::engine::renderer {

namespace {
uint32_t hash_page(VtPage p) {
  uint32_t h = (uint32_t)p.texture * 0x9E3779B1u ^ (uint32_t)p.mip * 0x85EBCA77u;
  h ^= ((uint32_t)p.x << 16 | p.y) * 0xC2B2AE3Du;
  return h ^ (h >> 15);
}
bool same(VtPage a, VtPage b) { return a.texture == b.texture && a.mip == b.mip && a.x == b.x && a.y == b.y; }
bool pow2(uint32_t v) { return v && !(v & (v - 1)); }
} // namespace

bool VtCache::init(Arena &arena, const VtConfig &cfg) {
  cfg_ = cfg;
  if (!cfg_.max_textures || !cfg_.pool_pages || cfg_.max_uploads_list < cfg_.uploads_per_frame) return false;
  uint32_t cap = 1;
  while (cap < cfg_.pool_pages * 2) cap <<= 1;
  tab_mask_ = cap - 1;
  tex_ = arena.alloc_array_zeroed<Tex>(cfg_.max_textures);
  slots_ = arena.alloc_array_zeroed<Slot>(cfg_.pool_pages);
  free_ = arena.alloc_array<uint32_t>(cfg_.pool_pages);
  tab_ = arena.alloc_array<int32_t>(cap);
  uploads_ = arena.alloc_array<VtUpload>(cfg_.max_uploads_list);
  if (!tex_ || !slots_ || !free_ || !tab_ || !uploads_) return false;
  for (uint32_t i = 0; i < cap; i++) tab_[i] = -1;
  free_n_ = cfg_.pool_pages;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) free_[i] = cfg_.pool_pages - 1 - i;
  return true;
}

int32_t VtCache::find(VtPage p) const {
  uint32_t i = hash_page(p) & tab_mask_;
  for (uint32_t probe = 0; probe <= tab_mask_; probe++, i = (i + 1) & tab_mask_) {
    const int32_t s = tab_[i];
    if (s == -1) return -1;
    if (s >= 0 && same(slots_[s].page, p)) return (int32_t)i;
  }
  return -1;
}

void VtCache::insert(VtPage p, uint32_t slot) {
  uint32_t i = hash_page(p) & tab_mask_;
  while (tab_[i] >= 0) i = (i + 1) & tab_mask_;
  tab_[i] = (int32_t)slot;
}

void VtCache::erase(VtPage p) {
  const int32_t i = find(p);
  if (i < 0) return;
  tab_[i] = -2;
  if (++tombstones_ > (tab_mask_ + 1) / 4) { // bos yuva tukenmesin: yerlesiklerden yeniden kur
    for (uint32_t j = 0; j <= tab_mask_; j++) tab_[j] = -1;
    for (uint32_t s = 0; s < cfg_.pool_pages; s++)
      if (slots_[s].used && !same(slots_[s].page, p)) insert(slots_[s].page, s);
    tombstones_ = 0;
  }
}

bool VtCache::valid(uint32_t t, uint32_t mip, uint32_t x, uint32_t y) const {
  if (t >= tex_n_ || mip >= tex_[t].mips) return false;
  const uint32_t w = tex_[t].w >> mip, h = tex_[t].h >> mip;
  return x < (w ? w : 1) && y < (h ? h : 1);
}

uint32_t VtCache::mip_count(uint32_t t) const { return t < tex_n_ ? tex_[t].mips : 0; }

uint32_t VtCache::take_slot() {
  if (free_n_) return free_[--free_n_];
  uint32_t best = ~0u, best_frame = ~0u;
  for (uint32_t i = 0; i < cfg_.pool_pages; i++) {
    const Slot &s = slots_[i];
    if (!s.used || s.pinned || s.last_used == frame_) continue;
    if (s.last_used < best_frame) { best_frame = s.last_used; best = i; }
  }
  if (best == ~0u) return ~0u;
  erase(slots_[best].page);
  slots_[best].used = 0;
  resident_--;
  stats_.evicted++;
  return best;
}

uint32_t VtCache::add_texture(uint32_t pw, uint32_t ph) {
  if (tex_n_ == cfg_.max_textures || !pow2(pw) || !pow2(ph) || pw > 65535 || ph > 65535) { tex_fail_++; return ~0u; }
  uint32_t mips = 1, m = pw > ph ? pw : ph;
  while (m > 1) { m >>= 1; mips++; }
  const uint32_t t = tex_n_;
  tex_[t] = Tex{pw, ph, mips};
  // En kaba mip'i (tek sayfa) yukle ve sabitle: resolve() bunun sayesinde her zaman gecerli.
  if (!free_n_) { tex_fail_++; return ~0u; }
  const uint32_t slot = free_[--free_n_];
  const VtPage top{(uint16_t)t, (uint16_t)(mips - 1), 0, 0};
  slots_[slot] = Slot{top, frame_, 1, 1};
  insert(top, slot);
  resident_++;
  pinned_++;
  tex_n_++;
  return t;
}

void VtCache::begin_frame() {
  frame_++;
  upload_n_ = 0;
  stats_ = VtStats{};
}

void VtCache::request(uint32_t t, uint32_t mip, uint32_t x, uint32_t y) {
  if (!valid(t, mip, x, y)) { stats_.invalid++; return; }
  const VtPage p{(uint16_t)t, (uint16_t)mip, (uint16_t)x, (uint16_t)y};
  const int32_t ti = find(p);
  if (ti >= 0) {
    Slot &s = slots_[tab_[ti]];
    if (s.last_used == frame_) return; // ayni karede tekrar
    s.last_used = frame_;
    stats_.requested++;
    stats_.hits++;
    return;
  }
  // Ayni karede ertelenen ayni istegi tekrar saymamak icin: ertelenen sayfa tabloda yok; her tekrar deferred++ olur.
  stats_.requested++;
  if (stats_.uploaded >= cfg_.uploads_per_frame) { stats_.deferred++; return; }
  const uint32_t slot = take_slot();
  if (slot == ~0u) { stats_.exhausted++; return; }
  slots_[slot] = Slot{p, frame_, 1, 0};
  insert(p, slot);
  resident_++;
  stats_.uploaded++;
  uploads_[upload_n_++] = VtUpload{p, slot};
}

void VtCache::end_frame() {}

bool VtCache::resident(VtPage p) const { return find(p) >= 0; }

VtResolved VtCache::resolve(uint32_t t, uint32_t mip, uint32_t x, uint32_t y) const {
  if (t >= tex_n_) return VtResolved{~0u, 0};
  const uint32_t top = tex_[t].mips - 1;
  if (mip > top) mip = top;
  for (uint32_t m = mip; m <= top; m++) {
    const uint32_t sx = x >> (m - mip), sy = y >> (m - mip);
    const int32_t i = find(VtPage{(uint16_t)t, (uint16_t)m, (uint16_t)sx, (uint16_t)sy});
    if (i >= 0) return VtResolved{(uint32_t)tab_[i], (uint16_t)m};
  }
  return VtResolved{~0u, 0}; // en kaba mip sabit oldugu icin ULASILMAZ (kapi bunu sinar)
}

} // namespace tulpar::engine::renderer
