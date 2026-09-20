#include "content/primitives.hpp"

namespace tulpar::engine::content {

namespace {

using renderer::Renderer;
using renderer::Vertex;

// Ureticiler indeks sayisi donduruyor; create_mesh tepe sayisini da istiyor.
// En buyuk indeks + 1 tam olarak yazilan tepe sayisidir (ureticiler tepeleri
// sirayla yazar ve hepsini en az bir kez indeksler).
//
// Tepe sayisini ELLE yazmak (33*17 gibi) segment varsayilanina GIZLI bir
// bagimlilik kurardi: seg_h 32'den baska bir sey olursa create_mesh YANLIS
// boyutta tampon ayirir ve GPU cop okur.
uint32_t verts_used(const uint32_t *idx, uint32_t n) {
  uint32_t m = 0;
  for (uint32_t i = 0; i < n; i++)
    if (idx[i] + 1u > m) m = idx[i] + 1u;
  return m;
}

} // namespace

uint32_t build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out) {
  if (!out) return 0;
  for (uint32_t i = 0; i < kPrimitiveSlotCount; i++) out[i] = renderer::MeshHandle{};

  // TEK gecici tampon, `static`: ~640 tepe * 32 B + 3328 indeks * 4 B ~ 33 KB.
  // Yiginda degil cunku bazi hostlarda ana yigin 1 MB; heap'te de degil cunku
  // AllocGate global `new`i sayiyor ve bu yol ayirma yapmamali. Fonksiyon
  // yukleme aninda TEK SEFER ve tek is parcacigindan cagriliyor.
  static Vertex v[Renderer::kPrimitiveMaxVerts];
  static uint32_t idx[Renderer::kPrimitiveMaxIndices];

  uint32_t built = 0;
  // Uretec tavani asmissa mesh KURULMAZ: bozuk mesh cizmektense eksik ciz.
  // Tavanlar renderer.hpp'de ureticilerin gercek sayilarinin ustunde tutuluyor,
  // yani bu dal bugun calismaz — ama sessiz bellek bozulmasi olacagi icin
  // varsayim ACIKCA olculuyor (bir uretecin segment sayisi degisirse burada
  // durur, GPU'da degil).
  auto make = [&](uint32_t slot, uint32_t index_count) {
    if (slot >= kPrimitiveSlotCount) return renderer::MeshHandle{};
    if (index_count == 0 || index_count > Renderer::kPrimitiveMaxIndices) return renderer::MeshHandle{};
    const uint32_t vc = verts_used(idx, index_count);
    if (vc == 0 || vc > Renderer::kPrimitiveMaxVerts) return renderer::MeshHandle{};
    out[slot] = r.create_mesh(v, vc, idx, index_count);
    if (out[slot].valid()) built++;
    return out[slot];
  };

  // Kup: parcacik yuvasiyla AYNI mesh'i paylasiyor — ayni geometri icin iki
  // GPU tamponu ayirmanin anlami yok. Yuva 0'in dolu olmasi parcacik cizimini
  // acan sey: eskiden hic doldurulmuyordu ve parcaciklar yayiliyor, simule
  // ediliyor ama TEK PIKSEL cizilmiyordu (sessizce, 0 da gecerli bir indeks).
  out[kPrimParticle] = make(kPrimCube, Renderer::cube(v, idx));

  make(kPrimPlane, Renderer::plane(v, idx));
  make(kPrimSphere, Renderer::sphere(v, idx));
  make(kPrimCapsule, Renderer::capsule(v, idx));
  make(kPrimCylinder, Renderer::cylinder(v, idx));
  make(kPrimCone, Renderer::cone(v, idx));
  make(kPrimQuad, Renderer::quad(v, idx));
  make(kPrimTorus, Renderer::torus(v, idx));
  return built;
}

} // namespace tulpar::engine::content
