// L6 CONTENT — STL ve OBJ içe aktarma (M2, docs/MODEL-ANIMASYON-PLANI.md).
//
// İkisi de glTF yükleyicisinin ürettiği AYNI `Model` yapısına iner (mesh, malzeme, tek instance, sınır kutusu, meshopt + LOD):
// çizim, gölge, LOD, fizik ve sahne yolu DEĞİŞMEZ. `gltf_load` uzantıya bakıp buraya yönlendirir (.stl / .obj), yani köprü,
// sahne çalışma zamanı ve editör çağrı noktalarına dokunulmaz.
//
//  STL   ikili ve ASCII; üçgen sayısı doğrulaması (ikili başlıktaki sayı ↔ dosya boyutu: kesik/yalan sayaçlı dosya REDDEDİLİR),
//        NaN/Inf reddi; köşe birleştirme + açı eşikli yumuşak normal; tek malzeme (renk). 3B baskı/CAD dosyaları genelde Z-yukarı ve
//        milimetre: varsayılan Y-yukarıya çevrilir ve en büyük boyut `fit_size` metreye sığdırılır (her zaman görünür), pivot taban merkezi.
//  OBJ   v / vt / vn / f (v, v/vt, v//vn, v/vt/vn, NEGATİF indeksler), çokgen yüzler üçgenlenir (kulak kesme), usemtl grupları ayrı mesh,
//        .mtl (yalnız Kd renk; map_Kd doku bu sürümde YOK ve sayılır). Normal yoksa STL ile aynı yumuşak normal.
//
// Sınırlar (sessiz değil, error doldurur): en çok 4M üçgen, 8M köşe.
#pragma once
#include "content/gltf.hpp"

namespace tulpar::engine::content {

struct MeshImportOptions {
  // STL/CAD: Z yukarı -> Y yukarı (x, y, z) -> (x, z, -y). OBJ varsayılanı false (çoğu OBJ zaten Y-yukarı).
  bool z_up = true;
  // > 0 ise model en büyük boyutu bu kadar metre olacak şekilde ölçeklenir (varsayılan 1 m: mm cinsinden dosya da görünür kalır).
  // 0 ise ölçek `scale` (dosya birimi -> metre).
  float fit_size = 1.0f;
  float scale = 1.0f;
  // Pivot: taban merkezi (x,z merkezde, y = 0'da yere oturur). false = dosya orijini.
  bool ground_pivot = true;
  // Yumuşak normal eşiği (derece): bu açıdan küçük komşu yüz normalleri köşede ortalanır; büyükler keskin kenar kalır.
  float smooth_angle_deg = 60.0f;
  // STL tek malzeme rengi.
  Vec3 color{0.8f, 0.8f, 0.85f};
};

// Her ikisi: false => out->error dolu. `opt` null = uzantıya göre varsayılan (STL: z_up, 1 m sığdır; OBJ: Y-yukarı, ölçek korunur).
bool stl_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim = GltfLimits{}, const MeshImportOptions *opt = nullptr);
bool obj_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim = GltfLimits{}, const MeshImportOptions *opt = nullptr);

// Bellekten (test + editör önizlemesi): STL baytları. Dosya yolu gerekmez.
bool stl_load_memory(Arena &arena, const unsigned char *data, size_t size, Model *out, const GltfLimits &lim = GltfLimits{},
                     const MeshImportOptions *opt = nullptr);

// P1: KODLA MESH. Bellekteki köşe (konum + uv) ve indeks listesinden Model (tek malzeme, tek mesh): dönüşüm YOK (eksen/ölçek/pivot
// çağıranın), normal yumuşak (smooth_angle_deg) hesaplanır, meshopt + LOD glTF yoluyla aynı. indeks >= nv ya da üçgen yoksa false (error dolu).
bool mesh_from_triangles(Arena &arena, const Vec3 *pos, const Vec2 *uv, uint32_t nv, const uint32_t *idx, uint32_t ntri, Vec3 color, Model *out,
                         const GltfLimits &lim = GltfLimits{}, float smooth_angle_deg = 60.0f);

// P1/P2: XZ düzlemindeki çokgenin (x,z çiftleri, n >= 3, en çok 256) kulak kesmeyle üçgenlenmesi. Çıktı `tri_out[3*(n-2)]` NOKTA indeksi,
// sarım HER ZAMAN +Y'ye bakan (yukarıdan CCW) — giriş sırası ne olursa olsun. Dönüş: üçgen sayısı; 0 = geçersiz (n < 3, alan ~ 0, NaN).
// Kendini kesen çokgende kulak bulunamazsa yelpaze (n-2 üçgen yine); içbükey çokgen doğru üçgenlenir.
uint32_t polygon_triangulate_xz(const float *xz, uint32_t n, uint32_t *tri_out);

} // namespace tulpar::engine::content
