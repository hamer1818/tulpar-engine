// L6 APP — Ilk acilis turu (editor_tur.hpp).
#include "app/editor_tur.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName (panel dikdortgeni)

#include "app/editor_chrome.hpp"
#include "app/editor_files.hpp"
#include "app/editor_layout.hpp"
#include "app/editor_ui.hpp"

namespace tulpar::engine::app {
namespace {

ImVec4 renk(Tone t) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3]);
}

struct Adim { const char *baslik; const char *metin; };
const Adim kAdimlar[4] = {
    {"Sahne", "Oyunundaki her nesne burada listelenir. Bir nesneye t\xC4\xB1kla, se\xC3\xA7ilir; "
              "\"+\" ile yeni nesne ekle."},
    {"G\xC3\xB6r\xC3\xBCn\xC3\xBCm", "Sahnenin 3B g\xC3\xB6r\xC3\xBCn\xC3\xBCm\xC3\xBC. Sa\xC4\x9F tu\xC5\x9F" "la d\xC3\xB6nd\xC3\xBCr, "
                                      "orta tu\xC5\x9F" "la kayd\xC4\xB1r, F ile se\xC3\xA7ili nesneye odaklan. Oklarla ta\xC5\x9F\xC4\xB1."},
    {"\xC3\x96zellikler", "Se\xC3\xA7ili nesnenin konumu, rengi, fizi\xC4\x9Fi. De\xC4\x9F" "i\xC5\x9Ftirdi\xC4\x9F" "in her \xC5\x9F" "ey "
                          "Ctrl+Z ile geri al\xC4\xB1n\xC4\xB1r."},
    {"Oynat", "Oyununu edit\xC3\xB6r\xC3\xBCn i\xC3\xA7inde \xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r (F5). Kaydet (Ctrl+S) ve "
              "Derle (Ctrl+B) oyunun sahnesini g\xC3\xBCnceller."},
};

bool hedef(int adim, ImVec2 *a, ImVec2 *b) {
  if (adim == 3) {
    float r[4];
    if (!chrome_probe_rect(ChromeRect::Transport, r)) return false;
    *a = ImVec2(r[0], r[1]);
    *b = ImVec2(r[2], r[3]);
    return true;
  }
  const char *ad = adim == 0 ? kPanelSahneLabel : adim == 1 ? kPanelGorunumLabel : kPanelOzelliklerLabel;
  ImGuiWindow *w = ImGui::FindWindowByName(ad);
  if (!w || !w->WasActive) return false;
  *a = w->Pos;
  *b = ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
  return true;
}

} // namespace

bool tur_goruldu() {
  char yol[1024];
  if (!editor_ayar_yolu(yol, sizeof yol, "tur", nullptr, nullptr)) return false;
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

void tur_goruldu_yaz() {
  char yol[1024];
  if (!editor_ayar_yolu(yol, sizeof yol, "tur", nullptr, nullptr)) return;
  if (std::FILE *f = std::fopen(yol, "wb")) { std::fputs("goruldu\n", f); std::fclose(f); }
}

bool tur_ciz(TurDurum &t) {
  if (!t.acik) return false;
  ImVec2 a, b;
  const bool var = hedef(t.adim, &a, &b);
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  if (var) {
    // Vurgu: hedefin cevresine iki katli cerceve (on planda; girdiyi engellemez).
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    const ImU32 vurgu = ImGui::GetColorU32(renk(Tone::AccentHi));
    fg->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), vurgu, 6.0f, 3.0f);
  }
  // Aciklama penceresi: hedefin yaninda, ekranin icinde kalir.
  const float w = 320.0f;
  ImVec2 p(vp->WorkPos.x + vp->WorkSize.x * 0.5f - w * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
  if (var) {
    if (b.x + 12 + w < vp->WorkPos.x + vp->WorkSize.x) p = ImVec2(b.x + 12, a.y + 12);
    else if (a.x - 12 - w > vp->WorkPos.x) p = ImVec2(a.x - 12 - w, a.y + 12);
    else p = ImVec2(a.x + 12, b.y + 12);
    if (p.y > vp->WorkPos.y + vp->WorkSize.y - 160) p.y = vp->WorkPos.y + vp->WorkSize.y - 160;
  }
  ImGui::SetNextWindowPos(p, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  bool kapandi = false;
  if (ImGui::Begin("##ilk_tur", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    const Adim &ad = kAdimlar[t.adim];
    push_text_size(TextSize::Lg);
    ImGui::TextColored(renk(Tone::AccentHi), "%s", ad.baslik);
    pop_text_size();
    ImGui::SameLine();
    ImGui::TextColored(renk(Tone::TextDim), "%d / 4", t.adim + 1);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - ImGui::GetStyle().WindowPadding.x * 2.0f);
    ImGui::TextWrapped("%s", ad.metin);
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button(t.adim < 3 ? "\xC4\xB0leri" : "Ba\xC5\x9Flayal\xC4\xB1m")) {
      if (++t.adim > 3) { t.acik = false; kapandi = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Turu atla") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { t.acik = false; kapandi = true; }
  }
  ImGui::End();
  if (kapandi && t.bitti_yaz) tur_goruldu_yaz();
  return kapandi;
}

} // namespace tulpar::engine::app
