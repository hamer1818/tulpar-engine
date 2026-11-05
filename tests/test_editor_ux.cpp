// Editor UX olcumu (app/editor_ux.hpp, docs/EDITOR-UYGULAMA-PLANI.md U0).
//
// Asil soru: imconfig.h/imgui.cpp'deki TULPAR YAMASI kancasi CANLI mi? ImGui yukseltilip yama
// kaybolursa UX kapisi hicbir metin gormez ve "kesik 0" sahte yesil olur. Bu test GPU'suz bir
// ImGui baglaminda dar pencereye bilerek uzun etiket koyar: kesik=1 OLMALI (pozitif kontrol);
// genis pencerede ayni etiket kesik=0 (kontrol).
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "app/editor_ux.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

namespace {

UxOlcum g_o;   // ~200 KB: yigin degil (test yigini kucuk olabilir)

// Bir kare: w genisliginde pencere, icinde dugme + metin. Donus: kesik sayisi.
uint32_t bir_kare(float w, const char *etiket) {
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(800, 600);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(w, 200), ImGuiCond_Always);
  ImGui::Begin("ux_test_penceresi", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
  ImGui::Button(etiket, ImVec2(-1.0f, 0.0f));   // genislik = pencere: sigmazsa RenderTextClipped keser
  ImGui::Text("kisa");
  ImGui::End();
  ImGui::Render();
  return g_o.kesik_n;
}

} // namespace

ENGINE_TEST(editor_ux_imgui_kancasi_kesik_metni_gorur) {
  IMGUI_CHECKVERSION();
  ImGuiContext *ctx = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  // Arka ucsuz baglam (ImGui 1.92): "dokulari isleyen arka uc" bayragi ile atlas CPU'da
  // kendiliginden kurulur; doku guncelleme istekleri (DrawData->Textures) yok sayilir.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  const char *uzun = "Bu etiket dar bir dugmeye asla sigmayacak kadar uzun bir metin";
  ux_olcum_sifirla(g_o);
  g_o.kare = 2;   // isinma karelerini gec (editor_ux.cpp: ilk iki kare kesik sayilmaz)
  ux_olcum_baslat(&g_o);
  const uint32_t genis = bir_kare(780.0f, uzun);
  CHECK(g_o.metin_n >= 2);               // "kisa" ve etiket goruldu: kanca canli
  CHECK((genis) == (0u));                   // kontrol: sigan etiket kesik degil
  const uint32_t dar = bir_kare(120.0f, uzun);
  CHECK((dar) == (1u));                     // POZITIF KONTROL: kesik yakalandi
  if (dar != 1u) for (uint32_t i = 0; i < g_o.kesik_n; i++) std::printf("    kesik %s: %s\n", g_o.kesik_pencere[i], g_o.kesik[i]);
  CHECK(std::strcmp(g_o.kesik_pencere[0], "ux_test_penceresi") == 0);
  CHECK(g_o.kesik_gerek[0] > g_o.kesik_alan[0] + 1.0f);
  bir_kare(120.0f, uzun);                       // ayni kesik ikinci kez sayilmaz (tekil)
  CHECK((g_o.kesik_n) == (1u));
  ux_olcum_bitir();
  bir_kare(120.0f, "kancasiz kare: bu da cok uzun bir etiket olsun ki kesilsin");
  CHECK((g_o.kesik_n) == (1u));             // kanca kapali: olcum degismez
  ImGui::DestroyContext(ctx);
}

ENGINE_TEST(editor_ux_kaplama_tasmasi_ve_gorunum_orani) {
  ux_olcum_sifirla(g_o);
  const UxRect ekran{0, 0, 1000, 500};
  const UxRect gorunum{100, 100, 500, 300};
  ux_olcum_kare(g_o, gorunum, ekran, UxRect{400, 120, 180, 200});   // icinde
  CHECK((g_o.kaplama_tasan_kare) == (0u));
  ux_olcum_kare(g_o, gorunum, ekran, UxRect{400, 120, 180, 330});   // alttan 50 px tasar
  CHECK((g_o.kaplama_tasan_kare) == (1u));
  CHECK(g_o.kaplama_tasma_px > 49.0f && g_o.kaplama_tasma_px < 51.0f);
  ux_olcum_kare(g_o, gorunum, ekran, UxRect{0, 0, 0, 0});           // kaplama yok
  CHECK((g_o.kaplama_tasan_kare) == (1u));
  CHECK(g_o.gorunum_orani_son > 0.299f && g_o.gorunum_orani_son < 0.301f);
}

ENGINE_TEST(editor_ux_tablo_dolunca_tasan_sayilir) {
  ux_olcum_sifirla(g_o);
  g_o.kare = 2;
  char b[32];
  for (uint32_t i = 0; i < UxOlcum::kKesik + 5; i++) {
    std::snprintf(b, sizeof b, "kesik-%u", i);
    ux_olcum_metin(g_o, "p", b, b + std::strlen(b), 100.0f, 10.0f);
  }
  CHECK((g_o.kesik_n) == (UxOlcum::kKesik));
  CHECK((g_o.kesik_tasan) == (5u));
}

ENGINE_TEST(editor_ux_isinma_kareleri_kesik_sayilmaz) {
  ux_olcum_sifirla(g_o);
  const char *m = "dock henuz boyutlanmadi";
  ux_olcum_metin(g_o, "p", m, m + std::strlen(m), 100.0f, 10.0f);   // kare 0
  CHECK((g_o.kesik_n) == (0u));
  CHECK((g_o.metin_n) == (1u));                                    // metin yine goruldu
  g_o.kare = 2;
  ux_olcum_metin(g_o, "p", m, m + std::strlen(m), 100.0f, 10.0f);
  CHECK((g_o.kesik_n) == (1u));
}
