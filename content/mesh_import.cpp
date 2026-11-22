#include "content/mesh_import.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/meshopt.hpp"

namespace tulpar::engine::content {
namespace {

constexpr uint32_t kMaxTris = 4u << 20;       // 4M üçgen
constexpr uint32_t kMaxCorners = 8u << 20;    // 8M köşe (üçgen başına 3, tavan yine de 4M üçgen)

void hata(Model *m, const char *what, const char *detail = nullptr) {
  std::snprintf(m->error, sizeof m->error, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

// malloc tabanlı küçük büyüyen dizi (STL yasağı: konteyner yok). Yükleme anı geçicisi; Arena'ya kopyalanır.
template <typename T>
struct Buf {
  T *p = nullptr;
  uint32_t n = 0, cap = 0;
  ~Buf() { std::free(p); }
  Buf() = default;
  Buf(const Buf &) = delete;
  Buf &operator=(const Buf &) = delete;
  bool push(const T &v) {
    if (n == cap) {
      const uint32_t nc = cap ? cap * 2 : 256;
      T *q = static_cast<T *>(std::realloc(p, sizeof(T) * nc));
      if (!q) return false;
      p = q; cap = nc;
    }
    p[n++] = v;
    return true;
  }
};

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

unsigned char *oku_dosya(const char *path, size_t *size) {
  std::FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return nullptr; }
  unsigned char *b = static_cast<unsigned char *>(std::malloc((size_t)n + 1));
  if (!b) { std::fclose(f); return nullptr; }
  const size_t got = std::fread(b, 1, (size_t)n, f);
  std::fclose(f);
  if (got != (size_t)n) { std::free(b); return nullptr; }
  b[n] = 0;
  *size = (size_t)n;
  return b;
}

// Satır ayırıcı (strtok_r MinGW'de güvenilir değil): cur'dan sonraki satırı NUL'layıp döndürür, bitince nullptr.
char *sonraki_satir(char *&cur) {
  if (!cur || !*cur) return nullptr;
  char *bas = cur;
  char *e = cur;
  while (*e && *e != '\n') e++;
  if (*e) { *e = 0; cur = e + 1; } else cur = e;
  return bas;
}

bool bitis(const char *s, const char *ek) {
  const size_t a = std::strlen(s), b = std::strlen(ek);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++) {
    char c = s[a - b + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c != ek[i]) return false;
  }
  return true;
}

// ---- Ortak kurucu --------------------------------------------------------------------------------------------------------
struct Giris {
  Vec3 *pos = nullptr;        // [3*ntri] köşe bazlı
  Vec3 *nrm = nullptr;        // [3*ntri] ya da null (hesaplanır)
  Vec2 *uv = nullptr;         // [3*ntri] ya da null
  uint32_t *tri_mat = nullptr; // [ntri] ya da null (hepsi 0)
  uint32_t ntri = 0;
  uint32_t nmat = 1;
  const Vec3 *mat_renk = nullptr;
};

// Yüz normali (alan ağırlıklı, normalleştirilmemiş).
Vec3 yuz_normali(const Vec3 *p) { return cross(p[1] - p[0], p[2] - p[0]); }

// Köşe birleştirme + açı eşikli yumuşak normal: aynı konumdaki köşelerde, kendi yüz normaline eşik içindeki yüzlerin alan ağırlıklı toplamı.
bool normal_hesapla(Giris &g, float aci_derece, Model *m) {
  const uint32_t nc = g.ntri * 3;
  uint32_t *sira = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * nc));
  Vec3 *fn = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * g.ntri));
  g.nrm = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * nc));
  if (!sira || !fn || !g.nrm) { std::free(sira); std::free(fn); hata(m, "bellek (normal)"); return false; }
  for (uint32_t t = 0; t < g.ntri; t++) fn[t] = yuz_normali(&g.pos[t * 3]);
  for (uint32_t i = 0; i < nc; i++) sira[i] = i;
  const Vec3 *P = g.pos;
  std::sort(sira, sira + nc, [P](uint32_t a, uint32_t b) {
    if (P[a].x != P[b].x) return P[a].x < P[b].x;
    if (P[a].y != P[b].y) return P[a].y < P[b].y;
    return P[a].z < P[b].z;
  });
  const float esik = std::cos(aci_derece * 3.14159265f / 180.0f);
  for (uint32_t i = 0; i < nc;) {
    uint32_t j = i + 1;
    while (j < nc && P[sira[j]].x == P[sira[i]].x && P[sira[j]].y == P[sira[i]].y && P[sira[j]].z == P[sira[i]].z) j++;
    const uint32_t k = j - i;
    for (uint32_t a = i; a < j; a++) {
      const uint32_t ca = sira[a], ta = ca / 3;
      const Vec3 na = fn[ta];
      const float la = length(na);
      Vec3 s = na;
      if (k <= 2048 && la > 1e-20f) {                     // patolojik (binlerce özdeş konum) -> düz gölgeleme
        s = Vec3{0, 0, 0};
        for (uint32_t b = i; b < j; b++) {
          const Vec3 nb = fn[sira[b] / 3];
          const float lb = length(nb);
          if (lb <= 1e-20f) continue;
          if (dot(na, nb) / (la * lb) >= esik) s = s + nb;
        }
      }
      const float ls = length(s);
      g.nrm[ca] = ls > 1e-20f ? s * (1.0f / ls) : Vec3{0, 1, 0};
    }
    i = j;
  }
  std::free(sira);
  std::free(fn);
  return true;
}

bool model_kur(Arena &arena, Model *out, Giris &g, const GltfLimits &lim, const MeshImportOptions &opt) {
  *out = Model{};
  if (g.ntri == 0) { hata(out, "bos model (ucgen yok)"); return false; }
  if (g.ntri > kMaxTris) { hata(out, "ucgen siniri asildi (en cok 4M)"); return false; }
  const uint32_t nc = g.ntri * 3;
  // 1) Z-yukarı -> Y-yukarı; sonra sınır kutusu, ölçek, pivot.
  if (opt.z_up)
    for (uint32_t i = 0; i < nc; i++) {
      const Vec3 p = g.pos[i];
      g.pos[i] = Vec3{p.x, p.z, -p.y};
      if (g.nrm) { const Vec3 n = g.nrm[i]; g.nrm[i] = Vec3{n.x, n.z, -n.y}; }
    }
  Vec3 lo = g.pos[0], hi = g.pos[0];
  for (uint32_t i = 1; i < nc; i++) {
    const Vec3 p = g.pos[i];
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  const float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
  const float buyuk = std::max(dx, std::max(dy, dz));
  if (!(buyuk > 0.0f) || !std::isfinite(buyuk)) { hata(out, "model boyutu sifir ya da gecersiz"); return false; }
  const float olcek = opt.fit_size > 0.0f ? opt.fit_size / buyuk : opt.scale;
  const Vec3 merkez{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
  for (uint32_t i = 0; i < nc; i++) {
    Vec3 p = g.pos[i];
    if (opt.ground_pivot) p = p - merkez;
    g.pos[i] = p * olcek;
  }
  // 2) Normal yoksa hesapla (ölçekten bağımsız: yön).
  if (!g.nrm && !normal_hesapla(g, opt.smooth_angle_deg, out)) return false;
  // 3) Malzemeler.
  const uint32_t nmat = g.nmat ? g.nmat : 1;
  if (nmat > lim.max_materials) { hata(out, "malzeme siniri asildi"); return false; }
  out->materials = arena.alloc_array_zeroed<ModelMaterial>(nmat);
  out->images = arena.alloc_array_zeroed<ModelImage>(1);
  if (!out->materials || !out->images) { hata(out, "arena dolu (malzeme)"); return false; }
  for (uint32_t i = 0; i < nmat; i++) {
    ModelMaterial &mm = out->materials[i];
    mm.base_color = g.mat_renk ? g.mat_renk[i] : opt.color;
    mm.image = -1;
    mm.metallic = 0.0f;
    mm.roughness = 0.8f;       // TUZAK 8u: alloc_array_zeroed yapici calistirmaz, varsayilan ELLE
    mm.has_pbr = true;
    mm.orm_image = mm.normal_image = mm.emissive_image = -1;
    mm.normal_scale = 1.0f;
  }
  out->material_count = nmat;
  out->image_count = 0;
  // 4) Malzeme başına bir mesh (yalnız üçgen alan malzemeler).
  uint32_t *say = static_cast<uint32_t *>(std::calloc(nmat, sizeof(uint32_t)));
  if (!say) { hata(out, "bellek"); return false; }
  for (uint32_t t = 0; t < g.ntri; t++) say[g.tri_mat ? std::min(g.tri_mat[t], nmat - 1) : 0]++;
  uint32_t nmesh = 0;
  for (uint32_t i = 0; i < nmat; i++) if (say[i]) nmesh++;
  if (nmesh > lim.max_meshes || nmesh > lim.max_instances) { std::free(say); hata(out, "mesh siniri asildi"); return false; }
  out->meshes = arena.alloc_array_zeroed<ModelMesh>(nmesh);
  out->instances = arena.alloc_array_zeroed<ModelInstance>(nmesh);
  if (!out->meshes || !out->instances) { std::free(say); hata(out, "arena dolu (mesh)"); return false; }
  uint32_t mi = 0;
  bool ilk = true;
  for (uint32_t m = 0; m < nmat; m++) {
    if (!say[m]) continue;
    ModelMesh &mm = out->meshes[mi];
    mm.vertex_count = say[m] * 3;
    mm.index_count = say[m] * 3;
    mm.material = (int32_t)m;
    mm.skin = -1;
    mm.verts = arena.alloc_array_zeroed<renderer::Vertex>(mm.vertex_count);
    mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
    if (!mm.verts || !mm.indices) { std::free(say); hata(out, "arena dolu (vertex)"); return false; }
    uint32_t k = 0;
    for (uint32_t t = 0; t < g.ntri; t++) {
      const uint32_t tm = g.tri_mat ? std::min(g.tri_mat[t], nmat - 1) : 0;
      if (tm != m) continue;
      for (uint32_t c = 0; c < 3; c++, k++) {
        const uint32_t src = t * 3 + c;
        mm.verts[k].pos = g.pos[src];
        mm.verts[k].nrm = g.nrm[src];
        mm.verts[k].uv = g.uv ? g.uv[src] : Vec2{0, 0};
        mm.indices[k] = k;
      }
    }
    if (lim.optimize && !mesh_optimize(arena, mm, &out->opt)) { std::free(say); hata(out, "arena dolu (meshopt)"); return false; }
    if (lim.lods) {
      const float ratios[kModelMaxLods] = {0.5f, 0.25f};
      if (!mesh_build_lods(arena, mm, ratios, kModelMaxLods, lim.lod_error)) { std::free(say); hata(out, "arena dolu (lod)"); return false; }
    }
    out->instances[mi].mesh = mi;
    out->instances[mi].world = Mat4::identity();
    for (uint32_t v = 0; v < mm.vertex_count; v++) {
      const Vec3 q = mm.verts[v].pos;
      if (ilk) { out->bounds_min = out->bounds_max = q; ilk = false; continue; }
      out->bounds_min = {std::min(out->bounds_min.x, q.x), std::min(out->bounds_min.y, q.y), std::min(out->bounds_min.z, q.z)};
      out->bounds_max = {std::max(out->bounds_max.x, q.x), std::max(out->bounds_max.y, q.y), std::max(out->bounds_max.z, q.z)};
    }
    mi++;
  }
  std::free(say);
  out->mesh_count = nmesh;
  out->instance_count = nmesh;
  return true;
}

// ---- STL ------------------------------------------------------------------------------------------------------------------
float f32_le(const unsigned char *p) {
  uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

bool stl_ascii(const unsigned char *d, size_t n, Giris &g, Model *m) {
  // "vertex x y z" üçlüleri; "facet normal" yok sayılır (yeniden hesaplanır). Tampon NUL'lu (oku_dosya / kopya).
  Buf<Vec3> v;
  const char *s = reinterpret_cast<const char *>(d);
  const char *son = s + n;
  while (s < son) {
    const char *k = std::strstr(s, "vertex");
    if (!k) break;
    char *e1 = nullptr, *e2 = nullptr, *e3 = nullptr;
    const float x = std::strtof(k + 6, &e1);
    const float y = std::strtof(e1, &e2);
    const float z = std::strtof(e2, &e3);
    if (e1 == k + 6 || e2 == e1 || e3 == e2) { hata(m, "STL (ASCII): bozuk 'vertex' satiri"); return false; }
    const Vec3 p{x, y, z};
    if (!finite3(p)) { hata(m, "STL: NaN/Inf koordinat"); return false; }
    if (v.n >= kMaxCorners || !v.push(p)) { hata(m, "STL: bellek ya da kose siniri"); return false; }
    s = e3;
  }
  if (v.n == 0 || v.n % 3 != 0) { hata(m, "STL (ASCII): 'vertex' sayisi 3'un kati degil (kesik dosya?)"); return false; }
  g.ntri = v.n / 3;
  g.pos = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * v.n));
  if (!g.pos) { hata(m, "bellek"); return false; }
  std::memcpy(g.pos, v.p, sizeof(Vec3) * v.n);
  return true;
}

bool stl_coz(const unsigned char *d, size_t n, Giris &g, Model *m) {
  if (n < 15) { hata(m, "STL cok kisa"); return false; }
  if (n >= 84) {
    const uint32_t cnt = (uint32_t)d[80] | ((uint32_t)d[81] << 8) | ((uint32_t)d[82] << 16) | ((uint32_t)d[83] << 24);
    if ((uint64_t)84 + (uint64_t)cnt * 50 == (uint64_t)n) {     // ikili: boyut sayaçla BİREBİR (aksi halde ASCII denenir)
      if (cnt == 0 || cnt > kMaxTris) { hata(m, cnt ? "STL: ucgen siniri asildi (en cok 4M)" : "STL: ucgen yok"); return false; }
      g.ntri = cnt;
      g.pos = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * cnt * 3));
      if (!g.pos) { hata(m, "bellek"); return false; }
      const unsigned char *p = d + 84;
      for (uint32_t t = 0; t < cnt; t++, p += 50)
        for (uint32_t c = 0; c < 3; c++) {
          const Vec3 q{f32_le(p + 12 + c * 12), f32_le(p + 16 + c * 12), f32_le(p + 20 + c * 12)};
          if (!finite3(q)) { hata(m, "STL: NaN/Inf koordinat"); return false; }
          g.pos[t * 3 + c] = q;
        }
      return true;
    }
  }
  // ASCII mi? "solid" ile başlar ve "facet"/"vertex" içerir.
  if (n >= 5 && std::strncmp(reinterpret_cast<const char *>(d), "solid", 5) == 0 && std::strstr(reinterpret_cast<const char *>(d), "vertex"))
    return stl_ascii(d, n, g, m);
  if (n >= 84) hata(m, "STL: ikili basliktaki ucgen sayisi dosya boyutuyla uyusmuyor (kesik ya da bozuk dosya)");
  else hata(m, "STL tanınmadi (ne ikili ne ASCII)");
  return false;
}

bool stl_isle(Arena &arena, const unsigned char *d, size_t n, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  Giris g;
  MeshImportOptions o = opt ? *opt : MeshImportOptions{};
  *out = Model{};
  const bool ok = stl_coz(d, n, g, out);
  bool tamam = false;
  if (ok) {
    g.nmat = 1;
    tamam = model_kur(arena, out, g, lim, o);
  }
  std::free(g.pos);
  std::free(g.nrm);
  return tamam;
}

// ---- OBJ ------------------------------------------------------------------------------------------------------------------
struct Ref { int32_t v, t, n; };   // 0-tabanlı, -1 yok

const char *atla_bosluk(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r') s++; return s; }

bool tam_say(const char *&s, int32_t *out) {
  s = atla_bosluk(s);
  char *e = nullptr;
  const long v = std::strtol(s, &e, 10);
  if (e == s) return false;
  *out = (int32_t)v;
  s = e;
  return true;
}

// "v", "v/t", "v//n", "v/t/n"; negatif indeks sayıma göre göreli.
bool yuz_koseleri(const char *&s, int32_t nv, int32_t nt, int32_t nn, Ref *r) {
  s = atla_bosluk(s);
  r->v = r->t = r->n = -1;
  int32_t a = 0;
  if (!tam_say(s, &a)) return false;
  r->v = a < 0 ? nv + a : a - 1;
  if (*s == '/') {
    s++;
    if (*s != '/') { int32_t b = 0; if (!tam_say(s, &b)) return false; r->t = b < 0 ? nt + b : b - 1; }
    if (*s == '/') { s++; int32_t c = 0; if (!tam_say(s, &c)) return false; r->n = c < 0 ? nn + c : c - 1; }
  }
  return r->v >= 0 && r->v < nv;
}

// Çokgen üçgenleme: Newell normaline dik düzleme izdusurüm + kulak kesme; başarısızsa yelpaze. idx: köşe numaraları (poly içinde), çıktı üçgen indeksleri.
void ucgenle(const Vec3 *p, uint32_t n, Buf<uint32_t> &cikti) {
  if (n < 3) return;
  if (n == 3) { cikti.push(0); cikti.push(1); cikti.push(2); return; }
  Vec3 nw{0, 0, 0};
  for (uint32_t i = 0; i < n; i++) {
    const Vec3 a = p[i], b = p[(i + 1) % n];
    nw.x += (a.y - b.y) * (a.z + b.z);
    nw.y += (a.z - b.z) * (a.x + b.x);
    nw.z += (a.x - b.x) * (a.y + b.y);
  }
  const float ax = std::fabs(nw.x), ay = std::fabs(nw.y), az = std::fabs(nw.z);
  const int dusur = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
  auto iz = [&](uint32_t i, float *u, float *v) {
    if (dusur == 0) { *u = p[i].y; *v = p[i].z; } else if (dusur == 1) { *u = p[i].z; *v = p[i].x; } else { *u = p[i].x; *v = p[i].y; }
  };
  const float isaret = (dusur == 0 ? nw.x : dusur == 1 ? nw.y : nw.z) >= 0 ? 1.0f : -1.0f;
  uint32_t *kal = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * n));
  if (!kal) { for (uint32_t i = 1; i + 1 < n; i++) { cikti.push(0); cikti.push(i); cikti.push(i + 1); } return; }
  for (uint32_t i = 0; i < n; i++) kal[i] = i;
  uint32_t m = n, guard = 0;
  while (m > 3 && guard < n * n) {
    bool kesti = false;
    for (uint32_t i = 0; i < m; i++, guard++) {
      const uint32_t a = kal[(i + m - 1) % m], b = kal[i], c = kal[(i + 1) % m];
      float au, av, bu, bv, cu, cv;
      iz(a, &au, &av); iz(b, &bu, &bv); iz(c, &cu, &cv);
      const float alan = ((bu - au) * (cv - av) - (bv - av) * (cu - au)) * isaret;
      if (alan <= 0) continue;                                   // içbükey köşe: kulak değil
      bool icerde = false;
      for (uint32_t j = 0; j < m && !icerde; j++) {
        const uint32_t q = kal[j];
        if (q == a || q == b || q == c) continue;
        float qu, qv; iz(q, &qu, &qv);
        const float d1 = ((bu - au) * (qv - av) - (bv - av) * (qu - au)) * isaret;
        const float d2 = ((cu - bu) * (qv - bv) - (cv - bv) * (qu - bu)) * isaret;
        const float d3 = ((au - cu) * (qv - cv) - (av - cv) * (qu - cu)) * isaret;
        if (d1 >= 0 && d2 >= 0 && d3 >= 0) icerde = true;
      }
      if (icerde) continue;
      cikti.push(a); cikti.push(b); cikti.push(c);
      for (uint32_t j = i; j + 1 < m; j++) kal[j] = kal[j + 1];
      m--;
      kesti = true;
      break;
    }
    if (!kesti) break;                                           // dejenere/kendini kesen: yelpazeye dusur
  }
  if (m == 3) { cikti.push(kal[0]); cikti.push(kal[1]); cikti.push(kal[2]); }
  else for (uint32_t i = 1; i + 1 < m; i++) { cikti.push(kal[0]); cikti.push(kal[i]); cikti.push(kal[i + 1]); }
  std::free(kal);
}

struct Mtl { char ad[64]; Vec3 kd; };

void mtl_oku(const char *obj_yolu, const char *ad, Buf<Mtl> &liste, uint32_t *harita_say) {
  char yol[1024];
  std::snprintf(yol, sizeof yol, "%s", obj_yolu);
  char *sl = std::strrchr(yol, '/');
  if (sl) sl[1] = 0; else yol[0] = 0;
  std::snprintf(yol + std::strlen(yol), sizeof yol - std::strlen(yol), "%s", ad);
  size_t n = 0;
  unsigned char *b = oku_dosya(yol, &n);
  if (!b) return;
  char *s = reinterpret_cast<char *>(b);
  Mtl *cur = nullptr;
  for (char *ln = sonraki_satir(s); ln; ln = sonraki_satir(s)) {
    const char *q = atla_bosluk(ln);
    if (!std::strncmp(q, "newmtl ", 7)) {
      Mtl m{};
      std::snprintf(m.ad, sizeof m.ad, "%s", atla_bosluk(q + 7));
      for (char *e = m.ad; *e; e++) if (*e == '\r' || *e == '\n') *e = 0;
      m.kd = Vec3{0.8f, 0.8f, 0.85f};
      liste.push(m);
      cur = &liste.p[liste.n - 1];
    } else if (cur && !std::strncmp(q, "Kd ", 3)) {
      char *e1, *e2, *e3;
      const float r = std::strtof(q + 3, &e1), g = std::strtof(e1, &e2), bl = std::strtof(e2, &e3);
      if (e3 != e2) cur->kd = Vec3{r, g, bl};
    } else if (!std::strncmp(q, "map_Kd", 6) || !std::strncmp(q, "map_Ka", 6) || !std::strncmp(q, "map_Bump", 8)) {
      (*harita_say)++;
    }
  }
  std::free(b);
}

bool obj_isle(Arena &arena, const char *path, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  *out = Model{};
  size_t n = 0;
  unsigned char *b = oku_dosya(path, &n);
  if (!b) { hata(out, "OBJ dosyasi acilamadi", path); return false; }
  MeshImportOptions o = opt ? *opt : MeshImportOptions{};
  if (!opt) { o.z_up = false; o.fit_size = 0.0f; o.scale = 1.0f; o.ground_pivot = false; }   // OBJ varsayılanı: dosya birimi ve orijin korunur
  Buf<Vec3> V, N;
  Buf<Vec2> T;
  Buf<Vec3> pos, nrm;
  Buf<Vec2> uv;
  Buf<uint32_t> tmat;
  Buf<Mtl> mtls;
  uint32_t harita_say = 0, cur_mat = 0;
  bool normal_var = true, uv_var = true, ok = true;
  char *s = reinterpret_cast<char *>(b);
  uint32_t satir = 0;
  Buf<Ref> poly;
  Buf<uint32_t> tri_i;
  Buf<Vec3> poly_p;
  for (char *ln = sonraki_satir(s); ln && ok; ln = sonraki_satir(s)) {
    satir++;
    const char *q = atla_bosluk(ln);
    if (q[0] == 'v' && q[1] == ' ') {
      char *e1, *e2, *e3;
      const float x = std::strtof(q + 2, &e1), y = std::strtof(e1, &e2), z = std::strtof(e2, &e3);
      if (e1 == q + 2 || e2 == e1 || e3 == e2 || !finite3(Vec3{x, y, z})) { char d[48]; std::snprintf(d, sizeof d, "satir %u", satir); hata(out, "OBJ: bozuk 'v' satiri", d); ok = false; break; }
      ok = V.push(Vec3{x, y, z});
    } else if (q[0] == 'v' && q[1] == 'n' && q[2] == ' ') {
      char *e1, *e2, *e3;
      const float x = std::strtof(q + 3, &e1), y = std::strtof(e1, &e2), z = std::strtof(e2, &e3);
      if (e3 == e2 || !finite3(Vec3{x, y, z})) { char d[48]; std::snprintf(d, sizeof d, "satir %u", satir); hata(out, "OBJ: bozuk 'vn' satiri", d); ok = false; break; }
      ok = N.push(Vec3{x, y, z});
    } else if (q[0] == 'v' && q[1] == 't' && q[2] == ' ') {
      char *e1, *e2;
      const float u = std::strtof(q + 3, &e1), v = std::strtof(e1, &e2);
      ok = T.push(Vec2{u, e2 != e1 ? v : 0.0f});
    } else if (q[0] == 'f' && q[1] == ' ') {
      poly.n = 0;
      const char *p = q + 2;
      Ref r;
      while (*atla_bosluk(p) && *atla_bosluk(p) != '#') {
        if (!yuz_koseleri(p, (int32_t)V.n, (int32_t)T.n, (int32_t)N.n, &r)) { char d[48]; std::snprintf(d, sizeof d, "satir %u", satir); hata(out, "OBJ: gecersiz yuz indeksi", d); ok = false; break; }
        if (!poly.push(r)) { ok = false; break; }
      }
      if (!ok) break;
      if (poly.n < 3) continue;
      poly_p.n = 0;
      for (uint32_t i = 0; i < poly.n; i++) poly_p.push(V.p[poly.p[i].v]);
      tri_i.n = 0;
      ucgenle(poly_p.p, poly.n, tri_i);
      for (uint32_t k = 0; k + 2 < tri_i.n + 0 && ok; k += 3) {
        if (pos.n / 3 >= kMaxTris) { hata(out, "ucgen siniri asildi (en cok 4M)"); ok = false; break; }
        for (uint32_t c = 0; c < 3; c++) {
          const Ref &rr = poly.p[tri_i.p[k + c]];
          pos.push(V.p[rr.v]);
          if (rr.n >= 0 && rr.n < (int32_t)N.n) nrm.push(N.p[rr.n]); else { normal_var = false; nrm.push(Vec3{0, 1, 0}); }
          if (rr.t >= 0 && rr.t < (int32_t)T.n) uv.push(T.p[rr.t]); else { uv_var = false; uv.push(Vec2{0, 0}); }
        }
        ok = tmat.push(cur_mat);
      }
    } else if (!std::strncmp(q, "mtllib ", 7)) {
      char ad[256];
      std::snprintf(ad, sizeof ad, "%s", atla_bosluk(q + 7));
      for (char *e = ad; *e; e++) if (*e == '\r' || *e == ' ') { *e = 0; break; }
      mtl_oku(path, ad, mtls, &harita_say);
    } else if (!std::strncmp(q, "usemtl ", 7)) {
      char ad[64];
      std::snprintf(ad, sizeof ad, "%s", atla_bosluk(q + 7));
      for (char *e = ad; *e; e++) if (*e == '\r') *e = 0;
      cur_mat = 0;
      for (uint32_t i = 0; i < mtls.n; i++) if (!std::strcmp(mtls.p[i].ad, ad)) { cur_mat = i; break; }
    }
  }
  std::free(b);
  if (!ok) { if (!out->error[0]) hata(out, "OBJ: bellek"); return false; }
  Giris g;
  g.ntri = pos.n / 3;
  g.pos = pos.p; g.uv = uv_var ? uv.p : nullptr; g.tri_mat = tmat.p;
  Buf<Vec3> renkler;
  uint32_t nmat = mtls.n ? mtls.n : 1;
  for (uint32_t i = 0; i < nmat; i++) renkler.push(mtls.n ? mtls.p[i].kd : o.color);
  g.nmat = nmat; g.mat_renk = renkler.p;
  if (normal_var) g.nrm = nrm.p;     // dosya normalleri; model_kur dönüşümleri bunlara da uygular
  // model_kur g.pos/g.nrm'i yerinde değiştirir ve gerekirse g.nrm'i malloc ile YENİDEN atar: Buf sahipliği için kopya kullanma.
  Vec3 *nrm_ilk = g.nrm;
  const bool tamam = model_kur(arena, out, g, lim, o);
  if (g.nrm != nrm_ilk) std::free(g.nrm);      // hesaplanan normal bizim; Buf'unkini Buf serbest bırakır
  if (tamam && harita_say) {
    char d[80];
    std::snprintf(d, sizeof d, "%u doku haritasi (map_*) bu surumde YOK, yalniz Kd rengi kullanildi", harita_say);
    std::snprintf(out->note, sizeof out->note, "%s", d);   // başarı + bilgi notu (konsol özeti okur)
  }
  return tamam;
}


// ---- PLY (2026-11-22) -----------------------------------------------------------------------------------------------------
// Stanford PLY: ascii / binary_little_endian / binary_big_endian 1.0. Okunan: vertex x y z, nx ny nz, UV (s t | u v | texture_u
// texture_v), renk red green blue (uchar ya da float 0..1); face vertex_indices | vertex_index (liste; cokgen OBJ'deki gibi
// uclenir). Bilinmeyen eleman/ozellik ATLANIR (ikilide boyutu hesaplanir). Kose rengi: Vertex'te renk kanali yok -> yuz rengi
// (3 kosenin ortalamasi) malzeme paletine iner; ayrik renk sayisi tavani (32) asarsa kanal basina bit azaltilarak nicemlenir ve
// not'a yazilir (MagicaVoxel PLY'si birkac renkle tam dogru gelir).
enum PlyTip : uint8_t { PT_YOK, PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_F32, PT_F64 };
PlyTip ply_tip(const char *s) {
  struct { const char *a; PlyTip t; } kt[] = {{"char", PT_I8},   {"int8", PT_I8},     {"uchar", PT_U8},   {"uint8", PT_U8},
                                               {"short", PT_I16}, {"int16", PT_I16},  {"ushort", PT_U16}, {"uint16", PT_U16},
                                               {"int", PT_I32},   {"int32", PT_I32},  {"uint", PT_U32},   {"uint32", PT_U32},
                                               {"float", PT_F32}, {"float32", PT_F32}, {"double", PT_F64}, {"float64", PT_F64}};
  for (const auto &k : kt) if (!std::strcmp(s, k.a)) return k.t;
  return PT_YOK;
}
uint32_t ply_boy(PlyTip t) { return t == PT_I8 || t == PT_U8 ? 1 : t == PT_I16 || t == PT_U16 ? 2 : t == PT_F64 ? 8 : 4; }
struct PlyOz {
  char ad[32];
  PlyTip tip = PT_YOK, say_tip = PT_YOK;   // say_tip != YOK: liste
};
struct PlyEl {
  char ad[32];
  uint32_t n = 0;
  PlyOz oz[24];
  uint32_t oz_n = 0;
};
// Ikili/ASCII okuyucu: tek deger (double). false: veri bitti / bozuk.
struct PlyOkur {
  const unsigned char *p, *son;
  int bicim;   // 0 ascii, 1 LE, 2 BE
  bool deger(PlyTip t, double *out) {
    if (bicim == 0) {
      while (p < son && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
      if (p >= son) return false;
      char *e = nullptr;
      *out = std::strtod(reinterpret_cast<const char *>(p), &e);
      if (reinterpret_cast<const unsigned char *>(e) == p) return false;
      p = reinterpret_cast<const unsigned char *>(e);
      return true;
    }
    const uint32_t b = ply_boy(t);
    if ((size_t)(son - p) < b) return false;
    unsigned char x[8];
    for (uint32_t i = 0; i < b; i++) x[i] = bicim == 1 ? p[i] : p[b - 1 - i];
    p += b;
    switch (t) {
      case PT_I8: *out = (int8_t)x[0]; break;
      case PT_U8: *out = x[0]; break;
      case PT_I16: { int16_t v; std::memcpy(&v, x, 2); *out = v; break; }
      case PT_U16: { uint16_t v; std::memcpy(&v, x, 2); *out = v; break; }
      case PT_I32: { int32_t v; std::memcpy(&v, x, 4); *out = v; break; }
      case PT_U32: { uint32_t v; std::memcpy(&v, x, 4); *out = v; break; }
      case PT_F32: { float v; std::memcpy(&v, x, 4); *out = v; break; }
      case PT_F64: { double v; std::memcpy(&v, x, 8); *out = v; break; }
      default: return false;
    }
    return true;
  }
};

bool ply_isle(Arena &arena, const char *path, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  MeshImportOptions o = opt ? *opt : MeshImportOptions{};
  if (!opt) { o.z_up = false; o.fit_size = 0.0f; }   // OBJ gibi: dosya ekseni/olcegi korunur (.ayar degistirir)
  *out = Model{};
  size_t n = 0;
  unsigned char *b = oku_dosya(path, &n);
  if (!b) { hata(out, "PLY dosyasi acilamadi", path); return false; }
  struct Serbest { unsigned char *b; ~Serbest() { std::free(b); } } serbest{b};
  if (n < 10 || std::strncmp(reinterpret_cast<const char *>(b), "ply", 3) != 0 || (b[3] != '\n' && b[3] != '\r')) { hata(out, "PLY: 'ply' basligi yok"); return false; }
  // --- baslik ---
  PlyEl el[8];
  uint32_t el_n = 0;
  int bicim = -1;
  const char *bas_son = std::strstr(reinterpret_cast<const char *>(b), "end_header");
  if (!bas_son) { hata(out, "PLY: 'end_header' yok (kesik baslik)"); return false; }
  const unsigned char *veri = reinterpret_cast<const unsigned char *>(bas_son) + 10;
  while (veri < b + n && *veri == '\r') veri++;
  if (veri < b + n && *veri == '\n') veri++;
  {
    char satir[256];
    const char *q = reinterpret_cast<const char *>(b);
    while (q < bas_son) {
      const char *e = q;
      while (e < bas_son && *e != '\n') e++;
      const size_t l = std::min<size_t>((size_t)(e - q), sizeof satir - 1);
      std::memcpy(satir, q, l);
      satir[l] = 0;
      q = e < bas_son ? e + 1 : e;
      for (char *c = satir; *c; c++) if (*c == '\r') *c = 0;
      char a[32] = {0}, b1[32] = {0}, c1[32] = {0}, d1[32] = {0}, e1[32] = {0};
      const int k = std::sscanf(satir, "%31s %31s %31s %31s %31s", a, b1, c1, d1, e1);
      if (k <= 0) continue;
      if (!std::strcmp(a, "format")) {
        bicim = !std::strcmp(b1, "ascii") ? 0 : !std::strcmp(b1, "binary_little_endian") ? 1 : !std::strcmp(b1, "binary_big_endian") ? 2 : -2;
      } else if (!std::strcmp(a, "element") && k >= 3) {
        if (el_n >= 8) { hata(out, "PLY: 8'den cok eleman"); return false; }
        PlyEl &x = el[el_n++];
        x = PlyEl{};
        std::snprintf(x.ad, sizeof x.ad, "%s", b1);
        const long long say = std::atoll(c1);
        if (say < 0 || say > (long long)kMaxCorners) { hata(out, "PLY: eleman sayisi gecersiz ya da cok buyuk"); return false; }
        x.n = (uint32_t)say;
      } else if (!std::strcmp(a, "property") && el_n) {
        PlyEl &x = el[el_n - 1];
        if (x.oz_n >= 24) { hata(out, "PLY: elemanda 24'ten cok ozellik"); return false; }
        PlyOz &z = x.oz[x.oz_n++];
        z = PlyOz{};
        if (!std::strcmp(b1, "list") && k >= 5) { z.say_tip = ply_tip(c1); z.tip = ply_tip(d1); std::snprintf(z.ad, sizeof z.ad, "%s", e1); }
        else { z.tip = ply_tip(b1); std::snprintf(z.ad, sizeof z.ad, "%s", c1); }
        if (z.tip == PT_YOK || (!std::strcmp(b1, "list") && z.say_tip == PT_YOK)) { hata(out, "PLY: bilinmeyen ozellik tipi", satir); return false; }
      }
    }
  }
  if (bicim < 0) { hata(out, bicim == -2 ? "PLY: bilinmeyen bicim" : "PLY: 'format' satiri yok"); return false; }
  // --- veri ---
  PlyOkur rd{veri, b + n, bicim};
  Buf<Vec3> V, VN, VC;
  Buf<Vec2> VT;
  bool normal_var = false, uv_var = false, renk_var = false;
  Buf<Vec3> pos, nrm;
  Buf<Vec2> uv;
  Buf<Vec3> yuz_renk;
  Buf<uint32_t> ucg;
  bool yuz_goruldu = false;
  Buf<uint32_t> liste, t;   // satir basina yeniden kullanilir (yuz basina malloc yok)
  for (uint32_t e = 0; e < el_n; e++) {
    PlyEl &x = el[e];
    const bool kose = !std::strcmp(x.ad, "vertex"), yuz = !std::strcmp(x.ad, "face");
    int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1, iu = -1, iv = -1, ir = -1, ig = -1, ib = -1, il = -1;
    for (uint32_t k = 0; k < x.oz_n; k++) {
      const char *a = x.oz[k].ad;
      if (!std::strcmp(a, "x")) ix = (int)k; else if (!std::strcmp(a, "y")) iy = (int)k; else if (!std::strcmp(a, "z")) iz = (int)k;
      else if (!std::strcmp(a, "nx")) inx = (int)k; else if (!std::strcmp(a, "ny")) iny = (int)k; else if (!std::strcmp(a, "nz")) inz = (int)k;
      else if (!std::strcmp(a, "s") || !std::strcmp(a, "u") || !std::strcmp(a, "texture_u")) iu = (int)k;
      else if (!std::strcmp(a, "t") || !std::strcmp(a, "v") || !std::strcmp(a, "texture_v")) iv = (int)k;
      else if (!std::strcmp(a, "red") || !std::strcmp(a, "r")) ir = (int)k;
      else if (!std::strcmp(a, "green") || !std::strcmp(a, "g")) ig = (int)k;
      else if (!std::strcmp(a, "blue") || !std::strcmp(a, "b")) ib = (int)k;
      else if (x.oz[k].say_tip != PT_YOK && (!std::strcmp(a, "vertex_indices") || !std::strcmp(a, "vertex_index"))) il = (int)k;
    }
    if (kose) {
      if (ix < 0 || iy < 0 || iz < 0) { hata(out, "PLY: vertex x/y/z yok"); return false; }
      normal_var = inx >= 0 && iny >= 0 && inz >= 0;
      uv_var = iu >= 0 && iv >= 0;
      renk_var = ir >= 0 && ig >= 0 && ib >= 0;
    }
    if (yuz) { if (il < 0) { hata(out, "PLY: face vertex_indices listesi yok"); return false; } yuz_goruldu = true; }
    double d[24];
    for (uint32_t i = 0; i < x.n; i++) {
      liste.n = 0;
      for (uint32_t k = 0; k < x.oz_n; k++) {
        const PlyOz &z = x.oz[k];
        if (z.say_tip == PT_YOK) { if (!rd.deger(z.tip, &d[k])) { hata(out, "PLY: veri beklenenden kisa (kesik dosya?)"); return false; } continue; }
        double c = 0;
        if (!rd.deger(z.say_tip, &c) || c < 0 || c > 4096) { hata(out, "PLY: liste uzunlugu gecersiz (kesik dosya?)"); return false; }
        for (uint32_t m = 0; m < (uint32_t)c; m++) {
          double v = 0;
          if (!rd.deger(z.tip, &v)) { hata(out, "PLY: veri beklenenden kisa (kesik dosya?)"); return false; }
          if ((int)k == il && (!liste.push(v < 0 ? 0xFFFFFFFFu : (uint32_t)v))) { hata(out, "bellek"); return false; }
        }
      }
      if (kose) {
        const Vec3 p{(float)d[ix], (float)d[iy], (float)d[iz]};
        if (!finite3(p)) { hata(out, "PLY: NaN/Inf koordinat"); return false; }
        bool ok = V.push(p);
        if (normal_var) ok = ok && VN.push(Vec3{(float)d[inx], (float)d[iny], (float)d[inz]});
        if (uv_var) ok = ok && VT.push(Vec2{(float)d[iu], 1.0f - (float)d[iv]});   // PLY UV'si alttan (OBJ gibi)
        if (renk_var) {
          const float bol = x.oz[ir].tip == PT_F32 || x.oz[ir].tip == PT_F64 ? 1.0f : x.oz[ir].tip == PT_U16 ? 65535.0f : 255.0f;
          ok = ok && VC.push(Vec3{(float)d[ir] / bol, (float)d[ig] / bol, (float)d[ib] / bol});
        }
        if (!ok) { hata(out, "bellek"); return false; }
      } else if (yuz) {
        if (liste.n < 3) continue;
        if (V.n == 0) { hata(out, "PLY: yuz koselerden once"); return false; }
        Vec3 cp[256];
        const uint32_t ln = std::min<uint32_t>(liste.n, 256);
        for (uint32_t m = 0; m < ln; m++) {
          if (liste.p[m] >= V.n) { hata(out, "PLY: yuz kose indeksi sinir disi"); return false; }
          cp[m] = V.p[liste.p[m]];
        }
        t.n = 0;
        ucgenle(cp, ln, t);
        for (uint32_t m = 0; m + 2 < t.n; m += 3) {
          if (pos.n / 3 >= kMaxTris) { hata(out, "PLY: ucgen siniri asildi (en cok 4M)"); return false; }
          Vec3 rk{0, 0, 0};
          for (uint32_t c = 0; c < 3; c++) {
            const uint32_t vi = liste.p[t.p[m + c]];
            bool ok = pos.push(V.p[vi]);
            if (normal_var) ok = ok && nrm.push(VN.p[vi]);
            if (uv_var) ok = ok && uv.push(VT.p[vi]);
            if (renk_var) rk = rk + VC.p[vi] * (1.0f / 3.0f);
            if (!ok) { hata(out, "bellek"); return false; }
          }
          if (renk_var && !yuz_renk.push(rk)) { hata(out, "bellek"); return false; }
        }
      }
    }
  }
  if (!pos.n) { hata(out, yuz_goruldu ? "PLY: ucgen yok" : "PLY: face elemani yok (nokta bulutu desteklenmiyor)"); return false; }
  Giris g;
  g.ntri = pos.n / 3;
  g.pos = pos.p;
  g.uv = uv_var ? uv.p : nullptr;
  // Renk paleti: ayrik yuz renkleri (8 bit), tavani asarsa kanal basina bit azalt
  const uint32_t tavan = std::min<uint32_t>(lim.max_materials ? lim.max_materials : 1, 32);
  Buf<uint32_t> tmat;
  Buf<Vec3> palet;
  uint32_t bit = 8;
  if (renk_var) {
    for (; bit >= 1; bit--) {
      palet.n = 0;
      tmat.n = 0;
      uint32_t anahtar[32];
      bool sigdi = true;
      for (uint32_t t = 0; t < g.ntri && sigdi; t++) {
        const Vec3 c = yuz_renk.p[t];
        auto kn = [&](float f) { const uint32_t v = (uint32_t)std::lround(std::fmin(1.0f, std::fmax(0.0f, f)) * 255.0f); return v >> (8 - bit); };
        const uint32_t key = (kn(c.x) << 16) | (kn(c.y) << 8) | kn(c.z);
        uint32_t m = 0;
        while (m < palet.n && anahtar[m] != key) m++;
        if (m == palet.n) {
          if (palet.n >= tavan) { sigdi = false; break; }
          anahtar[palet.n] = key;
          const float olc = 255.0f / (float)((1u << bit) - 1) / 255.0f;
          if (!palet.push(Vec3{(float)(key >> 16) * olc, (float)((key >> 8) & 0xFF) * olc, (float)(key & 0xFF) * olc})) { hata(out, "bellek"); return false; }
        }
        if (!tmat.push(m)) { hata(out, "bellek"); return false; }
      }
      if (sigdi) break;
    }
    if (bit < 1) bit = 1;
    g.tri_mat = tmat.p;
    g.nmat = palet.n ? palet.n : 1;
    g.mat_renk = palet.p;
  }
  if (normal_var) g.nrm = nrm.p;
  Vec3 *nrm_ilk = g.nrm;
  const bool tamam = model_kur(arena, out, g, lim, o);
  if (g.nrm != nrm_ilk) std::free(g.nrm);
  if (tamam && renk_var)
    std::snprintf(out->note, sizeof out->note, "k\xC3\xB6\xC5\x9F" "e rengi %u malzemeye indi (%s)", palet.n, bit == 8 ? "tam" : "renkler nicemlendi");
  return tamam;
}

} // namespace

bool stl_load_memory(Arena &arena, const unsigned char *data, size_t size, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  // ASCII ayrıştırma NUL'lu tampon ister: kopya.
  unsigned char *kopya = static_cast<unsigned char *>(std::malloc(size + 1));
  if (!kopya) { *out = Model{}; hata(out, "bellek"); return false; }
  std::memcpy(kopya, data, size);
  kopya[size] = 0;
  const bool r = stl_isle(arena, kopya, size, out, lim, opt);
  std::free(kopya);
  return r;
}

bool stl_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  *out = Model{};
  size_t n = 0;
  unsigned char *b = oku_dosya(path, &n);
  if (!b) { hata(out, "STL dosyasi acilamadi", path); return false; }
  const bool r = stl_isle(arena, b, n, out, lim, opt);
  std::free(b);
  return r;
}

bool obj_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  return obj_isle(arena, path, out, lim, opt);
}

bool ply_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim, const MeshImportOptions *opt) {
  return ply_isle(arena, path, out, lim, opt);
}

bool mesh_from_triangles(Arena &arena, const Vec3 *pos, const Vec2 *uv, uint32_t nv, const uint32_t *idx, uint32_t ntri, Vec3 color, Model *out,
                         const GltfLimits &lim, float smooth_angle_deg) {
  *out = Model{};
  if (!pos || !idx || ntri == 0 || nv == 0) { hata(out, "bos mesh (kose ya da ucgen yok)"); return false; }
  if (ntri > kMaxTris) { hata(out, "ucgen siniri asildi (en cok 4M)"); return false; }
  const uint32_t nc = ntri * 3;
  Giris g;
  g.ntri = ntri;
  g.pos = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * nc));
  g.uv = uv ? static_cast<Vec2 *>(std::malloc(sizeof(Vec2) * nc)) : nullptr;
  bool tamam = g.pos && (!uv || g.uv);
  for (uint32_t i = 0; tamam && i < nc; i++) {
    if (idx[i] >= nv) { hata(out, "ucgen indeksi kose sayisini asiyor"); tamam = false; break; }
    if (!finite3(pos[idx[i]])) { hata(out, "kose konumu sonlu degil (NaN/Inf)"); tamam = false; break; }
    g.pos[i] = pos[idx[i]];
    if (g.uv) g.uv[i] = uv[idx[i]];
  }
  if (!g.pos || (uv && !g.uv)) hata(out, "bellek");
  bool ok = false;
  if (tamam) {
    MeshImportOptions o;
    o.z_up = false; o.fit_size = 0.0f; o.scale = 1.0f; o.ground_pivot = false; o.smooth_angle_deg = smooth_angle_deg; o.color = color;
    ok = model_kur(arena, out, g, lim, o);
  }
  std::free(g.pos); std::free(g.uv); std::free(g.nrm);
  return ok;
}

uint32_t polygon_triangulate_xz(const float *xz, uint32_t n, uint32_t *tri_out) {
  if (!xz || !tri_out || n < 3 || n > 256) return 0;
  Vec3 *p = static_cast<Vec3 *>(std::malloc(sizeof(Vec3) * n));
  if (!p) return 0;
  float alan2 = 0;                                   // işaretli alan x2 (Newell y): + ise normal +Y
  for (uint32_t i = 0; i < n; i++) {
    if (!std::isfinite(xz[2 * i]) || !std::isfinite(xz[2 * i + 1])) { std::free(p); return 0; }
    p[i] = Vec3{xz[2 * i], 0, xz[2 * i + 1]};
  }
  for (uint32_t i = 0; i < n; i++) {
    const Vec3 a = p[i], b = p[(i + 1) % n];
    alan2 += (a.z - b.z) * (a.x + b.x);
  }
  if (std::fabs(alan2) < 1e-12f) { std::free(p); return 0; }
  Buf<uint32_t> t;
  ucgenle(p, n, t);
  std::free(p);
  if (t.n != 3 * (n - 2)) return 0;
  for (uint32_t i = 0; i < t.n; i += 3) {
    uint32_t a = t.p[i], b = t.p[i + 1], c = t.p[i + 2];
    if (alan2 < 0) std::swap(b, c);                  // -Y'ye bakıyordu: +Y'ye çevir
    tri_out[i] = a; tri_out[i + 1] = b; tri_out[i + 2] = c;
  }
  return n - 2;
}

} // namespace tulpar::engine::content
