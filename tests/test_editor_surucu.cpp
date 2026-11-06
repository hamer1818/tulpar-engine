// Arayuz surucusu (app/editor_surucu.hpp, docs/ARAYUZ-SURUCUSU.md): saf parcalar + ImGui test
// motoru kancasinin (imconfig.h TULPAR YAMASI) gercekten oge/etiket/dikdortgen verdigi.
// Uctan uca yol (penceresiz editor + betik) tools/arayuz_kapi.sh'te.
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "app/editor_surucu.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

ENGINE_TEST(surucu_tus_cozumu) {
  SurucuTus t;
  CHECK(surucu_tus_coz("Ctrl+S", &t));
  CHECK(t.tus == GLFW_KEY_S && t.ctrl && !t.shift && !t.alt);
  CHECK(surucu_tus_coz("ctrl+shift+z", &t));
  CHECK(t.tus == GLFW_KEY_Z && t.ctrl && t.shift);
  CHECK(surucu_tus_coz("F5", &t) && t.tus == GLFW_KEY_F5 && !t.ctrl);
  CHECK(surucu_tus_coz("F12", &t) && t.tus == GLFW_KEY_F12);
  CHECK(surucu_tus_coz("Escape", &t) && t.tus == GLFW_KEY_ESCAPE);
  CHECK(surucu_tus_coz("Enter", &t) && t.tus == GLFW_KEY_ENTER);
  CHECK(surucu_tus_coz("Delete", &t) && t.tus == GLFW_KEY_DELETE);
  CHECK(surucu_tus_coz("Alt+1", &t) && t.tus == GLFW_KEY_1 && t.alt);
  // Reddedilenler: bilinmeyen ad, yalniz degistirici, iki ana tus, F13, bos.
  CHECK(!surucu_tus_coz("Ctrl+Uydurma", &t));
  CHECK(!surucu_tus_coz("Ctrl", &t));
  CHECK(!surucu_tus_coz("A+B", &t));
  CHECK(!surucu_tus_coz("F13", &t));
  CHECK(!surucu_tus_coz("", &t));
}

ENGINE_TEST(surucu_jetonlar_ve_etiket_eslesmesi) {
  char b[256];
  const char *j[8];
  const uint32_t n = surucu_jetonla("tikla \"Yeni oyun\" pencere=Dosya sira=2\n", b, sizeof b, j, 8);
  CHECK(n == 4);
  CHECK(!std::strcmp(j[0], "tikla"));
  CHECK(!std::strcmp(j[1], "Yeni oyun"));
  CHECK(!std::strcmp(j[2], "pencere=Dosya"));
  CHECK(!std::strcmp(j[3], "sira=2"));
  CHECK(surucu_jetonla("yaz \"a \\\"b\\\"\"", b, sizeof b, j, 8) == 2 && !std::strcmp(j[1], "a \"b\""));
  char g[64];
  CHECK(surucu_gorunen("Kaydet##menu", g, sizeof g) == 6 && !std::strcmp(g, "Kaydet"));
  CHECK(surucu_gorunen("Durum###sabit", g, sizeof g) == 5);
  CHECK(surucu_eslesir("Kaydet##menu", "Kaydet") == 2);
  CHECK(surucu_eslesir("Kaydet##menu", "kaydet") == 2);       // ASCII buyuk/kucuk duyarsiz
  CHECK(surucu_eslesir("Farkli Kaydet...", "Kaydet") == 1);  // kismi
  CHECK(surucu_eslesir("\xEE\x85\xA1 Kaydet", "Kaydet") == 2);   // ikon + metin: tam sayilir
  CHECK(surucu_eslesir("##oynat", "oynat") == 1);             // gizli kimlikle bulunur
  CHECK(surucu_eslesir("Dosya", "Kaydet") == 0);
  CHECK(surucu_eslesir("Dosya", "") == 0);
}

namespace {
struct Toplanan {
  uint32_t eklenen = 0, etiketli = 0;
  bool kaydet = false;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  unsigned kaydet_id = 0;
};
Toplanan g_t;
unsigned g_son_id = 0;
float g_son[4];
void kanca(unsigned id, const float *d, const char *etiket, int) {
  if (d) { g_t.eklenen++; g_son_id = id; std::memcpy(g_son, d, sizeof g_son); return; }
  g_t.etiketli++;
  if (etiket && !std::strcmp(etiket, "Kaydet##dugme") && id == g_son_id) {
    g_t.kaydet = true; g_t.kaydet_id = id;
    g_t.x0 = g_son[0]; g_t.y0 = g_son[1]; g_t.x1 = g_son[2]; g_t.y1 = g_son[3];
  }
}
void kare(ImVec2 *dugme_min, ImVec2 *dugme_max) {
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(800, 600);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(50, 40));
  ImGui::SetNextWindowSize(ImVec2(300, 200));
  ImGui::Begin("surucu_test_penceresi");
  ImGui::Button("Kaydet##dugme");
  if (dugme_min) *dugme_min = ImGui::GetItemRectMin();
  if (dugme_max) *dugme_max = ImGui::GetItemRectMax();
  ImGui::End();
  ImGui::Render();
}
} // namespace

// Kanca + TestEngineHookItems: dugmenin kimligi, etiketi ve dikdortgeni (ImGui'nin kendi
// GetItemRect'iyle AYNI) gelir. POZITIF KONTROL: bayrak kapaliyken hicbir sey gelmez — kanca
// gercekten imgui.cpp'nin test motoru yolundan besleniyor.
ENGINE_TEST(surucu_imgui_test_motoru_kancasi_ogeyi_verir) {
  IMGUI_CHECKVERSION();
  ImGuiContext *ctx = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  GImGuiTulparOgeKanca = &kanca;

  ctx->TestEngineHookItems = false;
  g_t = Toplanan{};
  kare(nullptr, nullptr);
  CHECK(g_t.eklenen == 0 && g_t.etiketli == 0);   // kontrol: bayraksiz kanca sessiz

  ctx->TestEngineHookItems = true;
  g_t = Toplanan{};
  ImVec2 mn, mx;
  kare(&mn, &mx);
  std::printf("    [bilgi] %u ItemAdd, %u ItemInfo; Kaydet (%.0f,%.0f)-(%.0f,%.0f)\n", g_t.eklenen, g_t.etiketli, (double)g_t.x0,
              (double)g_t.y0, (double)g_t.x1, (double)g_t.y1);
  CHECK(g_t.kaydet);
  CHECK(g_t.x0 == mn.x && g_t.y0 == mn.y && g_t.x1 == mx.x && g_t.y1 == mx.y);
  CHECK(g_t.x0 >= 50.0f && g_t.y0 >= 40.0f);   // pencerenin icinde
  CHECK(g_t.eklenen >= 2);                     // pencere kaydi + dugme

  GImGuiTulparOgeKanca = nullptr;
  ctx->TestEngineHookItems = false;
  ImGui::DestroyContext(ctx);
}
