// L6 CONTENT — FBX yükleyici (madde 4, 2026-11-16; ufbx v0.23.1, MIT/Unlicense: third_party/ufbx).
//
// Eskiden FBX "kapalı biçim, Blender'dan glTF'e çevir" diye reddediliyordu; Mixamo, Unity Asset Store ve Unreal kaynaklarının çoğu FBX.
// ufbx dosyayı açar ve eksen/birim dönüşümünü YAPAR (Y-yukarı, metre: Unreal/Mixamo santimetresi burada metreye iner — 8eg'nin cm
// tahmini FBX'te gerekmez). Çıktı glTF ile AYNI Model yapısı: çizim, gölge, LOD, animatör, yeniden hedefleme (A6) değişmez.
//
//  - Mesh: yüz köşeleri üçgenlenir, AYNI köşe (konum/normal/UV/eklem/ağırlık) kaynaştırılır (LOD sadeleştirmesi paylaşılan köşe ister);
//    malzeme başına bir ModelMesh.
//  - İskelet: bütün skin kümelerinin kemikleri + ATALARI tek iskelet (glTF'teki birleşik iskeletle aynı fikir, 8eg). Ters bağ =
//    kümenin bind_to_world'ünün tersi; iskeletli köşeler bağ anındaki DÜNYA uzayına alınır (geometry_to_bone ile), yani mesh'ler
//    arası tutarlı. Kemiğin altındaki iskeletsiz parça "kemiğe takılı" çizilir (8ef).
//  - Klipler: her animasyon yığını 30 Hz örneklenir (ufbx_evaluate_transform; eğri türü fark etmez), ClipBuilder sıkıştırır.
//  - Malzeme: temel renk çarpanı + renk dokusu (gömülü içerik ya da dosya: göreli ad, ad, FBX dizininde taban ad), normal haritası.
// SINIR: morph (blend shape), kamera/ışık, çoklu UV kanalı yok (sayılmaz değil: Model::note söyler).
#include "content/fbx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stb_image.h>
#include <ufbx.h>

#include "content/meshopt.hpp"
#include "sim/animation.hpp"

namespace tulpar::engine::content {

namespace {
void hata(Model *out, const char *m, const char *ek = nullptr) {
  std::snprintf(out->error, sizeof out->error, "%s%s%s", m, ek ? ": " : "", ek ? ek : "");
}
Mat4 mat_of(const ufbx_matrix &u) {
  Mat4 m;
  for (int c = 0; c < 4; c++) {
    m.m[c][0] = (float)u.cols[c].x;
    m.m[c][1] = (float)u.cols[c].y;
    m.m[c][2] = (float)u.cols[c].z;
    m.m[c][3] = c == 3 ? 1.0f : 0.0f;
  }
  return m;
}
Vec3 v3(const ufbx_vec3 &v) { return Vec3{(float)v.x, (float)v.y, (float)v.z}; }
Quat q4(const ufbx_quat &q) { return Quat{(float)q.x, (float)q.y, (float)q.z, (float)q.w}; }

uint8_t *dosya_oku(const char *yol, size_t *n) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  const long s = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (s <= 0 || s > (256l << 20)) { std::fclose(f); return nullptr; }
  uint8_t *b = static_cast<uint8_t *>(std::malloc((size_t)s));
  if (b && std::fread(b, 1, (size_t)s, f) != (size_t)s) { std::free(b); b = nullptr; }
  std::fclose(f);
  *n = (size_t)s;
  return b;
}
// Dokuyu RGBA8'e: gomulu icerik > goreli ad > ad > FBX dizininde taban ad. false: bulunamadi/cozulemedi.
bool doku_yukle(Arena &arena, const ufbx_texture *t, const char *dir, ModelImage *out) {
  const uint8_t *veri = nullptr;
  size_t n = 0;
  uint8_t *sahip = nullptr;
  if (t->content.size && t->content.data) { veri = static_cast<const uint8_t *>(t->content.data); n = t->content.size; }
  else {
    char yol[1200];
    const char *adaylar[3] = {t->relative_filename.data, t->filename.data, nullptr};
    for (int k = 0; k < 3 && !veri; k++) {
      const char *ad = adaylar[k];
      if (k == 2) {   // taban ad
        const char *a = t->filename.length ? t->filename.data : t->relative_filename.data;
        if (!a) break;
        const char *s1 = std::strrchr(a, '/'), *s2 = std::strrchr(a, '\\');
        ad = std::max(s1, s2) ? std::max(s1, s2) + 1 : a;
      }
      if (!ad || !ad[0]) continue;
      if (ad[0] == '/' || (ad[0] && ad[1] == ':')) std::snprintf(yol, sizeof yol, "%s", ad);
      else std::snprintf(yol, sizeof yol, "%s%s", dir, ad);
      for (char *c = yol; *c; c++) if (*c == '\\') *c = '/';
      sahip = dosya_oku(yol, &n);
      veri = sahip;
    }
  }
  if (!veri) return false;
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(veri, (int)n, &w, &h, &comp, 4);
  if (sahip) std::free(sahip);
  if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return false; }
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->rgba = arena.alloc_array<uint8_t>((size_t)w * h * 4);
  if (out->rgba) std::memcpy(out->rgba, px, (size_t)w * h * 4);
  stbi_image_free(px);
  return out->rgba != nullptr;
}

// Kose kaynastirma: acik adresli hash (arena). Anahtar = SkinnedVertex baytlari (konum/normal/uv/eklem/agirlik).
struct Kaynak {
  uint32_t *tablo = nullptr;   // [cap] kose indeksi + 1 (0 bos)
  uint32_t cap = 0;
  static uint64_t ozet(const renderer::SkinnedVertex &v) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&v);
    for (size_t i = 0; i < sizeof v; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
  }
};
} // namespace

bool fbx_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim) {
  *out = Model{};
  ufbx_load_opts opts = {};
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  opts.generate_missing_normals = true;
  ufbx_error err;
  ufbx_scene *sc = ufbx_load_file(path, &opts, &err);
  if (!sc) {
    char ac[200];
    ufbx_format_error(ac, sizeof ac, &err);
    for (char *c = ac; *c; c++) if (*c == '\n') *c = ' ';
    hata(out, "FBX ayristirma", ac);
    return false;
  }
  struct Serbest { ufbx_scene *s; ~Serbest() { ufbx_free_scene(s); } } serbest{sc};
  char dir[1024];
  std::snprintf(dir, sizeof dir, "%s", path);
  if (char *s = std::strrchr(dir, '/')) s[1] = 0; else dir[0] = 0;

  // --- Iskelet: kume kemikleri + atalari (kok dugum haric), ebeveyn-once -----------------------------------------------------------
  const ufbx_node *kume[kModelMaxJoints];
  uint32_t kn = 0;
  bool sigdi = true;
  auto var_mi = [&](const ufbx_node *n) { for (uint32_t i = 0; i < kn; i++) if (kume[i] == n) return (int32_t)i; return -1; };
  auto ekle = [&](const ufbx_node *n) { if (!n || n->is_root || var_mi(n) >= 0) return; if (kn >= kModelMaxJoints) { sigdi = false; return; } kume[kn++] = n; };
  for (size_t s = 0; s < sc->skin_deformers.count; s++)
    for (size_t c = 0; c < sc->skin_deformers.data[s]->clusters.count; c++) ekle(sc->skin_deformers.data[s]->clusters.data[c]->bone_node);
  const uint32_t kemik_n = kn;
  for (uint32_t i = 0; i < kemik_n && sigdi; i++) for (const ufbx_node *a = kume[i]->parent; a && !a->is_root && sigdi; a = a->parent) ekle(a);
  // Kemik olmayan ama animasyonu olan dugumler (dugum animasyonu, 8em): yigit varsa mesh tasiyan/canli dugumler de girsin — basit
  // kural: skin yoksa ve yigit varsa butun mesh dugumleri ve atalari.
  if (sc->skin_deformers.count == 0 && sc->anim_stacks.count > 0)
    for (size_t n = 0; n < sc->nodes.count && sigdi; n++)
      if (sc->nodes.data[n]->mesh) for (const ufbx_node *a = sc->nodes.data[n]; a && !a->is_root && sigdi; a = a->parent) ekle(a);
  if (!sigdi) { hata(out, "iskelet 128 eklemi asiyor"); return false; }
  // ebeveyn-once sirala
  const ufbx_node *sirali[kModelMaxJoints];
  int16_t ebeveyn[kModelMaxJoints];
  bool yer[kModelMaxJoints] = {};
  uint32_t sn = 0;
  while (sn < kn) {
    const uint32_t once = sn;
    for (uint32_t i = 0; i < kn; i++) {
      if (yer[i]) continue;
      const int32_t p = var_mi(kume[i]->parent);
      if (p >= 0 && !yer[p]) continue;
      yer[i] = true;
      sirali[sn++] = kume[i];
    }
    if (sn == once) { hata(out, "eklem hiyerarsisinde dongu"); return false; }
  }
  auto sira_of = [&](const ufbx_node *n) { for (uint32_t i = 0; i < kn; i++) if (sirali[i] == n) return (int32_t)i; return -1; };
  for (uint32_t i = 0; i < kn; i++) ebeveyn[i] = (int16_t)sira_of(sirali[i]->parent);
  if (kn) {
    out->skins = arena.alloc_array_zeroed<ModelSkin>(1);
    if (!out->skins) { hata(out, "arena dolu (skin)"); return false; }
    ModelSkin &ms = out->skins[0];
    ms.joint_count = kn;
    ms.joints = arena.alloc_array_zeroed<sim::Joint>(kn);
    ms.inverse_bind = arena.alloc_array<Mat4>(kn);
    ms.bind_mats = arena.alloc_array<Mat4>(kn);
    ms.joint_names = reinterpret_cast<char (*)[48]>(arena.alloc_array_zeroed<char>((size_t)kn * 48));
    if (!ms.joints || !ms.inverse_bind || !ms.bind_mats || !ms.joint_names) { hata(out, "arena dolu (skin)"); return false; }
    std::snprintf(ms.name, sizeof ms.name, "fbx");
    ms.pre = Mat4{};
    ms.pre_birim = true;
    const ufbx_node *kp = sirali[0]->parent;
    if (kp && !kp->is_root) { ms.pre = mat_of(kp->node_to_world); ms.pre_birim = false; }
    for (uint32_t i = 0; i < kn; i++) {
      const ufbx_node *n = sirali[i];
      sim::Joint &j = ms.joints[i];
      j.parent = ebeveyn[i];
      j.bind_t = v3(n->local_transform.translation);
      j.bind_r = q4(n->local_transform.rotation);
      j.bind_s = v3(n->local_transform.scale);
      std::snprintf(ms.joint_names[i], 48, "%s", n->name.data ? n->name.data : "");
      // ters bag: bu kemigin ilk kumesinin bind_to_world'u; yalniz ata ise dinlenme dunyasi
      bool bulundu = false;
      for (size_t s = 0; s < sc->skin_deformers.count && !bulundu; s++)
        for (size_t c = 0; c < sc->skin_deformers.data[s]->clusters.count && !bulundu; c++) {
          const ufbx_skin_cluster *cl = sc->skin_deformers.data[s]->clusters.data[c];
          if (cl->bone_node != n) continue;
          ms.inverse_bind[i] = inverse_affine(mat_of(cl->bind_to_world));
          bulundu = true;
        }
      if (!bulundu) ms.inverse_bind[i] = inverse_affine(mat_of(n->node_to_world));
      ms.bind_mats[i] = mat_of(n->node_to_world) * ms.inverse_bind[i];
    }
    out->skin_count = 1;
  }

  // --- Malzemeler ve dokular ---------------------------------------------------------------------------------------------------------
  const uint32_t nmat = sc->materials.count ? (uint32_t)sc->materials.count : 1;
  if (nmat > lim.max_materials) { hata(out, "malzeme siniri asildi"); return false; }
  out->materials = arena.alloc_array_zeroed<ModelMaterial>(nmat);
  out->images = arena.alloc_array_zeroed<ModelImage>(lim.max_images ? lim.max_images : 1);
  if (!out->materials || !out->images) { hata(out, "arena dolu (malzeme)"); return false; }
  uint32_t doku_yok = 0;
  auto doku_indeksi = [&](const ufbx_texture *t, bool srgb, bool normal) -> int32_t {
    if (!t) return -1;
    if (!lim.images || out->image_count >= lim.max_images) return -1;
    ModelImage &im = out->images[out->image_count];
    im = ModelImage{};
    if (!doku_yukle(arena, t, dir, &im)) { doku_yok++; return -1; }
    im.srgb = srgb;
    im.normal_map = normal;
    return (int32_t)out->image_count++;
  };
  for (uint32_t i = 0; i < nmat; i++) {
    ModelMaterial &mm = out->materials[i];
    mm.base_color = {1, 1, 1};
    mm.image = -1;
    mm.metallic = 0.0f;
    mm.roughness = 0.8f;   // TUZAK 8u: varsayilan ELLE
    mm.has_pbr = true;
    mm.orm_image = mm.normal_image = mm.emissive_image = -1;
    mm.normal_scale = 1.0f;
    if (i >= sc->materials.count) continue;
    const ufbx_material *um = sc->materials.data[i];
    const ufbx_material_map &bc = um->pbr.base_color.has_value ? um->pbr.base_color : um->fbx.diffuse_color;
    if (bc.has_value) mm.base_color = Vec3{(float)bc.value_vec4.x, (float)bc.value_vec4.y, (float)bc.value_vec4.z};
    const ufbx_texture *bt = um->pbr.base_color.texture ? um->pbr.base_color.texture : um->fbx.diffuse_color.texture;
    mm.image = doku_indeksi(bt, true, false);
    if (mm.image >= 0) mm.base_color = Vec3{1, 1, 1};   // FBX'te dokulu malzemenin rengi cogu araclarda doku ile CARPILMAZ (Mixamo 0.8 gri)
    const ufbx_texture *nt = um->pbr.normal_map.texture ? um->pbr.normal_map.texture : um->fbx.normal_map.texture;
    mm.normal_image = doku_indeksi(nt, false, true);
  }
  out->material_count = nmat;

  // --- Mesh'ler: dugum x malzeme parcasi ---------------------------------------------------------------------------------------------
  uint32_t parca_n = 0;
  for (size_t n = 0; n < sc->nodes.count; n++) {
    const ufbx_node *nd = sc->nodes.data[n];
    if (!nd->mesh) continue;
    for (size_t p = 0; p < nd->mesh->material_parts.count; p++) parca_n += nd->mesh->material_parts.data[p].num_triangles ? 1u : 0u;
    if (!nd->mesh->material_parts.count && nd->mesh->num_triangles) parca_n++;
  }
  if (parca_n > lim.max_meshes || parca_n > lim.max_instances) { hata(out, "mesh siniri asildi"); return false; }
  out->meshes = arena.alloc_array_zeroed<ModelMesh>(parca_n ? parca_n : 1);
  out->instances = arena.alloc_array_zeroed<ModelInstance>(parca_n ? parca_n : 1);
  if (!out->meshes || !out->instances) { hata(out, "arena dolu (mesh)"); return false; }
  uint32_t mi = 0;
  bool ilk = true;
  auto sinir = [&](Vec3 q) {
    if (ilk) { out->bounds_min = out->bounds_max = q; ilk = false; return; }
    out->bounds_min = {std::min(out->bounds_min.x, q.x), std::min(out->bounds_min.y, q.y), std::min(out->bounds_min.z, q.z)};
    out->bounds_max = {std::max(out->bounds_max.x, q.x), std::max(out->bounds_max.y, q.y), std::max(out->bounds_max.z, q.z)};
  };
  uint32_t *tri = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * 3 * 64));
  uint32_t tri_cap = 64;
  struct TriSerbest { uint32_t **p; ~TriSerbest() { std::free(*p); } } tri_serbest{&tri};
  for (size_t n = 0; n < sc->nodes.count; n++) {
    const ufbx_node *nd = sc->nodes.data[n];
    const ufbx_mesh *me = nd->mesh;
    if (!me || !me->num_triangles) continue;
    if (me->max_face_triangles + 2 > tri_cap) {
      tri_cap = (uint32_t)me->max_face_triangles + 2;
      std::free(tri);
      tri = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * 3 * tri_cap));
      if (!tri) { hata(out, "bellek"); return false; }
    }
    const ufbx_skin_deformer *skin = me->skin_deformers.count ? me->skin_deformers.data[0] : nullptr;
    const bool iskeletli = skin && out->skin_count && skin->clusters.count;
    // Iskeletli kose: bag anindaki dunya uzayi = cluster.bind_to_world * geometry_to_bone (mesh'in her kumesinde ayni)
    Mat4 g2w = mat_of(nd->geometry_to_world);
    if (iskeletli) g2w = mat_of(skin->clusters.data[0]->bind_to_world) * mat_of(skin->clusters.data[0]->geometry_to_bone);
    int16_t kume_eklem[kModelMaxJoints * 4];   // kume -> iskelet eklemi
    const uint32_t kc = iskeletli ? (uint32_t)std::min<size_t>(skin->clusters.count, sizeof kume_eklem / sizeof kume_eklem[0]) : 0;
    for (uint32_t c = 0; c < kc; c++) kume_eklem[c] = (int16_t)sira_of(skin->clusters.data[c]->bone_node);
    const uint32_t parcalar = me->material_parts.count ? (uint32_t)me->material_parts.count : 1;
    for (uint32_t p = 0; p < parcalar; p++) {
      const ufbx_mesh_part *mp = me->material_parts.count ? &me->material_parts.data[p] : nullptr;
      const uint32_t yuz_n = mp ? (uint32_t)mp->num_faces : (uint32_t)me->num_faces;
      const uint32_t ucgen_n = mp ? (uint32_t)mp->num_triangles : (uint32_t)me->num_triangles;
      if (!ucgen_n) continue;
      ModelMesh &mm = out->meshes[mi];
      mm.skin = iskeletli ? 0 : -1;
      int32_t mat = -1;
      if (mp && p < me->materials.count && me->materials.data[p]) mat = (int32_t)me->materials.data[p]->typed_id;
      if (mp && nd->materials.count > p && nd->materials.data[p]) mat = (int32_t)nd->materials.data[p]->typed_id;
      mm.material = mat >= 0 && (uint32_t)mat < nmat ? mat : 0;
      const uint32_t kose_ust = ucgen_n * 3;
      renderer::SkinnedVertex *sv = arena.alloc_array_zeroed<renderer::SkinnedVertex>(kose_ust);
      mm.indices = arena.alloc_array<uint32_t>(kose_ust);
      Kaynak kay;
      kay.cap = 1;
      while (kay.cap < kose_ust * 2) kay.cap <<= 1;
      kay.tablo = arena.alloc_array_zeroed<uint32_t>(kay.cap);
      if (!sv || !mm.indices || !kay.tablo) { hata(out, "arena dolu (vertex)"); return false; }
      uint32_t vn = 0, in = 0;
      for (uint32_t f = 0; f < yuz_n; f++) {
        const ufbx_face yuz = me->faces.data[mp ? mp->face_indices.data[f] : f];
        const uint32_t t = ufbx_triangulate_face(tri, (size_t)tri_cap * 3, me, yuz);
        for (uint32_t k = 0; k < t * 3; k++) {
          const uint32_t ix = tri[k];
          renderer::SkinnedVertex v{};
          const Vec3 p0 = v3(ufbx_get_vertex_vec3(&me->vertex_position, ix));
          Vec3 nr = me->vertex_normal.exists ? v3(ufbx_get_vertex_vec3(&me->vertex_normal, ix)) : Vec3{0, 1, 0};
          if (iskeletli) {
            v.pos = transform_point(g2w, p0);
            const Vec4 n4 = g2w * Vec4{nr.x, nr.y, nr.z, 0.0f};
            nr = Vec3{n4.x, n4.y, n4.z};
          } else {
            v.pos = p0;
          }
          const float nl = std::sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
          v.nrm = nl > 1e-12f ? Vec3{nr.x / nl, nr.y / nl, nr.z / nl} : Vec3{0, 1, 0};
          if (me->vertex_uv.exists) {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&me->vertex_uv, ix);
            v.uv = Vec2{(float)uv.x, 1.0f - (float)uv.y};   // FBX UV'si alttan; glTF/Vulkan ustten
          }
          if (iskeletli) {
            const uint32_t cp = me->vertex_indices.data[ix];
            float w[4] = {0, 0, 0, 0};
            uint8_t jj[4] = {0, 0, 0, 0};
            if (cp < skin->vertices.count) {
              const ufbx_skin_vertex &svx = skin->vertices.data[cp];
              // en buyuk 4 agirlik
              for (uint32_t q = 0; q < svx.num_weights; q++) {
                const ufbx_skin_weight &sw = skin->weights.data[svx.weight_begin + q];
                if (sw.cluster_index >= kc || kume_eklem[sw.cluster_index] < 0) continue;
                const float a = (float)sw.weight;
                int en_kucuk = 0;
                for (int z = 1; z < 4; z++) if (w[z] < w[en_kucuk]) en_kucuk = z;
                if (a > w[en_kucuk]) { w[en_kucuk] = a; jj[en_kucuk] = (uint8_t)kume_eklem[sw.cluster_index]; }
              }
            }
            float top = w[0] + w[1] + w[2] + w[3];
            if (top <= 0) { w[0] = 1; top = 1; }
            for (int z = 0; z < 4; z++) { v.joints[z] = jj[z]; v.weights[z] = (uint16_t)std::lround(w[z] / top * 65535.0f); }
          }
          // kaynastir
          const uint64_t h = Kaynak::ozet(v);
          uint32_t s = (uint32_t)h & (kay.cap - 1), bulunan = 0;
          while (kay.tablo[s]) {
            const uint32_t a = kay.tablo[s] - 1;
            if (!std::memcmp(&sv[a], &v, sizeof v)) { bulunan = a + 1; break; }
            s = (s + 1) & (kay.cap - 1);
          }
          if (!bulunan) { sv[vn] = v; kay.tablo[s] = vn + 1; bulunan = ++vn; }
          mm.indices[in++] = bulunan - 1;
        }
      }
      mm.vertex_count = vn;
      mm.index_count = in;
      mm.verts = arena.alloc_array<renderer::Vertex>(vn ? vn : 1);
      if (!mm.verts) { hata(out, "arena dolu (vertex)"); return false; }
      for (uint32_t v = 0; v < vn; v++) mm.verts[v] = renderer::Vertex{sv[v].pos, sv[v].nrm, sv[v].uv};
      if (iskeletli) mm.skin_verts = sv;
      if (!iskeletli && lim.optimize && !mesh_optimize(arena, mm, &out->opt)) { hata(out, "arena dolu (meshopt)"); return false; }
      if (lim.lods && (!iskeletli || mm.index_count / 3 >= 1500)) {
        const float ratios[kModelMaxLods] = {0.5f, 0.25f};
        if (!mesh_build_lods(arena, mm, ratios, kModelMaxLods, iskeletli ? lim.lod_error * 0.5f : lim.lod_error)) { hata(out, "arena dolu (lod)"); return false; }
      }
      ModelInstance &inst = out->instances[mi];
      inst.mesh = mi;
      inst.world = iskeletli ? Mat4{} : mat_of(nd->geometry_to_world);
      // Kemige takili iskeletsiz parca: atalardaki (ya da kendisi) ilk eklem
      if (!iskeletli && out->skin_count)
        for (const ufbx_node *a = nd; a && !a->is_root && !inst.kemik_skin1; a = a->parent) {
          const int32_t k = sira_of(a);
          if (k < 0) continue;
          inst.kemik_skin1 = 1;
          inst.kemik = (uint8_t)k;
          inst.kemik_bag = inverse_affine(mat_of(a->node_to_world) * out->skins[0].inverse_bind[k]) * inst.world;
        }
      for (uint32_t v = 0; v < mm.vertex_count; v++) sinir(transform_point(inst.world, mm.verts[v].pos));
      mi++;
    }
  }
  out->mesh_count = mi;
  out->instance_count = mi;
  if (!mi) { hata(out, "FBX'te ucgenli mesh yok"); return false; }

  // --- Klipler: animasyon yiginlari, 30 Hz ----------------------------------------------------------------------------------------
  if (out->skin_count && sc->anim_stacks.count) {
    uint32_t nc = (uint32_t)sc->anim_stacks.count;
    if (nc > lim.max_clips) { out->clips_dropped = nc - lim.max_clips; nc = lim.max_clips; }
    out->clips = arena.alloc_array_zeroed<ModelClip>(nc);
    if (!out->clips) { hata(out, "arena dolu (klip)"); return false; }
    const float rate = lim.clip_sample_rate > 0 ? lim.clip_sample_rate : 30.0f;
    uint32_t cn = 0;
    for (uint32_t a = 0; a < nc; a++) {
      const ufbx_anim_stack *st = sc->anim_stacks.data[a];
      const float dur = (float)(st->time_end - st->time_begin);
      if (!(dur > 0.0f)) continue;
      const uint32_t samples = std::max<uint32_t>(2, (uint32_t)(dur * rate) + 1);
      sim::RawTrack *tracks = arena.alloc_array_zeroed<sim::RawTrack>(kn);
      Vec3 *tt = arena.alloc_array<Vec3>((size_t)kn * samples), *ss = arena.alloc_array<Vec3>((size_t)kn * samples);
      Quat *rr = arena.alloc_array<Quat>((size_t)kn * samples);
      if (!tracks || !tt || !ss || !rr) { hata(out, "arena dolu (klip)"); return false; }
      for (uint32_t j = 0; j < kn; j++) {
        for (uint32_t k = 0; k < samples; k++) {
          const double t = st->time_begin + std::min<double>((double)k / rate, dur);
          const ufbx_transform tr = ufbx_evaluate_transform(st->anim, sirali[j], t);
          tt[(size_t)j * samples + k] = v3(tr.translation);
          rr[(size_t)j * samples + k] = q4(tr.rotation);
          ss[(size_t)j * samples + k] = v3(tr.scale);
        }
        tracks[j].t = tt + (size_t)j * samples;
        tracks[j].r = rr + (size_t)j * samples;
        tracks[j].s = ss + (size_t)j * samples;
      }
      sim::RawClip raw;
      raw.tracks = tracks;
      raw.joints = kn;
      raw.samples = samples;
      raw.sample_rate = rate;
      ModelClip &mc = out->clips[cn];
      // Blender/Maya gelenegi "Armature|Run": son '|' sonrasi (otomatik animator rolleri kisa addan bulur)
      const char *ka = st->name.data ? st->name.data : "";
      if (const char *dik = std::strrchr(ka, '|')) if (dik[1]) ka = dik + 1;
      std::snprintf(mc.name, sizeof mc.name, "%s", ka);
      mc.duration = dur;
      mc.sample_rate = rate;
      mc.skin = 0;
      mc.clip = sim::ClipBuilder::build(arena, raw, 1e-5f, &mc.stats);
      if (!mc.clip) { hata(out, "klip sikistirilamadi"); return false; }
      cn++;
    }
    out->clip_count = cn;
  }
  if (doku_yok) std::snprintf(out->note, sizeof out->note, "%u doku bulunamad\xC4\xB1 (FBX'in yan\xC4\xB1ndaki dosyalar\xC4\xB1 da kopyala)", doku_yok);
  return true;
}

} // namespace tulpar::engine::content
