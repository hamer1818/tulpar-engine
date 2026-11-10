#include "app/editor_betik.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"

namespace tulpar::engine::app {

namespace {

constexpr uint32_t kTopMax = 256;
char g_yollar[kTopMax][content::kScenePathLen];   // editor UI tek thread; kare disi (Kaydet/Derle)

bool satir_basi_import(const char *p, const char *satir_sonu) {
  while (p < satir_sonu && (*p == ' ' || *p == '\t')) p++;
  return satir_sonu - p >= 7 && !std::strncmp(p, "import ", 7);
}

} // namespace

uint32_t betik_toplayici_metni(const content::SceneDesc &d, const char *scene_dir, const char *proje_kok, char *out,
                               uint32_t cap, BetikToplayici *s, bool (*dosya_var)(const char *)) {
  BetikToplayici yerel;
  BetikToplayici &r = s ? *s : yerel;
  r = BetikToplayici{};
  if (!out || cap == 0) return 0;
  out[0] = 0;
  const size_t kl = proje_kok ? std::strlen(proje_kok) : 0;
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const content::SceneEntity &e = d.entities[i];
    if (!(e.components & content::kSceneScript) || !e.script_file[0]) continue;
    char yol[1024];
    if (!editor_script_resolve(e.script_file, scene_dir, proje_kok, yol, sizeof yol)) continue;
    for (char *p = yol; *p; p++) if (*p == '\\') *p = '/';
    auto sorun = [&](uint32_t &sayac) {
      sayac++;
      if (!r.ilk_sorun[0]) std::snprintf(r.ilk_sorun, sizeof r.ilk_sorun, "%s", e.script_file);
    };
    if (dosya_var && !dosya_var(yol)) { sorun(r.eksik); continue; }
    if (kl == 0 || std::strncmp(yol, proje_kok, kl) != 0 || yol[kl] != '/') { sorun(r.disarida); continue; }
    const char *rel = yol + kl + 1;
    char imp[content::kScenePathLen];
    const size_t rl = std::strlen(rel);
    if (rl < 5 || std::strcmp(rel + rl - 4, ".tpr") != 0 || rl - 4 >= sizeof imp) continue;
    std::memcpy(imp, rel, rl - 4);
    imp[rl - 4] = 0;
    if (!std::strcmp(imp, "assets/betikler") || !std::strcmp(imp, "oyun")) continue; // kendini/ana dosyayi baglamaz
    bool var = false;
    for (uint32_t k = 0; k < n && !var; k++) var = !std::strcmp(g_yollar[k], imp);
    if (var || n >= kTopMax) continue;
    std::snprintf(g_yollar[n++], content::kScenePathLen, "%s", imp);
  }
  uint16_t sira[kTopMax];
  for (uint32_t k = 0; k < n; k++) sira[k] = (uint16_t)k;
  std::sort(sira, sira + n, [](uint16_t a, uint16_t b) { return std::strcmp(g_yollar[a], g_yollar[b]) < 0; });
  int w = std::snprintf(out, cap,
                        "// OTOMATIK \xE2\x80\x94 Tulpar Edit\xC3\xB6r yazar (Kaydet / Derle / Oynat). Elle d\xC3\xBCzenlemeyin.\n"
                        "// Sahnede nesnelere atanan betikler oyuna buradan ba\xC4\x9Flan\xC4\xB1r; oyun.tpr bu dosyay\xC4\xB1 bir kez\n"
                        "// import eder: import \"assets/betikler\";\n");
  if (w < 0 || (uint32_t)w >= cap) { out[0] = 0; return 0; }
  uint32_t o = (uint32_t)w;
  for (uint32_t k = 0; k < n; k++) {
    w = std::snprintf(out + o, cap - o, "import \"%s\";\n", g_yollar[sira[k]]);
    if (w < 0 || (uint32_t)w >= cap - o) { out[0] = 0; return 0; }
    o += (uint32_t)w;
  }
  r.betik = n;
  return o;
}

bool betik_baglanti_var(const char *m) {
  if (!m) return false;
  const size_t al = std::strlen(kBetikBaglantiSatiri);
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    if (!son) son = p + std::strlen(p);
    const char *q = p;
    while (q < son && (*q == ' ' || *q == '\t')) q++;
    if ((size_t)(son - q) >= al && !std::strncmp(q, kBetikBaglantiSatiri, al)) return true;
    p = *son ? son + 1 : son;
  }
  return false;
}

uint32_t betik_baglanti_ekle(const char *m, char *out, uint32_t cap) {
  if (!m || !out || cap == 0) return 0;
  // Son import satirinin sonu.
  const char *ek = nullptr;
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const char *bitis = son ? son : p + std::strlen(p);
    if (satir_basi_import(p, bitis)) ek = son ? son + 1 : bitis;
    if (!son) break;
    p = son + 1;
  }
  const size_t ml = std::strlen(m);
  const size_t on = ek ? (size_t)(ek - m) : 0;
  const bool nl_gerek = ek && on > 0 && m[on - 1] != '\n';
  const size_t sl = std::strlen(kBetikBaglantiSatiri);
  const size_t gerek = ml + sl + 2 + (nl_gerek ? 1 : 0);
  if (gerek >= cap) { out[0] = 0; return 0; }
  size_t o = 0;
  std::memcpy(out, m, on); o += on;
  if (nl_gerek) out[o++] = '\n';
  std::memcpy(out + o, kBetikBaglantiSatiri, sl); o += sl;
  out[o++] = '\n';
  std::memcpy(out + o, m + on, ml - on); o += ml - on;
  out[o] = 0;
  return (uint32_t)o;
}

int betik_dosya_yaz_degistiyse(const char *yol, const char *metin, uint32_t n, char *err, uint32_t err_cap) {
  if (err && err_cap) err[0] = 0;
  static char eski[64 * 1024];
  if (std::FILE *f = std::fopen(yol, "rb")) {
    const size_t k = std::fread(eski, 1, sizeof eski, f);
    std::fclose(f);
    if (k == n && std::memcmp(eski, metin, n) == 0) return 0;
  }
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) { if (err && err_cap) std::snprintf(err, err_cap, "yaz\xC4\xB1lamad\xC4\xB1: %s", yol); return -1; }
  const bool ok = std::fwrite(metin, 1, n, f) == n;
  if (std::fclose(f) != 0 || !ok) { if (err && err_cap) std::snprintf(err, err_cap, "yazma yar\xC4\xB1m kald\xC4\xB1: %s", yol); return -1; }
  return 1;
}

} // namespace tulpar::engine::app

// =====================================================================================================
// B2: Kod paneli
// =====================================================================================================
#include <IconsMaterialDesign.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "app/editor_ui.hpp"
#include "platform/fs.hpp"

namespace tulpar::engine::app {

uint32_t kod_satir_ofseti(const char *m, int satir) {
  if (!m) return 0;
  uint32_t o = 0;
  for (int s = 1; s < satir && m[o]; o++)
    if (m[o] == '\n') s++;
  return o;
}

int kod_satir_sayisi(const char *m) {
  if (!m) return 0;
  int n = 1;
  for (const char *p = m; *p; p++) if (*p == '\n') n++;
  return n;
}

bool kod_ac(KodPaneli &k, const char *yol, const char *etiket, int satir, char *err, uint32_t err_cap) {
  if (err && err_cap) err[0] = 0;
  const bool ayni = k.acik && !std::strcmp(k.yol, yol);
  if (k.acik && k.kirli && !ayni) {
    if (err && err_cap) std::snprintf(err, err_cap, "%s kaydedilmedi: \xC3\xB6nce Kaydet ya da Vazge\xC3\xA7", k.yol);
    return false;
  }
  if (!(ayni && k.kirli)) { // ayni dosya kirliyse metne dokunulmaz, yalniz satira gidilir
    std::FILE *f = std::fopen(yol, "rb");
    if (!f) { if (err && err_cap) std::snprintf(err, err_cap, "a\xC3\xA7\xC4\xB1lamad\xC4\xB1: %s", yol); return false; }
    const size_t n = std::fread(k.metin, 1, sizeof k.metin - 1, f);
    k.salt_okunur = !std::feof(f) && std::fgetc(f) != EOF; // kapasiteye sigmadi: kirpik dosya kaydedilmesin
    std::fclose(f);
    k.metin[n] = 0;
    k.kirli = false;
    k.dis_degisti = false;
    std::snprintf(k.yol, sizeof k.yol, "%s", yol);
    platform::fs_file_stamp(k.yol, &k.damga, &k.boy);
  }
  std::snprintf(k.etiket, sizeof k.etiket, "%s", etiket ? etiket : "");
  k.acik = true;
  k.odak_iste = true;
  k.vurgu_satir = satir > 0 ? satir : 0;
  k.git_satir = satir > 0 ? satir : 0;
  k.git_adim = 0;
  return true;
}

bool kod_kaydet(KodPaneli &k, char *err, uint32_t err_cap) {
  if (k.salt_okunur) { if (err && err_cap) std::snprintf(err, err_cap, "dosya %u KB'tan b\xC3\xBCy\xC3\xBCk: d\xC4\xB1\xC5\x9F edit\xC3\xB6rde d\xC3\xBCzenleyin", kKodKapasite / 1024); return false; }
  const uint32_t n = (uint32_t)std::strlen(k.metin);
  if (betik_dosya_yaz_degistiyse(k.yol, k.metin, n, err, err_cap) < 0) return false;
  k.kirli = false;
  k.dis_degisti = false;
  platform::fs_file_stamp(k.yol, &k.damga, &k.boy);
  return true;
}

namespace {
int kod_geri_cagri(ImGuiInputTextCallbackData *d) {
  KodPaneli *k = static_cast<KodPaneli *>(d->UserData);
  int s = 1, c = 1;
  for (int i = 0; i < d->CursorPos && d->Buf[i]; i++) {
    if (d->Buf[i] == '\n') { s++; c = 1; }
    else if (((unsigned char)d->Buf[i] & 0xC0) != 0x80) c++; // UTF-8 devam bayti sutun sayilmaz
  }
  k->imlec_satir = s;
  k->imlec_sutun = c;
  return 0;
}
} // namespace

KodEylem kod_paneli_ciz(KodPaneli &k, const char *secili_ad, bool secilide_bu) {
  KodEylem eylem = KodEylem::Yok;
  k.yaziyor = false;
  if (!k.acik) return eylem;
  // Disarida degisti mi (VS Code'da kaydedildi): ~0.5 s'de bir yoklanir. Kirli degilse SESSIZCE yeniden okunur.
  if (++k.kontrol_kare % 30 == 0) {
    int64_t d = 0, b = 0;
    if (platform::fs_file_stamp(k.yol, &d, &b) && (d != k.damga || b != k.boy)) {
      if (!k.kirli) { char e[64]; const int ys = k.git_satir; kod_ac(k, k.yol, k.etiket, 0, e, sizeof e); k.git_satir = ys; k.odak_iste = false; }
      else k.dis_degisti = true;
      k.damga = d; k.boy = b;
    }
  }
  const char *ad = k.yol;
  for (const char *p = k.yol; *p; p++) if (*p == '/' || *p == '\\') ad = p + 1;
  char baslik[400];
  std::snprintf(baslik, sizeof baslik, "%s %s%s###kod_paneli", ICON_MD_CODE, ad, k.kirli ? " \xE2\x97\x8F" : "");
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.5f, vp->WorkSize.y * 0.7f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.5f));
  if (k.odak_iste) { ImGui::SetNextWindowFocus(); k.odak_iste = false; }
  bool acik = true;
  if (!ImGui::Begin(baslik, &acik, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); if (!acik) k.acik = false; return eylem; }
  const ImGuiStyle &st = ImGui::GetStyle();
  // --- Arac satiri ---
  ImGui::BeginDisabled(k.salt_okunur || !k.kirli);
  bool kaydet = ImGui::Button(ICON_MD_SAVE " Kaydet");
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+S");
  ImGui::SameLine();
  if (ImGui::Button(ICON_MD_OPEN_IN_NEW " D\xC4\xB1\xC5\x9F edit\xC3\xB6rde a\xC3\xA7")) eylem = KodEylem::DisEditor;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("VS Code ya da sistemin metin d\xC3\xBCzenleyicisi; kaydedince burada da g\xC3\xBCncellenir.");
  if (secili_ad && !secilide_bu) {
    ImGui::SameLine();
    char eb[200];
    std::snprintf(eb, sizeof eb, ICON_MD_ADD_LINK " %.40s nesnesine ekle", secili_ad);
    if (ImGui::Button(eb)) eylem = KodEylem::NesneyeEkle;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bu betik se\xC3\xA7ili nesnede \xC3\xA7" "al\xC4\xB1\xC5\x9Fs\xC4\xB1n (Betik bile\xC5\x9F" "eni eklenir).");
  } else if (secili_ad && secilide_bu) {
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_MD_CHECK " %.40s nesnesinde \xC3\xA7" "al\xC4\xB1\xC5\x9F\xC4\xB1yor", secili_ad);
  }
  if (k.dis_degisti) {
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), ICON_MD_WARNING " Dosya d\xC4\xB1\xC5\x9F" "ar\xC4\xB1" "da de\xC4\x9Fi\xC5\x9Fti.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Diskteki halini y\xC3\xBCkle")) { char e[64]; k.kirli = false; kod_ac(k, k.yol, k.etiket, 0, e, sizeof e); }
  }
  if (k.salt_okunur) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Dosya %u KB'tan b\xC3\xBCy\xC3\xBCk: yaln\xC4\xB1z okunur.", kKodKapasite / 1024);
  // --- Metin + satir numaralari ---
  const float fs = ImGui::GetFontSize();
  const int satir_n = kod_satir_sayisi(k.metin);
  char nb[16];
  std::snprintf(nb, sizeof nb, "%d", satir_n < 100 ? 100 : satir_n);
  const float oluk = ImGui::CalcTextSize(nb).x + st.ItemInnerSpacing.x * 2.0f;
  const float alt = ImGui::GetFrameHeightWithSpacing();
  const ImVec2 bas = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(oluk, 1.0f));
  ImGui::SameLine(0.0f, 0.0f);
  const ImVec2 kutu(-FLT_MIN, -alt);
  if (k.git_satir > 0 && k.git_adim == 0) { ImGui::SetKeyboardFocusHere(); k.git_adim = 1; }
  const ImGuiInputTextFlags fl = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways |
                                 (k.salt_okunur ? ImGuiInputTextFlags_ReadOnly : 0);
  if (ImGui::InputTextMultiline("##kod", k.metin, sizeof k.metin, kutu, fl, kod_geri_cagri, &k)) { k.kirli = true; k.vurgu_satir = 0; }
  const ImGuiID kid = ImGui::GetItemID();
  k.yaziyor = ImGui::IsItemActive();
  ImGuiWindow *ic = nullptr; // metin kutusunun kendi alt penceresi (kaydirma onda)
  if (ImGuiWindow *w = ImGui::GetCurrentWindow(); w->DC.ChildWindows.Size > 0) ic = w->DC.ChildWindows.back();
  // Satira git: SetKeyboardFocusHere kutuyu BIR SONRAKI karede etkinlestirir; imlec ancak durum (state) olusunca
  // konur. Birkac kare beklenir (git_adim sayar); kaydirma her adimda verilir ki imlec konamasa da satir gorunsun.
  if (k.git_satir > 0 && k.git_adim >= 1) {
    if (ic) ImGui::SetScrollY(ic, std::max(0.0f, (k.git_satir - 1) * fs - ic->Size.y * 0.35f));
    bool bitti = ++k.git_adim > 6;
    if (ImGuiInputTextState *s = ImGui::GetInputTextState(kid)) {
      const int o = (int)kod_satir_ofseti(k.metin, k.git_satir);
      s->ReloadSelectionStart = s->ReloadSelectionEnd = o;
      s->WantReloadUserBuf = true;
      bitti = true;
    }
    if (bitti) { k.git_satir = 0; k.git_adim = 0; }
  }
  // Oluk: satir numaralari, metin kutusunun kaydirmasiyla hizali; hata satiri kirmizi.
  if (ic) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float ust = ic->Pos.y + st.FramePadding.y - ic->Scroll.y;
    const ImVec2 o0(bas.x, ic->Pos.y), o1(bas.x + oluk, ic->Pos.y + ic->Size.y);
    dl->PushClipRect(o0, o1, true);
    const int ilk = std::max(1, (int)((ic->Scroll.y - st.FramePadding.y) / fs));
    const int son = std::min(satir_n, ilk + (int)(ic->Size.y / fs) + 2);
    for (int s = ilk; s <= son; s++) {
      const float y = ust + (s - 1) * fs;
      const bool vurgu = s == k.vurgu_satir;
      if (vurgu) dl->AddRectFilled(ImVec2(o0.x, y), ImVec2(ic->Pos.x + ic->Size.x, y + fs), IM_COL32(200, 60, 50, 90));
      std::snprintf(nb, sizeof nb, "%d", s);
      const float tw = ImGui::CalcTextSize(nb).x;
      dl->AddText(ImVec2(o1.x - st.ItemInnerSpacing.x - tw, y),
                  vurgu ? IM_COL32(255, 140, 120, 255) : (s == k.imlec_satir ? IM_COL32(220, 220, 230, 255) : IM_COL32(120, 124, 140, 255)), nb);
    }
    dl->PopClipRect();
    if (k.vurgu_satir > 0) { // metnin uzerine de ince serit (metin kutusu kendi zeminini cizdi)
      const float y = ust + (k.vurgu_satir - 1) * fs;
      if (y + fs > ic->Pos.y && y < ic->Pos.y + ic->Size.y)
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ic->Pos.x, y), ImVec2(ic->Pos.x + ic->Size.x, y + fs), IM_COL32(220, 70, 55, 45));
    }
  }
  // --- Durum satiri ---
  ImGui::TextDisabled("Sat\xC4\xB1r %d, s\xC3\xBCtun %d  \xC2\xB7  %d sat\xC4\xB1r  \xC2\xB7  %s", k.imlec_satir, k.imlec_sutun, satir_n,
                      k.kirli ? "kaydedilmedi" : "kaydedildi");
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    kaydet = k.kirli && !k.salt_okunur;
  if (kaydet) {
    char e[256];
    if (!kod_kaydet(k, e, sizeof e)) std::fprintf(stderr, "[editor] kod paneli: %s\n", e);
  }
  ImGui::End();
  if (!acik) {
    if (k.kirli) { k.acik = true; ImGui::OpenPopup("###kod_kapat_sor"); } // kaydedilmemis: sor
    else k.acik = false;
  }
  if (ImGui::BeginPopupModal("Kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklik###kod_kapat_sor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s kaydedilmedi.", ad);
    if (ImGui::Button("Kaydet ve kapat")) { char e[256]; if (kod_kaydet(k, e, sizeof e)) k.acik = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Kaydetmeden kapat")) { k.kirli = false; k.acik = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Vazge\xC3\xA7")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return eylem;
}

} // namespace tulpar::engine::app
