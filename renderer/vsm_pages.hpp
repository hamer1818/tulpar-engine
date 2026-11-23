// L3 RENDERER — SANAL GOLGE HARITASI (VSM) SAYFA ONBELLEGI (Faz 9, ilk dilim: 2026-11-23).
//
// VSM'in GPU'dan bagimsiz cekirdegi: hangi gölge SAYFASININ gerektigi, hangisinin zaten fiziksel havuzda gecerli
// durdugu, hangisinin YENIDEN CIZILMESI gerektigi. PLAN §5: "VSM page update -> sadece dirty page'ler", butce sabit.
//
// MODEL
//   * Yonlu isik. Isik uzayi = isik yonune dik ortonormal taban (u, v). Clipmap: L seviye, seviye l'nin sayfa
//     kenari base_page_world * 2^l. Seviye secimi ornegin kameraya uzakligindan (yakin = ince seviye).
//   * SAYFA ANAHTARI MUTLAK isik-uzayi izgarasina oturur (seviye, floor(u/kenar), floor(v/kenar)) — kamera kaydikca
//     ayni dunya bolgesinin sayfasi AYNI anahtari tasir, yani onbellek isabeti kamerayla birlikte yasar.
//     (Kameraya gore merkezlenen klasik clipmap her harekette sayfa kaydirir; burada kaydirma yok.)
//   * Fiziksel havuz SABIT (init'te): pool_pages yuva. Yeni sayfa bos yuva alir; yoksa BU KAREDE KULLANILMAYAN en eski
//     yuva tahliye edilir (LRU). Hicbiri yoksa istek KARSILANMAZ ve exhausted SAYILIR — sessizce buyumez (A2).
//   * invalidate(aabb): hareket eden nesnenin isik-uzayi izdusumuyle kesisen YERLESIK sayfalar kirli olur; kirli sayfa
//     bir sonraki istekte yeniden cizim listesine girer. Statik sahne + duran kamera = 0 yeniden cizim.
//   * Kare icinde ayirma yok: tablo, havuz ve cizim listesi init'te arenadan.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::renderer {

struct VsmConfig {
  uint32_t levels = 8;              // clipmap seviye sayisi
  float base_page_world = 2.0f;     // seviye 0'da bir sayfanin dunya kenari (m)
  float level0_distance = 8.0f;     // bu uzakliga kadar seviye 0; her ikiye katlanmada bir seviye yukari
  uint32_t pool_pages = 1024;       // fiziksel sayfa yuvasi (ornek: 128x128 texel sayfa -> 4096^2 atlas)
  uint32_t max_render_per_frame = 256; // cizim listesi kapasitesi (asilirsa SAYILIR, sonraki kareye kalir)
};

struct VsmStats {
  uint32_t requested = 0;   // bu karede istenen ayri sayfa
  uint32_t hits = 0;        // gecerli yerlesik sayfa (cizim yok)
  uint32_t allocated = 0;   // yeni yuva alan sayfa (cizilecek)
  uint32_t redrawn = 0;     // kirli oldugu icin yeniden cizilecek
  uint32_t evicted = 0;     // LRU tahliyesi
  uint32_t exhausted = 0;   // havuz dolu, karsilanamadi
  uint32_t render_overflow = 0; // cizim listesi doldu
};

struct VsmPageKey {
  uint32_t level;
  int32_t x, y;
};

struct VsmRender {
  VsmPageKey key;
  uint32_t slot; // fiziksel yuva (atlas karosu)
};

class VsmPageCache {
public:
  bool init(Arena &arena, const VsmConfig &cfg, Vec3 light_dir);
  // Kare: begin -> request* -> end. camera: seviye secimi icin.
  void begin_frame(Vec3 camera);
  // Gorunur bir yuzey noktasi (or. derinlik tamponundan geri izdusum). Uygun seviyedeki sayfayi ister.
  void request(Vec3 world);
  // Dunya AABB'si degisti (nesne hareket etti): kesisen YERLESIK sayfalar kirli.
  uint32_t invalidate(Vec3 aabb_min, Vec3 aabb_max);
  void end_frame();

  const VsmStats &stats() const { return stats_; }
  uint32_t render_count() const { return render_n_; }
  const VsmRender *render_list() const { return render_; }
  uint32_t resident() const { return resident_; }
  // Tanilama/kapi: anahtarin seviyesi ve isik-uzayi izgarasi.
  uint32_t level_for(Vec3 world) const;
  VsmPageKey key_for(Vec3 world, uint32_t level) const;
  // Bu anahtar yerlesik ve gecerli mi (kapilar icin).
  bool is_valid(VsmPageKey k) const;

private:
  struct Slot { VsmPageKey key; uint32_t last_used; uint8_t used, dirty; };
  int32_t find(VsmPageKey k) const;    // tablo indeksi ya da -1
  void table_insert(VsmPageKey k, uint32_t slot);
  void table_erase(VsmPageKey k);
  uint32_t evict_victim();             // ~0u: tahliye edilebilir yuva yok
  void push_render(VsmPageKey k, uint32_t slot);
  float page_world(uint32_t level) const;

  VsmConfig cfg_{};
  Vec3 lu_{}, lv_{};                   // isik uzayi tabani
  Vec3 cam_{};
  Slot *slots_ = nullptr;
  uint32_t *free_ = nullptr;
  uint32_t free_n_ = 0;
  // acik adresli tablo: anahtar -> yuva (kapasite 2*havuz, 2'nin kuvveti)
  int32_t *tab_slot_ = nullptr;        // -1 bos, -2 silinmis, >=0 yuva
  uint32_t tab_mask_ = 0;
  uint32_t tombstones_ = 0;            // silinmis isaretleri; esik asilinca tablo yuvalardan yeniden kurulur
  VsmRender *render_ = nullptr;
  uint32_t render_n_ = 0;
  uint32_t frame_ = 0;
  uint32_t resident_ = 0;
  VsmStats stats_{};
};

} // namespace tulpar::engine::renderer
