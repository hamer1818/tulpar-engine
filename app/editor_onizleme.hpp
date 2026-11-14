// L6 APP — Kaynaklar karosunda MODEL ÖNİZLEMESİ (M1): GPU'da ikinci bir çizim geçişi açmadan, modelin en kaba LOD üçgenleri CPU'da
// döndürülüp dik izdüşümle karoya boyanır (ressam sıralaması + yön ışığı). Karo üstündeyken döner, değilken sabit açıda durur.
//
// Neden CPU: karo başına ayrı bir offscreen hedef + ImGui doku kaydı, kare grafiğine her karo için bir geçiş demekti; önizleme
// "bu hangi model, hangi yöne bakıyor, ne boyda" sorusu için yeterli kalite ister, doğru ışık değil. Bedel sabit: en çok
// kThumbMaxTris üçgen (kaba LOD, gerekirse adımlı örnekleme), ayırma yok (çağıranın tamponları).
//
// İskeletli mesh BIND pozunda çizilir (poz yok); örnek (instance) matrisleri ve içe aktarma ayarı (Model::import_xform) dahil —
// yani önizleme ayar penceresinde değiştirilen ekseni/pivotu da gösterir.
#pragma once
#include <cstdint>

#include "content/model.hpp"

namespace tulpar::engine::app {

constexpr uint32_t kThumbMaxTris = 1024;

struct ThumbTri {
  float x[3], y[3];   // karo pikseli (sol-üst 0,0; y aşağı)
  float shade;        // 0..1 (yön ışığı, arka yüz elenmiş)
  float depth;        // sıralama anahtarı (büyük = uzak; çizim uzaktan yakına)
};

// Model -> karoya sığdırılmış, sıralı üçgenler. yaw: Y ekseni etrafında radyan. pitch sabit (yukarıdan ~25°). w,h: karo pikseli.
// Dönüş: üretilen üçgen sayısı (<= cap). Boş / geçersiz modelde 0. out uzaktan yakına sıralı.
uint32_t model_thumb_triangles(const content::Model &m, float yaw, float w, float h, ThumbTri *out, uint32_t cap);

// ImGui çizim listesine boyar (p0 sol-üst, p1 sağ-alt). Renk tonu: base_rgb (0..1). Dönüş: çizilen üçgen.
uint32_t model_thumb_draw(const content::Model &m, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]);

} // namespace tulpar::engine::app
