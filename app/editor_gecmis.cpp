#include "app/editor_gecmis.hpp"

#include <cstdio>
#include <cstring>

#include <IconsMaterialDesign.h>
#include <imgui.h>

namespace tulpar::engine::app {

namespace {
const char *kisa_ad(const char *yol) {
  const char *a = yol;
  for (const char *p = yol; *p; p++) if (*p == '/' || *p == '\\') a = p + 1;
  return a;
}
} // namespace

void gecmis_op_metni(const content::SceneOp &op, uint32_t grup_n, char *out, uint32_t cap) {
  using content::SceneOp;
  const char *ne = "Değiştir";
  const char *ad = op.after.name;
  switch (op.kind) {
  case SceneOp::Set:
    if (!(op.before.pos == op.after.pos)) ne = "Taşı";
    else if (!(op.before.rot_deg == op.after.rot_deg)) ne = "Döndür";
    else if (!(op.before.scale == op.after.scale)) ne = "Ölçekle";
    else if (std::strcmp(op.before.name, op.after.name) != 0) { ne = "Yeniden adlandır"; ad = op.after.name; }
    else ne = "Özellik değişti";
    break;
  case SceneOp::Add: ne = "Ekle"; ad = op.after.name; break;
  case SceneOp::Remove: ne = "Sil"; ad = op.before.name; break;
  case SceneOp::World: std::snprintf(out, cap, "Dünya / ışık ayarı"); return;
  case SceneOp::Asset: std::snprintf(out, cap, "Kaynak ekle: %.60s", kisa_ad(op.asset_path)); return;
  }
  if (grup_n > 1) std::snprintf(out, cap, "%s: %.50s ve %u nesne daha", ne, ad, grup_n - 1);
  else std::snprintf(out, cap, "%s: %.80s", ne, ad);
}

void gecmis_kur(const content::SceneHistory &h, const OpGroups &g, GecmisGorunum *out) {
  *out = GecmisGorunum{};
  const uint32_t gn = g.count() < GecmisGorunum::kMax ? g.count() : GecmisGorunum::kMax;
  uint32_t toplam = 0;
  for (uint32_t i = 0; i < gn; i++) toplam += g.size_at(i);
  // Eylemler gunlugun SONUNA hizali: ilk eylemin ilk islemi = count - toplam (negatifse en eski gruplar gunlukten dusmus: ad yok).
  const int64_t bas = (int64_t)h.count() - (int64_t)toplam;
  out->eski = bas > 0 ? (uint32_t)bas : 0;
  uint32_t op = 0;
  for (uint32_t i = 0; i < gn; i++) {
    const uint32_t sz = g.size_at(i);
    GecmisSatir &r = out->s[i];
    r.op_say = sz;
    const int64_t idx = bas + (int64_t)op;
    const content::SceneOp *o = idx >= 0 ? h.op((uint32_t)idx) : nullptr;
    if (o) gecmis_op_metni(*o, sz, r.metin, sizeof r.metin);
    else std::snprintf(r.metin, sizeof r.metin, "%u işlem", sz);
    op += sz;
  }
  out->n = gn;
  out->imlec = g.cursor() < gn ? g.cursor() : gn;
}

int gecmis_ciz(const GecmisGorunum &v, bool kaydir) {
  int hedef = -1;
  const float fs = ImGui::GetFontSize();
  ImGui::TextDisabled("%u eylem · %u uygulanmış", v.n, v.imlec);
  if (v.n == 0) {
    ImGui::Dummy(ImVec2(0, fs * 0.5f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Henüz bir düzenleme yok. Bir nesneyi taşı, ekle ya da sil: eylemler burada listelenir; birine tıklayınca sahne o âna döner.");
    ImGui::PopTextWrapPos();
    return hedef;
  }
  if (ImGui::BeginChild("##gecmis_liste", ImVec2(0, 0), ImGuiChildFlags_None)) {
    if (v.eski) ImGui::TextDisabled("… %u daha eski işlem (ayrıntısı yok)", v.eski);
    for (uint32_t r = 0; r <= v.n; r++) {
      char etiket[160];
      if (r == 0) std::snprintf(etiket, sizeof etiket, ICON_MD_HISTORY "  Başlangıç###g0");
      else std::snprintf(etiket, sizeof etiket, "%u. %s###g%u", r, v.s[r - 1].metin, r);
      const bool secili = r == v.imlec;
      const bool gelecek = r > v.imlec; // yinelenebilir: soluk
      if (gelecek) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      if (ImGui::Selectable(etiket, secili) && r != v.imlec) hedef = (int)r;
      if (gelecek) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered() && r > 0) ImGui::SetTooltip("%u işlem%s\nTıkla: bu âna dön", v.s[r - 1].op_say, gelecek ? " (geri alınmış, yinelenebilir)" : "");
      if (kaydir && secili) ImGui::SetScrollHereY(0.6f);
    }
  }
  ImGui::EndChild();
  return hedef;
}

} // namespace tulpar::engine::app
