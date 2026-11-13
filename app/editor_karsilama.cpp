// L6 APP — Acilis (karsilama) ekrani (editor_karsilama.hpp).
#include "app/editor_karsilama.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <IconsMaterialDesign.h>
#include <imgui.h>
#include <imgui_internal.h>

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

// Girdide Enter: forma "Olustur" demek (klavyeyle bitirme). Kare basinda sifirlanir.
bool g_girdi_enter = false;

bool girdi(const char *etiket, const char *ipucu, char *buf, size_t cap, float w) {
  ImGui::TextColored(renk(Tone::TextDim), "%s", etiket);
  ImGui::SetNextItemWidth(w);
  char id[96];
  std::snprintf(id, sizeof id, "##%s", etiket);
  const bool r = ImGui::InputTextWithHint(id, ipucu, buf, cap);
  if (ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) g_girdi_enter = true;
  return r;
}

} // namespace

void karsilama_kok(char *out, uint32_t cap) {
  // TULPAR_OYUNLAR_DIZINI: kapilar ve arayuz surucusu kullanicinin GERCEK oyun klasorune yazmasin.
  const char *ozel = std::getenv("TULPAR_OYUNLAR_DIZINI");
  if (ozel && *ozel) { std::snprintf(out, cap, "%s", ozel); return; }
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
  g_girdi_enter = false;
  if (!d.baslatildi) {
    d.baslatildi = true;
    d.odak_ad = true;
    karsilama_kok(d.kok, sizeof d.kok);
    d.sablon_n = fabrika_sablonlar(d.sablonlar, 8);
    for (uint32_t i = 0; i < d.sablon_n; i++) {
      SablonBilgi b;
      fabrika_sablon_bilgi(d.sablonlar[i], &b);
      std::snprintf(d.sablon_ad[i], sizeof d.sablon_ad[i], "%s", b.ad);
      std::snprintf(d.sablon_aciklama[i], sizeof d.sablon_aciklama[i], "%s", b.aciklama);
      std::snprintf(d.sablon_ikon[i], sizeof d.sablon_ikon[i], "%s", b.ikon);
      d.sablon_yon[i] = b.yon;
    }
    if (d.sablon_n) d.yon = d.sablon_yon[d.sablon >= 0 && (uint32_t)d.sablon < d.sablon_n ? d.sablon : 0];
  }
  const ImGuiViewport *vp = ImGui::GetMainViewport();
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
  // Arkadaki editor gorunur ama soluk ("henuz baslamadin"). U11 A3: eskiden ayri bir karartma penceresiydi ve
  // dock panellerinin ALTINDA kaliyordu (ekran goruntusunde arka plan hic kararmamisti; pencerenin kendi
  // cizim listesine pencere disina cizmek de gorunmedi — olculdu). Simdi ON PLAN listesinde, karsilamanin
  // disina dusen dort dikdortgen: her paneli ortuyor, karsilamaya dokunmuyor. Karsilama acikken acilir
  // pencere/ipucu yalniz kendi alaninda dogdugu icin onlar da karanlikta kalmaz.
  {
    ImDrawList *dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    const ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    const ImVec2 v0 = vp->Pos, v1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    // Alfa 225: cerceve tamponu sRGB, karisim DOGRUSAL uzayda. Alfa 175 algida yalniz ~%35 karartiyordu
    // (olculdu 2026-11-11, sürücü: yazi 230 -> 144, göz farki seçmiyor); 225 hesapla ~85, kipli pencere gibi.
    const ImU32 kc = IM_COL32(4, 5, 8, 225);
    dl->PushClipRect(v0, v1, false);
    dl->AddRectFilled(v0, ImVec2(v1.x, a.y), kc);
    dl->AddRectFilled(ImVec2(v0.x, b.y), v1, kc);
    dl->AddRectFilled(ImVec2(v0.x, a.y), ImVec2(a.x, b.y), kc);
    dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(v1.x, b.y), kc);
    dl->PopClipRect();
  }
  // --- Baslik ---
  push_text_size(TextSize::Lg);
  ImGui::TextColored(renk(Tone::AccentHi), "Tulpar Edit\xC3\xB6r");
  pop_text_size();
  ImGui::SameLine();
  // Surum; "(kaynak derlemesi)" gelistirici bilgisi, yalniz Gelismis'te (U11 A3).
  if (surum && surum[0]) ImGui::TextColored(renk(Tone::TextDim), "%s", surum);
  else if (editor_gelismis()) ImGui::TextColored(renk(Tone::TextDim), "(kaynak derlemesi)");
  else ImGui::NewLine();
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
  // Klavye: karsilama acilinca ilk Tab beklemeden ad alanina yaz; Enter formu tamamlar (surucu gezintisi
  // 2026-11-13: Tab ad alanina inmiyor, yazilan metin bosa gidiyor, Enter diyalogu kapatiyordu).
  if (d.odak_ad) { ImGui::SetKeyboardFocusHere(); d.odak_ad = false; }
  bool degisti = girdi("Oyunun ad\xC4\xB1", "\xC3\xB6rn. Par\xC4\xB1lt\xC4\xB1 Av\xC4\xB1", d.tr, sizeof d.tr, w);
  // U11 A3: Ingilizce ad ISTEGE BAGLI; bos kalirsa Turkce ad kullanilir (ipucu bunu gosterir).
  {
    char ipucu[96];
    if (d.tr[0]) std::snprintf(ipucu, sizeof ipucu, "bo\xC5\x9F: %s", d.tr);
    else std::snprintf(ipucu, sizeof ipucu, "e.g. Spark Hunt");
    girdi("\xC4\xB0ngilizce ad\xC4\xB1 (iste\xC4\x9F" "e ba\xC4\x9Fl\xC4\xB1)", ipucu, d.en, sizeof d.en, w);
  }
  if (degisti) karsilama_oneri(d);
  // U11 A3: sablon KARTLARI (combo'daki ic ad "arena" yerine ad + ne oynanir). Kart secilince ekran yonu
  // sablonun onerisine gecer; yon yine degistirilebilir.
  if (d.sablon_n > 0) {
    ImGui::TextColored(renk(Tone::TextDim), "Ne t\xC3\xBCr bir oyun?");
    // Yukseklik: baslik satiri + uc satir aciklama (0.92 olcekli) + ic bosluk; uzun aciklama kesilmez, tasmaz.
    const float kw = (w - st.ItemSpacing.x) * 0.5f, kh = ImGui::GetTextLineHeight() * (1.45f + 3.0f * 0.92f) + st.FramePadding.x * 3.2f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (uint32_t i = 0; i < d.sablon_n; i++) {
      if (i % 2) ImGui::SameLine();
      ImGui::PushID((int)i);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##sablon_kart", ImVec2(kw, kh))) {
        d.sablon = (int)i;
        d.yon = d.sablon_yon[i];
      }
      // Gorunmez dugmenin etiketi yok: sablon adini bildir (arayuz surucusu kartı adiyla bulur).
      { ImGuiContext &g = *GImGui; IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), d.sablon_ad[i], 0); }
      const bool sec = d.sablon == (int)i, ust = ImGui::IsItemHovered();
      const ImVec2 p1(p0.x + kw, p0.y + kh);
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(renk(sec ? Tone::Bg3 : ust ? Tone::Bg2 : Tone::Bg1)), 6.0f);
      dl->AddRect(p0, p1, ImGui::GetColorU32(renk(sec ? Tone::Accent : Tone::Line)), 6.0f, sec ? 2.0f : 1.0f); // 1.92.8+: (.., rounding, THICKNESS)
      const char *ik = !std::strcmp(d.sablon_ikon[i], "top")       ? ICON_MD_SPORTS_SOCCER
                       : !std::strcmp(d.sablon_ikon[i], "bilardo") ? ICON_MD_ADJUST
                                                                    : ICON_MD_SPORTS_ESPORTS;
      const float pad = st.FramePadding.x * 1.5f, lh = ImGui::GetTextLineHeight();
      dl->AddText(ImVec2(p0.x + pad, p0.y + pad), ImGui::GetColorU32(renk(sec ? Tone::AccentHi : Tone::TextDim)), ik);
      dl->AddText(ImVec2(p0.x + pad + lh * 1.5f, p0.y + pad), ImGui::GetColorU32(renk(Tone::Text)), d.sablon_ad[i]);
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.92f, ImVec2(p0.x + pad, p0.y + pad + lh * 1.45f),
                  ImGui::GetColorU32(renk(Tone::TextDim)), d.sablon_aciklama[i], nullptr, kw - pad * 2.0f);
      if (sec) dl->AddText(ImVec2(p1.x - pad - lh, p0.y + pad), ImGui::GetColorU32(renk(Tone::Accent)), ICON_MD_CHECK_CIRCLE);
      ImGui::PopID();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(renk(Tone::TextDim), "Ekran:");
    ImGui::SameLine();
    ImGui::RadioButton("Yatay", &d.yon, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dikey", &d.yon, 1);
  }
  {
    // Konum: metin + "Sec..." (isletim sisteminin klasor secicisi, U4). Secilen klasor KOK olur,
    // oyun onun altinda <kimlik> dizinine kurulur.
    // U11 A3: konum KISA gosterilir ("~/TulparOyunlari/yildiz"); elle yazma Gelismis bolumunde.
    const float sec_w = ImGui::CalcTextSize("De\xC4\x9Fi\xC5\x9Ftir\xE2\x80\xA6").x + st.FramePadding.x * 2.0f;
    ImGui::TextColored(renk(Tone::TextDim), "Nereye kaydedilsin?");
    {
      char kisa[1024], kirp[1024];
      const char *ev = std::getenv("HOME");
      if (!ev || !*ev) ev = std::getenv("USERPROFILE");
      const size_t el = ev ? std::strlen(ev) : 0;
      const char *k = d.konum[0] ? d.konum : d.kok;
      if (el > 1 && !std::strncmp(k, ev, el) && (k[el] == '/' || k[el] == '\\' || !k[el])) std::snprintf(kisa, sizeof kisa, "~%s", k + el);
      else std::snprintf(kisa, sizeof kisa, "%s", k);
      editor_ellipsize(kisa, w - sec_w - st.ItemSpacing.x * 2.0f - st.FramePadding.x * 2.0f, kirp, sizeof kirp);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(renk(Tone::Text), ICON_MD_FOLDER " %s", kirp);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k);
    }
    ImGui::SameLine(w - sec_w);
    if (ImGui::Button("De\xC4\x9Fi\xC5\x9Ftir\xE2\x80\xA6##konum", ImVec2(sec_w, 0))) {
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
    // Agac dugumu icerigi girintiler: dis w ile girdi kaydirma cubugunun altina tasiyordu
    // (arayuz surucusu, 1280x720, 2026-11-13). Girintili alanin genisligi kullanilir.
    const float w = ImGui::GetContentRegionAvail().x - st.ItemSpacing.x;
    if (girdi("Konum (tam yol)", "klas\xC3\xB6r", d.konum, sizeof d.konum, w)) d.konum_elle = true;
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
  const bool hazir = d.tr[0] && d.kimlik[0] && d.konum[0]; // U11 A3: Ingilizce ad istege bagli
  // Vurgu rengi YALNIZ hazirken: soluk ama mavi dugme "basilabilir" okunuyordu (arayuz surucusu ekran
  // goruntusu, 2026-11-06: hazir degilken (34,80,188), hazirken (38,99,235) — fark goze gorunmuyordu).
  if (!hazir) ImGui::BeginDisabled();
  if (hazir) ImGui::PushStyleColor(ImGuiCol_Button, renk(Tone::Accent));
  if (ImGui::Button(ICON_MD_ROCKET_LAUNCH "  Olu\xC5\x9Ftur", ImVec2(w, ImGui::GetFrameHeight() * 1.4f)) || (hazir && g_girdi_enter)) {
    if (!d.en[0]) std::snprintf(d.en, sizeof d.en, "%s", d.tr); // bos Ingilizce ad: Turkce ad
    e = KarsilamaEylem::YeniOyun;
  }
  if (hazir) ImGui::PopStyleColor();
  if (!hazir) ImGui::EndDisabled();
  if (d.hata[0]) ImGui::TextColored(renk(Tone::Err), "%s", d.hata);
  else if (d.bilgi[0]) ImGui::TextColored(renk(Tone::Ok), "%s", d.bilgi);
  else if (!hazir) ImGui::TextColored(renk(Tone::TextMute), "Ba\xC5\x9Flamak i\xC3\xA7in oyununa bir ad ver.");
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
  if (ImGui::Button(ICON_MD_SPORTS_ESPORTS "  \xC3\x96rnek: Bilardo", ImVec2(bw, 0))) e = KarsilamaEylem::Ornek;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Oynanabilir bilardo \xC3\xB6rne\xC4\x9Fi (F5). \xC4\xB0lk a\xC3\xA7\xC4\xB1l\xC4\xB1\xC5\x9Fta oyun klas\xC3\xB6r\xC3\xBC" "n\xC3\xBC" "ze\n"
                      "Ornek_Bilardo olarak kopyalan\xC4\xB1r; de\xC4\x9Fi\xC5\x9Fiklikleri kaydedebilirsiniz.");
  if (ImGui::Button("Bo\xC5\x9F edit\xC3\xB6rle ba\xC5\x9Fla", ImVec2(-FLT_MIN, 0))) e = KarsilamaEylem::Kapat;
  ImGui::EndChild();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) e = KarsilamaEylem::Kapat;
  ImGui::End();
  return e;
}

} // namespace tulpar::engine::app
