// L6 APP — Editor ici guncellemenin arayuzu (neden ve sozlesme: editor_update.hpp).
//
// Iki yarisi var ve bilerek AYRI:
//   1) Saf kisim (ayar dosyasi, otomatik karar, rozet metni, yeniden baslatma
//      argv'si): ImGui'siz, dosya/ag'siz — engine_tests dogrudan olcer.
//   2) Suren kisim (UpdateUi): Updater'i kurar, her kare poll eder, durum
//      gecislerini Konsol'a yazar, pencereleri cizer. Updater'a giden HER
//      istek (check/download) tek bir yerden gecer ve sayilir (`requests`);
//      penceresiz kipte o yer istegi REDDEDER ve reddi de sayar.
#include "app/editor_update.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "app/editor_console.hpp"
#include "app/editor_ui.hpp"
#include "core/build_info.hpp"
#include "core/memory/arena.hpp"
#include "platform/fs.hpp"
#include "platform/fs_ops.hpp" // UTF-8 yollar (Windows W-API): kurulum dizini, ikilinin varligi
#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"

#include <imgui.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // MultiByteToWideChar: argv ANSI -> UTF-8 (yeniden baslatma)
#endif

namespace tulpar::engine::app {
namespace {

constexpr const char *kTag = "guncelleme"; // Konsol etiketi
constexpr uint64_t kNsPerSec = 1000000000ull;

#if defined(_WIN32)
constexpr const char *kEditorExe = "engine_editor.exe";
constexpr const char *kRebuildHint = "git pull && derle.bat";
#else
constexpr const char *kEditorExe = "engine_editor";
constexpr const char *kRebuildHint = "git pull && ./derle.sh";
#endif

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool span_eq(const char *a, size_t n, const char *lit) {
  const size_t m = std::strlen(lit);
  return n == m && std::memcmp(a, lit, n) == 0;
}

int64_t now_unix() { return (int64_t)std::time(nullptr); }

// Unix saniye -> "2026-09-25 09:44 UTC" (0 -> "hic").
void unix_text(int64_t t, char *out, size_t cap) {
  if (t <= 0) { std::snprintf(out, cap, "hi\xC3\xA7"); return; }
  const std::time_t tt = (std::time_t)t;
  const std::tm *g = std::gmtime(&tt); // tek is parcacigi (arayuz); statik tampon yeterli
  if (!g || std::strftime(out, cap, "%Y-%m-%d %H:%M UTC", g) == 0) std::snprintf(out, cap, "%lld", (long long)t);
}

// Cekirdegin gorecegi yapilandirma: alan > ortam ezmesi > varsayilan.
struct Resolved {
  char install_dir[kUpdPathCap] = {0};
  char version[kUpdTagLen] = {0};
  const char *api_url = nullptr;
  bool allow_file_urls = false;
};
void resolve(const UpdateUiConfig &c, Resolved *r) {
  const char *dir = c.install_dir;
  if (!dir || !*dir) dir = std::getenv("TULPAR_GUNCELLEME_DIZIN");
  if (dir && *dir) std::snprintf(r->install_dir, sizeof r->install_dir, "%s", dir);
  // UTF-8 (cekirdek ve process_start UTF-8 bekler; exe_dir Windows'ta ANSI verir).
  else if (!platform::fs_exe_dir_utf8(r->install_dir, sizeof r->install_dir)) r->install_dir[0] = 0;
  const char *v = c.current_version;
  if (!v) v = std::getenv("TULPAR_GUNCELLEME_SURUM");
  if (!v) v = build_version();
  std::snprintf(r->version, sizeof r->version, "%s", v ? v : "");
  r->api_url = (c.api_url && *c.api_url) ? c.api_url : nullptr;
  const char *env_url = std::getenv("TULPAR_GUNCELLEME_URL");
  r->allow_file_urls = c.allow_file_urls || (env_url && *env_url);
}
UpdaterConfig to_updater_config(const Resolved &r) {
  UpdaterConfig uc;
  uc.current_version = r.version;
  uc.platform = build_platform();
  uc.api_url = r.api_url;
  uc.install_dir = r.install_dir[0] ? r.install_dir : nullptr;
  uc.allow_file_urls = r.allow_file_urls;
  return uc;
}

// --- Updater'a giden TEK kapi ------------------------------------------------
// Penceresiz kipte reddeder (ve sayar); aksi halde durumu denetler, sayar,
// cagirir. Baska hicbir yer u.upd.check()/download() CAGIRMAZ.
bool busy(UpdState s) { return s == UpdState::Checking || s == UpdState::Downloading || s == UpdState::Verifying || s == UpdState::Extracting; }

bool request_check(UpdateUi &u, bool manual) {
  if (u.headless) {
    u.refused_headless++;
    console_log(ConsoleLevel::Uyari, kTag, "penceresiz kip: a\xC4\x9F denetimi yap\xC4\xB1lmaz (istek reddedildi)");
    return false;
  }
  if (!u.ready) return false;
  const UpdState s = u.upd.state();
  if (s == UpdState::Disabled || busy(s) || s == UpdState::Staged || s == UpdState::Installed) return false;
  u.manual = manual;
  u.download_stage = false;
  u.requests++;
  u.upd.check();
  return true;
}

bool request_download(UpdateUi &u) {
  if (u.headless) {
    u.refused_headless++;
    console_log(ConsoleLevel::Uyari, kTag, "penceresiz kip: indirme yap\xC4\xB1lmaz (istek reddedildi)");
    return false;
  }
  if (!u.ready || u.upd.state() != UpdState::Available) return false;
  u.download_stage = true;
  u.requests++;
  u.upd.download();
  return true;
}

void save_settings(UpdateUi &u) {
  if (u.headless || !u.write_settings || !u.settings_path[0]) return;
  if (!upd_settings_save(u.settings_path, u.settings))
    console_log(ConsoleLevel::Uyari, kTag, "ayar dosyas\xC4\xB1 yaz\xC4\xB1lamad\xC4\xB1: %s", u.settings_path);
}

// Basarili bir denetimin sonu (UpToDate ya da Available): zaman damgasi + bir
// sonraki otomatik denetim 24 saat sonra.
void check_done(UpdateUi &u) {
  u.settings.last_check = now_unix();
  save_settings(u);
  if (!u.headless && u.settings.auto_check) u.next_auto_ns = u.last_now_ns + (uint64_t)kUpdAutoIntervalSec * kNsPerSec;
}

bool skipped(const UpdateUi &u, const UpdRelease &r) { return u.settings.skip_tag[0] && !std::strcmp(u.settings.skip_tag, r.tag); }

void log_transition(UpdateUi &u, UpdState from, UpdState to) {
  u.transitions++;
  const UpdRelease &r = u.upd.release();
  switch (to) {
  case UpdState::Checking:
    console_log(ConsoleLevel::Bilgi, kTag, "denetleniyor (%s)", u.manual ? "elle" : "otomatik");
    break;
  case UpdState::UpToDate:
    console_log(ConsoleLevel::Bilgi, kTag, "g\xC3\xBCncel: kurulu %s en son s\xC3\xBCr\xC3\xBCm", u.current_version);
    check_done(u);
    break;
  case UpdState::Available: {
    if (from == UpdState::Checking) {
      char d[32];
      upd_date_short(r.published_at, d, sizeof d);
      const bool sk = skipped(u, r);
      console_log(ConsoleLevel::Bilgi, kTag, "yeni s\xC3\xBCr\xC3\xBCm: %s (kurulu %s, yay\xC4\xB1n %s)%s", r.tag, u.current_version, d,
                  sk && !u.manual ? " \xE2\x80\x94 atlanm\xC4\xB1\xC5\x9F s\xC3\xBCr\xC3\xBCm, rozet g\xC3\xB6sterilmiyor" : "");
      check_done(u);
      if (u.retry_download) {
        u.retry_download = false;
        request_download(u);
      }
    } else if (busy(from)) {
      console_log(ConsoleLevel::Uyari, kTag, "indirme iptal edildi: %s", r.tag);
    }
    break;
  }
  case UpdState::Downloading:
    console_log(ConsoleLevel::Bilgi, kTag, "indiriliyor: %s (%.1f MB)", r.asset_name, (double)r.asset_size / (1024.0 * 1024.0));
    break;
  case UpdState::Verifying: console_log(ConsoleLevel::Bilgi, kTag, "do\xC4\x9Frulan\xC4\xB1yor (SHA-256): %s", r.asset_name); break;
  case UpdState::Extracting: console_log(ConsoleLevel::Bilgi, kTag, "a\xC3\xA7\xC4\xB1l\xC4\xB1yor: %s", r.asset_name); break;
  case UpdState::Staged: {
    const UpdPlanSummary p = u.upd.plan_summary();
    console_log(ConsoleLevel::Bilgi, kTag,
                "kurulmaya haz\xC4\xB1r: %s \xE2\x80\x94 %u dosya g\xC3\xBCncellenecek (%u yeni, %u de\xC4\x9Fi\xC5\x9F" "en, %u kald\xC4\xB1r\xC4\xB1lan), %u ayn\xC4\xB1, %u kullan\xC4\xB1" "c\xC4\xB1 dosyas\xC4\xB1 korunacak",
                r.tag, p.add + p.replace + p.remove, p.add, p.replace, p.remove, p.same, p.kept_user);
    const uint32_t k = u.upd.kept_count();
    for (uint32_t i = 0; i < k; i++) {
      const char *kp = u.upd.kept_path(i);
      console_log(ConsoleLevel::Uyari, kTag, "korunuyor (siz de\xC4\x9Fi\xC5\x9Ftirmi\xC5\x9Fsiniz): %s \xE2\x80\x94 yenisi %s.yeni", kp, kp);
    }
    break;
  }
  case UpdState::Installed: console_log(ConsoleLevel::Bilgi, kTag, "kuruldu: %s", r.tag); break;
  case UpdState::Failed:
    // Kullanicinin baslattigi is (elle denetim, indirme) basarisizsa Hata;
    // OTOMATIK denetim (cogunlukla "ag yok") yalniz Uyari — cevrimdisi bir
    // makinede her acilista kirmizi satir gurultuden ibaret olurdu.
    if (upd_reason_rollback_incomplete(u.upd.reason()))
      console_log(ConsoleLevel::Hata, kTag, "KURULUM YARIM KALDI (%s): %s \xE2\x80\x94 %s/.guncelleme/ elle incelenmeli", upd_state_name(from),
                  u.upd.reason(), u.install_dir);
    else
      console_log((u.manual || u.download_stage) ? ConsoleLevel::Hata : ConsoleLevel::Uyari, kTag,
                  "ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z (%s): %s \xE2\x80\x94 kurulum dizini de\xC4\x9Fi\xC5\x9Fmedi", upd_state_name(from), u.upd.reason());
    break;
  case UpdState::Idle:
    if (busy(from)) console_log(ConsoleLevel::Uyari, kTag, "iptal edildi");
    break;
  case UpdState::Disabled: console_log(ConsoleLevel::Uyari, kTag, "g\xC3\xBCncelleyici kapand\xC4\xB1: %s", u.upd.reason()); break;
  }
}

// --- Cizim yardimcilari --------------------------------------------------------
ImVec4 tone(Tone t, float a = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * a);
}

bool primary_button(const char *label) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(tone(Tone::Accent, 0.55f)));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(tone(Tone::Accent, 0.8f)));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(tone(Tone::AccentHi, 0.9f)));
  const bool r = ImGui::Button(label);
  ImGui::PopStyleColor(3);
  return r;
}

void text_dim(const char *s) {
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  ImGui::TextUnformatted(s);
  ImGui::PopStyleColor();
}

void text_wrapped(const char *s, const char *end = nullptr) {
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(s, end);
  ImGui::PopTextWrapPos();
}

// Soluk + sarilmis: uzun aciklama satiri kendiliginden boyutlanan pencereyi
// GENISLETMESIN (sabit genislikli not/liste alanlari genisligi belirler).
void text_dim_wrapped(const char *s) {
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  text_wrapped(s);
  ImGui::PopStyleColor();
}

void label_value(const char *label, const char *value) {
  text_dim(label);
  ImGui::SameLine();
  ImGui::TextUnformatted(value && *value ? value : "\xE2\x80\x94");
}

// html_url'yi tarayicida ac. Yalniz https://github.com/ ile baslayan adres:
// JSON'dan gelen bir dizgiyi dis programa vermeden once daraltmak (argv, kabuk
// yok — yine de "dosya:///..." gibi bir seyi xdg-open'a vermeyelim).
bool open_url(const char *url, char *err, uint32_t cap) {
  static const char kPrefix[] = "https://github.com/";
  if (!url || std::strncmp(url, kPrefix, sizeof kPrefix - 1) != 0) {
    std::snprintf(err, cap, "adres GitHub d\xC4\xB1\xC5\x9F\xC4\xB1, a\xC3\xA7\xC4\xB1lmad\xC4\xB1");
    return false;
  }
#if defined(_WIN32)
  const CodeEditorCandidate c[] = {{"explorer.exe", nullptr}};
#elif defined(__APPLE__)
  const CodeEditorCandidate c[] = {{"open", nullptr}};
#else
  const CodeEditorCandidate c[] = {{"xdg-open", nullptr}};
#endif
  char used[256];
  return editor_open_with_candidates(url, c, 1, used, sizeof used, err, cap);
}

// GERI ALMA EKSIK (cekirdegin en kotu durumu): kurulum dizini KARISIK olabilir,
// yedek eski dosyalarin TEK kopyasi olabilir. "Kurulum dizini degismedi" burada
// YALAN olurdu; kullanici neyin nerede oldugunu gormeli ve elle kurtarmali.
void rollback_warning(UpdateUi &u) {
  u.draw_rollback_warning++;
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Err));
  text_wrapped("Kurulum dizini KARI\xC5\x9EIK durumda olabilir: baz\xC4\xB1 dosyalar yeni, baz\xC4\xB1lar\xC4\xB1 eski. "
               "Eski dosyalar\xC4\xB1n tek kopyas\xC4\xB1 yedek dizininde olabilir \xE2\x80\x94 edit\xC3\xB6r onu S\xC4\xB0LMEZ ve "
               "i\xC5\x9F" "aret kalkana kadar yeni g\xC3\xBCncelleme denemez.");
  ImGui::PopStyleColor();
  char line[kUpdPathCap + 96];
  std::snprintf(line, sizeof line, "Elle kurtar\xC4\xB1n: %s/.guncelleme/ (yedek-*/ ve GERI-ALMA-EKSIK.txt). Ayr\xC4\xB1nt\xC4\xB1: docs/GUNCELLEME.md",
                u.install_dir[0] ? u.install_dir : "<kurulum dizini>");
  text_wrapped(line);
  if (ImGui::SmallButton("Yolu kopyala")) {
    std::snprintf(line, sizeof line, "%s/.guncelleme", u.install_dir);
    ImGui::SetClipboardText(line);
  }
}

void release_links(UpdateUi &u, const UpdRelease &r) {
  if (!r.html_url[0]) return;
  if (r.notes_truncated) {
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Warn));
    ImGui::TextUnformatted("Notlar k\xC4\xB1salt\xC4\xB1ld\xC4\xB1 \xE2\x80\x94 devam\xC4\xB1 GitHub'da:");
    ImGui::PopStyleColor();
  } else {
    text_dim("S\xC3\xBCr\xC3\xBCm sayfas\xC4\xB1:");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Ba\xC4\x9Flant\xC4\xB1y\xC4\xB1 kopyala")) {
    ImGui::SetClipboardText(r.html_url);
    console_log(ConsoleLevel::Bilgi, kTag, "ba\xC4\x9Flant\xC4\xB1 panoya kopyaland\xC4\xB1: %s", r.html_url);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Taray\xC4\xB1" "c\xC4\xB1" "da a\xC3\xA7")) {
    char e[256];
    if (!open_url(r.html_url, e, sizeof e)) console_log(ConsoleLevel::Uyari, kTag, "taray\xC4\xB1" "c\xC4\xB1 a\xC3\xA7\xC4\xB1lamad\xC4\xB1: %s", e);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.html_url);
  (void)u;
}

void release_block(UpdateUi &u, const UpdRelease &r) {
  const float fs = ImGui::GetFontSize();
  char d[32];
  upd_date_short(r.published_at, d, sizeof d);
  push_text_size(TextSize::Lg);
  ImGui::Text("Yeni s\xC3\xBCr\xC3\xBCm: %s", r.tag[0] ? r.tag : "?");
  pop_text_size();
  char line[256];
  std::snprintf(line, sizeof line, "yay\xC4\xB1n %s \xC2\xB7 kurulu %s", d[0] ? d : "?", u.current_version[0] ? u.current_version : "kaynak derlemesi");
  text_dim(line);
  if (r.asset_name[0]) {
    std::snprintf(line, sizeof line, "paket %s (%.1f MB)", r.asset_name, (double)r.asset_size / (1024.0 * 1024.0));
    text_dim(line);
  }
  if (skipped(u, r)) {
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Warn));
    ImGui::TextUnformatted("Bu s\xC3\xBCr\xC3\xBCm\xC3\xBC atlam\xC4\xB1\xC5\x9Ft\xC4\xB1n\xC4\xB1z (rozet g\xC3\xB6sterilmiyordu).");
    ImGui::PopStyleColor();
  }
  ImGui::Spacing();
  text_dim("S\xC3\xBCr\xC3\xBCm notlar\xC4\xB1");
  // Kaydirilabilir, sarilmis. Sabit yukseklik: uzun notlar pencereyi ekrandan
  // tasirmasin (AlwaysAutoResize).
  ImGui::PushStyleColor(ImGuiCol_ChildBg, tone(Tone::Input));
  if (ImGui::BeginChild("##surum_notlari", ImVec2(fs * 34.0f, fs * 11.0f), ImGuiChildFlags_Borders)) {
    if (r.notes[0]) text_wrapped(r.notes);
    else text_dim("(not yok)");
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  release_links(u, r);
}

void progress_row(UpdState s, float p) {
  const float fs = ImGui::GetFontSize();
  char lbl[64];
  if (p >= 0.0f) std::snprintf(lbl, sizeof lbl, "%s  %%%d", upd_state_name(s), (int)(p * 100.0f + 0.5f));
  else std::snprintf(lbl, sizeof lbl, "%s\xE2\x80\xA6", upd_state_name(s));
  // Olcusu bilinmeyen is: ImGui'nin belirsiz (akan) cubugu (negatif oran).
  const float frac = p >= 0.0f ? (p > 1.0f ? 1.0f : p) : -1.0f * (float)ImGui::GetTime();
  ImGui::ProgressBar(frac, ImVec2(fs * 34.0f, 0.0f), lbl);
}

void close_window(UpdateUi &u) {
  u.window_open = false;
  u.window_need_open = false;
  ImGui::CloseCurrentPopup();
}

void skip_release(UpdateUi &u, const UpdRelease &r) {
  std::snprintf(u.settings.skip_tag, sizeof u.settings.skip_tag, "%s", r.tag);
  save_settings(u);
  u.manual = false; // rozet hemen kaybolsun
  console_log(ConsoleLevel::Bilgi, kTag, "%s atland\xC4\xB1: rozet g\xC3\xB6sterilmeyecek (elle denetimde yine g\xC3\xB6r\xC3\xBCn\xC3\xBCr)", r.tag);
}

UpdUiAction draw_update_window(UpdateUi &u) {
  static const char kLabel[] = "G\xC3\xBCncelleme###tulpar_guncelleme";
  if (u.window_need_open) {
    ImGui::OpenPopup(kLabel);
    u.window_need_open = false;
  }
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowSize(ImVec2(fs * 36.0f, 0.0f), ImGuiCond_Appearing);
  bool open = true;
  if (!ImGui::BeginPopupModal(kLabel, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    u.window_open = false; // X ile kapandi (ya da acilamadi)
    u.window_need_open = false;
    return UpdUiAction::None;
  }
  u.draw_update_win++;
  UpdUiAction act = UpdUiAction::None;
  const UpdState s = update_ui_state(u);
  const UpdRelease &r = update_ui_release(u);
  const char *reason = update_ui_reason(u);
  char line[512];

  switch (s) {
  case UpdState::Disabled: {
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Warn));
    ImGui::TextUnformatted("G\xC3\xBCncelleyici kapal\xC4\xB1");
    ImGui::PopStyleColor();
    if (reason && *reason) text_wrapped(reason);
    if (upd_reason_rollback_incomplete(reason)) rollback_warning(u);
    if (!u.current_version[0]) {
      ImGui::Spacing();
      text_wrapped("Bu edit\xC3\xB6r kaynaktan derlenmi\xC5\x9F; s\xC3\xBCr\xC3\xBCm paketi kendini g\xC3\xBCncelleyemez (yapi/ dizinini bozard\xC4\xB1). Kaynak a\xC4\x9F" "ac\xC4\xB1nda:");
      ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::AccentHi));
      ImGui::TextUnformatted(kRebuildHint);
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::SmallButton("Kopyala")) ImGui::SetClipboardText(kRebuildHint);
    }
    ImGui::Spacing();
    if (ImGui::Button("Kapat")) close_window(u);
    break;
  }
  case UpdState::Idle:
    ImGui::TextUnformatted("Hen\xC3\xBCz denetlenmedi.");
    if (u.headless) text_dim("Penceresiz kip: a\xC4\x9F denetimi yap\xC4\xB1lmaz.");
    ImGui::Spacing();
    if (primary_button("\xC5\x9Eimdi denetle")) request_check(u, true);
    ImGui::SameLine();
    if (ImGui::Button("Kapat")) close_window(u);
    break;
  case UpdState::Checking:
    ImGui::TextUnformatted("GitHub'daki en son s\xC3\xBCr\xC3\xBCm soruluyor\xE2\x80\xA6");
    progress_row(s, -1.0f);
    if (ImGui::Button("\xC4\xB0ptal")) u.upd.cancel();
    ImGui::SameLine();
    if (ImGui::Button("Arka planda s\xC3\xBCrs\xC3\xBCn")) close_window(u);
    break;
  case UpdState::UpToDate:
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Ok));
    std::snprintf(line, sizeof line, "\xE2\x9C\x93 En son s\xC3\xBCr\xC3\xBCm kurulu (%s)", u.current_version);
    ImGui::TextUnformatted(line);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (ImGui::Button("Kapat")) close_window(u);
    break;
  case UpdState::Available:
    release_block(u, r);
    ImGui::Spacing();
    if (primary_button("\xC4\xB0ndir ve kur")) request_download(u);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("\xC4\xB0ndirir, SHA-256 ile do\xC4\x9Frular ve a\xC3\xA7" "ar; kurmadan \xC3\xB6nce ne de\xC4\x9Fi\xC5\x9F" "ece\xC4\x9Fini g\xC3\xB6sterir");
    ImGui::SameLine();
    if (ImGui::Button("Sonra")) close_window(u);
    ImGui::SameLine();
    if (ImGui::Button("Bu s\xC3\xBCr\xC3\xBCm\xC3\xBC atla")) {
      skip_release(u, r);
      close_window(u);
    }
    break;
  case UpdState::Downloading:
  case UpdState::Verifying:
  case UpdState::Extracting:
    release_block(u, r);
    ImGui::Spacing();
    progress_row(s, u.fake ? u.fake_progress : u.upd.progress());
    if (ImGui::Button("\xC4\xB0ptal")) u.upd.cancel();
    ImGui::SameLine();
    if (ImGui::Button("Arka planda s\xC3\xBCrs\xC3\xBCn")) close_window(u);
    break;
  case UpdState::Staged: {
    const UpdPlanSummary p = u.fake ? u.fake_plan : u.upd.plan_summary();
    const uint32_t kn = u.fake ? u.fake_kept_count : u.upd.kept_count();
    push_text_size(TextSize::Lg);
    ImGui::Text("%s kurulmaya haz\xC4\xB1r", r.tag[0] ? r.tag : "?");
    pop_text_size();
    text_dim("\xC4\xB0ndirildi, ar\xC5\x9Fiv ve her dosya SHA-256 ile do\xC4\x9Fruland\xC4\xB1.");
    ImGui::Spacing();
    std::snprintf(line, sizeof line, "%u dosya g\xC3\xBCncellenecek (%u yeni, %u de\xC4\x9Fi\xC5\x9F" "en, %u kald\xC4\xB1r\xC4\xB1lan); %u dosya zaten ayn\xC4\xB1.", p.add + p.replace + p.remove,
                  p.add, p.replace, p.remove, p.same);
    text_wrapped(line);
    if (kn > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Warn));
      std::snprintf(line, sizeof line, "%u kullan\xC4\xB1" "c\xC4\xB1 dosyas\xC4\xB1 korunacak:", kn);
      ImGui::TextUnformatted(line);
      ImGui::PopStyleColor();
      text_wrapped("Bunlar\xC4\xB1 siz de\xC4\x9Fi\xC5\x9Ftirmi\xC5\x9Fsiniz. Sizin s\xC3\xBCr\xC3\xBCm\xC3\xBCn\xC3\xBCz yerinde kal\xC4\xB1r; yeni s\xC3\xBCr\xC3\xBCm\xC3\xBC yan\xC4\xB1na <ad>.yeni olarak yaz\xC4\xB1l\xC4\xB1r (fark\xC4\xB1 elle birle\xC5\x9Ftirin).");
      const float rows = kn < 6 ? (float)kn : 6.0f;
      if (ImGui::BeginChild("##korunan", ImVec2(fs * 34.0f, ImGui::GetTextLineHeightWithSpacing() * rows + ImGui::GetStyle().WindowPadding.y * 2.0f), ImGuiChildFlags_Borders)) {
        for (uint32_t i = 0; i < kn; i++) {
          const char *kp = u.fake ? u.fake_kept[i] : u.upd.kept_path(i);
          std::snprintf(line, sizeof line, "\xE2\x80\xA2 %s  \xE2\x86\x92  %s.yeni", kp, kp);
          ImGui::TextUnformatted(line);
        }
      }
      ImGui::EndChild();
    }
    text_dim_wrapped("De\xC4\x9Fi\xC5\x9F" "en her dosya \xC3\xB6nce .guncelleme/ alt\xC4\xB1na yedeklenir; bir ad\xC4\xB1m ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z olursa hepsi geri al\xC4\xB1n\xC4\xB1r.");
    ImGui::Spacing();
    if (primary_button("Kur ve yeniden ba\xC5\x9Flat")) {
      act = UpdUiAction::InstallAndRestart;
      close_window(u); // kaydet/onay kutusu acilabilsin (ayni seviyede tek kipli pencere)
    }
    ImGui::SameLine();
    if (ImGui::Button("Sonra")) close_window(u);
    break;
  }
  case UpdState::Installed:
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Ok));
    std::snprintf(line, sizeof line, "\xE2\x9C\x93 %s kuruldu", r.tag);
    ImGui::TextUnformatted(line);
    ImGui::PopStyleColor();
    if (u.last_error[0]) {
      text_wrapped(u.last_error);
      text_wrapped("Edit\xC3\xB6r\xC3\xBC elle yeniden ba\xC5\x9Flat\xC4\xB1n.");
      if (ImGui::Button("Kapat")) close_window(u);
    } else {
      text_dim("Edit\xC3\xB6r yeniden ba\xC5\x9Flat\xC4\xB1l\xC4\xB1yor\xE2\x80\xA6");
    }
    break;
  case UpdState::Failed: {
    const bool yarim = upd_reason_rollback_incomplete(reason);
    if (r.tag[0] && !yarim) release_block(u, r);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::Err));
    ImGui::TextUnformatted(yarim ? "Kurulum YARIM KALDI" : "Ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z");
    ImGui::PopStyleColor();
    text_wrapped(reason && *reason ? reason : "(sebep bildirilmedi)");
    if (yarim) {
      rollback_warning(u);
      ImGui::Spacing();
      if (ImGui::Button("Kapat")) close_window(u); // Tekrar dene YOK: cekirdek isaret varken denetlemez
    } else {
      text_dim("Kurulum dizini de\xC4\x9Fi\xC5\x9Fmedi.");
      ImGui::Spacing();
      if (primary_button("Tekrar dene")) {
        u.retry_download = u.download_stage; // indirmedeyse: denetim bitince indirmeye devam
        request_check(u, true);
      }
      ImGui::SameLine();
      if (ImGui::Button("Kapat")) close_window(u);
    }
    break;
  }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && u.window_open) close_window(u); // Esc = Sonra
  ImGui::EndPopup();
  return act;
}

void draw_about(UpdateUi &u) {
  static const char kLabel[] = "Hakk\xC4\xB1nda###tulpar_hakkinda";
  if (u.about_need_open) {
    ImGui::OpenPopup(kLabel);
    u.about_need_open = false;
  }
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowSize(ImVec2(fs * 32.0f, 0.0f), ImGuiCond_Appearing);
  bool open = true;
  if (!ImGui::BeginPopupModal(kLabel, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    u.about_open = false;
    u.about_need_open = false;
    return;
  }
  u.draw_about_win++;
  push_text_size(TextSize::Lg);
  ImGui::TextUnformatted("Tulpar Edit\xC3\xB6r");
  pop_text_size();
  text_dim("Mobil \xC3\xB6ncelikli (ARM + Vulkan) C++17 oyun motorunun edit\xC3\xB6r\xC3\xBC");
  ImGui::Separator();
  char line[kUpdPathCap + 64];
  label_value("S\xC3\xBCr\xC3\xBCm:", u.current_version[0] ? u.current_version : "kaynak derlemesi");
  label_value("Platform:", u.platform);
  label_value("Kurulum dizini:", u.install_dir);
  label_value("Ayar dosyas\xC4\xB1:", u.settings_path);
  const UpdState s = update_ui_state(u);
  const char *reason = update_ui_reason(u);
  if (reason && *reason && (s == UpdState::Disabled || s == UpdState::Failed))
    std::snprintf(line, sizeof line, "%s \xE2\x80\x94 %s", upd_state_name(s), reason);
  else std::snprintf(line, sizeof line, "%s", upd_state_name(s));
  text_dim("G\xC3\xBCncelleyici:");
  ImGui::SameLine();
  text_wrapped(line);
  char when[48];
  unix_text(u.settings.last_check, when, sizeof when);
  std::snprintf(line, sizeof line, "%s (son denetim %s; a\xC3\xA7\xC4\xB1l\xC4\xB1\xC5\x9Fta: %s)", u.settings.auto_check ? "a\xC3\xA7\xC4\xB1k" : "kapal\xC4\xB1", when, upd_auto_why_text(u.auto_why));
  label_value("Otomatik denetim:", line);
  if (u.settings.skip_tag[0]) label_value("Atlanan s\xC3\xBCr\xC3\xBCm:", u.settings.skip_tag);
  ImGui::Spacing();
  if (primary_button("G\xC3\xBCncellemeleri denetle\xE2\x80\xA6")) {
    u.about_open = false;
    ImGui::CloseCurrentPopup();
    update_ui_check(u, true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Kapat") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    u.about_open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace

// ============================================================================
// Ayar dosyasi
// ============================================================================

UpdSettingsStats upd_settings_parse(const char *text, size_t len, UpdSettings *out) {
  UpdSettingsStats st;
  if (!out || !text) return st;
  size_t i = 0;
  while (i < len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    size_t a = i, b = j;
    i = j + 1;
    while (a < b && is_space(text[a])) a++;
    while (b > a && is_space(text[b - 1])) b--;
    if (a == b || text[a] == '#') continue;
    st.lines++;
    size_t k = a;
    while (k < b && !is_space(text[k])) k++;
    size_t v = k;
    while (v < b && is_space(text[v])) v++;
    const char *key = text + a, *val = text + v;
    const size_t kn = k - a, vn = b - v;
    bool ok = false;
    if (span_eq(key, kn, "otomatik")) {
      if (span_eq(val, vn, "evet")) { out->auto_check = true; ok = true; }
      else if (span_eq(val, vn, "hayir") || span_eq(val, vn, "hay\xC4\xB1r")) { out->auto_check = false; ok = true; }
    } else if (span_eq(key, kn, "son_denetim")) {
      // Yalniz ondalik rakam, en cok 18 hane (int64 tasmasin); isaret yok.
      if (vn > 0 && vn <= 18) {
        int64_t t = 0;
        ok = true;
        for (size_t q = 0; q < vn; q++) {
          if (!is_digit(val[q])) { ok = false; break; }
          t = t * 10 + (val[q] - '0');
        }
        if (ok) out->last_check = t;
      }
    } else if (span_eq(key, kn, "atla")) {
      // Tek jeton, surum kalibinda (upd_version_parse): "atla xyz" bozuk sayilir.
      char tag[kUpdTagLen];
      if (vn > 0 && vn < sizeof tag) {
        bool one = true;
        for (size_t q = 0; q < vn; q++)
          if (is_space(val[q])) one = false;
        std::memcpy(tag, val, vn);
        tag[vn] = 0;
        UpdVersion ver;
        if (one && upd_version_parse(tag, &ver)) {
          std::memcpy(out->skip_tag, tag, vn + 1);
          ok = true;
        }
      }
    }
    if (ok) st.applied++;
    else st.bad++;
  }
  return st;
}

size_t upd_settings_write(const UpdSettings &s, char *buf, size_t cap) {
  int n = std::snprintf(buf, cap,
                        "# Tulpar Editor guncelleme ayarlari (editor yazar; elle de duzenlenebilir)\n"
                        "otomatik %s\n"
                        "son_denetim %lld\n",
                        s.auto_check ? "evet" : "hayir", (long long)(s.last_check > 0 ? s.last_check : 0));
  if (n < 0) return 0;
  size_t used = (size_t)n;
  if (s.skip_tag[0]) {
    const size_t off = used < cap ? used : cap;
    const int m = std::snprintf(buf ? buf + off : nullptr, cap - off, "atla %s\n", s.skip_tag);
    if (m > 0) used += (size_t)m;
  }
  return used;
}

bool upd_settings_default_path(char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  char dir[kUpdPathCap] = {0};
  const char *home = std::getenv("HOME");
  if (!home || !*home) home = std::getenv("USERPROFILE");
  if (home && *home) std::snprintf(dir, sizeof dir, "%s", home);
  else if (!platform::exe_dir(dir, sizeof dir)) return false;
  const int n = std::snprintf(out, cap, "%s/.tulpar_guncelleme", dir);
  if (n <= 0 || (size_t)n >= cap) { out[0] = 0; return false; }
  return true;
}

bool upd_settings_load(const char *path, UpdSettings *out, UpdSettingsStats *st) {
  if (st) *st = UpdSettingsStats{};
  if (!path || !*path || !out) return false;
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  char buf[4096];
  const size_t n = std::fread(buf, 1, sizeof buf, f);
  const bool more = n == sizeof buf && std::fgetc(f) != EOF;
  std::fclose(f);
  UpdSettingsStats r = upd_settings_parse(buf, n, out);
  if (more) r.bad++; // 4 KB'tan uzun: gerisi OKUNMADI — sessiz degil, sayilir
  if (st) *st = r;
  return true;
}

bool upd_settings_save(const char *path, const UpdSettings &s) {
  if (!path || !*path) return false;
  char buf[512];
  const size_t n = upd_settings_write(s, buf, sizeof buf);
  if (n == 0 || n >= sizeof buf) return false;
  char tmp[kUpdPathCap + 16];
  const int tn = std::snprintf(tmp, sizeof tmp, "%s.gecici", path);
  if (tn <= 0 || (size_t)tn >= sizeof tmp) return false;
  FILE *f = std::fopen(tmp, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(buf, 1, n, f) == n;
  const bool closed = std::fclose(f) == 0;
  if (!ok || !closed) { std::remove(tmp); return false; }
  if (platform::fs_replace_file(tmp, path) != 0) { std::remove(tmp); return false; }
  return true;
}

// ============================================================================
// Otomatik karar + rozet (saf)
// ============================================================================

UpdAutoWhy upd_auto_decide(const UpdSettings &s, bool updater_disabled, bool headless, const char *env, int64_t now) {
  if (updater_disabled) return UpdAutoWhy::Disabled;
  if (headless) return UpdAutoWhy::Headless;
  if (env && !std::strcmp(env, "0")) return UpdAutoWhy::EnvOff;
  if (!s.auto_check) return UpdAutoWhy::UserOff;
  // Saat geri gittiyse (son denetim gelecekte) "yeni" SAYILMAZ: yoksa saati
  // yanlis bir makinede otomatik denetim hic kosmazdi.
  if (s.last_check > 0 && now >= s.last_check && now - s.last_check < kUpdAutoIntervalSec) return UpdAutoWhy::Recent;
  return UpdAutoWhy::Go;
}

const char *upd_auto_why_text(UpdAutoWhy w) {
  switch (w) {
  case UpdAutoWhy::Go: return "denetlendi";
  case UpdAutoWhy::Disabled: return "g\xC3\xBCncelleyici kapal\xC4\xB1";
  case UpdAutoWhy::Headless: return "penceresiz kip (a\xC4\x9F yok)";
  case UpdAutoWhy::EnvOff: return "TULPAR_GUNCELLEME=0";
  case UpdAutoWhy::UserOff: return "ayarda kapal\xC4\xB1";
  case UpdAutoWhy::Recent: return "son denetim 24 saatten yeni";
  }
  return "?";
}

int64_t upd_auto_wait_sec(const UpdSettings &s, int64_t now) {
  if (s.last_check > 0 && now >= s.last_check && now - s.last_check < kUpdAutoIntervalSec) return kUpdAutoIntervalSec - (now - s.last_check);
  return 0;
}

bool upd_badge_visible(UpdState s, bool newer, const char *tag, const char *skip_tag, bool manual) {
  switch (s) {
  case UpdState::Available:
    if (!newer) return false;
    if (!manual && skip_tag && *skip_tag && tag && !std::strcmp(tag, skip_tag)) return false;
    return true;
  case UpdState::Downloading:
  case UpdState::Verifying:
  case UpdState::Extracting:
  case UpdState::Staged:
  case UpdState::Installed: return true;
  case UpdState::Failed: return tag && *tag && newer; // indirme/kurulum hatasi: tekrar denemenin yolu
  default: return false;
  }
}

uint32_t upd_badge_text(UpdState s, const char *tag, float progress, char *out, uint32_t cap) {
  if (!out || cap == 0) return 0;
  const char *t = (tag && *tag) ? tag : "?";
  int n = 0;
  switch (s) {
  case UpdState::Downloading:
  case UpdState::Verifying:
  case UpdState::Extracting:
    if (progress >= 0.0f) n = std::snprintf(out, cap, "\xE2\xAC\x86 %s  %%%d", t, (int)(progress * 100.0f + 0.5f));
    else n = std::snprintf(out, cap, "\xE2\xAC\x86 %s  \xE2\x80\xA6", t);
    break;
  case UpdState::Staged: n = std::snprintf(out, cap, "\xE2\xAC\x86 %s haz\xC4\xB1r", t); break;
  case UpdState::Installed: n = std::snprintf(out, cap, "\xE2\xAC\x86 %s kuruldu", t); break;
  case UpdState::Failed: n = std::snprintf(out, cap, "\xE2\xAC\x86 %s hata", t); break;
  default: n = std::snprintf(out, cap, "\xE2\xAC\x86 %s", t); break;
  }
  if (n < 0) { out[0] = 0; return 0; }
  return (uint32_t)n < cap ? (uint32_t)n : cap - 1;
}

bool upd_reason_rollback_incomplete(const char *reason) {
  return reason && (std::strstr(reason, "GERI ALMA EKSIK") || std::strstr(reason, "geri almasi eksik"));
}

void upd_date_short(const char *iso, char *out, uint32_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!iso) return;
  // YYYY-MM-DD... ise ilk 10 karakter; degilse oldugu gibi.
  const bool ymd = std::strlen(iso) >= 10 && is_digit(iso[0]) && is_digit(iso[3]) && iso[4] == '-' && iso[7] == '-' && is_digit(iso[9]);
  std::snprintf(out, cap, "%.*s", ymd ? 10 : (int)(cap - 1), iso);
}

// ============================================================================
// UpdateUi
// ============================================================================

bool update_ui_init(UpdateUi &u, Arena &a, const UpdateUiConfig &c, uint64_t now_ns) {
  u.ready = false;
  u.headless = c.headless;
  u.write_settings = c.write_settings;
  u.last_now_ns = now_ns;
  u.next_auto_ns = 0;
  Resolved r;
  resolve(c, &r);
  std::snprintf(u.install_dir, sizeof u.install_dir, "%s", r.install_dir);
  std::snprintf(u.current_version, sizeof u.current_version, "%s", r.version);
  std::snprintf(u.platform, sizeof u.platform, "%s", build_platform());
  if (c.settings_path && *c.settings_path) std::snprintf(u.settings_path, sizeof u.settings_path, "%s", c.settings_path);
  else if (!upd_settings_default_path(u.settings_path, sizeof u.settings_path)) u.settings_path[0] = 0;
  u.settings = UpdSettings{};
  u.settings_loaded = upd_settings_load(u.settings_path, &u.settings, &u.settings_stats);
  if (u.settings_stats.bad)
    console_log(ConsoleLevel::Uyari, kTag, "ayar dosyas\xC4\xB1nda %u bozuk sat\xC4\xB1r yok say\xC4\xB1ld\xC4\xB1 (%s)", u.settings_stats.bad, u.settings_path);

  char err[kUpdErrLen] = {0};
  const UpdaterConfig uc = to_updater_config(r);
  if (!u.upd.init(a, uc, err, sizeof err)) {
    console_log(ConsoleLevel::Hata, kTag, "g\xC3\xBCncelleyici kurulamad\xC4\xB1: %s", err);
    std::snprintf(u.last_error, sizeof u.last_error, "%s", err);
    return false;
  }
  u.ready = true;
  u.last_state = u.upd.state();
  const bool disabled = u.last_state == UpdState::Disabled;
  // Cekirdegin COZDUGU dizin (sondaki ayiricilar atilmis): yeniden baslatma
  // ve Hakkinda onu gosterir, ikinci bir yorum olmasin.
  if (!disabled && u.upd.install_dir() && u.upd.install_dir()[0])
    std::snprintf(u.install_dir, sizeof u.install_dir, "%s", u.upd.install_dir());
  // Yarim kalmis indirme / eski yedekler: yalniz pencereli kipte (penceresiz
  // kosu yedekleri silmez; kapilar kurulum dizinini temizlemesin).
  if (!disabled && !u.headless && u.install_dir[0]) Updater::cleanup(u.install_dir);
  const int64_t now = now_unix();
  u.auto_why = upd_auto_decide(u.settings, disabled, u.headless, std::getenv("TULPAR_GUNCELLEME"), now);
  if (disabled) console_log(ConsoleLevel::Bilgi, kTag, "g\xC3\xBCncelleyici kapal\xC4\xB1: %s", u.upd.reason());
  else console_log(ConsoleLevel::Bilgi, kTag, "kurulu %s (%s), otomatik denetim: %s", u.current_version, u.platform, upd_auto_why_text(u.auto_why));
  if (u.auto_why == UpdAutoWhy::Go) request_check(u, false);
  else if (u.auto_why == UpdAutoWhy::Recent) u.next_auto_ns = now_ns + (uint64_t)upd_auto_wait_sec(u.settings, now) * kNsPerSec;
  return true;
}

void update_ui_poll(UpdateUi &u, uint64_t now_ns) {
  u.last_now_ns = now_ns;
  if (!u.ready) return;
  // Uzun oturum: 24 saatte bir. Tek tamsayi karsilastirmasi; saat cagiranin
  // zaten okudugu monoton saat (kare basina ek syscall yok).
  if (u.next_auto_ns != 0 && now_ns >= u.next_auto_ns) {
    u.next_auto_ns = now_ns + (uint64_t)kUpdAutoIntervalSec * kNsPerSec;
    const int64_t now = now_unix();
    const UpdAutoWhy w = upd_auto_decide(u.settings, u.upd.state() == UpdState::Disabled, u.headless, std::getenv("TULPAR_GUNCELLEME"), now);
    if (w == UpdAutoWhy::Go) request_check(u, false);
    else if (w == UpdAutoWhy::Recent) u.next_auto_ns = now_ns + (uint64_t)upd_auto_wait_sec(u.settings, now) * kNsPerSec;
    else u.next_auto_ns = 0;
  }
  u.upd.poll();
  const UpdState s = u.upd.state();
  if (s != u.last_state) {
    const UpdState from = u.last_state;
    u.last_state = s;
    log_transition(u, from, s);
  }
}

void update_ui_check(UpdateUi &u, bool manual) {
  if (manual) update_ui_open_window(u);
  // Penceresiz kipte istek HER durumda (Disabled dahil) request_check'e gider
  // ve orada REDDEDILIP sayilir: kapi reddi kaynak derlemesinde de olcebilsin
  // (Tuzaklar 8cm: Disabled iken "istek 0" kendiliginden dogrudur).
  if (!u.headless && u.ready && u.upd.state() == UpdState::Disabled) {
    console_log(ConsoleLevel::Uyari, kTag, "g\xC3\xBCncelleyici kapal\xC4\xB1: %s", u.upd.reason());
    return;
  }
  request_check(u, manual);
}

void update_ui_toggle_auto(UpdateUi &u) {
  u.settings.auto_check = !u.settings.auto_check;
  save_settings(u);
  console_log(ConsoleLevel::Bilgi, kTag, "otomatik denetim %s", u.settings.auto_check ? "a\xC3\xA7\xC4\xB1ld\xC4\xB1" : "kapat\xC4\xB1ld\xC4\xB1");
  if (!u.settings.auto_check) {
    u.next_auto_ns = 0;
    return;
  }
  // Acildi: vadesi gelmisse bir sonraki karede, gelmemisse vadesinde.
  const int64_t wait = upd_auto_wait_sec(u.settings, now_unix());
  if (!u.headless && u.ready && u.upd.state() != UpdState::Disabled)
    u.next_auto_ns = u.last_now_ns + (uint64_t)(wait > 0 ? wait : 0) * kNsPerSec + 1;
}

void update_ui_open_window(UpdateUi &u) {
  u.window_open = true;
  u.window_need_open = true;
}

void update_ui_open_about(UpdateUi &u) {
  u.about_open = true;
  u.about_need_open = true;
}

UpdState update_ui_state(const UpdateUi &u) {
  if (u.fake) return u.fake_state;
  return u.ready ? u.upd.state() : UpdState::Disabled;
}

const UpdRelease &update_ui_release(const UpdateUi &u) {
  static const UpdRelease kEmpty{};
  if (u.fake) return u.fake_release;
  return u.ready ? u.upd.release() : kEmpty;
}

const char *update_ui_reason(const UpdateUi &u) {
  if (u.fake) return u.fake_reason ? u.fake_reason : "";
  if (!u.ready) return u.last_error;
  return u.upd.reason();
}

bool update_ui_disabled(const UpdateUi &u) { return update_ui_state(u) == UpdState::Disabled; }

const char *update_ui_badge(UpdateUi &u) {
  const UpdState s = update_ui_state(u);
  // Hizli yol: rozetsiz durumlar (en sik: Idle/UpToDate/Disabled) hicbir
  // dizgi isi yapmaz.
  if (s == UpdState::Idle || s == UpdState::UpToDate || s == UpdState::Disabled || s == UpdState::Checking) return nullptr;
  const UpdRelease &r = update_ui_release(u);
  const bool newer = u.fake ? u.fake_newer : u.upd.newer_than_current();
  if (!upd_badge_visible(s, newer, r.tag, u.settings.skip_tag, u.manual)) return nullptr;
  const float p = u.fake ? u.fake_progress : u.upd.progress();
  upd_badge_text(s, r.tag, p, u.badge_buf, sizeof u.badge_buf);
  return u.badge_buf;
}

UpdUiAction update_ui_draw(UpdateUi &u) {
  u.draw_calls++;
  if (!u.window_open && !u.about_open) return UpdUiAction::None; // maliyet sifir: ImGui'ye dokunulmaz
  if (!ImGui::GetCurrentContext()) return UpdUiAction::None;
  UpdUiAction act = UpdUiAction::None;
  if (u.window_open) act = draw_update_window(u);
  if (u.about_open) draw_about(u);
  return act;
}

bool update_ui_install(UpdateUi &u, char *err, size_t err_cap) {
  char e[kUpdErrLen] = {0};
  u.last_error[0] = 0;
  if (!u.ready || u.upd.state() != UpdState::Staged) {
    std::snprintf(e, sizeof e, "kurulacak paket yok (durum: %s)", upd_state_name(update_ui_state(u)));
  } else if (u.upd.install(e, sizeof e)) {
    const UpdState from = u.last_state;
    u.last_state = u.upd.state();
    if (u.last_state != from) u.transitions++;
    console_log(ConsoleLevel::Bilgi, kTag, "kuruldu: %s -> %s; edit\xC3\xB6r yeniden ba\xC5\x9Flat\xC4\xB1l\xC4\xB1yor", u.current_version, u.upd.release().tag);
    return true;
  }
  if (!e[0]) std::snprintf(e, sizeof e, "bilinmeyen hata");
  std::snprintf(u.last_error, sizeof u.last_error, "%s", e);
  if (err && err_cap) std::snprintf(err, err_cap, "%s", e);
  if (upd_reason_rollback_incomplete(e))
    console_log(ConsoleLevel::Hata, kTag, "KURULUM YARIM KALDI: %s \xE2\x80\x94 %s/.guncelleme/ elle incelenmeli", e, u.install_dir);
  else
    console_log(ConsoleLevel::Hata, kTag, "kurulum ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z: %s \xE2\x80\x94 kurulum dizini eski haline d\xC3\xB6nd\xC3\xBC", e);
  u.last_state = u.upd.state();
  update_ui_open_window(u); // sebebi ve "Tekrar dene"yi goster
  return false;
}

bool update_ui_shutdown(UpdateUi &u) {
  if (!u.ready || !busy(u.upd.state())) return false;
  const UpdState s = u.upd.state();
  u.upd.cancel();
  u.last_state = u.upd.state();
  console_log(ConsoleLevel::Uyari, kTag, "editor kapan\xC4\xB1yor: %s iptal edildi", upd_state_name(s));
  return true;
}

uint32_t update_restart_argv(const char *exe, int argc, char **argv, const char *scene, const char **out, uint32_t cap) {
  if (!exe || !out || cap < 2) return 0;
  uint32_t n = 0;
  auto put = [&](const char *s) {
    if (n + 1 < cap) out[n] = s;
    n++;
  };
  put(exe);
  bool scene_done = false;
  const bool have_scene = scene && *scene;
  for (int i = 1; i < argc && argv; i++) {
    if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) {
      put("--scene");
      put(have_scene ? scene : argv[i + 1]);
      scene_done = true;
      i++;
      continue;
    }
    put(argv[i]);
  }
  if (!scene_done && have_scene) {
    put("--scene");
    put(scene);
  }
  if (n + 1 > cap) { out[0] = nullptr; return 0; } // sigmadi: yarim argv baska bir komuttur
  out[n] = nullptr;
  return n;
}

bool update_arg_to_utf8(const char *in, char *out, size_t cap) {
  if (!in || !out || cap == 0) return false;
#if defined(_WIN32)
  // MinGW'nin dar main'i argv'yi ANSI kod sayfasinda (ACP) verir, editorun
  // yollari (sahne) da A-API'den gelir; process_start ise UTF-8 bekler
  // (CreateProcessW). ASCII'de ikisi aynidir — Turkce harfli bir sahne yolunda
  // yeni editor BOZUK yolu acmaya calisip cikardi.
  wchar_t w[kUpdPathCap];
  if (MultiByteToWideChar(CP_ACP, 0, in, -1, w, (int)kUpdPathCap) <= 0) return false;
  return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, nullptr, nullptr) > 0;
#else
  const int n = std::snprintf(out, cap, "%s", in); // POSIX: argv bayttir, cevrilmez
  return n >= 0 && (size_t)n < cap;
#endif
}

uint32_t update_acp() {
#if defined(_WIN32)
  return (uint32_t)GetACP();
#else
  return 65001; // UTF-8
#endif
}

bool update_ui_restart(const UpdateUi &u, int argc, char **argv, const char *scene, char *err, size_t err_cap) {
  char exe[kUpdPathCap + 32];
  const int n = std::snprintf(exe, sizeof exe, "%s/%s", u.install_dir, kEditorExe);
  if (n <= 0 || (size_t)n >= sizeof exe || !u.install_dir[0]) {
    std::snprintf(err, err_cap, "kurulum dizini bilinmiyor");
    return false;
  }
  if (!platform::fs_exists(exe)) {
    std::snprintf(err, err_cap, "yeni ikili bulunamad\xC4\xB1: %s", exe);
    return false;
  }
  const char *av[64];
  if (update_restart_argv(exe, argc, argv, scene, av, 64) == 0) {
    std::snprintf(err, err_cap, "arg\xC3\xBCman listesi \xC3\xA7ok uzun");
    return false;
  }
  // Tuzaklar 8cl: exe (av[0]) zaten UTF-8 (cekirdegin W-API ile cozdugu kurulum
  // dizini); argumanlar ve sahne yolu A-API'den gelir, cevrilir.
  static char conv[64][kUpdPathCap];
  for (uint32_t i = 1; i < 64 && av[i]; i++) {
    if (!update_arg_to_utf8(av[i], conv[i], sizeof conv[i])) {
      std::snprintf(err, err_cap, "arg\xC3\xBCman %u UTF-8'e \xC3\xA7" "evrilemedi", (unsigned)i);
      return false;
    }
    av[i] = conv[i];
  }
  platform::ProcessSpec s;
  s.argv = av;
  return platform::process_start_detached(s, err, err_cap);
}

void update_ui_test_inject(UpdateUi &u, UpdState s, const UpdRelease *release, float progress, const UpdPlanSummary *plan,
                           const char *const *kept, uint32_t kept_count, const char *reason) {
  u.fake = true;
  u.fake_state = s;
  u.fake_release = release ? *release : UpdRelease{};
  u.fake_progress = progress;
  u.fake_newer = true;
  u.fake_plan = plan ? *plan : UpdPlanSummary{};
  u.fake_reason = reason ? reason : "";
  u.fake_kept_count = 0;
  for (uint32_t i = 0; kept && i < kept_count && i < kUpdTestKeptMax; i++) u.fake_kept[u.fake_kept_count++] = kept[i];
}

void update_ui_test_clear(UpdateUi &u) {
  u.fake = false;
  u.fake_kept_count = 0;
}

// ============================================================================
// Komut satiri
// ============================================================================

int update_cli_version() {
  const char *v = build_version();
  std::printf("%s %s\n", (v && *v) ? v : "kaynak derlemesi", build_platform());
  return 0;
}

namespace {
// poll()'u kisa uykuyla surer; `until` durumlarindan birine ya da zaman
// asimina kadar. Ara durumlari (indiriliyor, dogrulaniyor...) bir kez basar.
UpdState cli_wait(Updater &up, const UpdState *until, uint32_t n_until, uint64_t timeout_s) {
  const uint64_t t0 = platform::now_ns();
  UpdState last = up.state();
  int last_pct = 0; // %0 basilmaz: durum satiri zaten "basladi" diyor
  for (;;) {
    up.poll();
    const UpdState s = up.state();
    for (uint32_t i = 0; i < n_until; i++)
      if (s == until[i]) return s;
    if (s != last) {
      std::printf("  %s\xE2\x80\xA6\n", upd_state_name(s));
      last = s;
      last_pct = 0;
    }
    const float p = up.progress();
    if (p >= 0.0f && (int)(p * 100.0f) >= last_pct + 10) {
      last_pct = (int)(p * 100.0f);
      std::printf("  %s %%%d\n", upd_state_name(s), last_pct);
    }
    std::fflush(stdout);
    if (!busy(s)) return s;
    if ((platform::now_ns() - t0) / kNsPerSec > timeout_s) {
      up.cancel();
      std::fprintf(stderr, "zaman a\xC5\x9F\xC4\xB1m\xC4\xB1 (%llu s): %s\n", (unsigned long long)timeout_s, upd_state_name(s));
      return UpdState::Failed;
    }
    platform::thread_sleep_us(20000);
  }
}
} // namespace

int update_cli(const char *verb) {
  const bool do_install = verb && !std::strcmp(verb, "kur");
  if (!verb || (!do_install && std::strcmp(verb, "denetle") != 0)) {
    std::fprintf(stderr, "kullan\xC4\xB1m: engine_editor --guncelleme denetle|kur  (verilen: %s)\n", verb ? verb : "(yok)");
    return kUpdCliUsage;
  }
  // Pencere/Vulkan yok. Bellek: cekirdegin calisma alani (A2) — tek seferlik.
  static SystemArena arena;
  if (!arena.reserve(Updater::arena_bytes() + (1u << 20), "guncelleme")) {
    std::fprintf(stderr, "bellek ayr\xC4\xB1lamad\xC4\xB1\n");
    return kUpdCliError;
  }
  UpdateUiConfig c;
  Resolved r;
  resolve(c, &r);
  static Updater up;
  char err[kUpdErrLen] = {0};
  if (!up.init(arena, to_updater_config(r), err, sizeof err)) {
    std::fprintf(stderr, "g\xC3\xBCncelleyici kurulamad\xC4\xB1: %s\n", err);
    return kUpdCliError;
  }
  std::printf("kurulu: %s (%s)\n", r.version[0] ? r.version : "kaynak derlemesi", build_platform());
  if (up.state() == UpdState::Disabled) {
    std::fprintf(stderr, "g\xC3\xBCncelleyici kapal\xC4\xB1: %s\n", up.reason());
    if (!r.version[0]) std::fprintf(stderr, "kaynak derlemesi: g\xC3\xBCncellemek i\xC3\xA7in kaynak a\xC4\x9F" "ac\xC4\xB1nda `%s`\n", kRebuildHint);
    return kUpdCliError;
  }
  if (do_install && r.install_dir[0]) Updater::cleanup(r.install_dir);
  up.check();
  const UpdState done_check[] = {UpdState::UpToDate, UpdState::Available, UpdState::Failed};
  UpdState s = cli_wait(up, done_check, 3, 120);
  if (s == UpdState::Failed) {
    std::fprintf(stderr, "denetlenemedi: %s\n", up.reason());
    return kUpdCliError;
  }
  const UpdRelease &rel = up.release();
  if (s == UpdState::UpToDate || (s == UpdState::Available && !up.newer_than_current())) {
    std::printf("en son: %s \xE2\x80\x94 g\xC3\xBCncel\n", rel.tag[0] ? rel.tag : r.version);
    return kUpdCliUpToDate;
  }
  if (s != UpdState::Available) {
    std::fprintf(stderr, "beklenmeyen durum: %s\n", upd_state_name(s));
    return kUpdCliError;
  }
  char d[32];
  upd_date_short(rel.published_at, d, sizeof d);
  std::printf("en son: %s (yay\xC4\xB1n %s)\n", rel.tag, d);
  std::printf("yeni s\xC3\xBCr\xC3\xBCm var: %s\n", rel.html_url[0] ? rel.html_url : rel.tag);
  if (!do_install) {
    std::printf("kurmak i\xC3\xA7in: engine_editor --guncelleme kur\n");
    return kUpdCliAvailable;
  }
  std::printf("indiriliyor: %s (%.1f MB)\n", rel.asset_name, (double)rel.asset_size / (1024.0 * 1024.0));
  up.download();
  const UpdState done_dl[] = {UpdState::Staged, UpdState::Failed, UpdState::Available, UpdState::Idle};
  s = cli_wait(up, done_dl, 4, 30u * 60u);
  if (s != UpdState::Staged) {
    std::fprintf(stderr, "indirilemedi: %s\n", s == UpdState::Failed ? up.reason() : upd_state_name(s));
    return kUpdCliError;
  }
  const UpdPlanSummary p = up.plan_summary();
  std::printf("plan: %u dosya g\xC3\xBCncellenecek (%u yeni, %u de\xC4\x9Fi\xC5\x9F" "en, %u kald\xC4\xB1r\xC4\xB1lan), %u ayn\xC4\xB1, %u kullan\xC4\xB1" "c\xC4\xB1 dosyas\xC4\xB1 korunacak\n",
              p.add + p.replace + p.remove, p.add, p.replace, p.remove, p.same, p.kept_user);
  for (uint32_t i = 0; i < up.kept_count(); i++) std::printf("  korunuyor: %s (yenisi %s.yeni)\n", up.kept_path(i), up.kept_path(i));
  if (!up.install(err, sizeof err)) {
    if (upd_reason_rollback_incomplete(err))
      std::fprintf(stderr, "KURULUM YARIM KALDI: %s \xE2\x80\x94 %s/.guncelleme/ elle incelenmeli\n", err, r.install_dir);
    else
      std::fprintf(stderr, "kurulamad\xC4\xB1: %s \xE2\x80\x94 kurulum dizini de\xC4\x9Fi\xC5\x9Fmedi\n", err);
    return kUpdCliError;
  }
  std::printf("kuruldu: %s -> %s. Edit\xC3\xB6r\xC3\xBC yeniden ba\xC5\x9Flat\xC4\xB1n.\n", r.version, rel.tag);
  return kUpdCliUpToDate;
}

} // namespace tulpar::engine::app
