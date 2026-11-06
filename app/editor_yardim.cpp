// L6 APP \xE2\x80\x94 Yardim ve destek (editor_yardim.hpp).
#include "app/editor_yardim.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"
#include <imgui.h>
#include "platform/fs.hpp"
#include "platform/fs_ops.hpp"
#include "platform/process.hpp"

namespace tulpar::engine::app {

namespace {

struct Sayfa {
  const char *baslik;
  const char *metin;
};
// Kisa, eylem odakli. Teknik terim YOK (Basit kip kurali, U5).
const Sayfa kSayfalar[] = {
    {"\xC4\xB0lk oyununuz",
     "1. Dosya > Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 ekran\xC4\xB1 > Yeni oyun: oyunun ad\xC4\xB1n\xC4\xB1 yaz\xC4\xB1n, Olu\xC5\x9Ftur'a bas\xC4\xB1n.\n"
     "2. Edit\xC3\xB6r yeni projenin sahnesini a\xC3\xA7" "ar. Oyun dosyalar\xC4\xB1 se\xC3\xA7ti\xC4\x9Finiz klas\xC3\xB6rdedir "
     "(varsay\xC4\xB1lan: ev dizininizde TulparOyunlari; Windows'ta Belgeler/TulparOyunlari).\n"
     "3. F5 ile oynat\xC4\xB1n. Bilgisayarda Tulpar yoksa edit\xC3\xB6r tek t\xC4\xB1kla kurmay\xC4\xB1 \xC3\xB6nerir.\n"
     "4. De\xC4\x9Fi\xC5\x9Fiklikleri Ctrl+S ile kaydedin; bir dahaki a\xC3\xA7\xC4\xB1l\xC4\xB1\xC5\x9Fta Son projeler listesinde bulursunuz."},
    {"Sahne",
     "Sahne, oyundaki nesnelerin (zemin, duvar, karakter, \xC4\xB1\xC5\x9F\xC4\xB1k) yerle\xC5\x9Fimidir.\n"
     "\xE2\x80\xA2 Soldaki listeden ya da g\xC3\xB6r\xC3\xBCn\xC3\xBCmde t\xC4\xB1klayarak nesne se\xC3\xA7in.\n"
     "\xE2\x80\xA2 Ta\xC5\x9F\xC4\xB1 / D\xC3\xB6nd\xC3\xBCr / \xC3\x96l\xC3\xA7" "ekle (T, R, S) ile tutamaklar\xC4\xB1 s\xC3\xBCr\xC3\xBCkleyin; sa\xC4\x9F" "daki \xC3\x96zellikler panelinde "
     "say\xC4\xB1yla da girebilirsiniz.\n"
     "\xE2\x80\xA2 Yeni nesne: Sahne panelindeki + d\xC3\xBC\xC4\x9Fmesi.\n"
     "\xE2\x80\xA2 Geri al Ctrl+Z, yinele Ctrl+Y."},
    {"Betik (oyun kodu)",
     "Oyunun kurallar\xC4\xB1 Tulpar dilinde yaz\xC4\xB1l\xC4\xB1r: projenin k\xC3\xB6k\xC3\xBCndeki oyun.tpr.\n"
     "\xE2\x80\xA2 Nesneye betik ba\xC4\x9Flamak: \xC3\x96zellikler panelindeki Betik b\xC3\xB6l\xC3\xBCm\xC3\xBC.\n"
     "\xE2\x80\xA2 Betik dosyas\xC4\xB1 \xC3\xA7ift t\xC4\xB1klan\xC4\xB1nca kod d\xC3\xBCzenleyicinizde a\xC3\xA7\xC4\xB1l\xC4\xB1r (VS Code varsa o).\n"
     "\xE2\x80\xA2 Kaydedip F5'e bas\xC4\xB1nca oyun yeniden derlenir; hatalar Konsol panelinde g\xC3\xB6r\xC3\xBCn\xC3\xBCr."},
    {"Oynat",
     "F5: oyunu G\xC3\xB6r\xC3\xBCn\xC3\xBCm panelinin Oyun sekmesinde oynat\xC4\xB1r; tekrar F5 durdurur.\n"
     "F6 duraklat\xC4\xB1r, F10 tek kare ilerletir.\n"
     "Ctrl+F5: oyunu ayr\xC4\xB1 pencerede \xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r\xC4\xB1r.\n"
     "Oyunun yazd\xC4\xB1\xC4\x9F\xC4\xB1 her sat\xC4\xB1r Konsol'a gelir; k\xC4\xB1rm\xC4\xB1z\xC4\xB1 sat\xC4\xB1rlar hatad\xC4\xB1r."},
    {"Android'e \xC3\xA7\xC4\xB1k\xC4\xB1\xC5\x9F",
     "Oyun > Android APK olu\xC5\x9Ftur: projenin APK's\xC4\xB1n\xC4\xB1 derler ve kendisinden do\xC4\x9Frular "
     "(\xC3\xA7\xC4\xB1kt\xC4\xB1: projede cikti/apk). Android ara\xC3\xA7lar\xC4\xB1 gerekir (tulpar update --android).\n"
     "Oyun > Projeyi do\xC4\x9Frula: oyunun kendi denetimlerini penceresiz ko\xC5\x9Fturur.\n"
     "Google Play'e y\xC3\xBCkleme edit\xC3\xB6rden yap\xC4\xB1lmaz."},
};
constexpr uint32_t kSayfaN = sizeof kSayfalar / sizeof kSayfalar[0];

constexpr const char *kIsaret = "oturum.acik";

bool tam_yol(char *out, size_t cap, const char *d, const char *ad) {
  const int n = std::snprintf(out, cap, "%s/%s", d, ad);
  return n > 0 && (size_t)n < cap;
}

struct EnYeni {
  char dizin[1100];
  char ad[256];
  int64_t mt;
};

} // namespace

uint32_t yardim_kisayol_satirlari(const CommandDesc *tablo, uint32_t n, KisayolSatiri *out, uint32_t cap) {
  uint32_t k = 0;
  for (uint32_t c = 0; c < kCommandCategoryCount; c++) {
    for (uint32_t i = 0; i < n; i++) {
      const CommandDesc &d = tablo[i];
      if ((uint32_t)d.category != c || d.id == CommandId::None) continue;
      if (k < cap) {
        KisayolSatiri &s = out[k];
        s.kategori = d.category;
        s.ad = d.name;
        s.aciklama = d.help;
        s.kisayol[0] = 0;
        char a[32] = "", b[32] = "";
        if (d.shortcut != kChordNone) command_shortcut_text(d.shortcut, a, sizeof a);
        if (d.shortcut_alt != kChordNone) command_shortcut_text(d.shortcut_alt, b, sizeof b);
        if (a[0] && b[0]) std::snprintf(s.kisayol, sizeof s.kisayol, "%s, %s", a, b);
        else std::snprintf(s.kisayol, sizeof s.kisayol, "%s", a[0] ? a : b);
      }
      k++;
    }
  }
  return k;
}

uint32_t yardim_rehber_sayisi() { return kSayfaN; }
const char *yardim_rehber_baslik(uint32_t i) { return i < kSayfaN ? kSayfalar[i].baslik : ""; }
const char *yardim_rehber_metin(uint32_t i) { return i < kSayfaN ? kSayfalar[i].metin : ""; }

bool yardim_oturum_baslat(const char *ayar, char *rapor, size_t cap) {
  if (rapor && cap) rapor[0] = 0;
  if (!ayar || !*ayar) return false;
  char isaret[1200];
  if (!tam_yol(isaret, sizeof isaret, ayar, kIsaret)) return false;
  const bool coktu = platform::fs_kind(isaret) == platform::FsKind::File;
  if (coktu && rapor && cap) {
    EnYeni e{};
    if (tam_yol(e.dizin, sizeof e.dizin, ayar, "cokme")) {
      e.mt = -1;
      platform::fs_list_dir(e.dizin, [](const char *ad, platform::FsKind k, void *u) {
        EnYeni *e = static_cast<EnYeni *>(u);
        if (k != platform::FsKind::File || std::strncmp(ad, "crash_", 6) != 0) return true;
        char p[1400];
        std::snprintf(p, sizeof p, "%s/%s", e->dizin, ad);
        int64_t mt = 0, sz = 0;
        if (platform::fs_file_stamp(p, &mt, &sz) && mt > e->mt) {
          e->mt = mt;
          std::snprintf(e->ad, sizeof e->ad, "%s", ad);
        }
        return true;
      }, &e);
      if (e.ad[0]) std::snprintf(rapor, cap, "%s/%s", e.dizin, e.ad);
    }
  }
  (void)platform::fs_write_all(isaret, "1\n", 2);
  return coktu;
}

void yardim_oturum_bitir(const char *ayar) {
  char isaret[1200];
  if (ayar && *ayar && tam_yol(isaret, sizeof isaret, ayar, kIsaret)) platform::fs_remove_file(isaret);
}

void yardim_ciz(YardimDurum &d, const CommandDesc *tablo, uint32_t n) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  if (d.rehber) {
    ImGui::SetNextWindowSize(ImVec2(620.0f, 380.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 rehberi###yardim_rehber", &d.rehber, ImGuiWindowFlags_NoCollapse)) {
      if (ImGui::BeginChild("##sayfalar", ImVec2(170.0f, 0.0f), ImGuiChildFlags_Borders)) {
        for (uint32_t i = 0; i < kSayfaN; i++)
          if (ImGui::Selectable(kSayfalar[i].baslik, d.sayfa == i)) d.sayfa = i;
      }
      ImGui::EndChild();
      ImGui::SameLine();
      if (ImGui::BeginChild("##metin", ImVec2(0.0f, 0.0f))) {
        const uint32_t s = d.sayfa < kSayfaN ? d.sayfa : 0;
        push_text_size(TextSize::Lg);
        ImGui::TextUnformatted(kSayfalar[s].baslik);
        pop_text_size();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(kSayfalar[s].metin);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (s > 0 && ImGui::Button("\xE2\x80\xB9 \xC3\x96nceki")) d.sayfa = s - 1;
        if (s > 0 && s + 1 < kSayfaN) ImGui::SameLine();
        if (s + 1 < kSayfaN && ImGui::Button("Sonraki \xE2\x80\xBA")) d.sayfa = s + 1;
      }
      ImGui::EndChild();
    }
    ImGui::End();
  }
  if (d.kisayollar) {
    static KisayolSatiri satir[kCommandCount + 8];
    const uint32_t k = yardim_kisayol_satirlari(tablo, n, satir, kCommandCount + 8);
    d.son_kisayol_satiri = k;
    ImGui::SetNextWindowSize(ImVec2(640.0f, 460.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("K\xC4\xB1sayollar###yardim_kisayol", &d.kisayollar, ImGuiWindowFlags_NoCollapse)) {
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##filtre", "Ara (ad ya da tu\xC5\x9F)\xE2\x80\xA6", d.filtre, sizeof d.filtre);
      if (ImGui::BeginTable("##kisayol", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Komut", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tu\xC5\x9F", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        int son_kat = -1;
        for (uint32_t i = 0; i < k && i < kCommandCount + 8; i++) {
          const KisayolSatiri &s = satir[i];
          if (d.filtre[0] && !std::strstr(s.ad, d.filtre) && !std::strstr(s.kisayol, d.filtre)) continue;
          if ((int)s.kategori != son_kat) {
            son_kat = (int)s.kategori;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", command_category_name(s.kategori));
          }
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(s.ad);
          if (s.aciklama[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.aciklama);
          ImGui::TableSetColumnIndex(1);
          if (s.kisayol[0]) ImGui::TextUnformatted(s.kisayol);
          else ImGui::TextDisabled("\xE2\x80\x94");
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();
  }
  if (d.cokme) {
    const char *baslik = "Edit\xC3\xB6r beklenmedik \xC5\x9F" "ekilde kapand\xC4\xB1###yardim_cokme";
    if (!ImGui::IsPopupOpen(baslik)) ImGui::OpenPopup(baslik);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(baslik, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 540.0f);
      ImGui::TextUnformatted("\xC3\x96nceki oturum d\xC3\xBCzg\xC3\xBCn kapanmad\xC4\xB1. Kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklikler kaybolmu\xC5\x9F olabilir.");
      ImGui::Spacing();
      if (d.cokme_rapor[0]) {
        ImGui::TextUnformatted("\xC3\x87\xC3\xB6kme raporu yaz\xC4\xB1ld\xC4\xB1; hatay\xC4\xB1 bildirirken bu dosyay\xC4\xB1 ekleyin:");
        ImGui::TextDisabled("%s", d.cokme_rapor);
      } else {
        ImGui::TextUnformatted("Rapor yaz\xC4\xB1lmam\xC4\xB1\xC5\x9F (edit\xC3\xB6r kapat\xC4\xB1lm\xC4\xB1\xC5\x9F ya da bilgisayar kapanm\xC4\xB1\xC5\x9F olabilir).");
      }
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
      if (d.cokme_rapor[0]) {
        if (ImGui::Button("Klas\xC3\xB6r\xC3\xBC a\xC3\xA7")) {
#if defined(_WIN32)
          const CodeEditorCandidate c[] = {{"explorer.exe", nullptr}};
#elif defined(__APPLE__)
          const CodeEditorCandidate c[] = {{"open", nullptr}};
#else
          const CodeEditorCandidate c[] = {{"xdg-open", nullptr}};
#endif
          char used[128], err[256];
          (void)editor_open_with_candidates(d.cokme_dizin, c, 1, used, sizeof used, err, sizeof err);
        }
        ImGui::SameLine();
        if (ImGui::Button("Yolu kopyala")) ImGui::SetClipboardText(d.cokme_rapor);
        ImGui::SameLine();
      }
      if (ImGui::Button("Tamam")) {
        d.cokme = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
}

} // namespace tulpar::engine::app
