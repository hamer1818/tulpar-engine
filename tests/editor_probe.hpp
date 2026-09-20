// Arayuz SONDASI (test yardimcisi): bir ImGui cizim geri cagrisini motorun
// offscreen gecisine, GERCEK editor yazi tipiyle (DejaVuSans, 17 px) ve editor
// temasiyla cizer; istenirse PPM yazar. Amaci: bir arayuz parcasini yazan
// kisi onu GORSUN (engine/tools/ppm2png.py ile PNG'ye cevirip bak). Piksel
// farki/vertex sayisi kapilarin olctugu seydir; goruntu dogrulamanin kendisi.
//
// Kullanim:
//   EditorProbe p; p.width = 800; p.height = 600; p.out_ppm = "/tmp/x.ppm";
//   p.draw = [](void *, uint32_t) { ImGui::Begin("A"); ...; ImGui::End(); };
//   PROBE_OR_RETURN(p);            // Ok degilse sebebi basar ve testten doner
//   ... p.pixels / p.vertices ...  // buraya YALNIZ Ok ile gelinir
// ImGui yerlesimi (boyut, dock, otomatik genislik) IKINCI karede oturur; frames
// varsayilani 2'dir ve son karenin pikselleri/vertex sayisi raporlanir.
#pragma once
#include <cstdint>

namespace tulpar::engine::test {

// Sonda sonucu. "Ok degil"in DORT ayri hali var, cunku ucu BIRBIRINDEN FARKLI
// sey soyluyor; tek isim altinda toplanirlarsa GERCEK bir hata iyi huylu bir
// ortam atlamasi gibi gorunur (olculdu 2026-09-20: `dev.init` dustugunde de
// `NoVulkan` donuluyordu ve "ATLANDI: Vulkan yok" basiliyordu — makinede
// calisan bir RTX 5080 / Vulkan 1.4 varken; yani mesaj YALAN soyluyordu):
//   Ok        — sonda cizdi, p.pixels gecerli.
//   NoVulkan  — Vulkan YUKLEYICISI yok (vk_api_load dustu). Gercek atlama.
//   NoDevice  — yukleyici var ama BU SURECTE hic cihaz acilamadi. Sanal /
//               surucusuz makine: olculemez, hata degil, gorunur ATLANIR.
//   Exhausted — cihaz ONCE acildi, sonra acilamaz oldu. Ortam eksikligi DEGIL,
//               surec ici kaynak tavani: KIRMIZI olmali (sebep p.err'de).
//   Fail      — sondanin kendi hatasi (p.err).
enum class ProbeStatus { Ok, NoVulkan, NoDevice, Exhausted, Fail };

struct EditorProbe {
  uint32_t width = 800, height = 600; // en fazla 1920x1080
  float font_px = 17.0f;              // editorun kullandigi boyut
  bool with_font = true;              // false: ImGui gomulu yazi tipi (kontrol)
  const char *out_ppm = nullptr;      // nullptr = yazma
  void (*draw)(void *ctx, uint32_t frame) = nullptr;
  void *ctx = nullptr;
  uint32_t frames = 2;
  // Sonuc (son kare):
  uint32_t vertices = 0, indices = 0;
  const uint8_t *pixels = nullptr; // RGBA8, width*height*4; bir sonraki sondaya kadar gecerli
  char err[512] = {0}; // rhi::OffscreenResult::error ile ayni boy (kirpma uyarisi yok)
};

ProbeStatus editor_probe_render(EditorProbe &p);

// Durumun kisa adi (rapor metinlerinde; "durum 2" hicbir sey anlatmiyordu).
const char *probe_status_text(ProbeStatus st);

// Ok OLMAYAN bir sondayi GORUNUR kilar ve cagirana "bu testten CIK" der.
// Donus: st != Ok. Ok ise hicbir sey basmaz ve false doner, yani su tek satir
// hem kapi hem rapor olur:
//     if (probe_not_ok(st, p, __FILE__, __LINE__)) return;
//
// NEDEN VAR: eskiden cagri yerleri `CHECK(editor_probe_render(p) == Ok);`
// yaziyordu. CHECK basarisizligi KAYDEDER ama DONMEZ; sonda dustugunde
// p.pixels nullptr olur ve hemen ardindaki memcpy/piksel okumasi SIGSEGV
// atardi. Kirmizi bir test cekirdek dokumune donunce hem sebebi kayboluyor
// hem de kosumun geri kalani hic kosmuyordu (olculdu 2026-09-20:
// `engine_tests editor` 139 ile oluyordu).
bool probe_not_ok(ProbeStatus st, const char *err, const char *file, int line);
inline bool probe_not_ok(ProbeStatus st, const EditorProbe &p, const char *file, int line) {
  return probe_not_ok(st, p.err, file, line);
}

// Piksel yardimcilari (RGBA8): (x,y) rengini okur; iki tampon arasindaki farkli
// piksel sayisi. Kapilar icin "bir sey cizildi mi"nin olcusu.
void probe_pixel(const EditorProbe &p, uint32_t x, uint32_t y, uint8_t out[4]);
uint32_t probe_diff(const uint8_t *a, const uint8_t *b, uint32_t n_pixels);

} // namespace tulpar::engine::test

// Sondayi kosar; Ok degilse sebebini raporlar (gorunur atlama ya da KIRMIZI)
// ve TESTTEN DONER. "Kontrol edip devam etme" kalibinin yerini alir: sondanin
// ardindan gelen kod p.pixels'i kosulsuz okuyabilir.
#define PROBE_OR_RETURN(p)                                                                       \
  do {                                                                                           \
    if (::tulpar::engine::test::probe_not_ok(::tulpar::engine::test::editor_probe_render(p),     \
                                             (p), __FILE__, __LINE__))                           \
      return;                                                                                    \
  } while (0)

// Durumu zaten elde olan (fikstur, yardimci) cagri yerleri icin ayni kapi.
#define PROBE_STATUS_OR_RETURN(st, p)                                                            \
  do {                                                                                           \
    if (::tulpar::engine::test::probe_not_ok((st), (p), __FILE__, __LINE__)) return;             \
  } while (0)
