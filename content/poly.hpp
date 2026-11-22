// L6 CONTENT — Çokgen (P2, docs/MODEL-ANIMASYON-PLANI.md): editörde çizilen XZ çokgeninin yükseltilmiş katı hali.
//
// Tek kaynak: sahne çalışma zamanı, editör çizimi ve fizik AYNI fonksiyonları çağırır (kopya = iki doğruluk kaynağı).
//  - `poly_soup`: üçgen çorbası mesh (üst, taban −Y, yan yüzler sert kenarlı dış normal). Köşe sayısı SABİT üst sınır `kPolyMaxVerts` ve
//    indeks gerekmez (sıralı): editörde renderer dinamik mesh'i olarak her düzenlemede aynı tampona yazılır (mesh tutamacı sızmaz).
//  - `poly_convex_parts`: üçgenleri Hertel–Mehlhorn ile birleştirip DIŞBÜKEY parçalara böler (içbükey çokgenin çarpışması için:
//    tek bir dışbükey gövde L'nin içindeki boşluğu kapatırdı).
// P2 düzenleme (2026-11-22, ERTELİ maddenin dilimi — "ProBuilder düzeyi"nin çokgene düşen kısmı):
//  - KÖŞE: nokta başına üst yükseklik farkı `dy` (rampa, eğimli çatı). Üst yüz üçgen başına düz normal; yan yüz dikey (düzlemsel) kalır.
//  - BOOLEAN (fark): çokgenden TEK delik çıkarılır (`hole`, en çok 16 nokta, tamamen içeride, kenarları dış kenarı kesmez). Dış halka ile
//    delik en yakın GÖRÜNÜR köşe çiftinden köprüyle tek halkaya (anahtar deliği) birleşir; kulak kesme çakışık köprü köşelerini dışlar.
//    Deliğin iç duvarları deliğe bakar. Delik noktalarının üst yüksekliği dış noktaların ters-uzaklık karesi ağırlıklı ortalaması.
//  - UV: üst/taban (x, z) `uv_rot` derece döndürülüp `uv_scale` ile çarpılır; yan yüzde u ÇEVRE BOYUNCA birikir (kenardan kenara
//    kesintisiz doku; eskiden her kenar 0'dan başlıyordu), v = y · uv_scale.
// Sınırlar (sessiz değil): en çok 32 nokta + 16 delik noktası; geçersiz şekil (alan ~ 0, NaN, <3 nokta, delik dışarı taşar ya da kesişir,
// üst tabanın altına iner) 0 / false döner.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kPolyMaxPoints = 32;
constexpr uint32_t kPolyMaxHolePoints = 16;
constexpr uint32_t kPolyMaxRing = kPolyMaxPoints + kPolyMaxHolePoints + 2;   // anahtar deliği halkası (köprü köşeleri iki kez)
// En çok üçgen: deliksiz üst (n-2) + taban (n-2) + yan 2n = 4n-4; delikli (n+m) + (n+m) + 2(n+m) = 4(n+m) = 192.
constexpr uint32_t kPolyMaxTris = 4 * (kPolyMaxPoints + kPolyMaxHolePoints);
constexpr uint32_t kPolyMaxVerts = kPolyMaxTris * 3;
constexpr uint32_t kPolyMaxParts = kPolyMaxRing;
constexpr uint32_t kPolyMaxPartVerts = 3 * kPolyMaxRing;

struct PolyShape {
  const float *xz = nullptr;     // n adet (x, z)
  uint32_t n = 0;
  float h = 1.0f;                // > 0 katı (taban y=0, üst y=h+dy); <= 0 yalnız üst yüz (y=dy)
  const float *dy = nullptr;     // [n] üst yükseklik farkı (nullptr = hepsi 0)
  const float *hole = nullptr;   // hole_n adet (x, z); hole_n = 0 delik yok
  uint32_t hole_n = 0;
  float uv_scale = 1.0f;         // doku tekrarı (metre başına)
  float uv_rot_deg = 0.0f;       // üst/taban doku açısı
};

// `out` en az kPolyMaxVerts köşe. Dönüş: üretilen köşe sayısı (3 x üçgen), 0 = geçersiz şekil. KULLANILMAYAN kuyruk dokunulmaz:
// çağıran gerekirse sıfır alanlı doldurur (`poly_soup_pad`).
uint32_t poly_soup(const PolyShape &s, renderer::Vertex *out);
// Eski imza (düz üst, deliksiz, varsayılan UV).
uint32_t poly_soup(const float *xz, uint32_t n, float h, renderer::Vertex *out);
// out[used .. kPolyMaxVerts) köşelerini ilk köşeye çökertir (sıfır alanlı üçgenler: çizilmez, ama tampon sabit boyutta kalır).
void poly_soup_pad(renderer::Vertex *out, uint32_t used);

// Dışbükey parçalar (şekilden): part_n[k] = k. parçanın köşe sayısı; part_pts = parçaların köşeleri art arda (x, üst y, z), yukarıdan
// CCW. Dönüş: parça sayısı (0 = geçersiz). part_n en az kPolyMaxParts, part_pts en az kPolyMaxPartVerts. Eğimli üstte parça gövdesi
// (taban + üst köşelerin dışbükey zarfı) eyer biçimli üstü hafifçe aşar.
uint32_t poly_convex_parts(const PolyShape &s, uint32_t *part_n, Vec3 *part_pts);
// Eski imza (deliksiz): part_idx = çokgenin NOKTA indeksleri. part_n en az kPolyMaxPoints, part_idx en az 3*kPolyMaxPoints.
uint32_t poly_convex_parts(const float *xz, uint32_t n, uint32_t *part_n, uint32_t *part_idx);

// Çokgenin alanı (m²; işaretsiz). Geçersiz girdi için 0.
float poly_area(const float *xz, uint32_t n);
// Şeklin alanı (dış − delik); geçersiz şekil 0.
float poly_shape_area(const PolyShape &s);

} // namespace tulpar::engine::content
