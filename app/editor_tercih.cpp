// L6 APP — Tercihler penceresi (editor_tercih.hpp).
#include "app/editor_tercih.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <IconsMaterialDesign.h>
#include <imgui.h>

#include "app/editor_ui.hpp"

namespace tulpar::engine::app {
namespace {

constexpr int kYazi[] = {90, 100, 115, 130};
constexpr int kOto[] = {0, 1, 2, 5, 10};

template <int N> int yakin(const int (&tab)[N], int v, bool *tam) {
  int en = tab[0];
  for (int x : tab) {
    if (x == v) { *tam = true; return x; }
    const int d = x > v ? x - v : v - x, de = en > v ? en - v : v - en;
    if (d < de) en = x;
  }
  *tam = false;
  return en;
}

ImVec4 renk(Tone t) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3]);
}

void baslik(const char *m) {
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
  ImGui::TextColored(renk(Tone::TextDim), "%s", m);
  ImGui::Separator();
}
void aciklama(const char *m) {
  // TextMute "pasif" metin icin (~2,7:1); burada paragraflar OKUNMALI (gezinti 2026-11-13) -> TextDim.
  ImGui::PushStyleColor(ImGuiCol_Text, renk(Tone::TextDim));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(m);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

} // namespace

bool tercih_ayristir(const char *m, Tercihler *t) {
  if (!m || !t) return false;
  bool ok = true;
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const size_t n = son ? (size_t)(son - p) : std::strlen(p);
    char satir[160];
    std::snprintf(satir, sizeof satir, "%.*s", (int)(n < sizeof satir - 1 ? n : sizeof satir - 1), p);
    char k[32];
    int v = 0;
    if (satir[0] != '#' && std::sscanf(satir, " %31[a-z_] = %d", k, &v) == 2) {
      bool tam = true;
      if (!std::strcmp(k, "yazi")) t->yazi = yakin(kYazi, v, &tam);
      else if (!std::strcmp(k, "oto_kayit_dk")) t->oto_kayit_dk = yakin(kOto, v, &tam);
      else if (!std::strcmp(k, "tema")) { tam = v == 0 || v == 1; t->tema = v == 1 ? 1 : 0; }
      else if (!std::strcmp(k, "canli_kod")) { tam = v == 0 || v == 1; t->canli_kod = v == 0 ? 0 : 1; }
      if (!tam) ok = false;
    }
    if (!son) break;
    p = son + 1;
  }
  return ok;
}

uint32_t tercih_metni(const Tercihler &t, char *out, uint32_t cap) {
  const int n = std::snprintf(out, cap, "# Tulpar Editor tercihleri (editor yazar; elle de duzenlenebilir)\nyazi = %d\noto_kayit_dk = %d\ntema = %d\ncanli_kod = %d\n",
                              t.yazi, t.oto_kayit_dk, t.tema, t.canli_kod);
  return n > 0 && (uint32_t)n < cap ? (uint32_t)n : 0;
}

bool tercih_oku(const char *yol, Tercihler *t) {
  *t = Tercihler{};
  std::FILE *f = yol ? std::fopen(yol, "rb") : nullptr;
  if (!f) return false;
  char m[2048];
  const size_t n = std::fread(m, 1, sizeof m - 1, f);
  std::fclose(f);
  m[n] = 0;
  tercih_ayristir(m, t);
  return true;
}

bool tercih_yaz(const char *yol, const Tercihler &t) {
  char m[512];
  const uint32_t n = tercih_metni(t, m, sizeof m);
  std::FILE *f = (yol && n) ? std::fopen(yol, "wb") : nullptr;
  if (!f) return false;
  const bool ok = std::fwrite(m, 1, n, f) == n;
  std::fclose(f);
  return ok;
}

uint32_t tercih_ciz(TercihPencere &p, Tercihler &t, bool *gelismis, bool guncelleme_otomatik) {
  uint32_t r = 0;
  if (!p.acik) return r;
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowSize(ImVec2(fs * 30.0f, 0.0f), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::Begin("Tercihler###tercihler", &p.acik, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return r;
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) p.acik = false;
  ImGui::PushTextWrapPos(fs * 29.0f);

  // --- Gorunum ---
  baslik("G\xC3\x96R\xC3\x9CN\xC3\x9CM");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Yaz\xC4\xB1 boyutu");
  {
    static const char *const kAd[] = {"K\xC3\xBC\xC3\xA7\xC3\xBCk", "Normal", "B\xC3\xBCy\xC3\xBCk", "\xC3\x87ok b\xC3\xBCy\xC3\xBCk"};
    for (int i = 0; i < 4; i++) {
      ImGui::SameLine();
      char id[64];
      std::snprintf(id, sizeof id, "%s##yazi%d", kAd[i], i);
      if (ImGui::RadioButton(id, t.yazi == kYazi[i]) && t.yazi != kYazi[i]) { t.yazi = kYazi[i]; r |= kTercihDegisti; }
    }
  }
  aciklama("B\xC3\xBCt\xC3\xBCn aray\xC3\xBCz\xC3\xBC (yaz\xC4\xB1lar, d\xC3\xBC\xC4\x9Fmeler, paneller) birlikte b\xC3\xBCy\xC3\xBCt\xC3\xBCr.");
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Tema");
  ImGui::SameLine();
  if (ImGui::RadioButton("Koyu##tema", t.tema == 0) && t.tema != 0) { t.tema = 0; r |= kTercihDegisti; }
  ImGui::SameLine();
  if (ImGui::RadioButton("A\xC3\xA7\xC4\xB1k##tema", t.tema == 1) && t.tema != 1) { t.tema = 1; r |= kTercihDegisti; }
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Aray\xC3\xBCz");
  ImGui::SameLine();
  if (ImGui::RadioButton("Basit##kip", !*gelismis) && *gelismis) { *gelismis = false; r |= kTercihKip; }
  ImGui::SameLine();
  if (ImGui::RadioButton("Geli\xC5\x9Fmi\xC5\x9F##kip", *gelismis) && !*gelismis) { *gelismis = true; r |= kTercihKip; }
  aciklama("Geli\xC5\x9Fmi\xC5\x9F: \xC3\xB6l\xC3\xA7\xC3\xBCmler (fps, \xC3\xA7izim), kamera koordinatlar\xC4\xB1, motorun teknik iletileri ve ek g\xC3\xB6r\xC3\xBCnt\xC3\xBC kipleri.");
  if (ImGui::Button(ICON_MD_DASHBOARD " Panel d\xC3\xBCzenini s\xC4\xB1" "f\xC4\xB1rla")) r |= kTercihDuzen;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Panelleri ilk a\xC3\xA7\xC4\xB1l\xC4\xB1\xC5\x9Ftaki yerlerine d\xC3\xB6nd\xC3\xBCr\xC3\xBCr (G\xC3\xB6r\xC3\xBCn\xC3\xBCm > D\xC3\xBCzeni s\xC4\xB1" "f\xC4\xB1rla).");

  // --- Kaydetme ---
  baslik("KAYDETME");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Otomatik kaydet");
  {
    static const char *const kAd[] = {"Kapal\xC4\xB1", "1 dk", "2 dk", "5 dk", "10 dk"};
    for (int i = 0; i < 5; i++) {
      ImGui::SameLine();
      char id[64];
      std::snprintf(id, sizeof id, "%s##oto%d", kAd[i], i);
      if (ImGui::RadioButton(id, t.oto_kayit_dk == kOto[i]) && t.oto_kayit_dk != kOto[i]) { t.oto_kayit_dk = kOto[i]; r |= kTercihDegisti; }
    }
  }
  aciklama("Kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklik varsa sahneyi bu aral\xC4\xB1kla kaydeder. Oynat\xC4\xB1rken ve hi\xC3\xA7 kaydedilmemi\xC5\x9F yeni sahnede kaydetmez.");

  // --- Oynatma ---
  baslik("OYNATMA");
  {
    bool c = t.canli_kod != 0;
    if (ImGui::Checkbox("De\xC4\x9Fi\xC5\x9Fiklikleri \xC3\xA7" "al\xC4\xB1\xC5\x9F" "an oyuna uygula##canli_kod", &c)) { t.canli_kod = c ? 1 : 0; r |= kTercihDegisti; }
  }
  aciklama("Oyun \xC3\xA7" "al\xC4\xB1\xC5\x9F\xC4\xB1rken: kod (oyun.tpr ya da bir betik) kaydedilirse oyun kendili\xC4\x9Finden kapan\xC4\xB1p yeniden a\xC3\xA7\xC4\xB1l\xC4\xB1r; sahnede bir \xC5\x9F" "ey de\xC4\x9Fi\xC5\x9F" "irse (konum, nesne ekleme...) oyun yeniden ba\xC5\x9Flamadan g\xC3\xBCncellenir. De\xC4\x9Fi\xC5\x9Ftir, hemen g\xC3\xB6r. Kodda hata varsa d\xC3\xBCzeltip tekrar kaydedince yeniden dener.");

  // --- Guncelleme ---
  baslik("G\xC3\x9CNCELLEME");
  bool g = guncelleme_otomatik;
  if (ImGui::Checkbox("A\xC3\xA7\xC4\xB1l\xC4\xB1\xC5\x9Fta ve g\xC3\xBCnde bir kez yeni s\xC3\xBCr\xC3\xBCm\xC3\xBC denetle", &g)) r |= kTercihGuncelleme;

  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
  ImGui::Separator();
  if (p.ayar_dizini[0]) {
    char kisa[1100];
    const char *ev = std::getenv("HOME");
    if (!ev || !*ev) ev = std::getenv("USERPROFILE");
    const size_t el = ev ? std::strlen(ev) : 0;
    if (el > 1 && !std::strncmp(p.ayar_dizini, ev, el)) std::snprintf(kisa, sizeof kisa, "~%s", p.ayar_dizini + el);
    else std::snprintf(kisa, sizeof kisa, "%s", p.ayar_dizini);
    ImGui::TextColored(renk(Tone::TextMute), "Ayarlar: %s", kisa);
  }
  if (ImGui::Button("Kapat", ImVec2(-FLT_MIN, 0))) p.acik = false;
  ImGui::PopTextWrapPos();
  ImGui::End();
  return r;
}

} // namespace tulpar::engine::app
