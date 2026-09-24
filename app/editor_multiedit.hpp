// L6 APP — editor_multiedit: coklu secimde alan duzenlemesini yaymak.
//
// NEDEN: 5 nesne secip Ozellikler panelinde bir alani degistirmek YALNIZ ana
// seciliyi degistiriyordu (panel "alanlar ana secilide" diyordu). UE5, Unity,
// Source 2 ve CryEngine'de N nesnenin ortak alani BIRLIKTE duzenlenir; 20
// nesnenin rengini degistirmek 20 ayri tik olmamali.
//
// YAKLASIM: her bilesen paneline ayri "coklu" kod yazmak yerine, ana secilideki
// degisiklik ALAN-YAPRAGI duzeyinde fark alinir ve digerlerine uygulanir.
// Yaprak: float/int/bool/enum tek basina, Vec'in her bileseni ayri (pos.x'i
// yazmak digerlerinin y/z'sine dokunmaz -- UE5 ile ayni), char dizisi butun.
// components/flags BIT duzeyinde; name ve parent hic yayilmaz.
#pragma once
#include <cstdint>

#include "content/scene.hpp"

namespace tulpar::engine::app {

// NESNE OZELLIKLERI (E3) yaprak degil: ada gore sirali bir liste. Fark ADA
// GORE alinir (eklenen / degisen / kaldirilan) ve YALNIZ ayni betigi tasiyan
// hedefe uygulanir -- ozelligin anlami betikten gelir; "hiz" dusman
// betiginde bir sey, kapi betiginde baska bir sey (ya da hic) demek. Betik
// yolu karsilastirmasi yapraklar uygulandiktan SONRA yapilir: ayni duzenleme
// betigi de degistirdiyse hedef artik o betigi tasir.
struct MultieditStats {
  uint32_t props_applied = 0;        // hedefe yazilan/silinen ozellik
  uint32_t props_script_skipped = 0; // ozellik farki vardi ama hedefin betigi farkli: yayilmadi
  uint32_t props_overflow = 0;       // hedefte yer yoktu (kSceneMaxProps): REDDEDILDI, SAYILDI
};

// before -> after farkini target'a uygular. Donus: target degisti mi.
// stats verilirse ozellik sayaclari ona EKLENIR (cagiran secim boyunca biriktirir).
bool multiedit_apply(const content::SceneEntity &before, const content::SceneEntity &after,
                     content::SceneEntity *target, MultieditStats *stats = nullptr);
// Tablodaki yaprak sayisi (kapi ve testler icin).
uint32_t multiedit_leaf_count();

} // namespace tulpar::engine::app
