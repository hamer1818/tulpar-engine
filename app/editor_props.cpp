#include "app/editor_props.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <IconsMaterialDesign.h> // ikon makrolari (IconFontCppHeaders, Zlib)
#include "platform/fs.hpp"

namespace tulpar::engine::app {

using content::SceneEntity;
using content::SceneProp;

// =============================================================================
// Tarayici
// =============================================================================
namespace {

enum TokKind : uint8_t { TEnd, TIdent, TNum, TStr, TTStr, TPunct };
struct Tok {
  TokKind k = TEnd;
  const char *s = nullptr; // TStr: tirnaklarin ICI; digerleri: belirtecin kendisi
  uint32_t n = 0;
  uint32_t line = 0;
  bool bad = false; // TNum: Tulpar'in reddedecegi sayi (sonek, basamaksiz 0x...)
};

bool ident_start(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c > 127; }
bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool ident_char(unsigned char c) { return ident_start(c) || is_digit(c); }
int digit_in_base(unsigned char c, int base) {
  int v;
  if (c >= '0' && c <= '9') v = c - '0';
  else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
  else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
  else return -1;
  return v < base ? v : -1;
}

// Tulpar sozcukleyicisinin (TulparLang src/lexer/lexer.cpp) BU TARAMA icin
// gereken kismi: bosluk, iki yorum bicimi, dize, sablon dize, sayi, tanimlayici.
// Cok karakterli isleçler tek tek noktalama olarak cikar — tarayici yalniz
// ( ) [ ] { } , . - + : karakterlerine bakiyor.
struct Lex {
  const char *p = nullptr, *end = nullptr;
  uint32_t line = 1;

  bool at(const char *q, char c) const { return q < end && *q == c; }
  // Dizenin govdesini atla (acilis tirnagindan SONRA cagrilir). Kapanmayan
  // dize dosya sonuna kadar gider.
  void skip_string_body() {
    while (p < end && *p != '"') {
      if (*p == '\\') {
        p++;
        if (p < end) { if (*p == '\n') line++; p++; }
        continue;
      }
      if (*p == '\n') line++;
      p++;
    }
  }
  Tok next() {
    for (;;) {
      if (p >= end) return Tok{TEnd, end, 0, line, false};
      const unsigned char c = (unsigned char)*p;
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
        if (c == '\n') line++;
        p++;
        continue;
      }
      if (c == '/' && at(p + 1, '/')) {
        p += 2;
        while (p < end && *p != '\n') p++;
        continue;
      }
      if (c == '/' && at(p + 1, '*')) { // IC ICE DEGIL (Tulpar'daki gibi)
        p += 2;
        while (p < end && !(*p == '*' && at(p + 1, '/'))) {
          if (*p == '\n') line++;
          p++;
        }
        if (p < end) p += 2;
        continue;
      }
      Tok t;
      t.s = p;
      t.line = line;
      if (is_digit(c)) {
        t.k = TNum;
        lex_number(t);
        t.n = (uint32_t)(p - t.s);
        return t;
      }
      if (c == 't' && at(p + 1, '"')) { // sablon dize: t"...{ifade}..."
        p += 2;
        int depth = 0;
        while (p < end) {
          const char d = *p;
          if (depth == 0) {
            if (d == '"') { p++; break; }
            if (d == '\\') {
              p++;
              if (p < end) { if (*p == '\n') line++; p++; }
              continue;
            }
            if (d == '{') depth = 1;
          } else if (d == '{') {
            depth++;
          } else if (d == '}') {
            depth--;
          } else if (d == '"') { // {ifade} icindeki dize
            p++;
            skip_string_body();
            if (p < end) p++;
            continue;
          }
          if (d == '\n') line++;
          p++;
        }
        t.k = TTStr;
        t.n = (uint32_t)(p - t.s);
        return t;
      }
      if (c == '"') {
        p++;
        t.s = p;
        skip_string_body();
        t.k = TStr;
        t.n = (uint32_t)(p - t.s);
        if (p < end) p++;
        else t.bad = true; // kapanmadi
        return t;
      }
      if (ident_start(c)) {
        while (p < end && ident_char((unsigned char)*p)) p++;
        t.k = TIdent;
        t.n = (uint32_t)(p - t.s);
        return t;
      }
      p++;
      t.k = TPunct;
      t.n = 1;
      return t;
    }
  }
  // Tulpar read_number'in aynisi: 0x/0b onekli tamsayi ya da onluk (`_`
  // yalniz iki basamak arasinda, tek `.` ve `..` araligi degil, `e±us`
  // yalniz ardinda basamak varsa). Sayiya bitisik harf Tulpar'da HATA:
  // belirtec `bad` isaretlenir ve harfler de yutulur.
  void lex_number(Tok &t) {
    if (*p == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X' || p[1] == 'b' || p[1] == 'B')) {
      const int base = (p[1] == 'x' || p[1] == 'X') ? 16 : 2;
      p += 2;
      int digits = 0;
      while (p < end) {
        if (*p == '_') {
          if (digits == 0 || !(p + 1 < end && digit_in_base((unsigned char)p[1], base) >= 0)) break;
          p++;
          continue;
        }
        if (digit_in_base((unsigned char)*p, base) < 0) break;
        digits++;
        p++;
      }
      if (digits == 0) t.bad = true;
    } else {
      bool dot = false;
      while (p < end) {
        const unsigned char ch = (unsigned char)*p;
        if (is_digit(ch)) { p++; continue; }
        if (ch == '_') {
          if (!is_digit((unsigned char)p[-1]) || !(p + 1 < end && is_digit((unsigned char)p[1]))) break;
          p++;
          continue;
        }
        if (ch == '.') {
          if (dot || at(p + 1, '.')) break;
          dot = true;
          p++;
          continue;
        }
        break;
      }
      if (p < end && (*p == 'e' || *p == 'E')) {
        const bool ok = (p + 1 < end && is_digit((unsigned char)p[1])) ||
                        (p + 2 < end && (p[1] == '+' || p[1] == '-') && is_digit((unsigned char)p[2]));
        if (ok) {
          p += 2;
          while (p < end && is_digit((unsigned char)*p)) p++;
        }
      }
    }
    if (p < end && ident_start((unsigned char)*p)) {
      t.bad = true;
      while (p < end && ident_char((unsigned char)*p)) p++;
    }
  }
};

bool tok_is(const Tok &t, TokKind k, const char *s) {
  const size_t n = std::strlen(s);
  return t.k == k && t.n == n && std::memcmp(t.s, s, n) == 0;
}
bool punct(const Tok &t, char c) { return t.k == TPunct && t.s[0] == c; }

struct CallName {
  const char *name;
  uint32_t type;
};
constexpr CallName kCalls[] = {
    {"ozellik_sayi", content::kScenePropSayi}, {"ozellik_tam", content::kScenePropTam},
    {"ozellik_bayrak", content::kScenePropBayrak}, {"ozellik_nokta", content::kScenePropNokta},
    {"prop_num", content::kScenePropSayi},     {"prop_int", content::kScenePropTam},
    {"prop_flag", content::kScenePropBayrak},  {"prop_point", content::kScenePropNokta},
};
uint32_t call_type(const Tok &t) {
  for (const CallName &c : kCalls)
    if (tok_is(t, TIdent, c.name)) return c.type;
  return 0;
}
// Tulpar'in fonksiyon anahtar sozcukleri (lexer.cpp KEYWORD_MAP, TOKEN_FUNC).
bool is_func_keyword(const Tok &t) {
  return tok_is(t, TIdent, "func") || tok_is(t, TIdent, "fonksiyon") || tok_is(t, TIdent, "fonk") ||
         tok_is(t, TIdent, "islev") || tok_is(t, TIdent, "i\xC5\x9Flev"); // işlev
}
// Bool literalleri (lexer.cpp: true/false, dogru/yanlis, doğru/yanlış).
int bool_literal(const Tok &t) {
  if (tok_is(t, TIdent, "true") || tok_is(t, TIdent, "dogru") || tok_is(t, TIdent, "do\xC4\x9Fru")) return 1;
  if (tok_is(t, TIdent, "false") || tok_is(t, TIdent, "yanlis") || tok_is(t, TIdent, "yanl\xC4\xB1\xC5\x9F")) return 0;
  return -1;
}

// TNum belirtecinin degeri. `_` ayiraclari atilir; onluk strtod ile (sahne
// ayristiricisi da strtof kullaniyor — ayni "C yerel ayari" varsayimi).
bool num_value(const Tok &t, double *out) {
  if (t.k != TNum || t.bad || t.n == 0 || t.n >= 64) return false;
  char buf[64];
  uint32_t m = 0;
  for (uint32_t i = 0; i < t.n; i++)
    if (t.s[i] != '_') buf[m++] = t.s[i];
  buf[m] = 0;
  if (m >= 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X' || buf[1] == 'b' || buf[1] == 'B')) {
    const int base = (buf[1] == 'x' || buf[1] == 'X') ? 16 : 2;
    unsigned long long acc = 0;
    for (uint32_t i = 2; i < m; i++) {
      const int d = digit_in_base((unsigned char)buf[i], base);
      if (d < 0) return false;
      if (acc > (~0ull - (unsigned long long)d) / (unsigned long long)base) return false; // 64 bit tasmasi
      acc = acc * (unsigned long long)base + (unsigned long long)d;
    }
    *out = (double)(long long)acc; // Tulpar: 64 bitin TAMAMI, isaretli (0xFF..FF -> -1)
    return true;
  }
  char *e = nullptr;
  const double v = std::strtod(buf, &e);
  if (e != buf + m) return false;
  *out = v;
  return true;
}
// [-|+] SAYI
bool signed_num(Lex &lx, double *out) {
  Tok t = lx.next();
  double sign = 1.0;
  if (punct(t, '-')) { sign = -1.0; t = lx.next(); }
  else if (punct(t, '+')) t = lx.next();
  double v = 0;
  if (!num_value(t, &v)) return false;
  *out = sign * v;
  return true;
}
bool float_ok(double v) { return std::isfinite(v) && std::fabs(v) <= 3.4028234663852886e38; }

// 3. arguman + cagriyi kapatan `)`. Yalniz TAM bir literal ve hemen ardindan
// `)` kabul edilir; `3 + x`, `(3)`, degisken -> false (varsayilan bilinmiyor).
bool parse_default(Lex &lx, uint32_t type, float def[3]) {
  def[0] = def[1] = def[2] = 0.0f;
  if (type == content::kScenePropSayi || type == content::kScenePropTam) {
    double v = 0;
    if (!signed_num(lx, &v) || !float_ok(v)) return false;
    if (type == content::kScenePropTam) {
      // scene_prop_set'in kurali: tamsayi ve |v| <= 2^24 (float'ta tam temsil).
      if (v != std::trunc(v) || std::fabs(v) > (double)content::kScenePropTamMax) return false;
      if (v == 0.0) v = 0.0; // -0 -> +0 (kanonik)
    }
    def[0] = (float)v;
  } else if (type == content::kScenePropBayrak) {
    const int b = bool_literal(lx.next());
    if (b < 0) return false;
    def[0] = (float)b;
  } else if (type == content::kScenePropNokta) {
    Tok t = lx.next();
    double v[3] = {0, 0, 0};
    if (tok_is(t, TIdent, "v3")) {
      if (!punct(lx.next(), '(')) return false;
      for (int k = 0; k < 3; k++) {
        if (!signed_num(lx, &v[k]) || !float_ok(v[k])) return false;
        if (!punct(lx.next(), k < 2 ? ',' : ')')) return false;
      }
    } else if (punct(t, '{')) { // { x: 1, y: 2, z: 3 } — anahtar sirasi serbest, her biri bir kez
      bool seen[3] = {false, false, false};
      for (int k = 0; k < 3; k++) {
        const Tok key = lx.next();
        const int a = tok_is(key, TIdent, "x") ? 0 : tok_is(key, TIdent, "y") ? 1 : tok_is(key, TIdent, "z") ? 2 : -1;
        if (a < 0 || seen[a]) return false;
        seen[a] = true;
        if (!punct(lx.next(), ':')) return false;
        if (!signed_num(lx, &v[a]) || !float_ok(v[a])) return false;
        if (!punct(lx.next(), k < 2 ? ',' : '}')) return false;
      }
    } else {
      return false;
    }
    for (int k = 0; k < 3; k++) def[k] = (float)v[k];
  } else {
    return false;
  }
  return punct(lx.next(), ')');
}

bool name_ok_span(const char *s, uint32_t n) {
  if (n == 0 || n >= content::kScenePropNameLen) return false;
  for (uint32_t i = 0; i < n; i++) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

uint32_t arity(uint32_t type) { return type == content::kScenePropNokta ? 3u : (type >= 1 && type <= 4) ? 1u : 0u; }

bool decl_same(const PropDecl &a, const PropDecl &b) {
  if (a.type != b.type || a.flags != b.flags) return false;
  for (uint32_t k = 0; k < arity(a.type); k++)
    if (std::memcmp(&a.def[k], &b.def[k], sizeof(float)) != 0) return false; // bit-tam
  return true;
}

void note_bad(PropScanResult &r, uint32_t line) {
  if (!r.first_bad_line) r.first_bad_line = line;
}

} // namespace

PropScanResult prop_scan(const char *text, size_t len, PropDecl *out, uint32_t cap) {
  PropScanResult r;
  if (!text) return r;
  if (!out) cap = 0;
  Lex lx;
  lx.p = text;
  lx.end = text + len;
  Tok prev;
  for (;;) {
    const Tok t = lx.next();
    if (t.k == TEnd) break;
    const uint32_t type = t.k == TIdent ? call_type(t) : 0;
    // Uye erisimi (`x.ozellik_sayi(`) ve TANIM (`func ozellik_sayi(`) cagri degil.
    if (type && !punct(prev, '.') && !is_func_keyword(prev)) {
      Lex look = lx; // ana tarama cagrinin ICINE de girer (ic ice cagri kacmasin)
      if (punct(look.next(), '(')) {
        r.calls++;
        // 1. arguman: dengeli herhangi bir ifade, ust duzey `,` a kadar.
        int depth = 0;
        bool arg1 = false;
        for (;;) {
          const Tok a = look.next();
          if (a.k == TEnd) break;
          if (a.k != TPunct) continue;
          const char c = a.s[0];
          if (c == '(' || c == '[' || c == '{') depth++;
          else if (c == ')' || c == ']' || c == '}') {
            if (depth == 0) break; // cagri virgulsuz kapandi
            depth--;
          } else if (c == ',' && depth == 0) {
            arg1 = true;
            break;
          }
        }
        const Tok nm = arg1 ? look.next() : Tok{};
        const Tok sep = nm.k == TStr ? look.next() : Tok{};
        if (nm.k != TStr || nm.bad || !(punct(sep, ',') || punct(sep, ')'))) {
          r.dynamic_name++;
          note_bad(r, t.line);
        } else if (!name_ok_span(nm.s, nm.n)) {
          r.bad_name++;
          note_bad(r, t.line);
        } else {
          PropDecl d;
          std::memcpy(d.name, nm.s, nm.n);
          d.type = type;
          d.line = t.line;
          // `)` hemen adin ardindaysa varsayilan YOK (derlenmez ama tarama
          // onu "bilinmiyor" diye listeler, sessizce yutmaz).
          const bool known = punct(sep, ',') && parse_default(look, type, d.def);
          if (!known) {
            d.flags |= kPropDeclDefaultUnknown;
            d.def[0] = d.def[1] = d.def[2] = 0.0f;
            r.bad_default++;
            note_bad(r, t.line);
          }
          const PropDecl *old = prop_decl_find(out, r.count, d.name);
          if (old) {
            if (!decl_same(*old, d)) {
              r.conflicts++;
              note_bad(r, t.line);
            }
          } else if (r.count < cap) {
            out[r.count++] = d;
          } else {
            r.overflow++;
            note_bad(r, t.line);
          }
        }
      }
    }
    prev = t;
  }
  return r;
}

const PropDecl *prop_decl_find(const PropDecl *d, uint32_t n, const char *name) {
  if (!d || !name) return nullptr;
  for (uint32_t i = 0; i < n; i++)
    if (!std::strcmp(d[i].name, name)) return &d[i];
  return nullptr;
}

uint32_t prop_orphans(const SceneEntity &e, const PropDecl *d, uint32_t n, uint32_t *idx_out, uint32_t cap) {
  uint32_t k = 0;
  const uint32_t pc = e.prop_count < content::kSceneMaxProps ? e.prop_count : content::kSceneMaxProps;
  for (uint32_t i = 0; i < pc; i++) {
    const PropDecl *x = prop_decl_find(d, n, e.props[i].name);
    if (x && x->type == e.props[i].type) continue;
    if (idx_out && k < cap) idx_out[k] = i;
    k++;
  }
  return k;
}

uint32_t prop_marker_points(const content::SceneDesc &s, uint32_t ent, const PropDecl *d, uint32_t n, bool scanned, PropMarker *out,
                            uint32_t cap) {
  if (ent >= s.entity_count) return 0;
  const SceneEntity &e = s.entities[ent];
  uint32_t k = 0;
  auto push = [&](const float local[3], uint32_t kind, const char *name) {
    if (out && k < cap) {
      content::scene_prop_point_world(s, ent, local, out[k].world);
      out[k].kind = kind;
      std::snprintf(out[k].name, sizeof out[k].name, "%s", name);
    }
    k++;
  };
  if (scanned) {
    for (uint32_t i = 0; i < n; i++) {
      if (d[i].type != content::kScenePropNokta) continue;
      const SceneProp *ov = content::scene_prop_find(e, d[i].name);
      if (ov && ov->type == content::kScenePropNokta) push(ov->v, kPropMarkerOverride, d[i].name);
      else if (!(d[i].flags & kPropDeclDefaultUnknown)) push(d[i].def, kPropMarkerDefault, d[i].name);
    }
  }
  const uint32_t pc = e.prop_count < content::kSceneMaxProps ? e.prop_count : content::kSceneMaxProps;
  for (uint32_t i = 0; i < pc; i++) {
    const SceneProp &p = e.props[i];
    if (p.type != content::kScenePropNokta) continue;
    if (!scanned) { push(p.v, kPropMarkerOverride, p.name); continue; } // betik bilinmiyor: yetim diyemeyiz
    const PropDecl *x = prop_decl_find(d, n, p.name);
    if (!x || x->type != content::kScenePropNokta) push(p.v, kPropMarkerOrphan, p.name);
  }
  return k;
}

// =============================================================================
// Nokta duzenleme kipi (E6)
// =============================================================================
const char *prop_edit_state_text(PropEditState s) {
  switch (s) {
  case PropEditState::Ok: return "s\xC3\xBCr\xC3\xBCklenebilir";
  case PropEditState::NoEntity: return "varl\xC4\xB1k yok";
  case PropEditState::Locked: return "varl\xC4\xB1k kilitli";
  case PropEditState::NotScanned: return "betik taranmad\xC4\xB1 (bildirim bilinmiyor)";
  case PropEditState::NotDeclared: return "betik bu noktay\xC4\xB1 okumuyor";
  case PropEditState::NoPosition: return "varsay\xC4\xB1lan kodda hesaplan\xC4\xB1yor: \xC3\xB6nce de\xC4\x9F" "er yaz";
  case PropEditState::Full: return "varl\xC4\xB1kta 16 \xC3\xB6zellik dolu";
  }
  return "?";
}

PropEditState prop_edit_state_entity(const SceneEntity &e, const char *name, const PropDecl *d, uint32_t n, bool scanned,
                                     float out_local[3], bool *is_override) {
  if (is_override) *is_override = false;
  if (!name || !name[0]) return PropEditState::NoEntity;
  // Sira denetci satirinin gorunur sebebiyle ayni: once kilit (gizmo da yok),
  // sonra bildirim, sonra konum/yer.
  if (e.flags & content::kSceneLocked) return PropEditState::Locked;
  if (!scanned) return PropEditState::NotScanned;
  const PropDecl *x = prop_decl_find(d, n, name);
  if (!x || x->type != content::kScenePropNokta) return PropEditState::NotDeclared;
  const SceneProp *ov = content::scene_prop_find(e, name);
  if (ov && ov->type == content::kScenePropNokta) {
    if (out_local) { out_local[0] = ov->v[0]; out_local[1] = ov->v[1]; out_local[2] = ov->v[2]; }
    if (is_override) *is_override = true;
    return PropEditState::Ok;
  }
  if (x->flags & kPropDeclDefaultUnknown) return PropEditState::NoPosition;
  // Ayni adla BASKA turde bir yazma varsa scene_prop_set onu yerinde degistirir
  // (yer gerekmez); yalniz ad YOKSA tavan sorulur.
  if (!ov && e.prop_count >= content::kSceneMaxProps) return PropEditState::Full;
  if (out_local) { out_local[0] = x->def[0]; out_local[1] = x->def[1]; out_local[2] = x->def[2]; }
  return PropEditState::Ok;
}

PropEditState prop_edit_state(const content::SceneDesc &s, int32_t ent, const char *name, const PropDecl *d, uint32_t n, bool scanned,
                              float out_local[3], bool *is_override) {
  if (is_override) *is_override = false;
  if (ent < 0 || (uint32_t)ent >= s.entity_count) return PropEditState::NoEntity;
  return prop_edit_state_entity(s.entities[ent], name, d, n, scanned, out_local, is_override);
}

bool prop_point_set_world(content::SceneDesc &s, uint32_t ent, const char *name, const float world[3], float out_local[3]) {
  if (ent >= s.entity_count) return false;
  float l[3];
  content::scene_prop_point_local(s, ent, world, l);
  if (!content::scene_prop_set(s.entities[ent], name, content::kScenePropNokta, l)) return false;
  if (out_local) { out_local[0] = l[0]; out_local[1] = l[1]; out_local[2] = l[2]; }
  return true;
}

int32_t prop_marker_hit(const PropMarkerScreen *m, uint32_t n, float mx, float my, float r) {
  int32_t best = -1;
  float best_d = 0.0f;
  for (uint32_t i = 0; i < n; i++) {
    if (!m[i].visible || !m[i].editable) continue;
    const float dx = m[i].x - mx, dy = m[i].y - my;
    const float d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (d > r) continue;
    if (best < 0 || d <= best_d) { best = (int32_t)i; best_d = d; } // <=: esitlikte sonra cizilen (ustteki)
  }
  return best;
}

// =============================================================================
// Onbellek
// =============================================================================
const char *prop_cache_state_text(PropCacheState s) {
  switch (s) {
  case PropCacheState::Missing: return "dosya yok";
  case PropCacheState::TooBig: return "cok buyuk (taranmadi)";
  case PropCacheState::ReadFail: return "okunamadi";
  case PropCacheState::Ok: return "tarandi";
  }
  return "?";
}

namespace {
// Damga zaten alindiysa (have) tekrar sorulmaz. Sira: ONCE damga, SONRA okuma —
// okuma sirasinda dosya degisirse kaydedilen damga eski kalir ve bir sonraki
// soru yeniden okutur (tersi, degisikligi kalici olarak kacirabilirdi).
void cache_load(PropCache &c, PropCacheEntry &x, bool have, int64_t mt, int64_t sz) {
  x.loads++;
  c.loads++;
  x.res = PropScanResult{};
  if (!have) {
    c.stats++;
    have = platform::fs_file_stamp(x.path, &mt, &sz);
  }
  if (!have) {
    x.state = PropCacheState::Missing;
    x.mtime_ns = 0;
    x.size = -1;
    return;
  }
  x.mtime_ns = mt;
  x.size = sz;
  if (sz > (int64_t)kPropScanTextMax) { x.state = PropCacheState::TooBig; return; }
  FILE *f = std::fopen(x.path, "rb");
  if (!f) { x.state = PropCacheState::ReadFail; return; }
  const size_t got = std::fread(c.text, 1, kPropScanTextMax, f);
  const bool fazla = got == kPropScanTextMax && std::fgetc(f) != EOF; // damgadan sonra buyudu
  const bool hata = std::ferror(f) != 0;
  std::fclose(f);
  if (fazla) { x.state = PropCacheState::TooBig; return; } // YARIM metin taranmaz
  if (hata) { x.state = PropCacheState::ReadFail; return; }
  c.text[got] = 0;
  x.res = prop_scan(c.text, got, x.decls, kPropDeclMax);
  x.state = PropCacheState::Ok;
}
} // namespace

const PropCacheEntry *prop_cache_get(PropCache &c, const char *path, uint32_t frame, bool may_io) {
  if (!path || !*path) return nullptr;
  for (PropCacheEntry &x : c.e) {
    if (!x.used || std::strcmp(x.path, path) != 0) continue;
    x.use_frame = frame;
    if (may_io && frame - x.stat_frame >= kPropRestatFrames) {
      x.stat_frame = frame;
      c.stats++;
      int64_t mt = 0, sz = 0;
      const bool ok = platform::fs_file_stamp(path, &mt, &sz);
      const bool same = ok ? (x.state != PropCacheState::Missing && mt == x.mtime_ns && sz == x.size) : x.state == PropCacheState::Missing;
      if (!same) cache_load(c, x, ok, mt, sz);
    }
    return &x;
  }
  if (!may_io) return nullptr;
  if (std::strlen(path) >= sizeof c.e[0].path) return nullptr; // anahtar sigmiyor: cagiran "taranamadi" der
  PropCacheEntry *slot = nullptr;
  for (PropCacheEntry &x : c.e)
    if (!x.used) { slot = &x; break; }
  if (!slot) {
    slot = &c.e[0];
    for (PropCacheEntry &x : c.e)
      if (x.use_frame < slot->use_frame) slot = &x;
    c.evictions++;
  }
  *slot = PropCacheEntry{};
  std::snprintf(slot->path, sizeof slot->path, "%s", path);
  slot->used = true;
  slot->use_frame = frame;
  slot->stat_frame = frame;
  cache_load(c, *slot, false, 0, 0);
  return slot;
}

const PropCacheEntry *prop_cache_peek(PropCache &c, const char *path, uint32_t frame) { return prop_cache_get(c, path, frame, false); }

void prop_cache_clear(PropCache &c) {
  for (PropCacheEntry &x : c.e) x.used = false;
}

// =============================================================================
// Denetci bolumu
// =============================================================================
namespace {

const char *type_text(uint32_t t) {
  switch (t) {
  case content::kScenePropSayi: return "say\xC4\xB1";   // sayı
  case content::kScenePropTam: return "tam";
  case content::kScenePropBayrak: return "bayrak";
  case content::kScenePropNokta: return "nokta";
  }
  return "?";
}
// Degerin okunur hali (salt okunur satirlar ve ipuclari icin).
void value_text(uint32_t type, const float v[3], char *out, size_t cap) {
  switch (type) {
  case content::kScenePropSayi: std::snprintf(out, cap, "%.3f", (double)v[0]); break;
  case content::kScenePropTam: std::snprintf(out, cap, "%d", (int)v[0]); break;
  case content::kScenePropBayrak: std::snprintf(out, cap, "%s", v[0] != 0.0f ? "evet" : "hay\xC4\xB1r"); break;
  case content::kScenePropNokta: std::snprintf(out, cap, "%.2f  %.2f  %.2f", (double)v[0], (double)v[1], (double)v[2]); break;
  default: std::snprintf(out, cap, "?"); break;
  }
}

ImVec4 tone4(Tone t) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3]);
}

void call_item(void (*on_item)(void *, const PropItem &), void *user, const PropItem &it) {
  if (on_item) on_item(user, it);
}

// Bildirilmis tek ozellik satiri.
void decl_row(SceneEntity &e, SceneEntity &after, const PropDecl &d, void (*on_item)(void *, const PropItem &), void *user,
              const char *point_edit, PropsPanelResult &r) {
  const SceneProp *ov = content::scene_prop_find(e, d.name);
  const bool is_ov = ov && ov->type == d.type;
  const bool unknown = (d.flags & kPropDeclDefaultUnknown) != 0;
  // Yeni ad + dolu varlik (16): yazma REDDEDILIRDI ve deger geri siçrardi.
  // Onun yerine alan kapali ve sebebi ipucunda.
  const bool full = !is_ov && e.prop_count >= content::kSceneMaxProps;
  float v[3] = {d.def[0], d.def[1], d.def[2]};
  if (is_ov) { v[0] = ov->v[0]; v[1] = ov->v[1]; v[2] = ov->v[2]; }

  char defs[64], help[256];
  if (unknown) std::snprintf(defs, sizeof defs, "kodda hesaplan\xC4\xB1yor (literal de\xC4\x9Fil)");
  else value_text(d.type, d.def, defs, sizeof defs);
  if (full)
    std::snprintf(help, sizeof help, "%s \xC2\xB7 betikteki varsay\xC4\xB1lan: %s (sat\xC4\xB1r %u)\nVarl\xC4\xB1k ba\xC5\x9F\xC4\xB1na en \xC3\xA7ok %u \xC3\xB6zellik yaz\xC4\xB1labilir: \xC3\xB6nce birini s\xC4\xB1" "f\xC4\xB1rla.",
                  type_text(d.type), defs, d.line, content::kSceneMaxProps);
  else if (is_ov)
    std::snprintf(help, sizeof help, "%s \xC2\xB7 bu varl\xC4\xB1\xC4\x9F\xC4\xB1n de\xC4\x9F" "eri\nBetikteki varsay\xC4\xB1lan: %s (sat\xC4\xB1r %u)",
                  type_text(d.type), defs, d.line);
  else
    std::snprintf(help, sizeof help, "%s \xC2\xB7 betikteki varsay\xC4\xB1lan: %s (sat\xC4\xB1r %u)\nDe\xC4\x9Fi\xC5\x9Ftirince yaln\xC4\xB1z bu varl\xC4\xB1\xC4\x9F" "a yaz\xC4\xB1l\xC4\xB1r.",
                  type_text(d.type), defs, d.line);

  const float bw = ImGui::GetFrameHeight();
  // Nokta satirinda IKI dugme: ✥ (gorunumde surukle, E6) + ↺. Digerlerinde
  // yalniz ↺ — ↺ her satirda en sagda kalir, sutun hizasi bozulmaz.
  const bool is_point = d.type == content::kScenePropNokta;
  prop_help(help);
  prop_label_tone(is_ov ? Tone::Text : Tone::TextDim);
  prop_reserve_trailing(is_point ? bw * 2.0f : bw);
  if (!is_ov) ImGui::PushStyleColor(ImGuiCol_Text, tone4(Tone::TextDim));
  if (full) ImGui::BeginDisabled();
  // Varsayilan satirinda deger SOLUK ve "varsayilan" yazili; bilinmeyen
  // varsayilanda sayi YOK (0 gostermek "varsayilan 0" diye yanlis okunurdu).
  const char *ffmt = is_ov ? "%.3f" : unknown ? "? \xC2\xB7 kodda" : "%.3f \xC2\xB7 varsay\xC4\xB1lan";
  const char *ifmt = is_ov ? "%d" : unknown ? "? \xC2\xB7 kodda" : "%d \xC2\xB7 varsay\xC4\xB1lan";
  const char *vfmt = (!is_ov && unknown) ? "?" : "%.2f";
  float nv[3] = {0, 0, 0};
  switch (d.type) {
  case content::kScenePropSayi: {
    float f = v[0];
    const PropItem it = prop_float(d.name, &f, 0.05f, 0, 0, ffmt);
    nv[0] = f;
    if (it.changed) content::scene_prop_set(e, d.name, d.type, nv);
    call_item(on_item, user, it);
    break;
  }
  case content::kScenePropTam: {
    int iv = (int)v[0];
    const int lim = (int)content::kScenePropTamMax;
    const PropItem it = prop_int(d.name, &iv, -lim, lim, ifmt);
    nv[0] = (float)iv;
    if (it.changed) content::scene_prop_set(e, d.name, d.type, nv);
    call_item(on_item, user, it);
    break;
  }
  case content::kScenePropBayrak: {
    bool b = v[0] != 0.0f;
    if (prop_check(d.name, &b).changed) {
      after = e;
      nv[0] = b ? 1.0f : 0.0f;
      if (content::scene_prop_set(after, d.name, d.type, nv)) r.commit = true;
    }
    if (!is_ov && prop_row_drawn()) {
      ImGui::SameLine();
      ImGui::TextUnformatted(unknown ? "kodda" : "varsay\xC4\xB1lan");
    }
    break;
  }
  case content::kScenePropNokta: {
    float p[3] = {v[0], v[1], v[2]};
    const PropItem it = prop_vec3(d.name, p, 0.05f, 0, 0, vfmt);
    if (it.changed) content::scene_prop_set(e, d.name, d.type, p);
    call_item(on_item, user, it);
    break;
  }
  default: break;
  }
  if (full) ImGui::EndDisabled();
  if (!is_ov) ImGui::PopStyleColor();
  // ✥ nokta duzenleme kipi (E6): kural prop_edit_state — kapaliysa ipucu
  // SEBEBI soyler (kilitli, varsayilan kodda, 16 dolu). Karar editorun: panel
  // yalniz "bu ad icin ac/kapa" niyetini doner.
  if (is_point && prop_row_drawn()) {
    const bool on = point_edit && !std::strcmp(point_edit, d.name);
    const PropEditState ps = prop_edit_state_entity(e, d.name, &d, 1, true, nullptr, nullptr);
    char tip[160];
    if (on) std::snprintf(tip, sizeof tip, "Nokta d\xC3\xBCzenlemeyi bitir (Esc)");
    else if (ps == PropEditState::Ok) std::snprintf(tip, sizeof tip, "Noktay\xC4\xB1 g\xC3\xB6r\xC3\xBCn\xC3\xBCmde s\xC3\xBCr\xC3\xBCkle (gizmo noktaya ta\xC5\x9F\xC4\xB1n\xC4\xB1r)");
    else std::snprintf(tip, sizeof tip, "S\xC3\xBCr\xC3\xBCklenemez: %s", prop_edit_state_text(ps));
    const bool enabled = on || ps == PropEditState::Ok;
    if (prop_trailing_slot(ICON_MD_OPEN_WITH, tip, bw, on, !enabled)) {
      r.point_toggle = true;
      std::snprintf(r.point_name, sizeof r.point_name, "%s", d.name);
    }
    r.point_rect = prop_trailing_slot_last_rect();
    r.point_buttons++;
    if (enabled) r.point_enabled++;
  }
  // Sifirla: ustune yazmayi SIL (varsayilana don). Varsayilan satirinda yer
  // yine ayrilir ki bildirim satirlari hizali kalsin.
  if (prop_trailing_button(is_ov ? ICON_MD_REPLAY : nullptr, "Varsay\xC4\xB1lana d\xC3\xB6n (betikteki de\xC4\x9F" "er)")) {
    after = e;
    if (content::scene_prop_remove(after, d.name)) r.commit = true;
  }
  if (prop_row_drawn()) {
    r.rows_declared++;
    if (is_ov) r.rows_overridden++;
  }
}

// Yetim ya da ham ustune yazma: salt okunur deger + sil.
void raw_row(SceneEntity &e, SceneEntity &after, const SceneProp &p, Tone label_tone, const char *why, PropsPanelResult &r) {
  char val[96], help[320];
  value_text(p.type, p.v, val, sizeof val);
  std::snprintf(help, sizeof help, "%s \xC2\xB7 %s", type_text(p.type), why);
  prop_help(help);
  prop_label_tone(label_tone);
  prop_reserve_trailing(ImGui::GetFrameHeight());
  ImGui::BeginDisabled();
  prop_text(p.name, val, sizeof val);
  ImGui::EndDisabled();
  if (prop_trailing_button(ICON_MD_DELETE, "Bu \xC3\xBCst\xC3\xBCne yazmay\xC4\xB1 sil")) {
    after = e;
    if (content::scene_prop_remove(after, p.name)) r.commit = true;
  }
  if (prop_row_drawn()) r.rows_orphan++;
}

} // namespace

PropsPanelResult props_panel(SceneEntity &e, SceneEntity &after, const PropsPanelInput &in, void (*on_item)(void *, const PropItem &),
                             void *user) {
  PropsPanelResult r;
  section_label("\xC3\x96ZELL\xC4\xB0KLER"); // ÖZELLİKLER
  const bool scanned = in.has_script && in.scan != nullptr;
  const ImVec4 dim = tone4(Tone::TextDim), warn = tone4(Tone::Warn);
  const float wrap = ImGui::GetContentRegionAvail().x;
  auto wrapped = [&](const ImVec4 &col, const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  };
  if (!in.has_script) {
    wrapped(dim, "Betik bile\xC5\x9F" "eni yok: bu de\xC4\x9F" "erler betik geri eklenince kullan\xC4\xB1l\xC4\xB1r.");
  } else if (!scanned) {
    char msg[320];
    std::snprintf(msg, sizeof msg, ICON_MD_WARNING " Betik taranamad\xC4\xB1: %s", in.note ? in.note : "?");
    wrapped(warn, msg);
  } else if (in.decl_count == 0 && e.prop_count == 0) {
    wrapped(dim, "Bu betik \xC3\xB6zellik bildirmiyor. Kodda ad\xC4\xB1 ve varsay\xC4\xB1lan\xC4\xB1 yaz\xC4\xB1l\xC4\xB1 bir \xC3\xA7" "a\xC4\x9Fr\xC4\xB1, "
                 "\xC3\xB6rn. ozellik_sayi(i, \"hiz\", 3.5), burada bir sat\xC4\xB1r olur.");
  }

  if (scanned && in.decl_count > 0 && prop_begin("ozellik_bildirim")) {
    for (uint32_t i = 0; i < in.decl_count; i++) {
      ImGui::PushID(in.decls[i].name);
      decl_row(e, after, in.decls[i], on_item, user, in.point_edit, r);
      ImGui::PopID();
    }
    prop_end();
  }

  // Yetimler (betik taranmissa) ya da ham ustune yazmalar (betik yok / taranamadi).
  uint32_t idx[content::kSceneMaxProps];
  uint32_t n_raw = 0;
  if (scanned) {
    n_raw = prop_orphans(e, in.decls, in.decl_count, idx, content::kSceneMaxProps);
  } else {
    const uint32_t pc = e.prop_count < content::kSceneMaxProps ? e.prop_count : content::kSceneMaxProps;
    for (uint32_t i = 0; i < pc; i++) idx[n_raw++] = i;
  }
  if (n_raw > 0) {
    if (scanned) wrapped(warn, ICON_MD_WARNING " Betik bunlar\xC4\xB1 okumuyor (ad\xC4\xB1 ya da t\xC3\xBCr\xC3\xBC de\xC4\x9Fi\xC5\x9Fmi\xC5\x9F olabilir): oyunda etkisiz.");
    if (prop_begin("ozellik_yetim")) {
      for (uint32_t k = 0; k < n_raw && k < content::kSceneMaxProps; k++) {
        const SceneProp p = e.props[idx[k]]; // kopya: sil dugmesi `after`i degistirir, `e`yi degil
        char why[192];
        if (!scanned) {
          std::snprintf(why, sizeof why, "%s", in.has_script ? "betik taranamad\xC4\xB1: bildirilip bildirilmedi\xC4\x9Fi bilinmiyor"
                                                              : "betik bile\xC5\x9F" "eni yok");
        } else {
          const PropDecl *x = prop_decl_find(in.decls, in.decl_count, p.name);
          if (x) std::snprintf(why, sizeof why, "t\xC3\xBCr uyu\xC5\x9Fmuyor: sahnede %s, betikte %s (sat\xC4\xB1r %u)", type_text(p.type),
                               type_text(x->type), x->line);
          else std::snprintf(why, sizeof why, "betik bu ad\xC4\xB1 okumuyor");
        }
        ImGui::PushID(p.name);
        raw_row(e, after, p, scanned ? Tone::Warn : Tone::TextDim, why, r);
        ImGui::PopID();
      }
      prop_end();
    }
  }

  // Tarama sorunlari: her biri GORUNUR, tek satirda, ilk satir numarasiyla.
  if (scanned && in.scan->problems() > 0) {
    const PropScanResult &s = *in.scan;
    char msg[512];
    int w = std::snprintf(msg, sizeof msg, ICON_MD_WARNING " Betik taramas\xC4\xB1:");
    auto add = [&](uint32_t n, const char *what) {
      if (!n || w < 0 || (size_t)w >= sizeof msg) return;
      const int k = std::snprintf(msg + w, sizeof msg - (size_t)w, " %u %s \xC2\xB7", n, what);
      if (k > 0) w += k;
    };
    add(s.bad_default, "varsay\xC4\xB1lan literal de\xC4\x9Fil");
    add(s.conflicts, "\xC3\xA7" "ak\xC4\xB1\xC5\x9Fma (ayn\xC4\xB1 ad, farkl\xC4\xB1 t\xC3\xBCr/varsay\xC4\xB1lan)");
    add(s.overflow, "s\xC4\xB1\xC4\x9Fmad\xC4\xB1 (en \xC3\xA7ok 32 bildirim)");
    add(s.bad_name, "ge\xC3\xA7" "ersiz ad");
    add(s.dynamic_name, "ad\xC4\xB1 literal de\xC4\x9Fil (listelenemez)");
    if (w > 0 && (size_t)w < sizeof msg) std::snprintf(msg + w, sizeof msg - (size_t)w, " ilk: sat\xC4\xB1r %u", s.first_bad_line);
    wrapped(warn, msg);
    r.diagnostics = true;
  }
  if (scanned && in.decl_count > 0)
    wrapped(tone4(Tone::TextMute), r.point_buttons > 0
                                       ? "Soluk = betikteki varsay\xC4\xB1lan \xC2\xB7 " ICON_MD_REPLAY " varsay\xC4\xB1lana d\xC3\xB6n \xC2\xB7 " ICON_MD_OPEN_WITH
                                         " noktay\xC4\xB1 g\xC3\xB6r\xC3\xBCn\xC3\xBCmde s\xC3\xBCr\xC3\xBCkle"
                                       : "Soluk = betikteki varsay\xC4\xB1lan \xC2\xB7 " ICON_MD_REPLAY " varsay\xC4\xB1lana d\xC3\xB6n");
  return r;
}

} // namespace tulpar::engine::app
