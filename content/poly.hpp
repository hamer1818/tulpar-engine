// L6 CONTENT — Çokgen (P2, docs/MODEL-ANIMASYON-PLANI.md): editörde çizilen XZ çokgeninin yükseltilmiş katı hali.
//
// Tek kaynak: sahne çalışma zamanı, editör çizimi ve fizik AYNI fonksiyonları çağırır (kopya = iki doğruluk kaynağı).
//  - `poly_soup`: üçgen çorbası mesh (üst +Y, taban −Y, yan yüzler sert kenarlı dış normal). Köşe sayısı SABİT üst sınır `kPolyMaxVerts` ve
//    indeks gerekmez (sıralı): editörde renderer dinamik mesh'i olarak her düzenlemede aynı tampona yazılır (mesh tutamacı sızmaz).
//  - `poly_convex_parts`: üçgenleri Hertel–Mehlhorn ile birleştirip DIŞBÜKEY parçalara böler (içbükey çokgenin çarpışması için:
//    tek bir dışbükey gövde L'nin içindeki boşluğu kapatırdı).
// Sınırlar (sessiz değil): en çok 32 nokta; geçersiz çokgen (alan ~ 0, NaN, <3 nokta) false döner.
#pragma once
#include <cstdint>

#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kPolyMaxPoints = 32;
// En çok üçgen: üst (n-2) + taban (n-2) + yan 2n = 4n-4 = 124; çorba köşesi = 3 x üçgen.
constexpr uint32_t kPolyMaxTris = 4 * kPolyMaxPoints - 4;
constexpr uint32_t kPolyMaxVerts = kPolyMaxTris * 3;

// xz: n adet (x, z) çifti; h > 0 kati (taban y=0, üst y=h), h <= 0 yalnız üst yüz (y=0). `out` en az kPolyMaxVerts köşe.
// Dönüş: üretilen köşe sayısı (3 x üçgen), 0 = geçersiz çokgen. KULLANILMAYAN kuyruk dokunulmaz: çağıran gerekirse sıfır alanlı doldurur
// (`poly_soup_pad`).
uint32_t poly_soup(const float *xz, uint32_t n, float h, renderer::Vertex *out);
// out[used .. kPolyMaxVerts) köşelerini ilk köşeye çökertir (sıfır alanlı üçgenler: çizilmez, ama tampon sabit boyutta kalır).
void poly_soup_pad(renderer::Vertex *out, uint32_t used);

// Dışbükey parçalar. part_n[k] = k. parçanın nokta sayısı, part_idx = parçaların nokta indeksleri art arda (toplam <= 3*n), hepsi yukarıdan CCW.
// Dönüş: parça sayısı (0 = geçersiz). part_n en az kPolyMaxPoints, part_idx en az 3*kPolyMaxPoints eleman olmalı.
uint32_t poly_convex_parts(const float *xz, uint32_t n, uint32_t *part_n, uint32_t *part_idx);

// Çokgenin alanı (m²; işaretsiz). Geçersiz girdi için 0.
float poly_area(const float *xz, uint32_t n);

} // namespace tulpar::engine::content
