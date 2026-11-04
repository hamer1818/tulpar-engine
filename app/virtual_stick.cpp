#include "app/virtual_stick.hpp"

#include <cmath>

namespace tulpar::engine::app {

void VirtualStick::update(const platform::TouchState &t, uint64_t now_ns) {
  look_delta = {0, 0};
  action = false;
  action_down = false;
  // Haric tutulan parmaklardan kalkanlari unut.
  for (int32_t &ig : ignored_) {
    if (ig < 0) continue;
    bool down = false;
    for (uint32_t i = 0; i < platform::TouchState::kMax; i++) if (t.pts[i].down && t.pts[i].id == ig) down = true;
    if (!down) ig = -1;
  }
  // Mevcut parmaklar hala basili mi?
  const platform::TouchPoint *mp = nullptr, *lp = nullptr;
  for (uint32_t i = 0; i < platform::TouchState::kMax; i++) {
    const platform::TouchPoint &p = t.pts[i];
    if (!p.down) continue;
    if (p.id == move_id_) mp = &p;
    if (p.id == look_id_) lp = &p;
  }
  if (move_id_ >= 0 && !mp) { move_id_ = -1; move_active = false; move = {0, 0}; }
  if (look_id_ >= 0 && !lp) {
    // Kalkti: kisa ve az hareketliyse eylem
    float held_ms = (float)((now_ns - look_down_ns_) / 1000000ull);
    if (!hold_action && held_ms <= tap_max_ms && look_moved_ <= tap_max_move_px) action = true;
    look_id_ = -1;
    look_active = false;
  }
  // Yeni parmaklar
  for (uint32_t i = 0; i < platform::TouchState::kMax; i++) {
    const platform::TouchPoint &p = t.pts[i];
    if (!p.down || p.id == move_id_ || p.id == look_id_) continue;
    bool yok = false;
    for (int32_t ig : ignored_) if (ig == p.id) yok = true;
    if (yok) continue;
    bool haric = false;
    for (const Rect &r : exclude)
      if (r.w > 0 && r.h > 0 && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h) haric = true;
    if (haric) {
      for (int32_t &ig : ignored_) if (ig < 0) { ig = p.id; break; }
      excluded_touches++;
      continue;
    }
    if (p.x < t.width * 0.5f) {
      if (move_id_ < 0) { move_id_ = p.id; stick_origin = {p.x, p.y}; move_active = true; mp = &p; }
    } else if (look_id_ < 0) {
      look_id_ = p.id; look_last_ = look_start_ = {p.x, p.y}; look_down_ns_ = now_ns; look_moved_ = 0; look_active = true; lp = &p;
      action_down = true;
    }
  }
  if (mp) {
    float dx = (mp->x - stick_origin.x) / stick_radius_px, dy = -(mp->y - stick_origin.y) / stick_radius_px;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.0f) { dx /= len; dy /= len; }
    move = {dx, dy};
  }
  if (hold_action && lp) action = true; // basili tut: parmak sag yarida durdukca her kare
  if (lp) {
    look_delta = {lp->x - look_last_.x, lp->y - look_last_.y};
    look_moved_ += std::fabs(look_delta.x) + std::fabs(look_delta.y);
    look_last_ = {lp->x, lp->y};
  }
}

} // namespace tulpar::engine::app
