#include "content/model_import.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/hash.hpp"

namespace tulpar::engine::content {
namespace {

bool ends_with_ci(const char *s, const char *ek) {
  if (!s) return false;
  const size_t sl = std::strlen(s), el = std::strlen(ek);
  if (sl < el) return false;
  for (size_t i = 0; i < el; i++) {
    char c = s[sl - el + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c != ek[i]) return false;
  }
  return true;
}
bool tok_eq(const char *a, size_t al, const char *b) { return std::strlen(b) == al && !std::memcmp(a, b, al); }
const char *unit_name(ModelUnit u) {
  switch (u) {
  case ModelUnit::Cm: return "cm";
  case ModelUnit::Mm: return "mm";
  case ModelUnit::Inch: return "inc";
  default: return "m";
  }
}
const char *pivot_name(ModelPivot p) { return p == ModelPivot::Base ? "taban" : p == ModelPivot::Center ? "merkez" : "koken"; }
// Sayi yazimi: en kisa geri okunabilir (%.9g bit-tam; once %g dene ki 1.5 "1.5" kalsin).
void put_num(char *b, size_t cap, float v) {
  std::snprintf(b, cap, "%g", (double)v);
  if (std::strtof(b, nullptr) != v) std::snprintf(b, cap, "%.9g", (double)v);
}
} // namespace

bool model_import_equal(const ModelImportSettings &a, const ModelImportSettings &b) {
  return a.unit == b.unit && a.z_up == b.z_up && a.scale == b.scale && a.fit == b.fit && a.pivot == b.pivot;
}

float model_unit_meters(ModelUnit u) {
  switch (u) {
  case ModelUnit::Cm: return 0.01f;
  case ModelUnit::Mm: return 0.001f;
  case ModelUnit::Inch: return 0.0254f;
  default: return 1.0f;
  }
}

ModelImportSettings model_import_defaults(const char *model_path) {
  ModelImportSettings s;
  if (ends_with_ci(model_path, ".stl")) {   // M2: 3B baski/CAD — Z-yukari, mm; her zaman gorunur olsun diye 1 m'ye sigdir, yere otur
    s.z_up = true;
    s.fit = 1.0f;
    s.pivot = ModelPivot::Base;
  }
  return s;
}

bool model_import_path(const char *model_path, char *out, size_t cap) {
  if (!model_path) return false;
  const int n = std::snprintf(out, cap, "%s.ayar", model_path);
  return n > 0 && (size_t)n < cap;
}

bool model_import_parse(const char *text, size_t len, ModelImportSettings *out, char *err, size_t err_cap) {
  ModelImportSettings s;
  uint32_t line = 0, seen = 0;
  bool header = false;
  size_t i = 0;
  auto fail = [&](const char *why) {
    if (err && err_cap) std::snprintf(err, err_cap, "model ayari satir %u: %s", line, why);
    return false;
  };
  while (i < len) {
    size_t e = i;
    while (e < len && text[e] != '\n') e++;
    line++;
    // jetonlar (bosluk/sekme ayirir, # sonrasi yorum)
    const char *tk[4];
    size_t tl[4];
    uint32_t n = 0;
    size_t p = i;
    while (p < e) {
      while (p < e && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r')) p++;
      if (p >= e || text[p] == '#') break;
      const size_t b = p;
      while (p < e && text[p] != ' ' && text[p] != '\t' && text[p] != '\r') p++;
      if (n >= 4) return fail("fazla jeton");
      tk[n] = text + b;
      tl[n] = p - b;
      n++;
    }
    i = e + 1;
    if (!n) continue;
    if (!header) {
      if (n != 2 || !tok_eq(tk[0], tl[0], "tulpar-model-ayar") || !tok_eq(tk[1], tl[1], "1")) return fail("ilk satir 'tulpar-model-ayar 1' olmali");
      header = true;
      continue;
    }
    if (n != 2) return fail("anahtar deger bekleniyor");
    char v[32];
    if (tl[1] >= sizeof v) return fail("deger cok uzun");
    std::memcpy(v, tk[1], tl[1]);
    v[tl[1]] = 0;
    auto once = [&](uint32_t bit) {
      if (seen & bit) return false;
      seen |= bit;
      return true;
    };
    auto num = [&](float *dst) {
      char *end = nullptr;
      const float f = std::strtof(v, &end);
      if (!end || *end || !std::isfinite(f)) return false;
      *dst = f;
      return true;
    };
    if (tok_eq(tk[0], tl[0], "birim")) {
      if (!once(1)) return fail("birim bir kez yazilir");
      if (!std::strcmp(v, "m")) s.unit = ModelUnit::M;
      else if (!std::strcmp(v, "cm")) s.unit = ModelUnit::Cm;
      else if (!std::strcmp(v, "mm")) s.unit = ModelUnit::Mm;
      else if (!std::strcmp(v, "inc")) s.unit = ModelUnit::Inch;
      else return fail("birim m | cm | mm | inc olmali");
    } else if (tok_eq(tk[0], tl[0], "yukari")) {
      if (!once(2)) return fail("yukari bir kez yazilir");
      if (!std::strcmp(v, "y")) s.z_up = false;
      else if (!std::strcmp(v, "z")) s.z_up = true;
      else return fail("yukari y | z olmali");
    } else if (tok_eq(tk[0], tl[0], "olcek")) {
      if (!once(4)) return fail("olcek bir kez yazilir");
      if (!num(&s.scale) || !(s.scale > 0.0f) || s.scale > 1e6f) return fail("olcek 0'dan buyuk (en cok 1e6) bir sayi olmali");
    } else if (tok_eq(tk[0], tl[0], "sigdir")) {
      if (!once(8)) return fail("sigdir bir kez yazilir");
      if (!num(&s.fit) || s.fit < 0.0f || s.fit > 1e6f) return fail("sigdir 0 (kapali) ya da 0'dan buyuk metre olmali");
    } else if (tok_eq(tk[0], tl[0], "pivot")) {
      if (!once(16)) return fail("pivot bir kez yazilir");
      if (!std::strcmp(v, "koken")) s.pivot = ModelPivot::Origin;
      else if (!std::strcmp(v, "taban")) s.pivot = ModelPivot::Base;
      else if (!std::strcmp(v, "merkez")) s.pivot = ModelPivot::Center;
      else return fail("pivot koken | taban | merkez olmali");
    } else {
      return fail("bilinmeyen anahtar (birim, yukari, olcek, sigdir, pivot)");
    }
  }
  if (!header) { line = 0; return fail("bos dosya ('tulpar-model-ayar 1' yok)"); }
  *out = s;
  return true;
}

size_t model_import_write(const ModelImportSettings &s, char *buf, size_t cap) {
  char sc[32], ft[32];
  put_num(sc, sizeof sc, s.scale);
  put_num(ft, sizeof ft, s.fit);
  const int n = std::snprintf(buf, cap, "tulpar-model-ayar 1\nbirim %s\nyukari %s\nolcek %s\nsigdir %s\npivot %s\n", unit_name(s.unit),
                              s.z_up ? "z" : "y", sc, ft, pivot_name(s.pivot));
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

bool model_import_load(const char *model_path, ModelImportSettings *out, bool *found, char *err, size_t err_cap) {
  *found = false;
  *out = model_import_defaults(model_path);
  char yol[1100];
  if (!model_import_path(model_path, yol, sizeof yol)) return true;
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return true;
  char text[2048];
  const size_t n = std::fread(text, 1, sizeof text, f);
  const bool more = std::fgetc(f) != EOF;
  std::fclose(f);
  *found = true;
  if (more) {
    if (err && err_cap) std::snprintf(err, err_cap, "model ayari cok uzun (en cok %u bayt): %s", (unsigned)sizeof text, yol);
    return false;
  }
  return model_import_parse(text, n, out, err, err_cap);
}

bool model_import_save(const char *model_path, const ModelImportSettings &s) {
  char yol[1100];
  if (!model_import_path(model_path, yol, sizeof yol)) return false;
  if (model_import_equal(s, model_import_defaults(model_path))) {
    std::remove(yol); // yoksa da sorun degil
    return true;
  }
  char text[512];
  const size_t n = model_import_write(s, text, sizeof text);
  if (!n) return false;
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(text, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}

uint64_t model_import_file_hash(const char *model_path) {
  char yol[1100];
  if (!model_import_path(model_path, yol, sizeof yol)) return 0;
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return 0;
  uint8_t buf[2048];
  const size_t n = std::fread(buf, 1, sizeof buf, f);
  std::fclose(f);
  const uint64_t h = content_fnv1a(buf, n);
  return h ? h : 1;
}

Mat4 model_import_matrix(const ModelImportSettings &s, Vec3 lo, Vec3 hi, Vec3 *out_lo, Vec3 *out_hi) {
  // 1) Eksen: Z-yukari -> Y-yukari, (x, y, z) -> (x, z, -y) (M2'nin STL donusumuyle ayni). Sutun-major: m[sutun][satir].
  Mat4 R;
  if (s.z_up) {
    R.m[1][1] = 0.0f; R.m[1][2] = -1.0f; // y -> -z
    R.m[2][1] = 1.0f; R.m[2][2] = 0.0f;  // z -> y
    const Vec3 a{lo.x, lo.z, -hi.y}, b{hi.x, hi.z, -lo.y};
    lo = a;
    hi = b;
  }
  // 2) Olcek: sigdir > 0 ve kutu gecerliyse en buyuk boyut `fit` metre; degilse birim * olcek.
  const float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
  const float buyuk = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
  float k = model_unit_meters(s.unit) * s.scale;
  if (s.fit > 0.0f && buyuk > 0.0f && std::isfinite(buyuk)) k = s.fit / buyuk;
  // 3) Pivot (eksen donusumunden SONRAKI kutuda): taban = x,z ortasi + en alt y; merkez = kutunun ortasi.
  Vec3 p{0, 0, 0};
  if (s.pivot == ModelPivot::Base) p = Vec3{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
  else if (s.pivot == ModelPivot::Center) p = Vec3{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
  // Sinir: kose basina AYNI islem sirasi ((q - p) * k), M2'nin kose kosedeki formuluyle bit-tam.
  if (out_lo) *out_lo = Vec3{(lo.x - p.x) * k, (lo.y - p.y) * k, (lo.z - p.z) * k};
  if (out_hi) *out_hi = Vec3{(hi.x - p.x) * k, (hi.y - p.y) * k, (hi.z - p.z) * k};
  return Mat4::scale(Vec3{k, k, k}) * Mat4::translate(Vec3{-p.x, -p.y, -p.z}) * R;
}

void model_import_apply(Model &m, const ModelImportSettings &s) {
  m.import_raw_min = m.bounds_min;
  m.import_raw_max = m.bounds_max;
  Vec3 lo, hi;
  const Mat4 C = model_import_matrix(s, m.bounds_min, m.bounds_max, &lo, &hi);
  m.import_xform = C;
  m.bounds_min = lo;
  m.bounds_max = hi;
  for (uint32_t i = 0; i < m.instance_count; i++) m.instances[i].world = C * m.instances[i].world;
}

void model_import_reapply(Model &m, const ModelImportSettings &s) {
  // Eski C: olcek * oteleme * eksen permutasyonu — tersi kesin afin ters. Instance'lar ham hale donup yeni C ile carpilir.
  const Mat4 inv = inverse_affine(m.import_xform);
  Vec3 lo, hi;
  const Mat4 C = model_import_matrix(s, m.import_raw_min, m.import_raw_max, &lo, &hi);
  const Mat4 D = C * inv;
  for (uint32_t i = 0; i < m.instance_count; i++) m.instances[i].world = D * m.instances[i].world;
  m.import_xform = C;
  m.bounds_min = lo;
  m.bounds_max = hi;
}

} // namespace tulpar::engine::content
