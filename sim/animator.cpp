#include "sim/animator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tulpar::engine::sim {
namespace {

void hata(AnimatorError *e, uint32_t satir, const char *fmt, const char *a = "", const char *b = "") {
  if (!e) return;
  e->line = satir;
  std::snprintf(e->msg, sizeof e->msg, fmt, a, b);
}

// Satır -> jetonlar (boşlukla ayrılır, '#' yorum). En çok 16 jeton. A8: ardından RAKAM gelen '#' yorum değil, klip numarasıdır
// (`klip #3`); yorumlar hep '# ' / '#metin' biçiminde (depodaki ve otomatik üretilen dosyalar), yani hiçbiri anlam değiştirmez.
uint32_t jetonla(char *ln, char *tok[16]) {
  uint32_t n = 0;
  char *p = ln;
  while (*p && n < 16) {
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (!*p || (*p == '#' && !(p[1] >= '0' && p[1] <= '9'))) break;
    tok[n++] = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
    if (*p) *p++ = 0;
  }
  return n;
}
bool esit(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
bool kopya_ad(char *dst, const char *src) {
  if (std::strlen(src) >= kAnimNameLen) return false;
  std::snprintf(dst, kAnimNameLen, "%s", src);
  return true;
}
bool kopya_klip(char *dst, const char *src) {
  if (std::strlen(src) >= kAnimClipNameLen) return false;
  std::snprintf(dst, kAnimClipNameLen, "%s", src);
  return true;
}
bool sayi(const char *s, float *v) {
  char *e = nullptr;
  *v = std::strtof(s, &e);
  return e != s && *e == 0;
}
int32_t durum_ara(const AnimatorDef &d, const char *ad) {
  for (uint32_t i = 0; i < d.state_count; i++) if (esit(d.states[i].name, ad)) return (int32_t)i;
  return -1;
}
float yumusat(float x) { return x <= 0 ? 0.0f : x >= 1 ? 1.0f : x * x * (3.0f - 2.0f * x); }

// Bir durumun (faz, parametre) için klip girdileri: {klip, an, ağırlık}; toplam ağırlık 1. n girdi sayısı.
uint32_t durum_girdileri(const AnimatorDef &d, const AnimatorRt &rt, int32_t si, float phase, const float *dur, uint32_t nclip, float scale,
                         AnimatorPose *out) {
  const AnimState &s = d.states[si];
  uint32_t i0 = 0, i1 = 0;
  float w1 = 0;
  if (s.blend && s.point_count > 1) {
    const float v = s.param >= 0 ? rt.params[s.param] : 0.0f;
    if (v <= s.points[0].threshold) { i0 = i1 = 0; }
    else if (v >= s.points[s.point_count - 1].threshold) { i0 = i1 = s.point_count - 1; }
    else {
      for (uint32_t k = 0; k + 1 < s.point_count; k++)
        if (v >= s.points[k].threshold && v <= s.points[k + 1].threshold) {
          i0 = k; i1 = k + 1;
          const float span = s.points[k + 1].threshold - s.points[k].threshold;
          w1 = span > 1e-9f ? (v - s.points[k].threshold) / span : 0.0f;
          break;
        }
    }
  }
  uint32_t n = 0;
  auto ekle = [&](uint32_t pk, float w) {
    if (w <= 0.0f || out->n >= kAnimMaxEntries) return;
    const int32_t c = s.points[pk].clip;
    const float dd = (c >= 0 && (uint32_t)c < nclip) ? dur[c] : 0.0f;
    out->e[out->n++] = {c, phase * dd, w * scale};
    n++;
  };
  if (i0 == i1) ekle(i0, 1.0f);
  else { ekle(i0, 1.0f - w1); ekle(i1, w1); }
  return n;
}

// Durumun faz hızı (1/s): iki komşu noktanın (süre/hız) değerleri ağırlıkla karışır.
float durum_faz_hizi(const AnimatorDef &d, const AnimatorRt &rt, int32_t si, const float *dur, uint32_t nclip) {
  const AnimState &s = d.states[si];
  AnimatorPose tmp;
  tmp.n = 0;
  float acc = 0.0f, wsum = 0.0f;
  durum_girdileri(d, rt, si, 0.0f, dur, nclip, 1.0f, &tmp);
  for (uint32_t i = 0; i < tmp.n; i++) {
    const int32_t c = tmp.e[i].clip;
    float hiz = 1.0f;
    for (uint32_t k = 0; k < s.point_count; k++) if (s.points[k].clip == c) { hiz = s.points[k].speed; break; }
    const float dd = (c >= 0 && (uint32_t)c < nclip) ? dur[c] : 0.0f;
    if (dd <= 0.0f) continue;
    acc += tmp.e[i].weight * (hiz / dd);
    wsum += tmp.e[i].weight;
  }
  return wsum > 0.0f ? acc / wsum : 0.0f;
}

bool kosul_saglandi(const AnimTransition &t, const AnimatorRt &rt) {
  if (t.trigger) return t.param >= 0 && rt.trig[t.param];
  if (t.on_end) return rt.ended;
  if (t.param < 0 || t.op == AnimOp::None) return false;
  const float v = rt.params[t.param];
  switch (t.op) {
    case AnimOp::Gt: return v > t.value;
    case AnimOp::Lt: return v < t.value;
    case AnimOp::Ge: return v >= t.value;
    case AnimOp::Le: return v <= t.value;
    case AnimOp::Eq: return v == t.value;
    default: return false;
  }
}
} // namespace

int32_t animator_param_index(const AnimatorDef &d, const char *name) {
  for (uint32_t i = 0; i < d.param_count; i++) if (esit(d.params[i].name, name)) return (int32_t)i;
  return -1;
}

bool animator_parse(const char *text, size_t len, AnimatorDef *out, AnimatorError *err) {
  *out = AnimatorDef{};
  if (err) *err = AnimatorError{};
  char *buf = static_cast<char *>(std::malloc(len + 1));
  if (!buf) { hata(err, 0, "bellek"); return false; }
  std::memcpy(buf, text, len);
  buf[len] = 0;
  char from_n[kAnimMaxTrans][kAnimNameLen], to_n[kAnimMaxTrans][kAnimNameLen];
  char param_n[kAnimMaxTrans][kAnimNameLen];   // blend parametre adlari durum basina (cozum sonda)
  char blend_param[kAnimMaxStates][kAnimNameLen] = {};
  char start_n[kAnimNameLen] = {0};
  char ev_state_n[kAnimMaxEvents][kAnimNameLen] = {};
  bool header = false, ok = true;
  int32_t in_blend = -1;
  uint32_t satir = 0;
  char *cur = buf;
  while (ok && cur && *cur) {
    char *ln = cur;
    char *e = cur;
    while (*e && *e != '\n') e++;
    if (*e) { *e = 0; cur = e + 1; } else cur = e;
    satir++;
    char *tok[16];
    const uint32_t n = jetonla(ln, tok);
    if (!n) continue;
    if (!header) {
      if (!(n == 2 && esit(tok[0], "tulpar-animator") && esit(tok[1], "1"))) { hata(err, satir, "ilk satir 'tulpar-animator 1' olmali"); ok = false; }
      header = true;
      continue;
    }
    if (in_blend >= 0) {
      AnimState &s = out->states[in_blend];
      if (esit(tok[0], "son")) {
        if (s.point_count < 2) { hata(err, satir, "karisim durumu en az 2 'nokta' ister"); ok = false; }
        in_blend = -1;
      } else if (esit(tok[0], "nokta") && n >= 3) {
        if (s.point_count >= kAnimMaxPoints) { hata(err, satir, "karisim durumunda en cok 4 nokta"); ok = false; break; }
        AnimPoint &pt = s.points[s.point_count];
        if (!sayi(tok[1], &pt.threshold) || !kopya_klip(pt.clip_name, tok[2])) { hata(err, satir, "nokta <esik> <klip> [hiz h]"); ok = false; break; }
        if (s.point_count && pt.threshold <= s.points[s.point_count - 1].threshold) { hata(err, satir, "nokta esikleri artan olmali"); ok = false; break; }
        for (uint32_t k = 3; k + 1 < n; k += 2) if (esit(tok[k], "hiz") && !sayi(tok[k + 1], &pt.speed)) { hata(err, satir, "gecersiz hiz"); ok = false; }
        s.point_count++;
      } else { hata(err, satir, "karisim blogunda yalniz 'nokta' ve 'son'"); ok = false; }
      continue;
    }
    if (esit(tok[0], "param") && n >= 3) {
      if (out->param_count >= kAnimMaxParams) { hata(err, satir, "en cok 16 parametre"); ok = false; break; }
      AnimParam &p = out->params[out->param_count];
      if (!kopya_ad(p.name, tok[1])) { hata(err, satir, "parametre adi en cok 23 karakter"); ok = false; break; }
      if (esit(tok[2], "tetik")) {
        p.trigger = true;
        if (n >= 5 && esit(tok[3], "tus")) {   // A8c: kodsuz tus
          if (std::strlen(tok[4]) >= kAnimKeyLen) { hata(err, satir, "tus adi en cok 11 karakter"); ok = false; break; }
          std::snprintf(p.key, sizeof p.key, "%s", tok[4]);
        } else if (n >= 4) { hata(err, satir, "param <ad> tetik [tus <TUS>]"); ok = false; break; }
      }
      else if (esit(tok[2], "sayi")) { if (n >= 4 && !sayi(tok[3], &p.def)) { hata(err, satir, "gecersiz varsayilan"); ok = false; break; } }
      else { hata(err, satir, "param <ad> sayi|tetik"); ok = false; break; }
      out->param_count++;
    } else if (esit(tok[0], "durum") && n >= 4) {
      if (out->state_count >= kAnimMaxStates) { hata(err, satir, "en cok 16 durum"); ok = false; break; }
      AnimState &s = out->states[out->state_count];
      if (!kopya_ad(s.name, tok[1])) { hata(err, satir, "durum adi en cok 23 karakter"); ok = false; break; }
      if (durum_ara(*out, tok[1]) >= 0) { hata(err, satir, "durum '%s' iki kez tanimli", tok[1]); ok = false; break; }
      if (esit(tok[2], "klip")) {
        s.point_count = 1;
        if (!kopya_klip(s.points[0].clip_name, tok[3])) { hata(err, satir, "klip adi en cok 63 karakter"); ok = false; break; }
        for (uint32_t k = 4; k < n; k++) {
          if (esit(tok[k], "dongu")) s.loop = true;
          else if (esit(tok[k], "tek")) s.loop = false;
          else if (esit(tok[k], "kok")) s.root_motion = true;
          else if (esit(tok[k], "hiz") && k + 1 < n && sayi(tok[k + 1], &s.points[0].speed)) k++;
          else { hata(err, satir, "gecersiz anahtar '%s'", tok[k]); ok = false; break; }
        }
      } else if (esit(tok[2], "karisim")) {
        s.blend = true;
        if (!kopya_ad(blend_param[out->state_count], tok[3])) { hata(err, satir, "parametre adi en cok 23 karakter"); ok = false; break; }
        in_blend = (int32_t)out->state_count;
      } else { hata(err, satir, "durum <ad> klip <klip> | karisim <param>"); ok = false; break; }
      out->state_count++;
    } else if (esit(tok[0], "gecis") && n >= 3) {
      if (out->trans_count >= kAnimMaxTrans) { hata(err, satir, "en cok 48 gecis"); ok = false; break; }
      const uint32_t ti = out->trans_count;
      AnimTransition &t = out->trans[ti];
      if (!kopya_ad(from_n[ti], tok[1]) || !kopya_ad(to_n[ti], tok[2])) { hata(err, satir, "durum adi en cok 23 karakter"); ok = false; break; }
      param_n[ti][0] = 0;
      bool kosul = false;
      for (uint32_t k = 3; k < n && ok; k++) {
        if (esit(tok[k], "sure") && k + 1 < n && sayi(tok[k + 1], &t.fade) && t.fade >= 0.0f) k++;
        else if (esit(tok[k], "bitince")) { t.on_end = true; kosul = true; }
        else if (esit(tok[k], "tetik") && k + 1 < n) { t.trigger = true; if (!kopya_ad(param_n[ti], tok[++k])) ok = false; kosul = true; }
        else if (esit(tok[k], "eger") && k + 3 < n) {
          if (!kopya_ad(param_n[ti], tok[k + 1])) ok = false;
          const char *o = tok[k + 2];
          t.op = esit(o, ">") ? AnimOp::Gt : esit(o, "<") ? AnimOp::Lt : esit(o, ">=") ? AnimOp::Ge : esit(o, "<=") ? AnimOp::Le : esit(o, "==") ? AnimOp::Eq : AnimOp::None;
          if (t.op == AnimOp::None || !sayi(tok[k + 3], &t.value)) { hata(err, satir, "eger <param> > < >= <= == <deger>"); ok = false; }
          kosul = true;
          k += 3;
        } else { hata(err, satir, "gecersiz gecis anahtari '%s'", tok[k]); ok = false; }
      }
      if (ok && !kosul) { hata(err, satir, "gecis kosulsuz olamaz: eger / tetik / bitince"); ok = false; }
      out->trans_count++;
    } else if (esit(tok[0], "olay") && n == 4) {
      if (out->event_count >= kAnimMaxEvents) { hata(err, satir, "en cok 16 olay"); ok = false; break; }
      AnimEvent &ev = out->events[out->event_count];
      if (!kopya_ad(ev_state_n[out->event_count], tok[1]) || !sayi(tok[2], &ev.phase) || ev.phase < 0.0f || ev.phase > 1.0f || !kopya_ad(ev.name, tok[3])) {
        hata(err, satir, "olay <durum> <faz 0..1> <ad>"); ok = false; break;
      }
      out->event_count++;
    } else if (esit(tok[0], "baslangic") && n == 2) {
      if (!kopya_ad(start_n, tok[1])) { hata(err, satir, "durum adi en cok 23 karakter"); ok = false; }
    } else {
      hata(err, satir, "taninmayan satir '%s'", tok[0]);
      ok = false;
    }
  }
  std::free(buf);
  if (ok && in_blend >= 0) { hata(err, satir, "karisim blogu 'son' ile kapanmadi"); ok = false; }
  if (ok && !header) { hata(err, 0, "bos dosya"); ok = false; }
  if (ok && out->state_count == 0) { hata(err, 0, "en az bir durum gerekli"); ok = false; }
  // Adlari coz
  for (uint32_t i = 0; ok && i < out->state_count; i++)
    if (out->states[i].blend) {
      out->states[i].param = animator_param_index(*out, blend_param[i]);
      if (out->states[i].param < 0) { hata(err, 0, "karisim parametresi '%s' tanimli degil", blend_param[i]); ok = false; }
    }
  for (uint32_t i = 0; ok && i < out->trans_count; i++) {
    AnimTransition &t = out->trans[i];
    t.from = esit(from_n[i], "herhangi") ? -1 : durum_ara(*out, from_n[i]);
    t.to = durum_ara(*out, to_n[i]);
    if ((t.from < 0 && !esit(from_n[i], "herhangi")) || t.to < 0) { hata(err, 0, "gecis: bilinmeyen durum '%s'", t.to < 0 ? to_n[i] : from_n[i]); ok = false; break; }
    if (param_n[i][0]) {
      t.param = animator_param_index(*out, param_n[i]);
      if (t.param < 0) { hata(err, 0, "gecis: parametre '%s' tanimli degil", param_n[i]); ok = false; break; }
      if (t.trigger && !out->params[t.param].trigger) { hata(err, 0, "'%s' bir tetik degil (param ... tetik)", param_n[i]); ok = false; break; }
      if (!t.trigger && out->params[t.param].trigger) { hata(err, 0, "'%s' tetik: 'tetik %s' yazin", param_n[i], param_n[i]); ok = false; break; }
    }
  }
  for (uint32_t i = 0; ok && i < out->event_count; i++) {
    out->events[i].state = durum_ara(*out, ev_state_n[i]);
    if (out->events[i].state < 0) { hata(err, 0, "olay: bilinmeyen durum '%s'", ev_state_n[i]); ok = false; }
  }
  if (ok) {
    out->start = start_n[0] ? durum_ara(*out, start_n) : 0;
    if (out->start < 0) { hata(err, 0, "baslangic durumu '%s' yok", start_n); ok = false; }
  }
  return ok;
}

bool animator_bind(AnimatorDef *def, AnimClipNameFn names, void *user, uint32_t clip_count, AnimatorError *err) {
  for (uint32_t i = 0; i < def->state_count; i++) {
    AnimState &s = def->states[i];
    for (uint32_t k = 0; k < s.point_count; k++) {
      s.points[k].clip = -1;
      const char *cn = s.points[k].clip_name;
      if (cn[0] == '#') {   // A8: sıra numarasıyla klip (`#3`) — adı jetona sığmayan klipler (boşluklu, 23 karakterden uzun)
        uint32_t v = 0, nd = 0;
        for (const char *q = cn + 1; *q >= '0' && *q <= '9' && nd < 6; q++, nd++) v = v * 10 + (uint32_t)(*q - '0');
        if (nd == 0 || cn[1 + nd] != 0) { hata(err, 0, "durum '%s': '%s' gecersiz klip numarasi (#0, #1 ...)", s.name, cn); return false; }
        if (v >= clip_count) {
          if (err) {
            err->line = 0;
            std::snprintf(err->msg, sizeof err->msg, "durum '%s': '%s' klibi yok — modelde %u klip var (#0'dan baslar)", s.name, cn, clip_count);
          }
          return false;
        }
        s.points[k].clip = (int32_t)v;
        continue;
      }
      for (uint32_t c = 0; c < clip_count; c++) {
        const char *nm = names(user, c);
        if (nm && esit(nm, cn)) { s.points[k].clip = (int32_t)c; break; }
      }
      if (s.points[k].clip < 0) { hata(err, 0, "durum '%s': modelde '%s' adli klip yok", s.name, cn); return false; }
    }
  }
  return true;
}

void animator_reset(const AnimatorDef &d, AnimatorRt *rt) {
  *rt = AnimatorRt{};
  for (uint32_t i = 0; i < d.param_count; i++) rt->params[i] = d.params[i].def;
  rt->cur = d.start;
}
void animator_set(const AnimatorDef &d, AnimatorRt *rt, int32_t param, float v) {
  if (param >= 0 && (uint32_t)param < d.param_count && !d.params[param].trigger) rt->params[param] = v;
}
void animator_trigger(const AnimatorDef &d, AnimatorRt *rt, int32_t param) {
  if (param >= 0 && (uint32_t)param < d.param_count && d.params[param].trigger) rt->trig[param] = true;
}

void animator_update(const AnimatorDef &d, AnimatorRt *rt, float dt, const float *dur, uint32_t nclip) {
  if (d.state_count == 0 || rt->cur < 0 || (uint32_t)rt->cur >= d.state_count) return;
  // 1) fazlar
  const AnimState &cs = d.states[rt->cur];
  const float phase_old = rt->cur_phase;
  rt->cur_phase += dt * durum_faz_hizi(d, *rt, rt->cur, dur, nclip);
  bool sardi = false;
  if (cs.loop) { while (rt->cur_phase >= 1.0f) { rt->cur_phase -= 1.0f; sardi = true; } while (rt->cur_phase < 0.0f) rt->cur_phase += 1.0f; }
  else if (rt->cur_phase >= 1.0f) { rt->cur_phase = 1.0f; rt->ended = true; }
  // A4 olaylari: (phase_old, phase_new] icinde kalan olaylar; dongude turu asarsa (phase_old,1] ve [0,phase_new]. Sira tanim sirasi (belirlenimli).
  for (uint32_t i = 0; i < d.event_count; i++) {
    const AnimEvent &ev = d.events[i];
    if (ev.state != rt->cur) continue;
    const bool vur = sardi ? (ev.phase > phase_old || ev.phase <= rt->cur_phase) : (ev.phase > phase_old && ev.phase <= rt->cur_phase);
    if (!vur) continue;
    rt->events_fired++;
    if (rt->ev_count >= kAnimEventQueue) { rt->events_dropped++; continue; }
    rt->evq[(rt->ev_head + rt->ev_count) % kAnimEventQueue] = (int32_t)i;
    rt->ev_count++;
  }
  if (rt->prev >= 0) {
    const AnimState &ps = d.states[rt->prev];
    rt->prev_phase += dt * durum_faz_hizi(d, *rt, rt->prev, dur, nclip);
    if (ps.loop) { while (rt->prev_phase >= 1.0f) rt->prev_phase -= 1.0f; while (rt->prev_phase < 0.0f) rt->prev_phase += 1.0f; }
    else if (rt->prev_phase > 1.0f) rt->prev_phase = 1.0f;
    rt->fade += dt;
    if (rt->fade >= rt->fade_dur) rt->prev = -1;
  }
  // 2) gecis kosullari (gecis surerken yeni gecis baslamaz: temiz, sicramasiz)
  if (rt->prev >= 0) return;
  for (uint32_t i = 0; i < d.trans_count; i++) {
    const AnimTransition &t = d.trans[i];
    if (t.from >= 0 && t.from != rt->cur) continue;
    if (t.to == rt->cur) continue;
    if (!kosul_saglandi(t, *rt)) continue;
    if (t.trigger) rt->trig[t.param] = false;                  // tetik tuketilir
    rt->prev = t.fade > 0.0f ? rt->cur : -1;
    rt->prev_phase = rt->cur_phase;
    rt->cur = t.to;
    rt->cur_phase = 0.0f;
    rt->fade = 0.0f;
    rt->fade_dur = t.fade;
    rt->ended = false;
    rt->transitions_taken++;
    break;
  }
}

void animator_goto(const AnimatorDef &d, AnimatorRt *rt, int32_t state, float fade) {
  if (!rt || state < 0 || (uint32_t)state >= d.state_count) return;
  const bool yumusak = fade > 0.0f && rt->cur != state;
  rt->prev = yumusak ? rt->cur : -1;
  rt->prev_phase = rt->cur_phase;
  rt->cur = state;
  rt->cur_phase = 0.0f;
  rt->fade = 0.0f;
  rt->fade_dur = yumusak ? fade : 0.0f;
  rt->ended = false;
}

int32_t animator_event_pop(AnimatorRt *rt) {
  if (rt->ev_count == 0) return -1;
  const int32_t i = rt->evq[rt->ev_head];
  rt->ev_head = (rt->ev_head + 1) % kAnimEventQueue;
  rt->ev_count--;
  return i;
}

void animator_pose(const AnimatorDef &d, const AnimatorRt &rt, const float *dur, uint32_t nclip, AnimatorPose *out) {
  out->n = 0;
  if (d.state_count == 0 || rt.cur < 0 || (uint32_t)rt.cur >= d.state_count) return;
  if (rt.prev >= 0 && rt.fade_dur > 0.0f) {
    const float w = yumusat(rt.fade / rt.fade_dur);          // smoothstep: baslangic/bitis hizi 0
    durum_girdileri(d, rt, rt.prev, rt.prev_phase, dur, nclip, 1.0f - w, out);
    durum_girdileri(d, rt, rt.cur, rt.cur_phase, dur, nclip, w, out);
  } else {
    durum_girdileri(d, rt, rt.cur, rt.cur_phase, dur, nclip, 1.0f, out);
  }
}

// --- A8: klip adlarından otomatik animatör -------------------------------------------------------------------------------------
namespace {
// Küçük harfe çevrilmiş ad içinde alt dize (ASCII; Türkçe harfler UTF-8 baytlarıyla aynen aranır).
bool ad_icerir(const char *ad, const char *alt) {
  char kucuk[128];
  uint32_t i = 0;
  for (; ad[i] && i + 1 < sizeof kucuk; i++) kucuk[i] = (ad[i] >= 'A' && ad[i] <= 'Z') ? (char)(ad[i] - 'A' + 'a') : ad[i];
  kucuk[i] = 0;
  return std::strstr(kucuk, alt) != nullptr;
}
bool ad_herhangi(const char *ad, const char *const *altlar) {
  for (uint32_t i = 0; altlar[i]; i++) if (ad_icerir(ad, altlar[i])) return true;
  return false;
}
// Ad .animator jetonu olarak yazılabilir mi (boşluksuz, '#' ile başlamaz, < kAnimClipNameLen).
bool jeton_olur(const char *ad) {
  if (!ad || !ad[0] || ad[0] == '#' || std::strlen(ad) >= kAnimClipNameLen) return false;
  for (const char *p = ad; *p; p++) if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') return false;
  return true;
}
struct Yazici {
  char *out; uint32_t cap, n = 0; bool tasti = false;
  void yaz(const char *fmt, const char *a = "", const char *b = "") {
    if (tasti) return;
    const int k = std::snprintf(out + n, cap - n, fmt, a, b);
    if (k < 0 || (uint32_t)k >= cap - n) { tasti = true; return; }
    n += (uint32_t)k;
  }
};
} // namespace

uint32_t animator_auto_text(AnimClipNameFn names, void *user, uint32_t clip_count, char *out, uint32_t cap, AnimAutoReport *rep) {
  AnimAutoReport r;
  if (rep) *rep = r;
  if (!out || cap < 64 || clip_count == 0) { if (out && cap) out[0] = 0; return 0; }
  static const char *const kZipla[] = {"jump", "zipla", "z\xC4\xB1pla", "leap", nullptr};
  static const char *const kKos[] = {"run", "kos", "ko\xC5\x9F", "sprint", "jog", nullptr};
  static const char *const kYuru[] = {"walk", "yuru", "y\xC3\xBCr\xC3\xBC", nullptr};
  static const char *const kBekle[] = {"idle", "bekle", "stand", "breath", "rest", nullptr};
  // Her klip EN ÇOK bir role girer; öncelik zıpla > koş > yürü > bekle ("RunJump" zıplamadır). Rol başına EN KISA adlı klip:
  // "Idle" > "IdleAggro", "Walk" > "Walk_Back" / "WalkLeft", "Jump" > "JumpStart" (Fab "Mike": ilk eşleşen kural saldırgan
  // beklemeyi seçmişti, 2026-11-16). Eşit uzunlukta ilk klip.
  size_t boy[4] = {(size_t)-1, (size_t)-1, (size_t)-1, (size_t)-1};
  int32_t *rol[4] = {&r.jump, &r.run, &r.walk, &r.idle};
  for (uint32_t c = 0; c < clip_count; c++) {
    const char *ad = names(user, c);
    if (!ad || !ad[0]) continue;
    const int k = ad_herhangi(ad, kZipla) ? 0 : ad_herhangi(ad, kKos) ? 1 : ad_herhangi(ad, kYuru) ? 2 : ad_herhangi(ad, kBekle) ? 3 : -1;
    if (k < 0) continue;
    const size_t l = std::strlen(ad);
    if (l < boy[k]) { boy[k] = l; *rol[k] = (int32_t)c; }
  }
  if (r.idle < 0) {   // bekleme klibi yok: rolsüz ilk klip, o da yoksa klip 0
    for (uint32_t c = 0; c < clip_count && r.idle < 0; c++)
      if ((int32_t)c != r.walk && (int32_t)c != r.run && (int32_t)c != r.jump) r.idle = (int32_t)c;
    if (r.idle < 0) r.idle = 0;
  }
  // Klip jetonu: ad sığıyorsa ad, sığmıyorsa #N.
  char jet[4][kAnimClipNameLen + 8];
  const int32_t roller[4] = {r.idle, r.walk, r.run, r.jump};
  for (uint32_t k = 0; k < 4; k++) {
    jet[k][0] = 0;
    if (roller[k] < 0) continue;
    const char *ad = names(user, (uint32_t)roller[k]);
    if (jeton_olur(ad)) std::snprintf(jet[k], sizeof jet[k], "%s", ad);
    else std::snprintf(jet[k], sizeof jet[k], "#%d", roller[k]);
  }
  Yazici w{out, cap};
  w.yaz("tulpar-animator 1\n");
  w.yaz("# Otomatik kuruldu (modelin klip adlarindan). Istedigin gibi degistir: kaydedince sahnede hemen guncellenir.\n");
  w.yaz("# 'hiz' oyunda nesnenin yatay hizindan (m/s), 'dikey' dikey hizindan KENDILIGINDEN gelir; kod gerekmez.\n");
  w.yaz("param hiz sayi 0\n");
  if (r.jump >= 0) w.yaz("param dikey sayi 0\n");
  const uint32_t nokta = 1 + (r.walk >= 0) + (r.run >= 0);
  if (nokta >= 2) {
    w.yaz("durum hareket karisim hiz\n");
    w.yaz("  nokta 0 %s\n", jet[0]);
    if (r.walk >= 0) w.yaz("  nokta 1.5 %s\n", jet[1]);
    if (r.run >= 0) w.yaz(r.walk >= 0 ? "  nokta 4.5 %s\n" : "  nokta 3 %s\n", jet[2]);
    w.yaz("son\n");
  } else {
    w.yaz("durum hareket klip %s dongu\n", jet[0]);
  }
  r.states = 1;
  if (r.jump >= 0) {
    w.yaz("durum zipla klip %s tek\n", jet[3]);
    w.yaz("gecis hareket zipla sure 0.1 eger dikey > 1.5\n");
    w.yaz("gecis zipla hareket sure 0.2 bitince\n");
    r.states++;
  }
  w.yaz("baslangic hareket\n");
  if (w.tasti) { out[0] = 0; return 0; }
  if (rep) *rep = r;
  return w.n;
}

} // namespace tulpar::engine::sim
