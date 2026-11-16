// L6 CONTENT — A6 karakter yeniden hedefleme (2026-11-16; MODEL-ANIMASYON-PLANI Faz A6).
//
// Bir modelin (kaynak: Mixamo'dan indirilmiş animasyon GLB'si, başka karakter) kliplerini BAŞKA iskeletli modele uygular.
// 1) İnsansı eşleme: eklem ADLARINDAN 21 kanonik kemik (kalça, omurga, göğüs, boyun, baş, köprücük/üst kol/ön kol/el, uyluk/baldır/ayak/
//    parmak x 2). Mixamo (`mixamorig:LeftForeArm`), Unreal (`lowerarm_l`, `spine_03`), Blender/Rigify (`forearm.L`), genel (`LeftArm`),
//    Sketchfab ekleri (`hand_l_042`) tanınır; parmaklar ve uç kemikler (`HeadTop_End`) atlanır.
// 2) Dünya uzayında dinlenme farkı: W_hedef(j) = (W_kaynak(anim) * W_kaynak(dinlenme)^-1) * W_hedef(dinlenme); yerel dönüş ebeveynden.
//    Kemik EKSEN gelenekleri farklı olsa da doğru (yerel dönüşü kopyalayan saf yöntem bozulur). Eşlenmeyen hedef eklem dinlenmede kalır.
//    Kalça ötelemesi kalça yükseklikleri oranıyla ölçeklenir (birim farkı da böylece düşer).
// SINIR: iki modelin dinlenme pozu benzer olmalı (ikisi de T ya da ikisi de A); T -> A farkı kollarda açı farkı olarak kalır. Dinlenme
// pozu keyfi olan modelde (Fab "Mike": yatık) sonuç yanlış olur — rapor eşlenen kemik sayısını verir, kalite göz kontrolü ister.
#pragma once
#include <cstdint>

#include "content/model.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kHumanoidBones = 21;
const char *humanoid_bone_name(uint32_t b);   // "hips", "upper_arm_l" ...
// Eklem indeksleri (kanonik -> skin eklemi, -1 yok). Dönüş: eşlenen kemik sayısı.
uint32_t humanoid_map(const ModelSkin &sk, int16_t out[kHumanoidBones]);

struct RetargetReport {
  uint32_t mapped = 0;          // iki modelde de bulunan kanonik kemik
  uint32_t clips = 0;           // eklenen klip
  uint32_t clips_dropped = 0;   // tavan (GltfLimits::max_clips) yüzünden eklenemeyen
  char error[160] = {0};
};
// Kaynağın iskeletli kliplerini hedefe EKLER (adlar aynen; hedef kendi kliplerini korur). false: rapor neden (kalça yok, < 8 ortak kemik).
// tek_ad: kaynakta TEK iskeletli klip varsa aktarilan klibin adi (Mixamo: dosya adi; klip adi hep "mixamo.com"). nullptr = klip adi.
bool model_retarget(Arena &arena, Model &target, const Model &source, uint32_t max_clips, RetargetReport *rep, const char *tek_ad = nullptr);

} // namespace tulpar::engine::content
