// L6 APP — Editor: motorun kendisi masaustunde duzenleme kipinde (PLAN L7:
// "editor motoru kutuphane olarak kullanir"). Dear ImGui panelleri + ImGuizmo
// gizmo. Veri modeli content::SceneDesc (.sahne dosyasi): yukle / kaydet /
// geri al / yinele (SceneHistory), ekle / sil, oynat = govdeler fizige girer,
// durdur = yazar donusumune doner (veri modeli gercek, sim turetilmis).
// Dunya paneli (gunes/ortam/golge/kamera), Derle (Ctrl+B) = runtime blob .sahneb.
// Coklu secim (Ctrl+tik; grup tasima/silme gunluge TEK eylem), Kaynaklar paneli
// (sahne dizinindeki glTF'ler), isik yaricapi / golge hacmi / gunes yonu gizmolari.
// Arka plan: Faz 2 demo sahnesi. Headless kip: betikli durumla N kare, PPM.
#pragma once
#include <cstdint>

#include "platform/window.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::app {

struct EditorOptions {
  uint32_t headless_frames = 0; // >0: pencere yok, offscreen
  const char *out_path = nullptr;
  uint32_t width = 1280, height = 720;
  bool validation = false;
  const char *scene_path = nullptr; // null: tests/assets/editor.sahne
  // --komut: 2. karede calistirilacak komutun DEGISMEZ anahtari
  // ("oynat.oyunu_calistir", "dosya.derle" ...). Otomasyon ve penceresiz
  // dogrulama icin; bilinmeyen anahtar editoru 2 ile bitirir (sessiz degil).
  const char *command = nullptr;
  // --ux-rapor <dosya>: UX olcumu (app/editor_ux.hpp; kesik metin, tasan kaplama, gorunum orani,
  // gorunen metinler) kapanista bu dosyaya. docs/EDITOR-UYGULAMA-PLANI.md U0.
  const char *ux_rapor = nullptr;
  // Acilis (karsilama) ekrani (U3): argumansiz pencereli acilista kendiliginden. --karsilama
  // penceresizde de gosterir (goruntu/olcum); --karsilama-yeni <dizin> penceresizde formu
  // doldurup "Olustur"un ISLEYICISINI calistirir (kapi: fabrika + sahne yukleme + derleme).
  bool karsilama = false;
  // --kip basit|gelismis (U5). null: pencereli kipte kullanicinin kayitli tercihi (yoksa basit),
  // penceresizde gelismis (yerlesik kapilar bugunku ekrani olcer).
  const char *kip = nullptr;
  // --tur: ilk acilis turunu penceresizde de goster (goruntu); --tur-adim N baslangic adimi.
  bool tur = false;
  // --kapisiz: penceresiz kipte yerlesik kapilari KOSTURMA (baska bir sahnede --komut ile urun
  // yolunu olcmek icin; kapilar tests/assets/editor.sahne fiksturlerine gore yazili).
  bool kapisiz = false;
  // --arac apk|dogrula: Oyun menusundeki proje aracini ikinci karede baslat; penceresizde editor
  // aracin bitmesini bekler (U6 kapisi: menu -> betik -> Konsol -> cikis kodu).
  const char *arac = nullptr;
  // --yardim rehber|kisayollar|cokme: yardim penceresini penceresizde de ac (U8 UX kapisi).
  const char *yardim = nullptr;
  // --surucu <betik> / --surucu-canli <fifo> (app/editor_surucu.hpp, docs/ARAYUZ-SURUCUSU.md):
  // penceresiz editoru komutla sur (tikla "Dosya", ekran, dogrula_oge ...). Kapilar kosmaz,
  // betikli secim/oynatma yok; editor `cik` ya da betik sonunda kapanir, cikis kodu = dogrulamalar.
  const char *surucu = nullptr;
  const char *surucu_dizin = nullptr;
  bool surucu_canli = false;
  int tur_adim = 0;
  const char *karsilama_yeni = nullptr;
  // Surecin kendi argumanlari (main'in argc/argv'si). Guncelleme kurulunca yeni
  // ikili AYNI argumanlarla yeniden baslatilir (app/editor_update.hpp). Bos
  // olabilir (testler/gomulu kullanim): o zaman yalniz --scene tasinir.
  int argc = 0;
  char **argv = nullptr;
};

struct EditorHost {
  void *user = nullptr;
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  // Olaylari isler; false = kapat. *w/*h framebuffer.
  bool (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  const platform::InputState *(*input)(void *user) = nullptr;
  // Pencere olcusu mantiksal pikselde (imlec uzayi). nullptr = framebuffer ile
  // ayni (isaretci olcegi 1). HiDPI'da yoksa tiklamalar arayuzu ISKALAR.
  void (*window_size)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  // Tam ekran (F11). nullptr = host desteklemiyor: menu ogesi SOLUK gorunur
  // (sessizce hicbir sey yapan bir dugme yok).
  bool (*set_fullscreen)(void *user, bool on) = nullptr;
  bool (*is_fullscreen)(void *user) = nullptr;
};

int editor_run(const EditorOptions &opts, const EditorHost *host);

} // namespace tulpar::engine::app
