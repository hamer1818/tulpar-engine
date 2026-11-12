// L6 APP — "Ilk adimlar" karti (editor_ilk_adim.hpp).
#include "app/editor_ilk_adim.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <IconsMaterialDesign.h>
#include <imgui.h>

#include "app/editor_ui.hpp"

namespace tulpar::engine::app {
namespace {

ImVec4 renk(Tone t, float a = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * a);
}

// "<bitler> <proje>" satiri bu projenin mi? Donus: bitler, yoksa -1.
long satir_proje(const char *s, size_t n, const char *proje) {
  char *son = nullptr;
  const long b = std::strtol(s, &son, 10);
  if (!son || son == s || *son != ' ') return -1;
  const char *p = son + 1;
  const size_t pl = std::strlen(proje);
  const size_t kalan = n - (size_t)(p - s);
  return (kalan == pl && !std::strncmp(p, proje, pl)) ? b : -1;
}

} // namespace

uint32_t ilk_adim_bitleri(const char *m, const char *proje) {
  if (!m || !proje || !*proje) return 0;
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const size_t n = son ? (size_t)(son - p) : std::strlen(p);
    const long b = satir_proje(p, n, proje);
    if (b >= 0) return (uint32_t)b;
    if (!son) break;
    p = son + 1;
  }
  return 0;
}

uint32_t ilk_adim_guncelle(const char *m, const char *proje, uint32_t bitler, char *out, uint32_t cap) {
  if (!out || cap == 0 || !proje || !*proje) return 0;
  // Satirlari say (64 proje tavani: en eski = en ustteki duser).
  const char *satir[65];
  size_t uz[65];
  uint32_t n = 0;
  for (const char *p = m ? m : ""; *p && n < 65;) {
    const char *son = std::strchr(p, '\n');
    const size_t l = son ? (size_t)(son - p) : std::strlen(p);
    if (l && satir_proje(p, l, proje) < 0) { satir[n] = p; uz[n] = l; n++; }
    if (!son) break;
    p = son + 1;
  }
  const uint32_t bas = n >= 64 ? n - 63 : 0;
  uint32_t o = 0;
  for (uint32_t i = bas; i < n; i++) {
    if (o + uz[i] + 2 >= cap) return 0;
    std::memcpy(out + o, satir[i], uz[i]);
    o += (uint32_t)uz[i];
    out[o++] = '\n';
  }
  const int w = std::snprintf(out + o, cap - o, "%u %s\n", bitler, proje);
  if (w < 0 || o + (uint32_t)w >= cap) return 0;
  return o + (uint32_t)w;
}

uint32_t ilk_adim_oku(const char *yol, const char *proje) {
  static char m[64 * 1100];
  std::FILE *f = yol ? std::fopen(yol, "rb") : nullptr;
  if (!f) return 0;
  const size_t n = std::fread(m, 1, sizeof m - 1, f);
  std::fclose(f);
  m[n] = 0;
  return ilk_adim_bitleri(m, proje);
}

uint32_t ilk_adim_isaretle(const char *yol, const char *proje, uint32_t ekle) {
  static char m[64 * 1100], y[64 * 1100 + 1200];
  size_t n = 0;
  if (std::FILE *f = yol ? std::fopen(yol, "rb") : nullptr) { n = std::fread(m, 1, sizeof m - 1, f); std::fclose(f); }
  m[n] = 0;
  const uint32_t eski = ilk_adim_bitleri(m, proje), yeni = eski | ekle;
  if (yeni == eski && n) return yeni;
  const uint32_t yn = ilk_adim_guncelle(m, proje, yeni, y, sizeof y);
  if (yn && yol) {
    if (std::FILE *f = std::fopen(yol, "wb")) { std::fwrite(y, 1, yn, f); std::fclose(f); }
  }
  return yeni;
}

IlkAdimEylem ilk_adim_ciz(uint32_t b, const char *aciklama, const char *kodda) {
  IlkAdimEylem e = IlkAdimEylem::Yok;
  const ImVec2 cur = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, renk(Tone::Bg2, 0.6f));
  if (ImGui::BeginChild("##ilk_adimlar", ImVec2(w, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_NoScrollbar)) {
    const bool bitti = (b & kIlkHepsi) == kIlkHepsi;
    ImGui::TextColored(renk(Tone::Accent), ICON_MD_ROCKET_LAUNCH " \xC4\xB0lk ad\xC4\xB1mlar");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    if (ImGui::SmallButton(ICON_MD_CLOSE "##ilk_gizle")) e = IlkAdimEylem::Gizle;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Bu kart\xC4\xB1 gizle (bu proje i\xC3\xA7in)");
    if (aciklama && *aciklama) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(renk(Tone::TextDim), "Bu oyunda: %s", aciklama);
      if (kodda && *kodda) ImGui::TextColored(renk(Tone::TextMute), ICON_MD_INFO " %s", kodda);
      ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 2.0f));
    struct Adim { uint32_t bit; const char *ad; const char *nasil; IlkAdimEylem ey; };
    static const Adim kAdim[] = {
        {kIlkOynat, "Oyunu dene", "F5 ile oyna, tekrar F5 ile durdur.", IlkAdimEylem::Oynat},
        {kIlkEkle, "Bir nesne ekle", "Ekle men\xC3\xBCs\xC3\xBC ya da yukar\xC4\xB1" "daki + K\xC3\xBCp d\xC3\xBC\xC4\x9Fmesi.", IlkAdimEylem::Ekle},
        {kIlkDavranis, "Bir nesneye davran\xC4\xB1\xC5\x9F ver", "Nesneyi se\xC3\xA7 \xE2\x86\x92 \xC3\x96zellikler \xE2\x86\x92 Davran\xC4\xB1\xC5\x9F ekle (betik).", IlkAdimEylem::Davranis},
        {kIlkKaydet, "Kaydet", "Ctrl+S. \xC4\xB0stersen Tercihler'den otomatik kayd\xC4\xB1 a\xC3\xA7.", IlkAdimEylem::Kaydet},
    };
    for (const Adim &a : kAdim) {
      const bool ok = (b & a.bit) != 0;
      ImGui::PushID((int)a.bit);
      ImGui::TextColored(ok ? renk(Tone::Ok) : renk(Tone::TextMute), "%s", ok ? ICON_MD_CHECK_CIRCLE : ICON_MD_RADIO_BUTTON_UNCHECKED);
      ImGui::SameLine();
      if (ok) {
        ImGui::TextColored(renk(Tone::TextDim), "%s", a.ad);
      } else {
        // Yapilmamis adim TIKLANIR: isi baslatir (Oynat/Ekle/Kaydet) ya da nasil yapilacagini gosterir.
        ImGui::PushStyleColor(ImGuiCol_Text, renk(Tone::Text));
        if (ImGui::Selectable(a.ad, false, ImGuiSelectableFlags_None)) e = a.ey;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.nasil);
      }
      ImGui::PopID();
    }
    if (bitti) {
      ImGui::Dummy(ImVec2(0, 2.0f));
      ImGui::TextColored(renk(Tone::Ok), "%s", "Haz\xC4\xB1rs\xC4\xB1n! Daha fazlas\xC4\xB1: ");
      ImGui::SameLine(0, 0);
      if (ImGui::SmallButton("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 rehberi")) e = IlkAdimEylem::Rehber;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  const ImVec2 son = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(cur, ImVec2(cur.x + 3.0f, son.y - ImGui::GetStyle().ItemSpacing.y), ImGui::GetColorU32(renk(Tone::Accent)), 2.0f);
  return e;
}

} // namespace tulpar::engine::app
