#include "content/gltf.hpp"
#include "content/cluster_dag.hpp"
#include "content/meshopt.hpp"
#include "content/mesh_import.hpp"
#include "content/model_import.hpp"
#include "content/ktx2.hpp"
#include "platform/fs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cgltf.h>    // govdeler: content/vendored_impl.c
#define STBI_NO_STDIO
#include <stb_image.h>

namespace tulpar::engine::content {

namespace {
constexpr uint32_t kSkinLodMinTris = 1500;   // A7: iskeletli LOD yalniz bu ucgen sayisindan buyuk mesh'lere
void set_error(Model *m, const char *what, const char *detail) {
  std::snprintf(m->error, sizeof m->error, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

// Dosyayi tumuyle oku (dis .bin ve dis goruntu icin). malloc: yukleme ani.
unsigned char *read_file(const char *path, size_t *size) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) { std::fclose(f); return nullptr; }
  unsigned char *buf = static_cast<unsigned char *>(std::malloc((size_t)n));
  if (!buf) { std::fclose(f); return nullptr; }
  size_t got = std::fread(buf, 1, (size_t)n, f);
  std::fclose(f);
  if (got != (size_t)n) { std::free(buf); return nullptr; }
  *size = got;
  return buf;
}

// "data:...;base64,XXXX" -> cozulmus bayt (cgltf'nin base64'u)
unsigned char *decode_data_uri(const char *uri, size_t *size) {
  const char *comma = std::strchr(uri, ',');
  if (!comma || std::strncmp(uri, "data:", 5) != 0) return nullptr;
  const char *b64 = comma + 1;
  size_t len = std::strlen(b64);
  size_t decoded = len / 4 * 3;
  if (len >= 1 && b64[len - 1] == '=') decoded--;
  if (len >= 2 && b64[len - 2] == '=') decoded--;
  cgltf_options opt{};
  void *out = nullptr;
  if (cgltf_load_buffer_base64(&opt, decoded, b64, &out) != cgltf_result_success) return nullptr;
  *size = decoded;
  return static_cast<unsigned char *>(out);
}

// Goruntuyu coz: data URI, dis dosya (gltf dizinine gore) ya da bufferView (GLB).
bool load_image(const cgltf_image *img, const char *gltf_dir, Arena &arena, ModelImage *out) {
  unsigned char *encoded = nullptr;
  size_t enc_size = 0;
  bool owned = false;
  if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
    encoded = static_cast<unsigned char *>(img->buffer_view->buffer->data) + img->buffer_view->offset;
    enc_size = img->buffer_view->size;
  } else if (img->uri && std::strncmp(img->uri, "data:", 5) == 0) {
    encoded = decode_data_uri(img->uri, &enc_size);
    owned = encoded != nullptr;
  } else if (img->uri) {
    char path[1024];
    std::snprintf(path, sizeof path, "%s%s", gltf_dir, img->uri);
    encoded = read_file(path, &enc_size);
    owned = encoded != nullptr;
  }
  if (!encoded) return false;
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(encoded, (int)enc_size, &w, &h, &comp, 4);
  if (owned) std::free(encoded);
  if (!px || w <= 0 || h <= 0) return false;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->rgba = arena.alloc_array<uint8_t>((size_t)w * h * 4);
  if (out->rgba) std::memcpy(out->rgba, px, (size_t)w * h * 4);
  stbi_image_free(px);
  return out->rgba != nullptr;
}

const cgltf_accessor *find_attr(const cgltf_primitive &p, cgltf_attribute_type t) {
  for (cgltf_size i = 0; i < p.attributes_count; i++)
    if (p.attributes[i].type == t) return p.attributes[i].data;
  return nullptr;
}

// UV KANALI (2026-11-16, Fab "Mike": Unreal donusumu TEXCOORD_0'i TAMAMEN SIFIR yazmis, gercek UV TEXCOORD_1'de; malzeme kanal 0'i
// istiyor -> her kose dokunun tek pikselini ornekliyor, model duz renk). Once malzemenin renk dokusunun istedigi kanal (glTF
// textureInfo.texCoord); o kanal dejenereyse (butun koseler ayni deger) ve dejenere olmayan baska kanal varsa o — sayilir, not'a yazilir.
bool uv_dejenere(const cgltf_accessor *a) {
  if (!a || a->count < 2) return false;
  float f0[2] = {0, 0};
  cgltf_accessor_read_float(a, 0, f0, 2);
  for (cgltf_size i = 1; i < a->count; i++) {
    float f[2] = {0, 0};
    cgltf_accessor_read_float(a, i, f, 2);
    if (f[0] != f0[0] || f[1] != f0[1]) return false;
  }
  return true;
}
const cgltf_accessor *uv_kanali(const cgltf_primitive &p, uint32_t *degisti) {
  int istenen = 0;
  if (p.material && p.material->has_pbr_metallic_roughness && p.material->pbr_metallic_roughness.base_color_texture.texture)
    istenen = p.material->pbr_metallic_roughness.base_color_texture.texcoord;
  const cgltf_accessor *sec = nullptr, *ilk = nullptr;
  for (cgltf_size i = 0; i < p.attributes_count; i++) {
    if (p.attributes[i].type != cgltf_attribute_type_texcoord) continue;
    if (!ilk) ilk = p.attributes[i].data;
    if (p.attributes[i].index == istenen) sec = p.attributes[i].data;
  }
  if (!sec) sec = ilk;
  if (!sec || !uv_dejenere(sec)) return sec;
  for (cgltf_size i = 0; i < p.attributes_count; i++)
    if (p.attributes[i].type == cgltf_attribute_type_texcoord && p.attributes[i].data != sec && !uv_dejenere(p.attributes[i].data)) {
      if (degisti) (*degisti)++;
      return p.attributes[i].data;
    }
  return sec;
}

// Dugumun yerel TRS'si (matrix verilmisse ayristirilir: olcek sutun boyu,
// donus normalize 3x3'ten quaternion).
Quat quat_from_cols(const float c0[3], const float c1[3], const float c2[3]) {
  const float m00 = c0[0], m01 = c1[0], m02 = c2[0];
  const float m10 = c0[1], m11 = c1[1], m12 = c2[1];
  const float m20 = c0[2], m21 = c1[2], m22 = c2[2];
  const float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0.0f) {
    const float sq = std::sqrt(tr + 1.0f) * 2.0f;
    q.w = 0.25f * sq; q.x = (m21 - m12) / sq; q.y = (m02 - m20) / sq; q.z = (m10 - m01) / sq;
  } else if (m00 > m11 && m00 > m22) {
    const float sq = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q.w = (m21 - m12) / sq; q.x = 0.25f * sq; q.y = (m01 + m10) / sq; q.z = (m02 + m20) / sq;
  } else if (m11 > m22) {
    const float sq = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q.w = (m02 - m20) / sq; q.x = (m01 + m10) / sq; q.y = 0.25f * sq; q.z = (m12 + m21) / sq;
  } else {
    const float sq = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q.w = (m10 - m01) / sq; q.x = (m02 + m20) / sq; q.y = (m12 + m21) / sq; q.z = 0.25f * sq;
  }
  return normalize(q);
}
void node_local_trs(const cgltf_node &n, Vec3 *t, Quat *r, Vec3 *sc) {
  *t = {0, 0, 0}; *r = Quat{}; *sc = {1, 1, 1};
  if (n.has_matrix) {
    const float *m = n.matrix;
    *t = {m[12], m[13], m[14]};
    float c0[3] = {m[0], m[1], m[2]}, c1[3] = {m[4], m[5], m[6]}, c2[3] = {m[8], m[9], m[10]};
    const float l0 = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]);
    const float l1 = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    const float l2 = std::sqrt(c2[0] * c2[0] + c2[1] * c2[1] + c2[2] * c2[2]);
    *sc = {l0, l1, l2};
    for (int i = 0; i < 3; i++) { if (l0 > 0) c0[i] /= l0; if (l1 > 0) c1[i] /= l1; if (l2 > 0) c2[i] /= l2; }
    *r = quat_from_cols(c0, c1, c2);
    return;
  }
  if (n.has_translation) *t = {n.translation[0], n.translation[1], n.translation[2]};
  if (n.has_rotation) *r = normalize(Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
  if (n.has_scale) *sc = {n.scale[0], n.scale[1], n.scale[2]};
}
// Animasyon kanalini t aninda degerlendirir (out[comps]). LINEAR/STEP; CUBICSPLINE
// icin anahtar degeri alinip dogrusal karistirilir (teget yok sayilir; bilgi).
void eval_channel(const cgltf_animation_sampler &sm, float t, int comps, float *out) {
  const cgltf_accessor *in = sm.input, *ov = sm.output;
  const uint32_t n = (uint32_t)in->count;
  if (n == 0) return;
  const int stride = sm.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
  const int mid = stride == 3 ? 1 : 0;
  auto read = [&](uint32_t k, float *v) { cgltf_accessor_read_float(ov, (cgltf_size)(k * stride + mid), v, (cgltf_size)comps); };
  float t0 = 0, t1 = 0;
  cgltf_accessor_read_float(in, 0, &t0, 1);
  if (t <= t0 || n == 1) { read(0, out); return; }
  cgltf_accessor_read_float(in, n - 1, &t1, 1);
  if (t >= t1) { read(n - 1, out); return; }
  uint32_t lo = 0, hi = n - 1; // t0 <= t < t1: [lo, lo+1] araligi
  while (hi - lo > 1) {
    const uint32_t mi = (lo + hi) / 2;
    float tm = 0;
    cgltf_accessor_read_float(in, mi, &tm, 1);
    if (tm <= t) lo = mi; else hi = mi;
  }
  float ta = 0, tb = 0;
  cgltf_accessor_read_float(in, lo, &ta, 1);
  cgltf_accessor_read_float(in, lo + 1, &tb, 1);
  float a[4] = {0, 0, 0, 1}, b[4] = {0, 0, 0, 1};
  read(lo, a);
  if (sm.interpolation == cgltf_interpolation_type_step) { for (int i = 0; i < comps; i++) out[i] = a[i]; return; }
  read(lo + 1, b);
  const float u = tb > ta ? (t - ta) / (tb - ta) : 0.0f;
  if (comps == 4) {
    const Quat q = slerp(Quat{a[0], a[1], a[2], a[3]}, Quat{b[0], b[1], b[2], b[3]}, u);
    out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
    return;
  }
  for (int i = 0; i < comps; i++) out[i] = a[i] + (b[i] - a[i]) * u;
}
// A5: `weights` kanali: cikis SCALAR, anahtar basina nt_full deger (kubikte 3 x: giris teget, deger, cikis teget). Yalniz ilk nt yazilir.
void eval_weights(const cgltf_animation_sampler &sm, float t, uint32_t nt_full, uint32_t nt, float *out) {
  const cgltf_accessor *in = sm.input, *ov = sm.output;
  const uint32_t n = (uint32_t)in->count;
  for (uint32_t j = 0; j < nt; j++) out[j] = 0.0f;
  if (n == 0 || nt_full == 0) return;
  const uint32_t stride = sm.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1, mid = stride == 3 ? 1 : 0;
  auto read = [&](uint32_t k, float *v) {
    for (uint32_t j = 0; j < nt; j++) {
      v[j] = 0.0f;
      const cgltf_size el = (cgltf_size)((k * stride + mid) * nt_full + j);
      if (el < ov->count) cgltf_accessor_read_float(ov, el, &v[j], 1);
    }
  };
  float t0 = 0, t1 = 0;
  cgltf_accessor_read_float(in, 0, &t0, 1);
  if (t <= t0 || n == 1) { read(0, out); return; }
  cgltf_accessor_read_float(in, n - 1, &t1, 1);
  if (t >= t1) { read(n - 1, out); return; }
  uint32_t lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    const uint32_t mi = (lo + hi) / 2;
    float tm = 0;
    cgltf_accessor_read_float(in, mi, &tm, 1);
    if (tm <= t) lo = mi; else hi = mi;
  }
  float ta = 0, tb = 0, a[kModelMaxMorph], b[kModelMaxMorph];
  cgltf_accessor_read_float(in, lo, &ta, 1);
  cgltf_accessor_read_float(in, lo + 1, &tb, 1);
  read(lo, a);
  if (sm.interpolation == cgltf_interpolation_type_step) { for (uint32_t j = 0; j < nt; j++) out[j] = a[j]; return; }
  read(lo + 1, b);
  const float u = tb > ta ? (t - ta) / (tb - ta) : 0.0f;
  for (uint32_t j = 0; j < nt; j++) out[j] = a[j] + (b[j] - a[j]) * u;
}
uint32_t mesh_target_count(const cgltf_mesh &me) {
  if (me.weights_count) return (uint32_t)me.weights_count;
  return me.primitives_count ? (uint32_t)me.primitives[0].targets_count : 0;
}

uint32_t count_mesh_primitives(const cgltf_data *d) {
  uint32_t n = 0;
  for (cgltf_size m = 0; m < d->meshes_count; m++)
    for (cgltf_size p = 0; p < d->meshes[m].primitives_count; p++)
      if (d->meshes[m].primitives[p].type == cgltf_primitive_type_triangles) n++;
  return n;
}

void extend_bounds(Model *out, const Mat4 &world, const renderer::Vertex *v, uint32_t n, bool *first) {
  for (uint32_t i = 0; i < n; i++) {
    Vec4 p = world * Vec4{v[i].pos.x, v[i].pos.y, v[i].pos.z, 1.0f};
    Vec3 q{p.x, p.y, p.z};
    if (*first) { out->bounds_min = out->bounds_max = q; *first = false; continue; }
    out->bounds_min = {q.x < out->bounds_min.x ? q.x : out->bounds_min.x, q.y < out->bounds_min.y ? q.y : out->bounds_min.y,
                       q.z < out->bounds_min.z ? q.z : out->bounds_min.z};
    out->bounds_max = {q.x > out->bounds_max.x ? q.x : out->bounds_max.x, q.y > out->bounds_max.y ? q.y : out->bounds_max.y,
                       q.z > out->bounds_max.z ? q.z : out->bounds_max.z};
  }
}

void extend_bounds_skinned(Model *out, const Mat4 *mats, uint32_t joint_count, const renderer::SkinnedVertex *v, uint32_t n, bool *first) {
  for (uint32_t i = 0; i < n; i++) {
    Vec3 q{0, 0, 0};
    float ws = 0;
    for (int c = 0; c < 4; c++) {
      const float w = v[i].weights[c] * (1.0f / 65535.0f);
      if (w <= 0 || v[i].joints[c] >= joint_count) continue;
      q = q + transform_point(mats[v[i].joints[c]], v[i].pos) * w;
      ws += w;
    }
    if (ws <= 0) q = v[i].pos;
    if (*first) { out->bounds_min = out->bounds_max = q; *first = false; continue; }
    out->bounds_min = {q.x < out->bounds_min.x ? q.x : out->bounds_min.x, q.y < out->bounds_min.y ? q.y : out->bounds_min.y,
                       q.z < out->bounds_min.z ? q.z : out->bounds_min.z};
    out->bounds_max = {q.x > out->bounds_max.x ? q.x : out->bounds_max.x, q.y > out->bounds_max.y ? q.y : out->bounds_max.y,
                       q.z > out->bounds_max.z ? q.z : out->bounds_max.z};
  }
}
} // namespace

namespace {
bool gltf_load_file(Arena &arena, const char *path, Model *out, const GltfLimits &lim);
}
// M1: tek giris. Ayar dosyasi (`<yol>.ayar`, content/model_import.hpp) okunur, bicim ayarsiz (dokunulmamis) yuklenir, sonra C
// instance'lara uygulanir. STL'nin M2 varsayilani (Z-yukari, 1 m, taban) artik ayarin VARSAYILANI: ayni sonuc, tek yol.
bool gltf_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim) {
  ModelImportSettings ayar;
  bool var = false;
  char hata_metni[160] = {0};
  if (!model_import_load(path, &ayar, &var, hata_metni, sizeof hata_metni)) {
    *out = Model{};
    std::snprintf(out->error, sizeof out->error, "%s", hata_metni);
    return false;
  }
  if (!gltf_load_file(arena, path, out, lim)) return false;
  model_import_apply(*out, ayar);
  // Madde 3c: doku ayari
  if (ayar.doku_en_cok) {
    uint32_t en = 0;
    for (uint32_t i = 0; i < out->image_count; i++) en = std::max(en, std::max(out->images[i].width, out->images[i].height));
    const uint32_t n = model_limit_textures(arena, *out, ayar.doku_en_cok);
    if (n) {
      const size_t nl = std::strlen(out->note);
      std::snprintf(out->note + nl, sizeof out->note - nl, "%s%u doku k\xC3\xBC\xC3\xA7\xC3\xBCld\xC3\xBC (%u \xE2\x86\x92 en \xC3\xA7ok %u px)",
                    nl ? " \xC2\xB7 " : "", n, en, ayar.doku_en_cok);
    }
  }
  out->tex_astc = ayar.doku_astc;
  if (ayar.doku_astc) std::snprintf(out->tex_cache_dir, sizeof out->tex_cache_dir, "%s.doku", path);
  return true;
}

namespace {
float g_s2l[256];
bool g_s2l_hazir = false;
float s2l(uint8_t v) {
  if (!g_s2l_hazir) {
    for (int i = 0; i < 256; i++) { const float c = i / 255.0f; g_s2l[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
    g_s2l_hazir = true;
  }
  return g_s2l[v];
}
uint8_t l2s(float l) {
  const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
  const float x = c * 255.0f + 0.5f;
  return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}
// 2x2 kutu ile yari boyut (tek boyutta kenar tekrari). Normal: [-1,1]'e cozulur, ortalanir, normallestirilir.
uint8_t *yarila(Arena &arena, const uint8_t *src, uint32_t w, uint32_t h, bool srgb, bool normal, uint32_t *ow, uint32_t *oh) {
  const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
  uint8_t *dst = arena.alloc_array<uint8_t>((size_t)nw * nh * 4);
  if (!dst) return nullptr;
  for (uint32_t y = 0; y < nh; y++)
    for (uint32_t x = 0; x < nw; x++) {
      const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1), y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
      const uint8_t *p[4] = {src + ((size_t)y0 * w + x0) * 4, src + ((size_t)y0 * w + x1) * 4, src + ((size_t)y1 * w + x0) * 4, src + ((size_t)y1 * w + x1) * 4};
      uint8_t *o = dst + ((size_t)y * nw + x) * 4;
      if (normal) {
        float v[3] = {0, 0, 0};
        for (int k = 0; k < 4; k++) for (int c = 0; c < 3; c++) v[c] += p[k][c] / 127.5f - 1.0f;
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int c = 0; c < 3; c++) { const float n = l > 1e-6f ? v[c] / l : (c == 2 ? 1.0f : 0.0f); o[c] = (uint8_t)std::lround((n * 0.5f + 0.5f) * 255.0f); }
      } else if (srgb) {
        for (int c = 0; c < 3; c++) o[c] = l2s((s2l(p[0][c]) + s2l(p[1][c]) + s2l(p[2][c]) + s2l(p[3][c])) * 0.25f);
      } else {
        for (int c = 0; c < 3; c++) o[c] = (uint8_t)((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
      }
      o[3] = (uint8_t)((p[0][3] + p[1][3] + p[2][3] + p[3][3] + 2) / 4);
    }
  *ow = nw;
  *oh = nh;
  return dst;
}
} // namespace

uint32_t model_limit_textures(Arena &arena, Model &m, uint32_t max_px) {
  if (!max_px) return 0;
  uint32_t n = 0;
  for (uint32_t i = 0; i < m.image_count; i++) {
    ModelImage &im = m.images[i];
    if (!im.rgba || std::max(im.width, im.height) <= max_px) continue;
    while (std::max(im.width, im.height) > max_px) {
      uint32_t w = 0, h = 0;
      uint8_t *k = yarila(arena, im.rgba, im.width, im.height, im.srgb, im.normal_map, &w, &h);
      if (!k) return n;
      im.rgba = k;
      im.width = w;
      im.height = h;
    }
    n++;
  }
  m.images_downscaled += n;
  return n;
}
namespace {
bool gltf_load_file(Arena &arena, const char *path, Model *out, const GltfLimits &lim) {
  // M2: uzantiya gore yonlendirme (.stl / .obj -> content/mesh_import.cpp, ayni Model yapisi). Bridge, sahne calisma zamani ve editorun
  // tum cagri noktalari gltf_load'u kullandigi icin onlara dokunulmaz.
  if (path) {
    const size_t pl = std::strlen(path);
    auto bit = [&](const char *ek) {
      const size_t el = std::strlen(ek);
      if (pl < el) return false;
      for (size_t i = 0; i < el; i++) { char c = path[pl - el + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != ek[i]) return false; }
      return true;
    };
    // Ayarsiz (dokunulmamis) yukleme: eksen/olcek/pivot gltf_load'da model_import_apply ile (instance matrisi).
    MeshImportOptions ham;
    ham.z_up = false; ham.fit_size = 0.0f; ham.scale = 1.0f; ham.ground_pivot = false;
    if (bit(".stl")) return stl_load(arena, path, out, lim, &ham);
    if (bit(".obj")) return obj_load(arena, path, out, lim, &ham);
  }
  *out = Model{};
  cgltf_options opt{};
  cgltf_data *data = nullptr;
  cgltf_result r = cgltf_parse_file(&opt, path, &data);
  if (r != cgltf_result_success) { set_error(out, "glTF ayristirma", path); return false; }
  r = cgltf_load_buffers(&opt, data, path);
  if (r != cgltf_result_success) { cgltf_free(data); set_error(out, "glTF tampon yukleme", path); return false; }

  // gltf dizini (dis goruntu yollari icin)
  char dir[1024];
  std::snprintf(dir, sizeof dir, "%s", path);
  char *slash = std::strrchr(dir, '/');
  if (slash) slash[1] = 0; else dir[0] = 0;

  bool ok = true;
  // Goruntuler
  uint32_t nimg = (uint32_t)data->images_count;
  if (nimg > lim.max_images) { set_error(out, "goruntu siniri asildi", nullptr); ok = false; }
  out->images = arena.alloc_array_zeroed<ModelImage>(nimg ? nimg : 1);
  for (uint32_t i = 0; ok && i < nimg; i++) {
    if (!load_image(&data->images[i], dir, arena, &out->images[i])) {
      set_error(out, "goruntu cozulemedi", data->images[i].uri && std::strncmp(data->images[i].uri, "data:", 5) ? data->images[i].uri : "(gomulu)");
      ok = false;
    }
  }
  out->image_count = ok ? nimg : 0;
  // Malzemeler
  uint32_t nmat = (uint32_t)data->materials_count;
  if (ok && nmat > lim.max_materials) { set_error(out, "malzeme siniri asildi", nullptr); ok = false; }
  out->materials = arena.alloc_array_zeroed<ModelMaterial>(nmat ? nmat : 1);
  int32_t *sep_occ = arena.alloc_array<int32_t>(nmat ? nmat : 1); // malzeme -> ayri occlusion goruntusu (-1 yok)
  if (!sep_occ) { set_error(out, "arena dolu (malzeme)", nullptr); ok = false; }
  for (uint32_t i = 0; sep_occ && i < (nmat ? nmat : 1); i++) sep_occ[i] = -1;
  for (uint32_t i = 0; ok && i < nmat; i++) {
    const cgltf_material &cm = data->materials[i];
    ModelMaterial &mm = out->materials[i];
    mm.base_color = {1, 1, 1};
    mm.image = -1;
    // TUZAK 8u: `alloc_array_zeroed` YAPICI CALISTIRMAZ — ModelMaterial'daki
    // `roughness = 1.0f` varsayilani uygulanmaz, alan 0 kalir. roughness 0 =
    // ayna; yani pbr blogu olmayan her malzeme sessizce AYNAYA donerdi.
    // Sifirdan farkli her varsayilan ELLE atanmali (base_color/image zaten oyle).
    mm.metallic = 0.0f;
    mm.roughness = 1.0f;
    mm.has_pbr = false;
    mm.orm_image = mm.normal_image = mm.emissive_image = -1;
    mm.normal_scale = 1.0f;
    mm.occlusion_strength = 0.0f;
    mm.occlusion_separate = false;
    if (cm.has_pbr_metallic_roughness) {
      const cgltf_pbr_metallic_roughness &pbr = cm.pbr_metallic_roughness;
      mm.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
      if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
        mm.image = (int32_t)(pbr.base_color_texture.texture->image - data->images);
      // metallic/roughness CARPANLARI. Doku varsa shader bunlarla CARPAR
      // (glTF 2.0: "multiplied with the texture values").
      mm.metallic = pbr.metallic_factor;
      mm.roughness = pbr.roughness_factor;
      // metallicRoughness DOKUSU: G = roughness, B = metallic (glTF 2.0 spec).
      // cgltf bunu SOYLEMEZ — saf ayristiricidir, yalnizca doku isaretcisini ve
      // `scale`i tasir; esleme spec'ten gelir ve `content_gltf_orm_channel_mapping`
      // kapisi kanallari takas ederek olcer.
      if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
        mm.orm_image = (int32_t)(pbr.metallic_roughness_texture.texture->image - data->images);
      mm.has_pbr = true;
    }
    mm.emissive = {cm.emissive_factor[0], cm.emissive_factor[1], cm.emissive_factor[2]};
    // TUZAK (Tuzaklar 8u'nun ayni sinifi, bu kez SATICI AYRISTIRICISINDA):
    // cgltf `texture_view.scale`i YALNIZ o JSON nesnesi VARSA 1'e kurar
    // (cgltf_parse_json_texture_view); malzeme duzeyinde varsayilan YOK. Yani
    // normalTexture yazilmamissa `normal_texture.scale` SIFIR kalir — kosulsuz
    // okumak butun normal haritalarini duzlestirirdi. Bu yuzden scale yalniz
    // doku VARKEN okunur, yoksa 1 kalir.
    if (cm.normal_texture.texture && cm.normal_texture.texture->image) {
      mm.normal_image = (int32_t)(cm.normal_texture.texture->image - data->images);
      mm.normal_scale = cm.normal_texture.scale;
    }
    if (cm.emissive_texture.texture && cm.emissive_texture.texture->image)
      mm.emissive_image = (int32_t)(cm.emissive_texture.texture->image - data->images);
    // occlusionTexture: R kanali. Varliklarin ezici cogunlugunda metallicRoughness
    // ILE AYNI goruntudur ("ORM" paketlemesi) — o zaman bedava, ayni sampler'dan
    // okunur. FARKLI bir goruntuyse bugun okunmuyor: besinci bir doku baglamasi
    // TBDR'da bu kazanca degmez. Sessizce yutulmuyor, `occlusion_separate` ile
    // isaretleniyor.
    if (cm.occlusion_texture.texture && cm.occlusion_texture.texture->image) {
      const int32_t occ = (int32_t)(cm.occlusion_texture.texture->image - data->images);
      mm.occlusion_strength = cm.occlusion_texture.scale; // scale == strength (cgltf)
      if (occ != mm.orm_image) {
        mm.occlusion_separate = true;
        sep_occ[i] = occ; // asagida ORM ile birlestirilir
      }
    }
  }
  out->material_count = ok ? nmat : 0;
  // --- Ayri occlusionTexture -> ORM'e BIRLESTIR -----------------------------
  // Yeni goruntu = ORM'in G/B'si (yoksa 255: carpan oldugu gibi kalir) + occlusion
  // goruntusunun R'si (ORM boyutuna EN YAKIN komsu ile; ORM yoksa occlusion
  // boyutu). Ayni (ORM, occlusion) cifti tek goruntu. Orijinal ORM degismez
  // (baska malzeme onu tek basina kullaniyor olabilir). Yeni goruntu dogrusal
  // veri; asagidaki renk uzayi gecisi onu as_data sayar.
  if (ok && nmat) {
    uint32_t extra = 0;
    for (uint32_t i = 0; i < nmat; i++) if (sep_occ[i] >= 0) extra++;
    ModelImage *imgs = extra ? arena.alloc_array_zeroed<ModelImage>(out->image_count + extra) : nullptr;
    if (extra && imgs) {
      for (uint32_t k = 0; k < out->image_count; k++) imgs[k] = out->images[k];
      int32_t *pair_orm = arena.alloc_array<int32_t>(extra), *pair_occ = arena.alloc_array<int32_t>(extra);
      uint32_t n = out->image_count, np = 0;
      for (uint32_t i = 0; pair_orm && pair_occ && i < nmat; i++) {
        ModelMaterial &mm = out->materials[i];
        const int32_t occ = sep_occ[i];
        if (occ < 0) continue;
        if (!mm.has_pbr || occ >= (int32_t)out->image_count || !imgs[occ].rgba) { out->occlusion_unmerged++; mm.occlusion_strength = 0.0f; continue; }
        int32_t hit = -1;
        for (uint32_t q = 0; q < np; q++) if (pair_orm[q] == mm.orm_image && pair_occ[q] == occ) hit = (int32_t)(out->image_count + q);
        if (hit < 0) {
          const ModelImage &oi = imgs[occ];
          const bool has_orm = mm.orm_image >= 0 && mm.orm_image < (int32_t)out->image_count && imgs[mm.orm_image].rgba;
          const uint32_t w = has_orm ? imgs[mm.orm_image].width : oi.width, h = has_orm ? imgs[mm.orm_image].height : oi.height;
          ModelImage &ni = imgs[n];
          ni.width = w;
          ni.height = h;
          ni.rgba = arena.alloc_array<uint8_t>((size_t)w * h * 4);
          if (!ni.rgba) { out->occlusion_unmerged++; mm.occlusion_strength = 0.0f; continue; }
          for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
              const uint32_t ox = (uint32_t)(((uint64_t)x * oi.width) / w), oy = (uint32_t)(((uint64_t)y * oi.height) / h);
              uint8_t *d = ni.rgba + ((size_t)y * w + x) * 4;
              d[0] = oi.rgba[((size_t)oy * oi.width + ox) * 4];
              if (has_orm) {
                const uint8_t *src = imgs[mm.orm_image].rgba + ((size_t)y * w + x) * 4;
                d[1] = src[1];
                d[2] = src[2];
              } else {
                d[1] = d[2] = 255;
              }
              d[3] = 255;
            }
          pair_orm[np] = mm.orm_image;
          pair_occ[np] = occ;
          np++;
          hit = (int32_t)n++;
          out->occlusion_merged++;
        }
        mm.orm_image = hit;
        mm.occlusion_separate = false;
      }
      out->images = imgs;
      out->image_count = n;
    } else if (extra) {
      // Ayrilamadi: occlusion YOK sayilir (ORM.R baska bir sey tasiyor olabilir).
      out->occlusion_unmerged += extra;
      for (uint32_t i = 0; i < nmat; i++) if (sep_occ[i] >= 0) out->materials[i].occlusion_strength = 0.0f;
    }
  }
  // --- Goruntu renk uzayi: KULLANIMDAN turetilir --------------------------
  // glTF goruntusu renk uzayi tasimaz; onu MALZEMEDEKI YERI belirler (spec):
  // baseColor + emissive = sRGB, metallicRoughness + normal + occlusion =
  // DOGRUSAL veri. Bir goruntu yalniz VERI olarak kullanildiysa dogrusal olur;
  // her iki rolde de geciyorsa (glTF'te gecersiz) renk kazanir ve sayac artar.
  // TUZAK 8u: `alloc_array_zeroed` YAPICI CALISTIRMAZ, yani ModelImage'in
  // `srgb = true` varsayilani uygulanmaz ve alan SIFIR (= dogrusal) gelir.
  // Guvenli varsayilan ESKI davranistir (sRGB); dogrusal olan ACIKCA isaretlenir.
  for (uint32_t i = 0; ok && i < out->image_count; i++) out->images[i].srgb = true;
  {
    bool *as_color = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    bool *as_data = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    // Normal haritalari AYRI izleniyor: renk uzayi acisindan ORM ile ayni
    // (dogrusal veri) ama mip uretimi acisindan degil — bkz. ModelImage.
    bool *as_normal = arena.alloc_array_zeroed<bool>(out->image_count ? out->image_count : 1);
    if (as_color && as_data && as_normal) {
      for (uint32_t i = 0; ok && i < out->material_count; i++) {
        const ModelMaterial &mm = out->materials[i];
        if (mm.image >= 0 && (uint32_t)mm.image < out->image_count) as_color[mm.image] = true;
        if (mm.emissive_image >= 0 && (uint32_t)mm.emissive_image < out->image_count) as_color[mm.emissive_image] = true;
        if (mm.orm_image >= 0 && (uint32_t)mm.orm_image < out->image_count) as_data[mm.orm_image] = true;
        if (mm.normal_image >= 0 && (uint32_t)mm.normal_image < out->image_count) {
          as_data[mm.normal_image] = true;
          as_normal[mm.normal_image] = true;
        }
      }
      for (uint32_t i = 0; ok && i < out->image_count; i++) {
        out->images[i].srgb = as_color[i] || !as_data[i]; // hicbir yerde kullanilmayan: sRGB (eski davranis)
        // Bir goruntu hem renk hem normal olarak kullaniliyorsa (catisma)
        // normal mip yolu KAPALI kalir: sRGB texel'i normal sanip
        // normallestirmek catismayi buyutur, kucultmez.
        out->images[i].normal_map = as_normal[i] && !as_color[i];
        if (as_color[i] && as_data[i]) out->colorspace_conflicts++;
      }
    }
  }

  // Iskeletler (skin): eklemler ebeveyn-once yeniden siralanir (sim::to_model kurali). Eslemeler: glTF skin g -> bizim skin
  // (gskin_to), glTF eklem j -> bizim eklem (gskin_remap[g][j]; vertex'lere uygulanir), bizim skin si eklem k -> dugum (skin_nodes).
  //
  // BIRLESIK ISKELET (2026-11-16, Fab "Mike the Customizable Robot": 52 skin, mesh basina 1-17 eklemlik ALT KUME, toplam 65 eklem;
  // klipler eklem OLMAYAN ara dugumleri de oynatir — goz, kas, ayak IK): birden cok skin ya da COK KOKLU eklem ormani varsa butun
  // eklemler + ATALARI tek iskelette toplanir (<= kModelMaxJoints), her mesh'in eklem indeksi ona cevrilir, klipler ona orneklenir.
  // Tek palet: kare basina tek skin matrisi kumesi (52 skin x kucuk palet yerine). Kosul: ayni dugumun ters bag matrisi butun
  // skin'lerde ayni; degilse ya da tavan asilirsa eski yol (skin basina iskelet + pose_from) ve neden Model::note'a yazilir.
  // Tek skin + tek kok: degisiklik YOK (eski iskeletler bit esit).
  const uint32_t gskin_n = (uint32_t)data->skins_count;
  int32_t *gskin_to = arena.alloc_array_zeroed<int32_t>(gskin_n ? gskin_n : 1);
  uint8_t **gskin_remap = arena.alloc_array_zeroed<uint8_t *>(gskin_n ? gskin_n : 1);
  if (!gskin_to || !gskin_remap) { set_error(out, "arena dolu (skin)", nullptr); ok = false; }
  for (uint32_t gs = 0; ok && gs < gskin_n; gs++) {
    gskin_to[gs] = -1;
    const cgltf_size jn = data->skins[gs].joints_count;
    gskin_remap[gs] = arena.alloc_array_zeroed<uint8_t>(jn ? jn : 1);
    if (!gskin_remap[gs]) { set_error(out, "arena dolu (skin)", nullptr); ok = false; }
  }
  auto ibm_oku = [](const cgltf_skin &sk, cgltf_size j) {
    Mat4 ib;
    if (sk.inverse_bind_matrices) {
      float f[16];
      cgltf_accessor_read_float(sk.inverse_bind_matrices, j, f, 16);
      for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) ib.m[c][rr] = f[c * 4 + rr];
    }
    return ib;
  };
  // Birlesik iskelet karari
  const cgltf_node *kume[kModelMaxJoints];
  uint32_t kn = 0;
  bool birlesik = false;
  // DUGUM ANIMASYONU (2026-11-16, Khronos InterpolationTest; Mike'in silah klipleri): animasyon kanali (konum/donus/olcek) hicbir skin'in
  // eklemi OLMAYAN bir dugumu suruyorsa (kapi, pervane, silah) o dugum de birlesik iskelete girer — skin hic olmasa da. Ters bagi bag
  // pozundaki dunyasinin tersi; mesh'i o dugumde ya da altinda olan iskeletsiz parca "kemige takili" cizilir: W_poz * W_bag^-1 * W_mesh.
  // Eskiden bu klipler "ne iskelete ne morph'a bagli" diye SESSIZCE atlaniyordu.
  auto eklem_mi = [&](const cgltf_node *nd) {
    for (uint32_t gs = 0; gs < gskin_n; gs++)
      for (cgltf_size j = 0; j < data->skins[gs].joints_count; j++) if (data->skins[gs].joints[j] == nd) return true;
    return false;
  };
  uint32_t dugum_anim_n = 0;
  for (cgltf_size a = 0; ok && a < data->animations_count; a++)
    for (cgltf_size c = 0; c < data->animations[a].channels_count; c++) {
      const cgltf_animation_channel &ch = data->animations[a].channels[c];
      // Tek anahtarli (sure 0) kanal hareket etmez ve klibi zaten atlanir (Mike'in silah "Action"lari): iskeleti boşuna buyutmesin.
      const bool hareketli = ch.sampler && ch.sampler->input && ch.sampler->input->count >= 2;
      if (ch.target_node && hareketli && ch.target_path != cgltf_animation_path_type_weights && !eklem_mi(ch.target_node)) dugum_anim_n++;
    }
  if (ok && (gskin_n >= 1 || dugum_anim_n > 0)) {
    bool sigdi = true;
    auto var_mi = [&](const cgltf_node *nd) { for (uint32_t i = 0; i < kn; i++) if (kume[i] == nd) return true; return false; };
    auto ekle = [&](const cgltf_node *nd) { if (var_mi(nd)) return; if (kn >= kModelMaxJoints) { sigdi = false; return; } kume[kn++] = nd; };
    for (uint32_t gs = 0; gs < gskin_n; gs++)
      for (cgltf_size j = 0; j < data->skins[gs].joints_count; j++) ekle(data->skins[gs].joints[j]);
    for (cgltf_size a = 0; dugum_anim_n && a < data->animations_count; a++)
      for (cgltf_size c = 0; c < data->animations[a].channels_count; c++) {
        const cgltf_animation_channel &ch = data->animations[a].channels[c];
        if (ch.target_node && ch.sampler && ch.sampler->input && ch.sampler->input->count >= 2 && ch.target_path != cgltf_animation_path_type_weights)
          ekle(ch.target_node);
      }
    uint32_t kok = 0;
    for (uint32_t i = 0; i < kn && sigdi; i++) if (!kume[i]->parent || !var_mi(kume[i]->parent)) kok++;
    if (!sigdi && dugum_anim_n) std::snprintf(out->note, sizeof out->note, "animasyonlu d\xC3\xBC\xC4\x9F\xC3\xBCm say\xC4\xB1s\xC4\xB1 %u eklemi a\xC5\x9F\xC4\xB1yor: d\xC3\xBC\xC4\x9F\xC3\xBCm animasyonu oynamaz", kModelMaxJoints);
    if (sigdi && (gskin_n >= 2 || kok > 1 || dugum_anim_n > 0)) {
      const uint32_t eklem_n = kn;
      for (uint32_t i = 0; i < eklem_n && sigdi; i++)
        for (const cgltf_node *a = kume[i]->parent; a && sigdi; a = a->parent) ekle(a);
      // ters bag tutarliligi: ayni dugum, butun skin'lerde ayni matris
      bool tutarli = true;
      for (uint32_t i = 0; i < kn && tutarli && sigdi; i++) {
        bool ilk = true;
        Mat4 ref;
        for (uint32_t gs = 0; gs < gskin_n && tutarli; gs++)
          for (cgltf_size j = 0; j < data->skins[gs].joints_count; j++) {
            if (data->skins[gs].joints[j] != kume[i]) continue;
            const Mat4 ib = ibm_oku(data->skins[gs], j);
            if (ilk) { ref = ib; ilk = false; continue; }
            for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
              const float e = 1e-4f * (std::fabs(ref.m[c][rr]) > 1.0f ? std::fabs(ref.m[c][rr]) : 1.0f);
              if (std::fabs(ib.m[c][rr] - ref.m[c][rr]) > e) tutarli = false;
            }
          }
      }
      if (!sigdi) std::snprintf(out->note, sizeof out->note, "birlesik iskelet sigmadi (> %u eklem+ata): skin basina iskelet", kModelMaxJoints);
      else if (!tutarli) std::snprintf(out->note, sizeof out->note, "skin'lerin ters bag matrisleri farkli: skin basina iskelet");
      birlesik = sigdi && tutarli;
    }
  }
  uint32_t nskin = birlesik ? 1u : (gskin_n > lim.max_skins ? lim.max_skins : gskin_n);
  out->skins = arena.alloc_array_zeroed<ModelSkin>(nskin ? nskin : 1);
  const cgltf_node ***skin_nodes = arena.alloc_array_zeroed<const cgltf_node **>(nskin ? nskin : 1);
  if (!out->skins || !skin_nodes) { set_error(out, "arena dolu (skin)", nullptr); ok = false; }
  // Ebeveyn-once siralama (dugum listesi -> sira; dongu yoksa her turda en az biri yerlesir). parent_in: listedeki ebeveyn, -1 kok.
  auto sirala = [&](const cgltf_node *const *liste, uint32_t jc, uint32_t *sira, uint8_t *remap, int32_t *parent_in, bool *placed) {
    for (uint32_t j = 0; j < jc; j++) {
      parent_in[j] = -1;
      placed[j] = false;
      const cgltf_node *p = liste[j]->parent;
      for (uint32_t k = 0; p && k < jc; k++) if (liste[k] == p) { parent_in[j] = (int32_t)k; break; }
    }
    uint32_t n = 0;
    while (n < jc) {
      const uint32_t before = n;
      for (uint32_t j = 0; j < jc; j++)
        if (!placed[j] && (parent_in[j] < 0 || placed[parent_in[j]])) { sira[n] = j; remap[j] = (uint8_t)n; placed[j] = true; n++; }
      if (n == before) return false;
    }
    return true;
  };
  for (uint32_t si = 0; ok && si < nskin; si++) {
    ModelSkin &ms = out->skins[si];
    const cgltf_node *const *liste = birlesik ? kume : (const cgltf_node *const *)data->skins[si].joints;
    const uint32_t jc = birlesik ? kn : (uint32_t)data->skins[si].joints_count;
    if (jc > kModelMaxJoints) { set_error(out, "eklem siniri asildi (128)", data->skins[si].name); ok = false; break; }
    if (!birlesik && data->skins[si].name) std::snprintf(ms.name, sizeof ms.name, "%s", data->skins[si].name);
    if (birlesik) std::snprintf(ms.name, sizeof ms.name, gskin_n ? "birlesik (%u skin)" : "d\xC3\xBC\xC4\x9F\xC3\xBCm animasyonu", gskin_n);
    ms.joints = arena.alloc_array_zeroed<sim::Joint>(jc ? jc : 1);
    ms.inverse_bind = arena.alloc_array_zeroed<Mat4>(jc ? jc : 1);
    skin_nodes[si] = arena.alloc_array_zeroed<const cgltf_node *>(jc ? jc : 1);
    uint32_t *sira = arena.alloc_array_zeroed<uint32_t>(jc ? jc : 1);
    uint8_t *remap = arena.alloc_array_zeroed<uint8_t>(jc ? jc : 1);
    int32_t *parent_in = arena.alloc_array_zeroed<int32_t>(jc ? jc : 1);
    bool *placed = arena.alloc_array_zeroed<bool>(jc ? jc : 1);
    if (!ms.joints || !ms.inverse_bind || !skin_nodes[si] || !sira || !remap || !parent_in || !placed) { set_error(out, "arena dolu (skin)", nullptr); ok = false; break; }
    if (!sirala(liste, jc, sira, remap, parent_in, placed)) { set_error(out, "eklem hiyerarsisinde dongu", ms.name); ok = false; break; }
    for (uint32_t k = 0; k < jc; k++) {
      const uint32_t j = sira[k];
      sim::Joint &jt = ms.joints[k];
      jt.parent = parent_in[j] < 0 ? (int16_t)-1 : (int16_t)remap[parent_in[j]];
      node_local_trs(*liste[j], &jt.bind_t, &jt.bind_r, &jt.bind_s);
      skin_nodes[si][k] = liste[j];
      if (!birlesik) { ms.inverse_bind[k] = ibm_oku(data->skins[si], j); continue; }
      // birlesik: dugum bir skin'in eklemiyse onun ters bagi; yalniz ata ise bag pozundaki dunyasinin tersi (vertex kullanmaz)
      bool bulundu = false;
      for (uint32_t gs = 0; gs < gskin_n && !bulundu; gs++)
        for (cgltf_size gj = 0; gj < data->skins[gs].joints_count && !bulundu; gj++)
          if (data->skins[gs].joints[gj] == liste[j]) { ms.inverse_bind[k] = ibm_oku(data->skins[gs], gj); bulundu = true; }
      if (!bulundu) {
        float wm[16];
        cgltf_node_transform_world(liste[j], wm);
        Mat4 w;
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) w.m[c][rr] = wm[c * 4 + rr];
        ms.inverse_bind[k] = inverse_affine(w);
      }
    }
    ms.joint_count = jc;
    // glTF skin(ler)i bu iskelete esle
    for (uint32_t gs = 0; gs < gskin_n; gs++) {
      if (birlesik ? false : gs != si) continue;
      gskin_to[gs] = (int32_t)si;
      for (cgltf_size gj = 0; gj < data->skins[gs].joints_count; gj++) gskin_remap[gs][gj] = remap[gj];
    }
    if (birlesik)
      for (uint32_t gs = 0; gs < gskin_n; gs++) {
        gskin_to[gs] = 0;
        for (cgltf_size gj = 0; gj < data->skins[gs].joints_count; gj++)
          for (uint32_t k = 0; k < jc; k++) if (skin_nodes[0][k] == data->skins[gs].joints[gj]) { gskin_remap[gs][gj] = (uint8_t)k; break; }
      }
  }
  // Dugum -> (bizim skin, bizim eklem); yoksa -1.
  auto bizim_eklem = [&](const cgltf_node *nd, int32_t *k_out) -> int32_t {
    for (uint32_t si = 0; si < nskin && si < (ok ? nskin : 0); si++)
      for (uint32_t k = 0; k < out->skins[si].joint_count; k++)
        if (skin_nodes[si][k] == nd) { if (k_out) *k_out = (int32_t)k; return (int32_t)si; }
    return -1;
  };
  // Kok ustu donusum (ModelSkin::pre): ilk (ebeveyn-once) eklemin ebeveyni eklem degilse onun DUNYA matrisi.
  for (uint32_t si = 0; ok && si < nskin; si++) {
    ModelSkin &ms = out->skins[si];
    // VARSAYILAN ACIKCA: skins alloc_array_zeroed ile ayrildi — kurucu CALISMAZ, pre sifir matris / pre_birim false kalir ve kok
    // ustu dugumu olmayan HER iskeletin pozu sifirlanirdi (kapi model_paylasilan_kemikler yakaladi, 2026-11-16).
    ms.pre = Mat4{};
    ms.pre_birim = true;
    if (!ms.joint_count) continue;
    const cgltf_node *kokd = skin_nodes[si][0];
    if (!kokd->parent) continue;
    float wm[16];
    cgltf_node_transform_world(kokd->parent, wm);
    Mat4 pre;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) pre.m[c][rr] = wm[c * 4 + rr];
    bool birim = true;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) if (pre.m[c][rr] != (c == rr ? 1.0f : 0.0f)) birim = false;
    ms.pre = pre;
    ms.pre_birim = birim;
  }
  // Bag pozu skin matrisleri (ModelSkin::bind_mats): dugumun glTF dunya matrisi (pre dahil butun atalar) * ters bag.
  for (uint32_t si = 0; ok && si < nskin; si++) {
    ModelSkin &ms = out->skins[si];
    ms.bind_mats = arena.alloc_array<Mat4>(ms.joint_count ? ms.joint_count : 1);
    if (!ms.bind_mats) { set_error(out, "arena dolu (skin)", nullptr); ok = false; break; }
    for (uint32_t k = 0; k < ms.joint_count; k++) {
      float wm[16];
      cgltf_node_transform_world(skin_nodes[si][k], wm);
      Mat4 w;
      for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) w.m[c][rr] = wm[c * 4 + rr];
      ms.bind_mats[k] = w * ms.inverse_bind[k];
    }
  }
  // Paylasilan kemikler (birlesik olmayan yol): s'nin HER eklemi (ayni dugum) p'de varsa s, p'nin pozundan doldurulur (pose_from).
  for (uint32_t sa = 0; ok && sa < nskin; sa++)
    for (uint32_t p = 0; ok && p < nskin; p++) {
      if (p == sa) continue;
      const uint32_t sj = out->skins[sa].joint_count, pj = out->skins[p].joint_count;
      uint8_t *map = arena.alloc_array_zeroed<uint8_t>(sj ? sj : 1);
      if (!map) { set_error(out, "arena dolu (skin)", nullptr); ok = false; break; }
      bool tam = sj > 0;
      for (uint32_t k = 0; k < sj && tam; k++) {
        bool bulundu = false;
        for (uint32_t q = 0; q < pj && !bulundu; q++)
          if (skin_nodes[p][q] == skin_nodes[sa][k]) { map[k] = (uint8_t)q; bulundu = true; }
        tam = bulundu;
      }
      if (tam) out->skins[sa].pose_from[p] = map;
    }
  out->skin_count = ok ? nskin : 0;

  // Klipler: glTF animasyonu sabit hizda orneklenir (kanal LINEAR/STEP/CUBIC
  // degerleri), ClipBuilder sikistirir (sabit iz eleme + niceleme; bilgi stats).
  uint32_t nclip = (uint32_t)data->animations_count;
  if (nclip > lim.max_clips) { out->clips_dropped = nclip - lim.max_clips; nclip = lim.max_clips; }
  out->clips = arena.alloc_array_zeroed<ModelClip>(nclip ? nclip : 1);
  if (!out->clips) { set_error(out, "arena dolu (klip)", nullptr); ok = false; }
  uint32_t cn = 0;
  for (uint32_t ai = 0; ok && ai < nclip; ai++) {
    const cgltf_animation &an = data->animations[ai];
    int32_t skin_idx = -1;
    float duration = 0;
    for (cgltf_size c = 0; c < an.channels_count; c++) {
      const cgltf_animation_channel &ch = an.channels[c];
      if (!ch.sampler || !ch.target_node || !ch.sampler->input || !ch.sampler->output) continue;
      if (skin_idx < 0) skin_idx = bizim_eklem(ch.target_node, nullptr);
      const cgltf_size nk = ch.sampler->input->count;
      if (nk) { float tl = 0; cgltf_accessor_read_float(ch.sampler->input, nk - 1, &tl, 1); if (tl > duration) duration = tl; }
    }
    // A5: `weights` kanallari (morph'lu mesh'i olan dugum). Iskeletsiz klip yalniz bunlarla gecerli.
    uint32_t wch = 0;
    for (cgltf_size c = 0; c < an.channels_count; c++) {
      const cgltf_animation_channel &ch = an.channels[c];
      if (ch.target_path == cgltf_animation_path_type_weights && ch.sampler && ch.sampler->input && ch.sampler->output && ch.target_node &&
          ch.target_node->mesh && mesh_target_count(*ch.target_node->mesh))
        wch++;
    }
    const bool skin_ok = skin_idx >= 0 && (uint32_t)skin_idx < nskin;
    if ((!skin_ok && wch == 0) || duration <= 0) continue; // ne iskelete ne morph'a bagli: atla (bilgi)
    const float rate = lim.clip_sample_rate > 0 ? lim.clip_sample_rate : 30.0f;
    uint32_t samples = (uint32_t)(duration * rate) + 1;
    if (samples < 2) samples = 2;
    ModelClip &mc = out->clips[cn];
    if (an.name) std::snprintf(mc.name, sizeof mc.name, "%s", an.name);
    mc.duration = (float)(samples - 1) / rate;
    mc.sample_rate = rate;
    mc.skin = -1;
    if (wch) {
      if (wch > kModelMaxMorphTracks) { out->morph_tracks_dropped += wch - kModelMaxMorphTracks; wch = kModelMaxMorphTracks; }
      mc.morph_tracks = arena.alloc_array_zeroed<ModelMorphTrack>(wch);
      if (!mc.morph_tracks) { set_error(out, "arena dolu (morph izi)", nullptr); ok = false; break; }
      for (cgltf_size c = 0; c < an.channels_count && mc.morph_track_count < wch; c++) {
        const cgltf_animation_channel &ch = an.channels[c];
        if (ch.target_path != cgltf_animation_path_type_weights || !ch.sampler || !ch.sampler->input || !ch.sampler->output || !ch.target_node ||
            !ch.target_node->mesh)
          continue;
        const uint32_t nt_full = mesh_target_count(*ch.target_node->mesh);
        if (!nt_full) continue;
        const uint32_t nt = nt_full < kModelMaxMorph ? nt_full : kModelMaxMorph;
        ModelMorphTrack &tr = mc.morph_tracks[mc.morph_track_count];
        tr.gltf_mesh = (int32_t)(ch.target_node->mesh - data->meshes);
        tr.weight_count = nt;
        tr.samples = samples;
        tr.w = arena.alloc_array<float>(samples * nt);
        if (!tr.w) { set_error(out, "arena dolu (morph izi)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < samples; i++) eval_weights(*ch.sampler, (float)i / rate, nt_full, nt, tr.w + i * nt);
        mc.morph_track_count++;
      }
      if (!ok) break;
    }
    if (!skin_ok) { cn++; continue; }   // yalniz morph izli klip
    const ModelSkin &ms = out->skins[skin_idx];
    const uint32_t jc = ms.joint_count;
    sim::RawTrack *tracks = arena.alloc_array_zeroed<sim::RawTrack>(jc);
    Vec3 *tt = arena.alloc_array<Vec3>(jc * samples);
    Quat *rr = arena.alloc_array<Quat>(jc * samples);
    Vec3 *ss = arena.alloc_array<Vec3>(jc * samples);
    if (!tracks || !tt || !rr || !ss) { set_error(out, "arena dolu (klip)", nullptr); ok = false; break; }
    for (uint32_t k = 0; k < jc; k++) {
      const cgltf_node *node = skin_nodes[skin_idx][k];
      Vec3 bt, bs; Quat br;
      node_local_trs(*node, &bt, &br, &bs);
      const cgltf_animation_channel *ct = nullptr, *cr = nullptr, *cs = nullptr;
      for (cgltf_size c = 0; c < an.channels_count; c++) {
        const cgltf_animation_channel &ch = an.channels[c];
        if (ch.target_node != node || !ch.sampler) continue;
        if (ch.target_path == cgltf_animation_path_type_translation) ct = &ch;
        else if (ch.target_path == cgltf_animation_path_type_rotation) cr = &ch;
        else if (ch.target_path == cgltf_animation_path_type_scale) cs = &ch;
      }
      for (uint32_t i = 0; i < samples; i++) {
        const float t = (float)i / rate;
        float v[4];
        Vec3 p = bt, scl = bs; Quat q = br;
        if (ct) { eval_channel(*ct->sampler, t, 3, v); p = {v[0], v[1], v[2]}; }
        if (cr) { eval_channel(*cr->sampler, t, 4, v); q = normalize(Quat{v[0], v[1], v[2], v[3]}); }
        if (cs) { eval_channel(*cs->sampler, t, 3, v); scl = {v[0], v[1], v[2]}; }
        tt[k * samples + i] = p; rr[k * samples + i] = q; ss[k * samples + i] = scl;
      }
      tracks[k].t = tt + k * samples; tracks[k].r = rr + k * samples; tracks[k].s = ss + k * samples;
    }
    sim::RawClip raw;
    raw.tracks = tracks; raw.joints = jc; raw.samples = samples; raw.sample_rate = rate;
    mc.skin = skin_idx;
    mc.clip = sim::ClipBuilder::build(arena, raw, 1e-5f, &mc.stats);
    if (!mc.clip) { set_error(out, "klip sikistirma", an.name); ok = false; break; }
    cn++;
  }
  out->clip_count = ok ? cn : 0;
  // Mesh'ler (ucgen primitifleri; her primitif ayri ModelMesh)
  uint32_t nprim = count_mesh_primitives(data);
  if (ok && nprim > lim.max_meshes) { set_error(out, "mesh siniri asildi", nullptr); ok = false; }
  out->meshes = arena.alloc_array_zeroed<ModelMesh>(nprim ? nprim : 1);
  // cgltf mesh -> ilk ModelMesh indeksi ve sayisi (instance kurarken)
  uint32_t *mesh_first = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t *mesh_n = arena.alloc_array_zeroed<uint32_t>(data->meshes_count ? data->meshes_count : 1);
  uint32_t mi = 0;
  uint32_t uv_kanal_degisti = 0;   // dejenere UV kanali yerine digeri kullanilan primitif sayisi (Model::note)
  for (cgltf_size m = 0; ok && m < data->meshes_count; m++) {
    mesh_first[m] = mi;
    for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++) {
      const cgltf_primitive &prim = data->meshes[m].primitives[p];
      if (prim.type != cgltf_primitive_type_triangles) continue;
      const cgltf_accessor *pos = find_attr(prim, cgltf_attribute_type_position);
      const cgltf_accessor *nrm = find_attr(prim, cgltf_attribute_type_normal);
      const cgltf_accessor *uv = uv_kanali(prim, &uv_kanal_degisti);
      if (!pos) { set_error(out, "POSITION yok", data->meshes[m].name); ok = false; break; }
      ModelMesh &mm = out->meshes[mi];
      mm.skin = -1; // alloc_array_zeroed: varsayilan kurucu CALISMAZ (0 = "skin 0" olurdu; cokme goruldu)
      mm.vertex_count = (uint32_t)pos->count;
      mm.verts = arena.alloc_array<renderer::Vertex>(mm.vertex_count);
      if (!mm.verts) { set_error(out, "arena dolu (vertex)", nullptr); ok = false; break; }
      for (uint32_t i = 0; i < mm.vertex_count; i++) {
        float f[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos, i, f, 3);
        mm.verts[i].pos = {f[0], f[1], f[2]};
        f[0] = 0; f[1] = 1; f[2] = 0;
        if (nrm) cgltf_accessor_read_float(nrm, i, f, 3);
        mm.verts[i].nrm = {f[0], f[1], f[2]};
        f[0] = f[1] = 0;
        if (uv) cgltf_accessor_read_float(uv, i, f, 2);
        mm.verts[i].uv = {f[0], f[1]};
      }
      if (prim.indices) {
        mm.index_count = (uint32_t)prim.indices->count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
      } else {
        mm.index_count = mm.vertex_count;
        mm.indices = arena.alloc_array<uint32_t>(mm.index_count);
        if (!mm.indices) { set_error(out, "arena dolu (indeks)", nullptr); ok = false; break; }
        for (uint32_t i = 0; i < mm.index_count; i++) mm.indices[i] = i;
      }
      mm.material = prim.material ? (int32_t)(prim.material - data->materials) : -1;
      // Iskeletli primitif: JOINTS_0 + WEIGHTS_0 ve bu mesh'i kullanan dugumun skin'i.
      const cgltf_accessor *jacc = find_attr(prim, cgltf_attribute_type_joints);
      const cgltf_accessor *wacc = find_attr(prim, cgltf_attribute_type_weights);
      int32_t gsk = -1;   // bu mesh'i kullanan dugumun glTF skin'i
      if (jacc && wacc && nskin) {
        for (cgltf_size n = 0; n < data->nodes_count && mm.skin < 0; n++)
          if (data->nodes[n].mesh == &data->meshes[m] && data->nodes[n].skin) {
            const int32_t gs = (int32_t)(data->nodes[n].skin - data->skins);
            if (gs >= 0 && (uint32_t)gs < gskin_n && gskin_to[gs] >= 0) { mm.skin = gskin_to[gs]; gsk = gs; }
          }
      }
      if (mm.skin >= 0) {
        mm.skin_verts = arena.alloc_array_zeroed<renderer::SkinnedVertex>(mm.vertex_count);
        if (!mm.skin_verts) { set_error(out, "arena dolu (skin vertex)", nullptr); ok = false; break; }
        const uint32_t jc = (uint32_t)data->skins[gsk].joints_count;   // glTF eklem indeksi bu skin'in listesine gore
        for (uint32_t i = 0; i < mm.vertex_count; i++) {
          renderer::SkinnedVertex &sv = mm.skin_verts[i];
          sv.pos = mm.verts[i].pos; sv.nrm = mm.verts[i].nrm; sv.uv = mm.verts[i].uv;
          cgltf_uint ju[4] = {0, 0, 0, 0};
          float w[4] = {1, 0, 0, 0};
          cgltf_accessor_read_uint(jacc, i, ju, 4);
          cgltf_accessor_read_float(wacc, i, w, 4);
          float sum = w[0] + w[1] + w[2] + w[3];
          if (sum <= 0) { w[0] = 1; sum = 1; }
          for (int c = 0; c < 4; c++) {
            const uint32_t j = ju[c] < jc ? gskin_remap[gsk][ju[c]] : 0;
            sv.joints[c] = (uint8_t)j;
            float wn = w[c] / sum;
            if (wn < 0) wn = 0;
            sv.weights[c] = (uint16_t)(wn * 65535.0f + 0.5f);
          }
        }
      }
      // A5: morph hedefleri (POSITION/NORMAL farki). Morph'lu mesh'te meshopt/LOD/DAG atlanir (kose sirasi korunmali). Iskeletli mesh'te de
      // (yuz animasyonu): farklar BIND uzayinda tabana eklenir, sonra GPU deri giydirir (glTF sirasi: morph -> skin).
      if (prim.targets_count > 0) {
        if (mm.vertex_count > kModelMaxMorphVerts) out->morph_meshes_dropped++;
        else {
          // Hedef t = glTF weights[t]: POSITION'u olmayan hedef SIFIR fark olur (indeks kaymasin).
          const uint32_t nt = (uint32_t)(prim.targets_count < kModelMaxMorph ? prim.targets_count : kModelMaxMorph);
          if (prim.targets_count > kModelMaxMorph) out->morph_dropped += (uint32_t)(prim.targets_count - kModelMaxMorph);
          mm.morph_deltas = arena.alloc_array_zeroed<Vec3>(nt * mm.vertex_count);
          if (!mm.morph_deltas) { set_error(out, "arena dolu (morph)", nullptr); ok = false; break; }
          for (uint32_t t = 0; t < nt; t++) {
            const cgltf_accessor *tp = nullptr, *tn = nullptr;
            for (cgltf_size a = 0; a < prim.targets[t].attributes_count; a++) {
              if (prim.targets[t].attributes[a].type == cgltf_attribute_type_position) tp = prim.targets[t].attributes[a].data;
              if (prim.targets[t].attributes[a].type == cgltf_attribute_type_normal) tn = prim.targets[t].attributes[a].data;
            }
            if (tp && tp->count == pos->count)
              for (uint32_t v = 0; v < mm.vertex_count; v++) {
                float d[3] = {0, 0, 0};
                cgltf_accessor_read_float(tp, v, d, 3);
                mm.morph_deltas[t * mm.vertex_count + v] = {d[0], d[1], d[2]};
              }
            // A5: NORMAL farki (ilk gorulen hedefte dizi ayrilir; NORMAL'siz hedef sifir fark). Taban NORMAL'i yoksa anlamsiz: atlanir.
            if (tn && nrm && tn->count == pos->count) {
              if (!mm.morph_normal_deltas) {
                mm.morph_normal_deltas = arena.alloc_array_zeroed<Vec3>(nt * mm.vertex_count);
                if (!mm.morph_normal_deltas) { set_error(out, "arena dolu (morph normal)", nullptr); ok = false; break; }
              }
              for (uint32_t v = 0; v < mm.vertex_count; v++) {
                float d[3] = {0, 0, 0};
                cgltf_accessor_read_float(tn, v, d, 3);
                mm.morph_normal_deltas[t * mm.vertex_count + v] = {d[0], d[1], d[2]};
              }
            }
          }
          if (!ok) break;
          mm.morph_count = nt;
        }
      }
      if (mm.skin < 0 && mm.morph_count == 0 && lim.optimize && !mesh_optimize(arena, mm, &out->opt)) { set_error(out, "arena dolu (meshopt)", nullptr); ok = false; break; }
      // Ince esik: kucuk iskeletli mesh'e LOD uretmek renderer mesh kapasitesini (256) bosuna tuketir (her LOD ayri mesh tutamaci; havuz testi
      // bridge_model_pool_holds_game_characters_and_reports 108 kucuk iskeletli modelle bunu yakaladi). Yalniz >= kSkinLodMinTris ucgenli.
      if (lim.lods && mm.morph_count == 0 && (mm.skin < 0 || mm.index_count / 3 >= kSkinLodMinTris)) {
        // A7 (mobil karakter butcesi, P20 Pro): ISKELETLI mesh'lere de LOD. Sadelestirme yalniz var olan kose indekslerini secer (ayni kose
        // tamponu), yani skin_verts (eklem/agirlik) ayni indekslerle gecerli kalir. Deformasyonu korumak icin hata esigi yariya iner.
        const float ratios[kModelMaxLods] = {0.5f, 0.25f};
        const float er = mm.skin >= 0 ? lim.lod_error * 0.5f : lim.lod_error;
        if (!mesh_build_lods(arena, mm, ratios, kModelMaxLods, er)) { set_error(out, "arena dolu (lod)", nullptr); ok = false; break; }
      }
      if (mm.skin < 0 && mm.morph_count == 0 && lim.cluster_dag) {
        const DeviceClass dc = lim.cluster_class == 0 ? DeviceClass::Low : (lim.cluster_class >= 2 ? DeviceClass::High : DeviceClass::Mid);
        ClusterDag *dag = arena.alloc_array_zeroed<ClusterDag>(1);
        if (!dag) { set_error(out, "arena dolu (kume DAG)", nullptr); ok = false; break; }
        *dag = ClusterDag{};
        if (cluster_dag_build(arena, mm, cluster_dag_preset(dc), dag)) mm.dag = dag;
      }
      mi++;
      mesh_n[m]++;
    }
  }
  out->mesh_count = ok ? mi : 0;
  // A5: morph izlerini ModelMesh araligina coz (glTF mesh -> ilk primitif + sayisi).
  for (uint32_t c = 0; ok && c < out->clip_count; c++)
    for (uint32_t k = 0; k < out->clips[c].morph_track_count; k++) {
      ModelMorphTrack &tr = out->clips[c].morph_tracks[k];
      if (tr.gltf_mesh < 0 || (cgltf_size)tr.gltf_mesh >= data->meshes_count) { tr.mesh_n = 0; continue; }
      tr.mesh_first = mesh_first[tr.gltf_mesh];
      tr.mesh_n = mesh_n[tr.gltf_mesh];
    }
  // Instance'lar: sahnedeki (ya da tum) dugumler, dunya matrisiyle
  uint32_t ninst = 0;
  out->instances = arena.alloc_array_zeroed<ModelInstance>(lim.max_instances);
  bool first_bound = true;
  // SINIR POZU (2026-11-16, Fab "Mike"): animasyonlu modelde sinirlar BEKLEME klibinin ilk karesinden (adinda "idle" gecen en kisa
  // klip, yoksa klip 0). Dinlenme (bag) pozu dosyadan dosyaya keyfi: Mike'ta yatik, sinir 0,39 m boy x 1,26 m derinlik -> editorde F
  // odagi, taban pivotu, secim kutusu, birim tahmini hepsi yanlis olcuye bakiyordu. Klip yoksa / iskelete bagli degilse bag pozu.
  const size_t poz_mark = arena.mark();
  PoseScratch *poz_sc = nullptr;
  ModelPose sinir_poz;
  bool sinir_pozlu = false;
  if (ok && out->clip_count) {
    int32_t sec = -1;
    size_t sec_boy = (size_t)-1;
    for (uint32_t c = 0; c < out->clip_count; c++) {
      char k[64];
      uint32_t i = 0;
      for (; out->clips[c].name[i] && i + 1 < sizeof k; i++) { const char ch = out->clips[c].name[i]; k[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }
      k[i] = 0;
      if (out->clips[c].clip && out->clips[c].skin >= 0 && std::strstr(k, "idle") && i < sec_boy) { sec = (int32_t)c; sec_boy = i; }
    }
    if (sec < 0) for (uint32_t c = 0; c < out->clip_count && sec < 0; c++) if (out->clips[c].clip && out->clips[c].skin >= 0) sec = (int32_t)c;
    if (sec >= 0 && (poz_sc = arena.alloc_array<PoseScratch>(1)) != nullptr) {
      out->instance_count = 0;   // poz islevleri instance okumaz; acikca 0
      sinir_pozlu = model_pose_evaluate(*out, (uint32_t)sec, 0.0f, *poz_sc, &sinir_poz);
      if (sinir_pozlu) out->bounds_clip = sec;
    }
  }
  for (cgltf_size n = 0; ok && n < data->nodes_count; n++) {
    const cgltf_node &node = data->nodes[n];
    if (!node.mesh) continue;
    cgltf_size m = (cgltf_size)(node.mesh - data->meshes);
    float wm[16];
    cgltf_node_transform_world(&node, wm);
    Mat4 world;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) world.m[c][rr] = wm[c * 4 + rr]; // glTF sutun-major
    // Kemige takili kati parca: kendisi ya da atalardaki ilk eklem (bizim sira).
    uint8_t kemik_skin1 = 0, kemik = 0;
    Mat4 kemik_bag;
    // Dugumun KENDISI de eklem olabilir (dugum animasyonu: pervane mesh'i kendi dugumunde donar).
    for (const cgltf_node *a = &node; a && !kemik_skin1; a = a->parent) {
      int32_t kk = -1;
      const int32_t s = bizim_eklem(a, &kk);
      if (s >= 0 && kk >= 0) {
        kemik_skin1 = (uint8_t)(s + 1);
        kemik = (uint8_t)kk;
        float ew[16];
        cgltf_node_transform_world(a, ew);
        Mat4 we;
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) we.m[c][rr] = ew[c * 4 + rr];
        kemik_bag = inverse_affine(we * out->skins[s].inverse_bind[kemik]) * world;
      }
    }
    for (uint32_t k = 0; k < mesh_n[m]; k++) {
      if (ninst >= lim.max_instances) { set_error(out, "instance siniri asildi", nullptr); ok = false; break; }
      out->instances[ninst].mesh = mesh_first[m] + k;
      // glTF: iskeletli mesh'i tasiyan dugumun donusumu YOK SAYILIR (eklem dunya donusumleri yeter); iskeletsizde dunya matrisi.
      const bool iskeletli = out->meshes[mesh_first[m] + k].skin >= 0;
      out->instances[ninst].world = iskeletli ? Mat4{} : world;
      out->instances[ninst].kemik_skin1 = iskeletli ? 0 : kemik_skin1;
      out->instances[ninst].kemik = iskeletli ? 0 : kemik;
      out->instances[ninst].kemik_bag = kemik_bag;
      const ModelMesh &bm = out->meshes[mesh_first[m] + k];
      const ModelSkin *bsk = (iskeletli && bm.skin_verts) ? &out->skins[bm.skin] : nullptr;
      if (bsk && sinir_pozlu && sinir_poz.skin_mats[bm.skin])   // bekleme klibinin ilk karesi
        extend_bounds_skinned(out, sinir_poz.skin_mats[bm.skin], sinir_poz.joint_count[bm.skin], bm.skin_verts, bm.vertex_count, &first_bound);
      else if (bsk && bsk->bind_mats)   // glTF bag pozu (mesh dugumu YOK SAYILIR)
        extend_bounds_skinned(out, bsk->bind_mats, bsk->joint_count, bm.skin_verts, bm.vertex_count, &first_bound);
      else if (!iskeletli && kemik_skin1 && sinir_pozlu)   // kemige takili kati parca: o pozdaki yeri
        extend_bounds(out, model_instance_world(*out, out->instances[ninst], &sinir_poz, Mat4{}), bm.verts, bm.vertex_count, &first_bound);
      else
        extend_bounds(out, world, bm.verts, bm.vertex_count, &first_bound);
      ninst++;
    }
  }
  if (poz_sc) arena.reset_to(poz_mark);   // poz tamponu yalniz sinir icin (instance dizisi oncesinde ayrildi, korunur)
  out->instance_count = ok ? ninst : 0;
  if (ok && uv_kanal_degisti) {
    const size_t nl = std::strlen(out->note);
    std::snprintf(out->note + nl, sizeof out->note - nl, "%s%u mesh'te UV kanal\xC4\xB1 bo\xC5\x9F (hep ayn\xC4\xB1 de\xC4\x9F" "er): di\xC4\x9F" "er kanal kullan\xC4\xB1ld\xC4\xB1",
                  nl ? " \xC2\xB7 " : "", uv_kanal_degisti);
  }
  cgltf_free(data);
  return ok;
}
} // namespace (gltf_load_file)

// NORMAL HARITASI MIP ZINCIRI — CPU'da, her seviyeden sonra YENIDEN
// NORMALLESTIREREK.
//
// Neden blit yetmiyor: donanim blit'i dogrusal suzuyor, yani dort komsu
// normalin BILESENLERINI ortaliyor. Iki komsu normal birbirine ters egimliyse
// ortalama vektorun boyu 1 degil, ~0 olur; encode edilince (0.5,0.5,~1) yani
// "duz yuzey" cikar. Sonuc: uzaktaki yuzey sessizce duzlesir ve isik yassilar.
// Bu, hicbir seyin kizarmadigi, yalnizca goruntunun yanlis oldugu sinif.
//
// Cozum kucultmeden SONRA normallestirmek. Filtre kutu (2x2) — ilgi cekici bir
// secim degil ama dogru olan: normal haritasinda yuksek frekansi korumaya
// calismak (Kaiser vb.) aliasing'i artirir.
//
// Alfa kanali ortalanip birakiliyor: normal haritasinda kullanilmiyor, ama
// sifirlamak da bilgiyi ATMAK olurdu.
bool build_normal_mips(Arena &arena, const ModelImage &img, uint32_t levels,
                              uint8_t **data, uint32_t *sizes) {
  uint32_t w = img.width, h = img.height;
  data[0] = img.rgba;
  sizes[0] = w * h * 4;
  for (uint32_t l = 1; l < levels; l++) {
    const uint32_t pw = w, ph = h;
    w = w > 1 ? w >> 1 : 1;
    h = h > 1 ? h >> 1 : 1;
    uint8_t *dst = arena.alloc_array<uint8_t>(w * h * 4);
    if (!dst) return false;
    const uint8_t *src = data[l - 1];
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        // Kaynak 2x2 (tek boyutta kenarda ayni piksel iki kez okunur).
        const uint32_t x0 = x * 2, x1 = (x * 2 + 1 < pw) ? x * 2 + 1 : x * 2;
        const uint32_t y0 = y * 2, y1 = (y * 2 + 1 < ph) ? y * 2 + 1 : y * 2;
        const uint32_t idx[4] = {(y0 * pw + x0) * 4, (y0 * pw + x1) * 4,
                                 (y1 * pw + x0) * 4, (y1 * pw + x1) * 4};
        float nx = 0, ny = 0, nz = 0, na = 0;
        for (int k = 0; k < 4; k++) {
          // UNORM [0,1] -> [-1,1]
          nx += (float)src[idx[k] + 0] / 255.0f * 2.0f - 1.0f;
          ny += (float)src[idx[k] + 1] / 255.0f * 2.0f - 1.0f;
          nz += (float)src[idx[k] + 2] / 255.0f * 2.0f - 1.0f;
          na += (float)src[idx[k] + 3];
        }
        float len = nx * nx + ny * ny + nz * nz;
        if (len > 1e-12f) {
          len = 1.0f / sqrtf(len);
          nx *= len; ny *= len; nz *= len;
        } else {
          // Dort normal birbirini tam goturdu: duz yuzey en az yanlis cevap.
          nx = 0; ny = 0; nz = 1;
        }
        auto enc = [](float v) -> uint8_t {
          float u = (v * 0.5f + 0.5f) * 255.0f + 0.5f;
          if (u < 0) u = 0;
          if (u > 255) u = 255;
          return (uint8_t)u;
        };
        const uint32_t o = (y * w + x) * 4;
        dst[o + 0] = enc(nx);
        dst[o + 1] = enc(ny);
        dst[o + 2] = enc(nz);
        dst[o + 3] = (uint8_t)(na * 0.25f + 0.5f);
      }
    }
    data[l] = dst;
    sizes[l] = w * h * 4;
  }
  return true;
}

namespace {
// ASTC (doku_sikistir astc, madde 3c): mip zinciri 6x6 ASTC'ye kodlanir; `<model>.doku/<i>_<ozet>.ktx2` onbellegi (ikinci yukleme kodlamaz).
// Normal haritasi `Data` kipiyle (iki kanalli XY DEGIL: malzeme yolu RGB normal okur). Basarisizlikta gecersiz tutamac -> cagiran RGBA.
renderer::TextureHandle astc_doku(renderer::Renderer &r, Arena &arena, const Model &m, uint32_t i, bool *onbellekten) {
  *onbellekten = false;
  const ModelImage &im = m.images[i];
  if (!im.rgba || !m.tex_cache_dir[0]) return renderer::TextureHandle{};
  constexpr uint32_t kB = 6, kSurum = 1;
  uint64_t h = 1469598103934665603ull;
  auto kat = [&](const void *p, size_t n) { const uint8_t *b = static_cast<const uint8_t *>(p); for (size_t k = 0; k < n; k++) { h ^= b[k]; h *= 1099511628211ull; } };
  const uint32_t baslik[6] = {im.width, im.height, im.srgb ? 1u : 0u, im.normal_map ? 1u : 0u, kB, kSurum};
  kat(baslik, sizeof baslik);
  kat(im.rgba, (size_t)im.width * im.height * 4);
  char yol[420];
  std::snprintf(yol, sizeof yol, "%s/%u_%016llx.ktx2", m.tex_cache_dir, i, (unsigned long long)h);
  const size_t mark = arena.mark();
  Ktx2Image img;
  if (ktx2_load(arena, yol, &img) && img.astc) {
    *onbellekten = true;
  } else {
    arena.reset_to(mark);
    const bool renk = im.srgb && !im.normal_map;
    const AstcMap harita = renk ? AstcMap::Color : AstcMap::Data;
    uint8_t *seviye_veri[Ktx2Image::kMaxLevels] = {};
    uint32_t seviye_boy[Ktx2Image::kMaxLevels] = {};
    uint32_t n = 0, w = im.width, hh = im.height;
    const uint8_t *px = im.rgba;
    while (n < Ktx2Image::kMaxLevels) {
      if (!astc_encode_rgba(arena, px, w, hh, kB, kB, renk, 60.0f, &seviye_veri[n], &seviye_boy[n], harita)) { arena.reset_to(mark); return renderer::TextureHandle{}; }
      n++;
      if (w == 1 && hh == 1) break;
      uint32_t nw = 0, nh = 0;
      px = yarila(arena, px, w, hh, im.srgb, im.normal_map, &nw, &nh);
      if (!px) { arena.reset_to(mark); return renderer::TextureHandle{}; }
      w = nw;
      hh = nh;
    }
    platform::fs_mkdir_one(m.tex_cache_dir);
    const VkFormat fmt = renk ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK : VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
    if (!ktx2_write(yol, fmt, im.width, im.height, n, seviye_veri, seviye_boy, kB, kB) || !ktx2_load(arena, yol, &img)) {
      arena.reset_to(mark);
      return renderer::TextureHandle{};
    }
  }
  Ktx2UploadInfo info;
  const renderer::TextureHandle t = ktx2_upload(r, r.device_caps(), arena, img, &info);
  arena.reset_to(mark);   // bloklar GPU'ya kopyalandi
  return info.hardware ? t : renderer::TextureHandle{};
}
} // namespace

bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out) {
  *out = UploadedModel{};
  out->textures = arena.alloc_array_zeroed<renderer::TextureHandle>(m.image_count ? m.image_count : 1);
  out->materials = arena.alloc_array_zeroed<renderer::MaterialHandle>(m.material_count ? m.material_count : 1);
  out->meshes = arena.alloc_array_zeroed<renderer::MeshHandle>(m.mesh_count ? m.mesh_count : 1);
  out->lod_meshes = arena.alloc_array_zeroed<renderer::MeshHandle>((m.mesh_count ? m.mesh_count : 1) * kModelMaxLods);
  if (!out->textures || !out->materials || !out->meshes || !out->lod_meshes) return false;
  // Tutamaclar GECERSIZ baslar: sifirla doldurulmus dizide id 0 GECERLI bir
  // tutamactir (renderer'in ILK mesh'i). Geri bildirim #27 (2026-10-08, ilk
  // gercek oyun): LOD'u uretilmemis her mesh (dusuk poligonlu kutu, sopa, lazer,
  // kalkan) kameraya 20 m'den uzakta "LOD1" diye mesh #0 ile — oyunda kafa
  // kupu — cizildi; 2:1 ekranda kenardaki birlik o uzakliga dusuyordu: sopa
  // ekran kenarinda koca kup, lazer duvar, kalkan kup, renk/golge farkli.
  for (uint32_t i = 0; i < (m.mesh_count ? m.mesh_count : 1); i++) out->meshes[i] = renderer::MeshHandle{};
  for (uint32_t i = 0; i < (m.mesh_count ? m.mesh_count : 1) * kModelMaxLods; i++) out->lod_meshes[i] = renderer::MeshHandle{};
  const bool astc = m.tex_astc && r.device_caps().texture_compression_astc_ldr;
  out->astc_unsupported = m.tex_astc && !astc;   // masaustu NVIDIA: CPU cozumu bellek kazandirmaz, RGBA kalir (cagiran soyler)
  for (uint32_t i = 0; i < m.image_count; i++) {
    if (astc) {
      bool onb = false;
      out->textures[i] = astc_doku(r, arena, m, i, &onb);
      if (out->textures[i].valid()) { out->astc_textures++; out->astc_cached += onb ? 1u : 0u; continue; }
    }
    // RENK UZAYI: sRGB doku donanimda dogrusala cozer; ORM/normal UNORM kalir
    // ve texel oldugu gibi gelir. Bu, "goruntu biraz yanlis ama hicbir sey
    // kizarmaz" sinifinin ta kendisidir — bu yuzden kapiyla olculuyor
    // (content_gltf_texture_colorspace).
    if (m.images[i].normal_map) {
      // Seviye sayisi blit yolundakiyle AYNI hesap (en buyuk kenar).
      uint32_t levels = 1, mm = m.images[i].width > m.images[i].height ? m.images[i].width : m.images[i].height;
      while (mm > 1) { mm >>= 1; levels++; }
      const size_t mark = arena.mark();
      uint8_t **lv = arena.alloc_array_zeroed<uint8_t *>(levels);
      uint32_t *sz = arena.alloc_array_zeroed<uint32_t>(levels);
      if (lv && sz && build_normal_mips(arena, m.images[i], levels, lv, sz)) {
        out->textures[i] = r.create_texture_levels(VK_FORMAT_R8G8B8A8_UNORM, m.images[i].width, m.images[i].height, levels,
                                                   (const uint8_t *const *)lv, sz);
      }
      if (out->textures[i].valid()) out->normal_mip_textures++;
      arena.reset_to(mark); // seviyeler GPU'ya kopyalandi
      // Sessiz dusme yok: CPU yolu basarisizsa blit yoluna donuluyor, ama
      // goruntu o zaman ESKI (normallestirilmemis) davranisi gosterir.
      if (!out->textures[i].valid())
        out->textures[i] = r.create_texture(m.images[i].rgba, m.images[i].width, m.images[i].height, true, false);
    } else {
      out->textures[i] = r.create_texture(m.images[i].rgba, m.images[i].width, m.images[i].height, true, m.images[i].srgb);
    }
    if (!out->textures[i].valid()) return false;
  }
  out->texture_count = m.image_count;
  for (uint32_t i = 0; i < m.material_count; i++) {
    const ModelMaterial &mm = m.materials[i];
    renderer::TextureHandle t = mm.image >= 0 ? out->textures[mm.image] : r.default_texture();
    // Dosyada pbr_metallic_roughness YOKSA eski Lambert yolu — boylece PBR'siz
    // bir varlik bugunku goruntusunu KORUR (gate: pbr_default_material_stays_near_lambert).
    if (mm.has_pbr) {
      renderer::PbrParams pbr;
      pbr.metallic = mm.metallic;
      pbr.roughness = mm.roughness;
      pbr.emissive = mm.emissive;
      // Doku basina kanallar (set 1 binding 2/3/4). Tutamac gecersizse shader
      // dali kapali kalir: dokusuz malzemenin goruntusu de maliyeti de ayni.
      renderer::PbrTextures tex;
      const uint32_t nt = m.image_count;
      if (mm.orm_image >= 0 && (uint32_t)mm.orm_image < nt) tex.orm = out->textures[mm.orm_image];
      if (mm.normal_image >= 0 && (uint32_t)mm.normal_image < nt) tex.normal = out->textures[mm.normal_image];
      if (mm.emissive_image >= 0 && (uint32_t)mm.emissive_image < nt) tex.emissive = out->textures[mm.emissive_image];
      tex.normal_scale = mm.normal_scale;
      tex.occlusion_strength = mm.occlusion_strength;
      out->materials[i] = r.create_material(t, mm.base_color, pbr, tex);
    } else {
      out->materials[i] = r.create_material(t, mm.base_color);
    }
    if (!out->materials[i].valid()) return false;
  }
  out->material_count = m.material_count;
  for (uint32_t i = 0; i < m.mesh_count; i++) {
    if (m.meshes[i].skin >= 0 && m.meshes[i].skin_verts)
      out->meshes[i] = r.create_skinned_mesh(m.meshes[i].skin_verts, m.meshes[i].vertex_count, m.meshes[i].indices, m.meshes[i].index_count);
    else
      out->meshes[i] = r.create_mesh(m.meshes[i].verts, m.meshes[i].vertex_count, m.meshes[i].indices, m.meshes[i].index_count);
    if (!out->meshes[i].valid()) return false;
    for (uint32_t k = 0; k < kModelMaxLods; k++) {
      if (!m.meshes[i].lod_index_count[k]) continue;
      out->lod_meshes[i * kModelMaxLods + k] =
          (m.meshes[i].skin >= 0 && m.meshes[i].skin_verts)
              ? r.create_skinned_mesh(m.meshes[i].skin_verts, m.meshes[i].vertex_count, m.meshes[i].lod_indices[k], m.meshes[i].lod_index_count[k],
                                      m.meshes[i].lod_vertex_count[k])
              : r.create_mesh(m.meshes[i].verts, m.meshes[i].vertex_count, m.meshes[i].lod_indices[k], m.meshes[i].lod_index_count[k]);
      if (!out->lod_meshes[i * kModelMaxLods + k].valid()) return false;
    }
  }
  out->mesh_count = m.mesh_count;
  return true;
}

namespace {
template <class T> T *copy_array(Arena &a, const T *src, uint32_t n) {
  T *d = a.alloc_array<T>(n ? n : 1);
  if (d && src && n) std::memcpy((void *)d, (const void *)src, sizeof(T) * n);
  return d;
}
} // namespace

bool model_compact(Arena &keep, const Model &src, Model *dst) {
  *dst = src; // sayilar, sinirlar, istatistik, hata metni; isaretciler asagida yeniden kurulur
  dst->images = copy_array(keep, src.images, src.image_count);
  dst->materials = copy_array(keep, src.materials, src.material_count);
  dst->meshes = copy_array(keep, src.meshes, src.mesh_count);
  dst->instances = copy_array(keep, src.instances, src.instance_count);
  dst->skins = copy_array(keep, src.skins, src.skin_count);
  dst->clips = copy_array(keep, src.clips, src.clip_count);
  if (!dst->images || !dst->materials || !dst->meshes || !dst->instances || !dst->skins || !dst->clips) return false;
  for (uint32_t i = 0; i < src.image_count; i++) dst->images[i].rgba = nullptr;
  for (uint32_t i = 0; i < src.mesh_count; i++) {
    ModelMesh &mm = dst->meshes[i];
    const ModelMesh &sm = src.meshes[i];
    mm.verts = nullptr; mm.indices = nullptr; mm.skin_verts = nullptr; mm.dag = nullptr; mm.morph_deltas = nullptr; mm.morph_normal_deltas = nullptr;
    for (uint32_t k = 0; k < kModelMaxLods; k++) mm.lod_indices[k] = nullptr;
    if (sm.morph_count && sm.morph_deltas && sm.verts) {   // A5: morph her kare CPU'da karilir: taban kose + farklar kalici kopyada KALIR
      mm.verts = copy_array(keep, sm.verts, sm.vertex_count);
      mm.indices = copy_array(keep, sm.indices, sm.index_count);   // dinamik (kopya) mesh kurmak icin
      mm.morph_deltas = copy_array(keep, sm.morph_deltas, sm.morph_count * sm.vertex_count);
      if (!mm.verts || !mm.indices || !mm.morph_deltas) return false;
      if (sm.skin_verts) {   // iskeletli+morph: taban iskeletli koseler (eklem/agirlik) de kalir
        mm.skin_verts = copy_array(keep, sm.skin_verts, sm.vertex_count);
        if (!mm.skin_verts) return false;
      }
      if (sm.morph_normal_deltas) {
        mm.morph_normal_deltas = copy_array(keep, sm.morph_normal_deltas, sm.morph_count * sm.vertex_count);
        if (!mm.morph_normal_deltas) return false;
      }
    }
  }
  for (uint32_t i = 0; i < src.skin_count; i++) {
    ModelSkin &s = dst->skins[i];
    s.joints = copy_array(keep, src.skins[i].joints, s.joint_count);
    s.inverse_bind = copy_array(keep, src.skins[i].inverse_bind, s.joint_count);
    if (!s.joints || !s.inverse_bind) return false;
    for (uint32_t p = 0; p < kModelMaxSkins; p++) {
      if (!src.skins[i].pose_from[p]) continue;
      s.pose_from[p] = copy_array(keep, src.skins[i].pose_from[p], s.joint_count);
      if (!s.pose_from[p]) return false;
    }
  }
  for (uint32_t i = 0; i < src.clip_count; i++) {
    const sim::ClipHeader *h = src.clips[i].clip;
    if (!h) continue;
    // Sikistirilmis klip TEK blok (ClipBuilder::build: ofsetler basliga gore), memcpy yeter.
    void *blob = keep.alloc(h->total_bytes, 16);
    if (!blob) return false;
    std::memcpy(blob, h, h->total_bytes);
    dst->clips[i].clip = static_cast<const sim::ClipHeader *>(blob);
  }
  for (uint32_t i = 0; i < src.clip_count; i++) {   // A5: morph izleri (kalici kopya)
    const ModelClip &sc = src.clips[i];
    if (!sc.morph_track_count) continue;
    ModelMorphTrack *tr = copy_array(keep, sc.morph_tracks, sc.morph_track_count);
    if (!tr) return false;
    for (uint32_t k = 0; k < sc.morph_track_count; k++) {
      tr[k].w = copy_array(keep, sc.morph_tracks[k].w, sc.morph_tracks[k].samples * sc.morph_tracks[k].weight_count);
      if (!tr[k].w) return false;
    }
    dst->clips[i].morph_tracks = tr;
  }
  return true;
}

bool uploaded_model_copy(Arena &keep, const UploadedModel &src, UploadedModel *dst) {
  *dst = src;
  const uint32_t nm = src.mesh_count ? src.mesh_count : 1;
  dst->textures = copy_array(keep, src.textures, src.texture_count);
  dst->materials = copy_array(keep, src.materials, src.material_count);
  dst->meshes = copy_array(keep, src.meshes, src.mesh_count);
  dst->lod_meshes = copy_array(keep, src.lod_meshes, nm * kModelMaxLods);
  return dst->textures && dst->materials && dst->meshes && dst->lod_meshes;
}

uint32_t model_clip_morph_weights(const Model &m, uint32_t clip, float time_s, uint32_t mesh, float out[kModelMaxMorph]) {
  if (clip >= m.clip_count) return 0;
  const ModelClip &c = m.clips[clip];
  for (uint32_t k = 0; k < c.morph_track_count; k++) {
    const ModelMorphTrack &tr = c.morph_tracks[k];
    if (mesh < tr.mesh_first || mesh >= tr.mesh_first + tr.mesh_n || !tr.samples || !tr.w || !tr.weight_count) continue;
    float x = time_s * c.sample_rate;
    if (!(x > 0.0f)) x = 0.0f;   // NaN dahil
    const float son = (float)(tr.samples - 1);
    if (x > son) x = son;
    const uint32_t i0 = (uint32_t)x, i1 = i0 + 1 < tr.samples ? i0 + 1 : i0;
    const float f = x - (float)i0;
    for (uint32_t j = 0; j < tr.weight_count; j++) out[j] = tr.w[i0 * tr.weight_count + j] * (1.0f - f) + tr.w[i1 * tr.weight_count + j] * f;
    return tr.weight_count;
  }
  return 0;
}

// Kok ustu donusum (ModelSkin::pre): to_model'in model uzayi kok eklemin EBEVEYNINE goreydi; dunya donusumu atalari da icerir.
static void kok_ustu(const ModelSkin &sk, PoseScratch &scratch) {
  if (sk.pre_birim) return;
  for (uint32_t j = 0; j < sk.joint_count; j++) scratch.model[j] = sk.pre * scratch.model[j];
}

// Klibin skin'i (p) hesaplandi (scratch.model = p'nin eklem model matrisleri): ayni kemikleri paylasan obur skin'ler de doldurulur.
static void paylasan_skinler(const Model &m, uint32_t p, PoseScratch &scratch, ModelPose *pose) {
  for (uint32_t s = 0; s < m.skin_count && s < kModelMaxSkins; s++) {
    if (s == p || p >= kModelMaxSkins) continue;
    const ModelSkin &sk = m.skins[s];
    const uint8_t *map = sk.pose_from[p];
    if (!map) continue;
    for (uint32_t j = 0; j < sk.joint_count; j++) scratch.skin[s][j] = scratch.model[map[j]] * sk.inverse_bind[j];
    pose->skin_mats[s] = scratch.skin[s];
    pose->joint_count[s] = sk.joint_count;
  }
}

bool model_pose_evaluate(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, ModelPose *pose) {
  if (clip >= m.clip_count || !m.clips[clip].clip || m.clips[clip].skin < 0) return false;
  const ModelClip &mc = m.clips[clip];
  const ModelSkin &sk = m.skins[mc.skin];
  sim::LocalPose lp{scratch.t, scratch.r, scratch.s, sk.joint_count};
  sim::ClipSampler::sample(mc.clip, time_s, lp);
  sim::Skeleton skel{sk.joints, sk.joint_count};
  sim::ClipSampler::to_model(skel, lp, scratch.model);
  kok_ustu(sk, scratch);
  sim::ClipSampler::skin_matrices(scratch.model, sk.inverse_bind, sk.joint_count, scratch.skin[mc.skin]);
  pose->skin_mats[mc.skin] = scratch.skin[mc.skin];
  pose->joint_count[mc.skin] = sk.joint_count;
  paylasan_skinler(m, (uint32_t)mc.skin, scratch, pose);
  return true;
}

ModelBudget model_budget(const Model &m) {
  ModelBudget b;
  b.meshes = m.mesh_count; b.instances = m.instance_count; b.images = m.image_count;
  b.clips = m.clip_count; b.clips_dropped = m.clips_dropped;
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const uint32_t mi = m.instances[i].mesh;
    if (mi >= m.mesh_count) continue;
    const ModelMesh &mm = m.meshes[mi];
    b.triangles += mm.index_count / 3;
    if (mm.skin >= 0) b.skinned_vertices += mm.vertex_count;
  }
  for (uint32_t i = 0; i < m.skin_count; i++)
    if (m.skins[i].joint_count > b.joints) b.joints = m.skins[i].joint_count;
  for (uint32_t i = 0; i < m.image_count; i++) b.texture_bytes += (uint64_t)m.images[i].width * m.images[i].height * 4u * 4u / 3u;
  return b;
}

uint32_t model_budget_text(const Model &m, char *out, uint32_t cap) {
  // Parçalar " · " ile ayrılır: editör dili İngilizce iken her parça sözlükten ayrı çevrilir (app/editor_dil.hpp).
  const ModelBudget b = model_budget(m);
  uint32_t warn = 0;
  uint32_t n = 0;
  auto ekle = [&](const char *fmt, uint32_t a0, uint32_t a1 = 0) {
    if (n >= cap) return;
    n += (uint32_t)std::snprintf(out + n, cap - n, "%s", n ? " · " : "");
    if (n < cap) n += (uint32_t)std::snprintf(out + n, cap - n, fmt, a0, a1);
  };
  out[0] = 0;
  ekle("%u mesh", b.meshes);
  ekle("%u üçgen", b.triangles);
  ekle("%u klip", b.clips);
  {   // M1: ice aktarma ayarindan SONRAKI boyut (metre): "20 m'lik bir kapi" ya da "2 cm'lik bir karakter" burada gorunur
    const Vec3 d = m.bounds_max - m.bounds_min;
    if (n < cap) n += (uint32_t)std::snprintf(out + n, cap - n, " · boyut %.2f × %.2f × %.2f m", (double)d.x, (double)d.y, (double)d.z);
  }
  if (b.joints) {
    ekle("iskelet %u eklem", b.joints);
    ekle("%u köşe", b.skinned_vertices);
  }
  if (b.joints && b.skinned_vertices && b.skinned_vertices <= kMobileSkinnedVertexBudget / 4)
    ekle("mobilde ≈ %u kopya sığar", kMobileSkinnedVertexBudget / b.skinned_vertices);
  if (m.note[0] && n + 4 < cap) { n += (uint32_t)std::snprintf(out + n, cap - n, " · NOT: %s", m.note); }
  // M1: yalniz sayacta kalan sinirlar artik okunur (kullanici "yuz ifadesi neden calismiyor" diye aramasin).
  if (m.morph_dropped) { ekle("UYARI: %u morph hedefi yüklenmedi (mesh başına en çok %u)", m.morph_dropped, kModelMaxMorph); warn = 1; }
  if (m.morph_meshes_dropped) { ekle("UYARI: %u mesh morph'suz yüklendi (morph için en çok %u köşe)", m.morph_meshes_dropped, kModelMaxMorphVerts); warn = 1; }
  if (m.morph_tracks_dropped) { ekle("UYARI: %u morph animasyon izi yüklenmedi (klip başına en çok %u)", m.morph_tracks_dropped, kModelMaxMorphTracks); warn = 1; }
  if (m.morph_skipped) { ekle("UYARI: %u iskeletli mesh'in morph'u atlandı (iskelet + morph bu sürümde yok)", m.morph_skipped); warn = 1; }
  if (b.clips_dropped) { ekle("UYARI: %u klip yüklenmedi (model başına en çok %u)", b.clips_dropped, GltfLimits{}.max_clips); warn = 1; }
  if (b.texture_bytes) ekle("doku ≈ %u MB", (uint32_t)((b.texture_bytes + (1u << 19)) >> 20));
  if (b.texture_bytes > kMobileTextureBudget && !m.tex_astc && !m.images_downscaled) {
    ekle("UYARI: dokular telefon için ağır (≲ %u MB önerilir) — içe aktarma ayarlarında Doku en çok / ASTC", (uint32_t)(kMobileTextureBudget >> 20));
    warn = 1;
  }
  if (b.skinned_vertices > kMobileSkinnedVertexBudget / 4) {
    ekle("UYARI: ağır iskeletli model, mobilde kare başına ≲ %uK köşe önerilir", kMobileSkinnedVertexBudget / 1000);
    ekle("≈ %u kopya", b.skinned_vertices ? kMobileSkinnedVertexBudget / b.skinned_vertices : 0);
    warn = 1;
  }
  return warn;
}

bool model_pose_blend(const Model &m, uint32_t clip_a, float time_a, uint32_t clip_b, float time_b, float w, PoseScratch &scratch,
                      ModelPose *pose) {
  if (clip_a >= m.clip_count || clip_b >= m.clip_count) return false;
  const ModelClip &ca = m.clips[clip_a], &cb = m.clips[clip_b];
  if (!ca.clip || !cb.clip || ca.skin < 0 || ca.skin != cb.skin) return false;
  if (!(w > 0.0f)) return model_pose_evaluate(m, clip_a, time_a, scratch, pose);   // w <= 0 / NaN: yalniz A (bit esit)
  if (w >= 1.0f) return model_pose_evaluate(m, clip_b, time_b, scratch, pose);
  const ModelSkin &sk = m.skins[ca.skin];
  sim::LocalPose la{scratch.t, scratch.r, scratch.s, sk.joint_count};
  sim::LocalPose lb{scratch.t2, scratch.r2, scratch.s2, sk.joint_count};
  sim::ClipSampler::sample(ca.clip, time_a, la);
  sim::ClipSampler::sample(cb.clip, time_b, lb);
  for (uint32_t j = 0; j < sk.joint_count; j++) {
    la.t[j] = la.t[j] + (lb.t[j] - la.t[j]) * w;
    la.s[j] = la.s[j] + (lb.s[j] - la.s[j]) * w;
    la.r[j] = slerp(la.r[j], lb.r[j], w);
  }
  sim::Skeleton skel{sk.joints, sk.joint_count};
  sim::ClipSampler::to_model(skel, la, scratch.model);
  kok_ustu(sk, scratch);
  sim::ClipSampler::skin_matrices(scratch.model, sk.inverse_bind, sk.joint_count, scratch.skin[ca.skin]);
  pose->skin_mats[ca.skin] = scratch.skin[ca.skin];
  pose->joint_count[ca.skin] = sk.joint_count;
  paylasan_skinler(m, (uint32_t)ca.skin, scratch, pose);
  return true;
}

bool model_pose_mix(const Model &m, const PoseMixEntry *e, uint32_t n, PoseScratch &scratch, ModelPose *pose, bool lock_root) {
  uint32_t first = n;
  for (uint32_t i = 0; i < n; i++) if (e[i].weight > 0.0f && e[i].clip < m.clip_count && m.clips[e[i].clip].clip && m.clips[e[i].clip].skin >= 0) { first = i; break; }
  if (first == n) return false;
  const int32_t skin = m.clips[e[first].clip].skin;
  const ModelSkin &sk = m.skins[skin];
  sim::LocalPose la{scratch.t, scratch.r, scratch.s, sk.joint_count};
  sim::ClipSampler::sample(m.clips[e[first].clip].clip, e[first].time, la);
  float cum = e[first].weight;
  sim::LocalPose lb{scratch.t2, scratch.r2, scratch.s2, sk.joint_count};
  for (uint32_t i = first + 1; i < n; i++) {
    if (!(e[i].weight > 0.0f) || e[i].clip >= m.clip_count) continue;
    const ModelClip &mc = m.clips[e[i].clip];
    if (!mc.clip || mc.skin != skin) return false;                        // farkli iskelet: karisim YOK
    sim::ClipSampler::sample(mc.clip, e[i].time, lb);
    const float f = e[i].weight / (cum + e[i].weight);                    // kumulatif agirlikla: ilk girdiye geri donuk yanlilik yok
    for (uint32_t j = 0; j < sk.joint_count; j++) {
      la.t[j] = la.t[j] + (lb.t[j] - la.t[j]) * f;
      la.s[j] = la.s[j] + (lb.s[j] - la.s[j]) * f;
      la.r[j] = slerp(la.r[j], lb.r[j], f);
    }
    cum += e[i].weight;
  }
  if (lock_root && sk.joint_count) { la.t[0].x = sk.joints[0].bind_t.x; la.t[0].z = sk.joints[0].bind_t.z; }
  sim::Skeleton skel{sk.joints, sk.joint_count};
  sim::ClipSampler::to_model(skel, la, scratch.model);
  kok_ustu(sk, scratch);
  sim::ClipSampler::skin_matrices(scratch.model, sk.inverse_bind, sk.joint_count, scratch.skin[skin]);
  pose->skin_mats[skin] = scratch.skin[skin];
  pose->joint_count[skin] = sk.joint_count;
  paylasan_skinler(m, (uint32_t)skin, scratch, pose);
  return true;
}

bool model_clip_root_xz(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, float out_xz[2]) {
  if (clip >= m.clip_count || !m.clips[clip].clip || m.clips[clip].skin < 0) return false;
  const ModelSkin &sk = m.skins[m.clips[clip].skin];
  if (!sk.joint_count) return false;
  sim::LocalPose lp{scratch.t, scratch.r, scratch.s, sk.joint_count};
  sim::ClipSampler::sample(m.clips[clip].clip, time_s, lp);
  // M1: ice aktarma ayarinin dogrusal kismi (eksen + olcek; oteleme konum farkinda duser): olcekli modelde yuruyus da olceklenir.
  const Mat4 &C = m.import_xform;
  const Vec3 t = lp.t[0];
  out_xz[0] = C.m[0][0] * t.x + C.m[1][0] * t.y + C.m[2][0] * t.z;
  out_xz[1] = C.m[0][2] * t.x + C.m[1][2] * t.y + C.m[2][2] * t.z;
  return true;
}

void ModelLod::set_view(Vec3 eye, Vec3 target, float fov_y) {
  camera_pos = eye;
  const Vec3 f = target - eye;
  const float len = length(f);
  camera_forward = len > 1e-6f ? f * (1.0f / len) : Vec3{0, 0, 0};
  const float ref = std::tan(kModelLodRefFovY * 0.5f);
  depth_scale = (fov_y > 0.0f && ref > 0.0f) ? std::tan(fov_y * 0.5f) / ref : 1.0f;
}

ModelLod model_lod_effective(const Model &m, const ModelLod &lod) {
  ModelLod ml = lod;
  if (m.lod_distance1 > 0.0f) ml.distance1 = m.lod_distance1;
  if (m.lod_distance2 > 0.0f) ml.distance2 = m.lod_distance2;
  if (ml.distance2 < ml.distance1) ml.distance2 = ml.distance1;
  return ml;
}

uint32_t model_lod_level(const ModelLod &lod, Vec3 p) {
  const Vec3 d{p.x - lod.camera_pos.x, p.y - lod.camera_pos.y, p.z - lod.camera_pos.z};
  const Vec3 f = lod.camera_forward;
  float m2; // olcutun karesi (karekok yok)
  if (f.x != 0.0f || f.y != 0.0f || f.z != 0.0f) {
    const float z = (d.x * f.x + d.y * f.y + d.z * f.z) * lod.depth_scale; // gorus derinligi
    m2 = z * z;
  } else {
    m2 = d.x * d.x + d.y * d.y + d.z * d.z; // eski oklid yolu
  }
  if (m2 >= lod.distance2 * lod.distance2) return 2;
  if (m2 >= lod.distance1 * lod.distance1) return 1;
  return 0;
}

Mat4 model_instance_world(const Model &m, const ModelInstance &inst, const ModelPose *pose, const Mat4 &model) {
  if (inst.kemik_skin1 && pose) {
    const uint32_t ks = inst.kemik_skin1 - 1u;
    if (ks < kModelMaxSkins && pose->skin_mats[ks] && inst.kemik < pose->joint_count[ks])
      return model * m.import_xform * pose->skin_mats[ks][inst.kemik] * inst.kemik_bag;
  }
  return model * inst.world;
}

void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint,
                const ModelLod *lod, uint32_t lod_counts[kModelMaxLods + 1], const ModelPose *pose) {
  // A7: modelin kendi esikleri (.ayar) cagiraninkini ezer; biri verilmisse obur cagirandan. lod2 < lod1 olamaz (ayristirici
  // reddeder; burada yalniz karisik kaynak — biri modelden biri cagirandan — icin kenetlenir).
  ModelLod ml;
  if (lod && (m.lod_distance1 > 0.0f || m.lod_distance2 > 0.0f)) {
    ml = model_lod_effective(m, *lod);
    lod = &ml;
  }
  uint32_t palet[kModelMaxSkins];
  bool palet_denendi[kModelMaxSkins] = {};
  for (uint32_t s = 0; s < kModelMaxSkins; s++) palet[s] = renderer::Renderer::kNoPalette;
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const ModelInstance &inst = m.instances[i];
    if (inst.mesh >= u.mesh_count) continue;
    int32_t mat = m.meshes[inst.mesh].material;
    renderer::MaterialHandle mh = (mat >= 0 && (uint32_t)mat < u.material_count) ? u.materials[mat] : r.default_material();
    const Mat4 world = model * inst.world;
    renderer::MeshHandle mesh = u.meshes[inst.mesh];
    const int32_t skin = m.meshes[inst.mesh].skin;
    if (skin >= 0) { // iskeletli: pozu yoksa BIND pozunda (Tuzaklar 8da)
      // Skin matrisi = model[j] * ters_bind[j]; bind pozunda model[j] ters_bind'in
      // tersidir, yani her eklem icin BIRIM matris. Eskiden pozsuz iskeletli mesh
      // hic cizilmiyordu ama lod_counts'a "cizildi" diye giriyordu: klip atanmamis
      // model sessizce gorunmezdi (kopru kapisi "statik" kareyi bos ekranla
      // karsilastirip farki animasyona yaziyordu).
      static const Mat4 kBindPose[kModelMaxJoints] = {};
      uint32_t slevel = 0;                    // A7: iskeletli LOD (static ile ayni olcut ve geri dusme kurali)
      if (lod && u.lod_meshes) {
        slevel = model_lod_level(*lod, Vec3{world.m[3][0], world.m[3][1], world.m[3][2]});
        while (slevel > 0 && !u.lod_meshes[inst.mesh * kModelMaxLods + (slevel - 1)].valid()) slevel--;
        if (slevel > 0) mesh = u.lod_meshes[inst.mesh * kModelMaxLods + (slevel - 1)];
      }
      // Palet skin basina BIR KEZ yuklenir, ayni skin'i kullanan butun mesh'ler paylasir (Renderer::skin_palette).
      if ((uint32_t)skin < kModelMaxSkins && (uint32_t)skin < m.skin_count) {
        if (palet[skin] == renderer::Renderer::kNoPalette && !palet_denendi[skin]) {
          palet_denendi[skin] = true;
          if (pose && pose->skin_mats[skin]) palet[skin] = r.skin_palette(pose->skin_mats[skin], pose->joint_count[skin]);
          else palet[skin] = r.skin_palette(m.skins[skin].bind_mats ? m.skins[skin].bind_mats : kBindPose, m.skins[skin].joint_count);   // glTF bag pozu
        }
        r.draw_skinned_palette(mesh, mh, world, tint, palet[skin]);
      }
      if (lod_counts) lod_counts[slevel]++;
      continue;
    }
    // Kemige takili kati parca: pozla birlikte tasinir (bacak, kol, kafa parcalari). Pozsuz/eklemsiz: eskisi gibi sabit.
    const Mat4 parca_world = (inst.kemik_skin1 && pose) ? model_instance_world(m, inst, pose, model) : world;
    uint32_t level = 0;
    if (lod && u.lod_meshes) {
      level = model_lod_level(*lod, Vec3{parca_world.m[3][0], parca_world.m[3][1], parca_world.m[3][2]});
      // Istenen seviye yoksa bir alta dus (LOD2 yoksa LOD1, o da yoksa LOD0).
      while (level > 0 && !u.lod_meshes[inst.mesh * kModelMaxLods + (level - 1)].valid()) level--;
      if (level > 0) mesh = u.lod_meshes[inst.mesh * kModelMaxLods + (level - 1)];
    }
    if (lod_counts) lod_counts[level]++;
    r.draw(mesh, mh, parca_world, tint);
  }
}

} // namespace tulpar::engine::content
