// L6 APP — Acilis (karsilama) ekrani (editor_karsilama.hpp).
#include "app/editor_karsilama.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <IconsMaterialDesign.h>
#include <imgui.h>
#include <imgui_internal.h> // BringWindowToDisplayFront (karartma katmani)

#include "app/editor_fabrika.hpp"
#include "app/editor_ui.hpp"
#include "platform/dosya_secici.hpp"

namespace tulpar::engine::app {
namespace {

ImVec4 renk(Tone t) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3]);
}

// Son proje yolundan gosterilecek ad: .../<proje>/assets/x.sahne -> "<proje>  (x.sahne)".
void proje_adi(const char *yol, char *out, size_t cap) {
  const char *son = std::strrchr(yol, '/');
  const char *dosya = son ? son + 1 : yol;
  char dizin[1024];
  std::snprintf(dizin, sizeof dizin, "%.*s", son ? (int)(son - yol) : 0, yol);
  char *s2 = std::strrchr(dizin, '/');
  const char *ust = s2 ? s2 + 1 : dizin;
  if (!std::strcmp(ust, "assets") && s2) {
    *s2 = 0;
    char *s3 = std::strrchr(dizin, '/');
    ust = s3 ? s3 + 1 : dizin;
  }
  std::snprintf(out, cap, "%s  \xC2\xB7  %s", ust[0] ? ust : "?", dosya);
}

bool girdi(const char *etiket, const char *ipucu, char *buf, size_t cap, float w) {
  ImGui::TextColored(renk(Tone::TextDim), "%s", etiket);
  ImGui::SetNextItemWidth(w);
  char id[96];
  std::snprintf(id, sizeof id, "##%s", etiket);
  return ImGui::InputTextWithHint(id, ipucu, buf, cap);
}

} // namespace

void karsilama_kok(char *out, uint32_t cap) {
#if defined(_WIN32)
  const char *h = std::getenv("USERPROFILE");
  std::snprintf(out, cap, "%s/Documents/TulparOyunlari", (h && *h) ? h : ".");
#else
  const char *h = std::getenv("HOME");
  std::snprintf(out, cap, "%s/TulparOyunlari", (h && *h) ? h : ".");
#endif
}

void karsilama_oneri(KarsilamaDurum &d) {
  char k[40], o[16];
  const bool kv = fabrika_kimlik_oner(d.tr, k, sizeof k);
  if (!d.kimlik_elle) std::snprintf(d.kimlik, sizeof d.kimlik, "%s", kv ? k : "");
  if (!d.onek_elle) {
    if (d.kimlik[0] && fabrika_onek_oner(d.kimlik, o, sizeof o)) std::snprintf(d.onek, sizeof d.onek, "%s", o);
    else d.onek[0] = 0;
  }
  if (!d.konum_elle) {
    if (d.kimlik[0]) std::snprintf(d.konum, sizeof d.konum, "%s/%s", d.kok, d.kimlik);
    else d.konum[0] = 0;
  }
}

const char *karsilama_sablon(const KarsilamaDurum &d) {
  return (d.sablon >= 0 && (uint32_t)d.sablon < d.sablon_n) ? d.sablonlar[d.sablon] : "arena";
}
const char *karsilama_yon(const KarsilamaDurum &d) { return d.yon == 1 ? "portrait" : "landscape"; }

KarsilamaEylem karsilama_ciz(KarsilamaDurum &d, const char *surum, const char *const *son, const bool *son_var,
                             uint32_t son_n) {
  KarsilamaEylem e = KarsilamaEylem::Yok;
  if (!d.acik) return e;
  if (!d.baslatildi) {
    d.baslatildi = true;
    karsilama_kok(d.kok, sizeof d.kok);
    d.sablon_n = fabrika_sablonlar(d.sablonlar, 8);
  }
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  // Arkadaki editor gorunur ama soluk ("henuz baslamadin"): tam ekran, girdisiz karartma penceresi.
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 1.0f));
  ImGui::Begin("##karsilama_karartma", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // dock panellerinin USTUNE (karsilamanin altina)
  ImGui::End();
  ImGui::PopStyleColor();
  // Ekranin cogu: kenarlarda 6% pay, en az 640x420.
  ImVec2 boy(vp->WorkSize.x * 0.88f, vp->WorkSize.y * 0.86f);
  if (boy.x > 1100.0f) boy.x = 1100.0f;
  if (boy.y > 680.0f) boy.y = 680.0f;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - boy.x) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - boy.y) * 0.5f));
  ImGui::SetNextWindowSize(boy);
  ImGui::SetNextWindowFocus();
  const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
  if (!ImGui::Begin("Tulpar Edit\xC3\xB6r##karsilama", nullptr, wf)) { ImGui::End(); return e; }
  const ImGuiStyle &st = ImGui::GetStyle();
  // --- Baslik ---
  push_text_size(TextSize::Lg);
  ImGui::TextColored(renk(Tone::AccentHi), "Tulpar Edit\xC3\xB6r");
  pop_text_size();
  ImGui::SameLine();
  ImGui::TextColored(renk(Tone::TextDim), "%s", surum && surum[0] ? surum : "(kaynak derlemesi)");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
  if (ImGui::Button(ICON_MD_CLOSE "##karsilama_kapat", ImVec2(ImGui::GetFrameHeight(), 0))) e = KarsilamaEylem::Kapat;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bo\xC5\x9F edit\xC3\xB6rle ba\xC5\x9Fla (Esc)");
  ImGui::TextColored(renk(Tone::TextDim), "Yeni bir oyun ba\xC5\x9Flat ya da kald\xC4\xB1\xC4\x9F\xC4\xB1n yerden devam et.");
  ImGui::Separator();

  const float sol_w = ImGui::GetContentRegionAvail().x * 0.56f;
  // --- Sol: Yeni oyun ---
  ImGui::BeginChild("##yeni", ImVec2(sol_w, 0), ImGuiChildFlags_None);
  push_text_size(TextSize::Lg);
  ImGui::TextColored(renk(Tone::Text), ICON_MD_ADD " Yeni oyun");
  pop_text_size();
  ImGui::Dummy(ImVec2(0, st.ItemSpacing.y));
  const float w = ImGui::GetContentRegionAvail().x - st.ItemSpacing.x;
  bool degisti = girdi("Oyunun ad\xC4\xB1", "\xC3\xB6rn. Par\xC4\xB1lt\xC4\xB1 Av\xC4\xB1", d.tr, sizeof d.tr, w);
  girdi("\xC4\xB0ngilizce ad\xC4\xB1 (ma\xC4\x9F" "aza ve \xC4\xB0ngilizce men\xC3\xBC i\xC3\xA7in)", "e.g. Spark Hunt", d.en,
        sizeof d.en, w);
  if (degisti) karsilama_oneri(d);
  // Sablon + yon
  if (d.sablon_n > 0) {
    ImGui::TextColored(renk(Tone::TextDim), "\xC5\x9E" "ablon");
    ImGui::SetNextItemWidth(w * 0.5f - st.ItemSpacing.x);
    if (ImGui::BeginCombo("##sablon", karsilama_sablon(d))) {
      for (uint32_t i = 0; i < d.sablon_n; i++)
        if (ImGui::Selectable(d.sablonlar[i], d.sablon == (int)i)) d.sablon = (int)i;
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::RadioButton("Yatay", &d.yon, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dikey", &d.yon, 1);
  }
  {
    // Konum: metin + "Sec..." (isletim sisteminin klasor secicisi, U4). Secilen klasor KOK olur,
    // oyun onun altinda <kimlik> dizinine kurulur.
    const float sec_w = ImGui::CalcTextSize("Se\xC3\xA7...").x + st.FramePadding.x * 2.0f;
    if (girdi("Konum", "klas\xC3\xB6r", d.konum, sizeof d.konum, w - sec_w - st.ItemSpacing.x)) d.konum_elle = true;
    ImGui::SameLine();
    if (ImGui::Button("Se\xC3\xA7...##konum", ImVec2(sec_w, 0))) {
      platform::SeciciIstek r;
      r.kip = platform::SeciciKip::KlasorSec;
      r.baslik = "Oyunun kurulaca\xC4\x9F\xC4\xB1 klas\xC3\xB6r";
      r.dizin = d.kok;
      char sec[1024], err[192];
      const platform::SeciciSonuc s = platform::dosya_secici(r, sec, sizeof sec, err, sizeof err);
      if (s == platform::SeciciSonuc::Secildi) {
        std::snprintf(d.kok, sizeof d.kok, "%s", sec);
        d.konum_elle = false;
        karsilama_oneri(d);
        if (!d.konum[0]) std::snprintf(d.konum, sizeof d.konum, "%s", sec);
      } else if (s != platform::SeciciSonuc::Vazgecildi) {
        std::snprintf(d.hata, sizeof d.hata, "Klas\xC3\xB6r se\xC3\xA7icisi a\xC3\xA7\xC4\xB1lamad\xC4\xB1 (%s); yolu elle yaz.", err);
      }
    }
  }
  if (ImGui::TreeNodeEx("Geli\xC5\x9Fmi\xC5\x9F", d.gelismis ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
    d.gelismis = true;
    if (girdi("Kimlik (dosya ve kay\xC4\xB1t ad\xC4\xB1)", "parilti_avi", d.kimlik, sizeof d.kimlik, w)) {
      d.kimlik_elle = true;
      karsilama_oneri(d);
    }
    if (girdi("\xC3\x96nek (ortam de\xC4\x9Fi\xC5\x9Fkenleri)", "PA", d.onek, sizeof d.onek, w)) d.onek_elle = true;
    girdi("Android paket ad\xC4\xB1 (bo\xC5\x9F: otomatik)", "dev.studyo.oyun", d.paket, sizeof d.paket, w);
    ImGui::TreePop();
  } else {
    d.gelismis = false;
  }
  ImGui::Dummy(ImVec2(0, st.ItemSpacing.y));
  const bool hazir = d.tr[0] && d.en[0] && d.kimlik[0] && d.konum[0];
  // Vurgu rengi YALNIZ hazirken: soluk ama mavi dugme "basilabilir" okunuyordu (arayuz surucusu ekran
  // goruntusu, 2026-11-06: hazir degilken (34,80,188), hazirken (38,99,235) — fark goze gorunmuyordu).
  if (!hazir) ImGui::BeginDisabled();
  if (hazir) ImGui::PushStyleColor(ImGuiCol_Button, renk(Tone::Accent));
  if (ImGui::Button(ICON_MD_ROCKET_LAUNCH "  Olu\xC5\x9Ftur", ImVec2(w, ImGui::GetFrameHeight() * 1.4f))) e = KarsilamaEylem::YeniOyun;
  if (hazir) ImGui::PopStyleColor();
  if (!hazir) ImGui::EndDisabled();
  if (d.hata[0]) ImGui::TextColored(renk(Tone::Err), "%s", d.hata);
  else if (d.bilgi[0]) ImGui::TextColored(renk(Tone::Ok), "%s", d.bilgi);
  else if (!hazir) ImGui::TextColored(renk(Tone::TextMute), "Oyunun ad\xC4\xB1n\xC4\xB1 ve \xC4\xB0ngilizce ad\xC4\xB1n\xC4\xB1 yaz.");
  ImGui::EndChild();

  ImGui::SameLine();
  // --- Sag: Devam et ---
  ImGui::BeginChild("##devam", ImVec2(0, 0), ImGuiChildFlags_None);
  push_text_size(TextSize::Lg);
  ImGui::TextColored(renk(Tone::Text), ICON_MD_HISTORY " Son projeler");
  pop_text_size();
  ImGui::Dummy(ImVec2(0, st.ItemSpacing.y));
  const float liste_h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.6f;
  ImGui::BeginChild("##son", ImVec2(0, liste_h > 80.0f ? liste_h : 80.0f), ImGuiChildFlags_Borders);
  if (son_n == 0) ImGui::TextColored(renk(Tone::TextMute), "Hen\xC3\xBCz proje yok.");
  for (uint32_t i = 0; i < son_n; i++) {
    char ad[300], kirp[300];
    proje_adi(son[i], ad, sizeof ad);
    editor_ellipsize(ad, ImGui::GetContentRegionAvail().x - st.FramePadding.x * 2.0f, kirp, sizeof kirp);
    ImGui::PushID((int)i);
    if (!son_var[i]) ImGui::BeginDisabled();
    if (ImGui::Selectable(kirp, d.son_secili == (int32_t)i, ImGuiSelectableFlags_AllowDoubleClick)) {
      d.son_secili = (int32_t)i;
      e = KarsilamaEylem::SonProje;
    }
    if (!son_var[i]) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s%s", son[i], son_var[i] ? "" : "\n(dosya bulunamad\xC4\xB1)");
    ImGui::PopID();
  }
  ImGui::EndChild();
  const float bw = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.5f;
  if (ImGui::Button(ICON_MD_FOLDER_OPEN "  A\xC3\xA7...", ImVec2(bw, 0))) e = KarsilamaEylem::Ac;
  ImGui::SameLine();
  if (ImGui::Button(ICON_MD_SPORTS_ESPORTS "  \xC3\x96rnek sahne", ImVec2(bw, 0))) e = KarsilamaEylem::Ornek;
  if (ImGui::Button("Bo\xC5\x9F edit\xC3\xB6rle ba\xC5\x9Fla", ImVec2(-FLT_MIN, 0))) e = KarsilamaEylem::Kapat;
  ImGui::EndChild();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) e = KarsilamaEylem::Kapat;
  ImGui::End();
  return e;
}

} // namespace tulpar::engine::app
