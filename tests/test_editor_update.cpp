// app/editor_update — editor ici guncellemenin ARAYUZU. Olculen sey:
//   1) ayar dosyasi: gidis-donus bayt bayt, bozuk satir yok sayilir VE sayilir;
//   2) otomatik denetim karari: her sebep TEK BASINA tetiklenir (sira dahil);
//   3) rozet kurallari + metni, yeniden baslatma argv'si (saf fonksiyonlar);
//   4) AG KURALI: penceresiz UpdateUi Updater'a HIC istek vermez (sayac 0, red
//      sayilir); POZITIF KONTROL: ayni yapilandirma pencereli kurulunca AYNI
//      sayac 1 olur (file:// fikstur, TULPAR_GUNCELLEME_URL ile ayni yol);
//   5) sonda: sahte Available enjekte edilince rozet + pencere GERCEKTEN cizilir,
//      rozete tiklamak pencereyi acar, ImGui kimlik cakismasi yok; KONTROL:
//      Available degilken rozet YOK; pencereler kapaliyken vertex sayisi
//      update_ui_draw hic cagrilmamis gibi (maliyet sifir);
//   6) gercek ikili: `engine_editor --guncelleme denetle` cikis kodlari (10 yeni,
//      0 guncel, 2 bilinmeyen fiil), `--surum`, ve penceresiz editorun kendi
//      "guncelleme kapisi" (etkin bir guncelleyiciyle bile istek 0).
// Hicbir test ag'a CIKMAZ: kaynak file:// fikstur.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_chrome.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_ui.hpp"
#include "app/editor_update.hpp"
#include "core/build_info.hpp"
#include "core/crypto/sha256.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/fs_ops.hpp"
#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"
#include "tests/editor_probe.hpp"
#include "tests/test.hpp"

#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName, HoveredIdPreviousFrameItemCount (kimlik cakismasi)

using namespace tulpar::engine;
using namespace tulpar::engine::test;
using app::UpdState;

namespace {

bool write_file(const char *path, const char *text) {
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  const size_t n = std::strlen(text);
  const bool ok = std::fwrite(text, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}

size_t slurp(const char *path, char *buf, size_t cap) {
  buf[0] = 0;
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  const size_t n = std::fread(buf, 1, cap - 1, f);
  std::fclose(f);
  buf[n] = 0;
  return n;
}

// Yolu file:// adresine cevirir: MUTLAK yol (tmp_dir Windows'ta "." olabilir),
// POSIX file:///tmp/x, Windows file:///C:/x; bosluk/ASCII disi/% # ? kacisli
// (tests/test_updater.cpp ile ayni kural — curl ikisini de boyle bekler).
void file_url(const char *path, char *out, size_t cap) {
  char abs[1024];
  if (!platform::fs_abs_path(path, abs, sizeof abs)) std::snprintf(abs, sizeof abs, "%s", path);
  size_t n = (size_t)std::snprintf(out, cap, "%s", abs[0] == '/' ? "file://" : "file:///");
  for (const char *p = abs; *p && n + 4 < cap; p++) {
    const unsigned char c = (unsigned char)*p;
    if (c == '\\') out[n++] = '/';
    else if (c <= 0x20 || c >= 0x7F || c == '%' || c == '#' || c == '?') n += (size_t)std::snprintf(out + n, cap - n, "%%%02X", c);
    else out[n++] = (char)c;
  }
  out[n] = 0;
}

// DOSYALAR.txt satiri: "<64 kucuk hex>  <yol>\n" (sha256sum bicimi). Cekirdek
// paketleyicinin KESIN bicimini ister (docs/GUNCELLEME.md): satirlar BAYT
// sirali, SURUM.txt listelenir, DOSYALAR.txt listelenmez — cagiran sirayi korur.
bool write_rel(const char *dir, const char *rel, const char *text) {
  char p[1200];
  std::snprintf(p, sizeof p, "%s/%s", dir, rel);
  return write_file(p, text);
}
bool manifest_line(char *man, size_t cap, size_t *n, const char *text, const char *rel) {
  uint8_t d[32];
  sha256(text, std::strlen(text), d);
  char h[65];
  sha256_to_hex(d, h);
  const int w = std::snprintf(man + *n, cap - *n, "%s  %s\n", h, rel);
  if (w <= 0 || (size_t)w >= cap - *n) return false;
  *n += (size_t)w;
  return true;
}

// Sahte kurulu paket + sahte GitHub yaniti. Paket: DOSYALAR.txt + SURUM.txt +
// bos bir dosya (bicim gecerli olsun: cekirdek gecersiz manifestte guncelleyiciyi
// KAPATIR ve o zaman buradaki kapilar hicbir sey olcmezdi — ilk surum oyle
// yakalandi). Yanit: releases/latest bicimi; bu platformun arsivi + SHA256SUMS.
struct Fx {
  char dir[512] = {0};
  char json[640] = {0};
  char url[800] = {0};
  char settings[640] = {0};
};

bool make_fx(Fx &f, const char *stem, const char *tag) {
  if (!tmp_mkdir(f.dir, sizeof f.dir, stem)) return false;
  char p[700], surum[96], man[512];
  std::snprintf(surum, sizeof surum, "v0.0.1 %s\n", build_platform()); // paket bicimi: "<etiket> <platform>"
  size_t n = 0;
  // Bayt sirasi: 'S' (0x53) < 'b' (0x62).
  if (!write_rel(f.dir, "SURUM.txt", surum) || !manifest_line(man, sizeof man, &n, surum, "SURUM.txt")) return false;
  if (!write_rel(f.dir, "bos.txt", "") || !manifest_line(man, sizeof man, &n, "", "bos.txt")) return false;
  man[n] = 0;
  if (!write_rel(f.dir, "DOSYALAR.txt", man)) return false;
  std::snprintf(f.settings, sizeof f.settings, "%s/.tulpar_guncelleme", f.dir);
  std::snprintf(f.json, sizeof f.json, "%s/latest.json", f.dir);
  file_url(f.json, f.url, sizeof f.url);
  const char *plat = build_platform();
  const bool win = std::strncmp(plat, "windows", 7) == 0;
  char arsiv_url[900], sums_url[900], body[4096];
  std::snprintf(p, sizeof p, "%s/arsiv", f.dir);
  file_url(p, arsiv_url, sizeof arsiv_url);
  std::snprintf(p, sizeof p, "%s/ozet.txt", f.dir);
  file_url(p, sums_url, sizeof sums_url);
  std::snprintf(body, sizeof body,
                "{\"tag_name\": \"%s\", \"name\": \"%s\", \"draft\": false, \"prerelease\": false,\n"
                " \"published_at\": \"2026-09-25T09:44:12Z\",\n"
                " \"html_url\": \"https://github.com/hamer1818/tulpar-engine/releases/tag/%s\",\n"
                " \"body\": \"## Degisiklikler\\n- guncelleme fiksturu\\n- \\u00e7\\u015f\\u011f\",\n"
                " \"assets\": [\n"
                "  {\"name\": \"tulpar-engine-%s-%s.%s\", \"size\": 1234, \"browser_download_url\": \"%s\"},\n"
                "  {\"name\": \"tulpar-engine-%s-SHA256SUMS.txt\", \"size\": 99, \"browser_download_url\": \"%s\"}\n"
                " ]}\n",
                tag, tag, tag, tag, plat, win ? "zip" : "tar.gz", arsiv_url, tag, sums_url);
  return write_file(f.json, body);
}

// Updater'in calisma bellegi (A2): testler boyunca tek rezerv, GERI SARILMAZ —
// once kurulan bir UpdateUi'nin (Impl'i arenada) belligi sonrakinin altinda
// ezilmesin. Kurulum sayisi sabit ve kucuk (5); pay olculur.
Arena &test_arena() {
  static SystemArena a;
  static bool ok = false;
  if (!ok) ok = a.reserve(8 * app::Updater::arena_bytes() + (4u << 20), "guncelleme_test");
  return a;
}

// Penceresiz/pencereli UpdateUi yapilandirmasi (fikstur uzerinde).
app::UpdateUiConfig fx_config(const Fx &f, bool headless) {
  app::UpdateUiConfig c;
  c.settings_path = f.settings;
  c.install_dir = f.dir;
  c.api_url = f.url;
  c.current_version = "v0.0.1";
  c.headless = headless;
  c.allow_file_urls = true;
  return c;
}

// Durum Checking/Downloading... disina cikana kadar poll (en cok ~15 s).
UpdState poll_until_idle(app::UpdateUi &u) {
  for (int i = 0; i < 1500; i++) {
    app::update_ui_poll(u, platform::now_ns());
    const UpdState s = app::update_ui_state(u);
    if (s != UpdState::Checking && s != UpdState::Downloading && s != UpdState::Verifying && s != UpdState::Extracting) return s;
    platform::thread_sleep_us(10000);
  }
  return app::update_ui_state(u);
}

} // namespace

// --- 1) Ayar dosyasi -----------------------------------------------------------
ENGINE_TEST(editor_update_settings_roundtrip_and_bad_lines) {
  app::UpdSettings a;
  a.auto_check = false;
  a.last_check = 1790000000;
  std::snprintf(a.skip_tag, sizeof a.skip_tag, "v0.2.0-rc.1");
  char t1[512], t2[512];
  const size_t n1 = app::upd_settings_write(a, t1, sizeof t1);
  CHECK(n1 > 0 && n1 < sizeof t1);
  app::UpdSettings b;
  const app::UpdSettingsStats s1 = app::upd_settings_parse(t1, n1, &b);
  CHECK(s1.bad == 0 && s1.applied == 3 && s1.lines == 3);
  CHECK(!b.auto_check && b.last_check == 1790000000 && !std::strcmp(b.skip_tag, "v0.2.0-rc.1"));
  const size_t n2 = app::upd_settings_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // ayni ayar -> ayni bayt

  // Varsayilan: dosya yok -> otomatik evet, hic denetlenmedi, atlanan yok.
  app::UpdSettings d;
  CHECK(d.auto_check && d.last_check == 0 && d.skip_tag[0] == 0);

  // Bozuk satirlar yok sayilir VE sayilir; gecerliler (bosluk/CRLF dahil) uygulanir.
  static const char kBozuk[] =
      "# yorum sayilmaz\n"
      "\n"
      "otomatik belki\n"      // bozuk deger
      "son_denetim -5\n"      // isaret yok
      "son_denetim 12x\n"     // rakam disi
      "son_denetim 1234567890123456789012\n" // tasma
      "atla\n"                // deger yok
      "atla bir iki\n"        // iki jeton
      "atla 0.2.0\n"          // v yok: surum degil
      "bilinmeyen 1\n"        // taninmayan anahtar
      "  otomatik   hayir \r\n"
      "son_denetim 42\n"
      "atla v1.2.3\n";
  app::UpdSettings c;
  const app::UpdSettingsStats s2 = app::upd_settings_parse(kBozuk, sizeof kBozuk - 1, &c);
  std::printf("    [bilgi] bozuk fikstur: %u satir, %u uygulandi, %u bozuk\n", s2.lines, s2.applied, s2.bad);
  CHECK(s2.lines == 11 && s2.applied == 3 && s2.bad == 8);
  CHECK(!c.auto_check && c.last_check == 42 && !std::strcmp(c.skip_tag, "v1.2.3"));
  // Turkce yazim da kabul: "hayır".
  app::UpdSettings e;
  CHECK(app::upd_settings_parse("otomatik hay\xC4\xB1r\n", 15, &e).applied == 1 && !e.auto_check);

  // Dosya gidis-donusu (gecici dosya + yerine tasima) ve "dosya yok".
  char dir[512], path[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "guncelleme_ayar"));
  std::snprintf(path, sizeof path, "%s/.tulpar_guncelleme", dir);
  app::UpdSettings yok;
  app::UpdSettingsStats st;
  CHECK(!app::upd_settings_load(path, &yok, &st) && yok.auto_check && st.lines == 0);
  CHECK(app::upd_settings_save(path, a));
  app::UpdSettings geri;
  CHECK(app::upd_settings_load(path, &geri, &st) && st.bad == 0);
  CHECK(geri.auto_check == a.auto_check && geri.last_check == a.last_check && !std::strcmp(geri.skip_tag, a.skip_tag));
  CHECK(app::upd_settings_save(path, a)); // ikinci kayit: hedef VARKEN yerine tasima (Windows tuzagi)
  char disk[512];
  CHECK(slurp(path, disk, sizeof disk) == n1 && std::memcmp(disk, t1, n1) == 0);
}

// --- 2) Otomatik denetim karari ------------------------------------------------
ENGINE_TEST(editor_update_auto_decide_each_reason) {
  using app::UpdAutoWhy;
  app::UpdSettings s;
  const int64_t now = 1790000000;
  const int64_t day = app::kUpdAutoIntervalSec;
  // Pozitif: her kosul tutuyor -> denetle.
  CHECK(app::upd_auto_decide(s, false, false, nullptr, now) == UpdAutoWhy::Go);
  CHECK(app::upd_auto_decide(s, false, false, "1", now) == UpdAutoWhy::Go);
  // Her sebep TEK BASINA.
  CHECK(app::upd_auto_decide(s, true, false, nullptr, now) == UpdAutoWhy::Disabled);
  CHECK(app::upd_auto_decide(s, false, true, nullptr, now) == UpdAutoWhy::Headless);
  CHECK(app::upd_auto_decide(s, false, false, "0", now) == UpdAutoWhy::EnvOff);
  app::UpdSettings off = s;
  off.auto_check = false;
  CHECK(app::upd_auto_decide(off, false, false, nullptr, now) == UpdAutoWhy::UserOff);
  app::UpdSettings yeni = s;
  yeni.last_check = now - day + 1;
  CHECK(app::upd_auto_decide(yeni, false, false, nullptr, now) == UpdAutoWhy::Recent);
  CHECK(app::upd_auto_wait_sec(yeni, now) == 1);
  yeni.last_check = now - day; // tam 24 saat: vadesi geldi
  CHECK(app::upd_auto_decide(yeni, false, false, nullptr, now) == UpdAutoWhy::Go);
  CHECK(app::upd_auto_wait_sec(yeni, now) == 0);
  yeni.last_check = now + 3600; // saat geri gitti: "yeni" SAYILMAZ, denetle
  CHECK(app::upd_auto_decide(yeni, false, false, nullptr, now) == UpdAutoWhy::Go);
  // Penceresiz kip her seyi (Disabled haric) ezer: otomatik acik, env yok, vade gecmis.
  CHECK(app::upd_auto_decide(s, false, true, "1", now + 10 * day) == UpdAutoWhy::Headless);
  for (int w = 0; w <= (int)UpdAutoWhy::Recent; w++) CHECK(app::upd_auto_why_text((UpdAutoWhy)w)[0] != 0);
}

// --- 3) Rozet + yeniden baslatma argv'si ---------------------------------------
ENGINE_TEST(editor_update_badge_rules_and_text) {
  CHECK(app::upd_badge_visible(UpdState::Available, true, "v1.2.3", "", false));
  CHECK(!app::upd_badge_visible(UpdState::Available, false, "v1.2.3", "", false)); // kurulu daha yeni
  CHECK(!app::upd_badge_visible(UpdState::Available, true, "v1.2.3", "v1.2.3", false)); // atlandi
  CHECK(app::upd_badge_visible(UpdState::Available, true, "v1.2.3", "v1.2.3", true));   // ...elle denetimde gorunur
  CHECK(app::upd_badge_visible(UpdState::Available, true, "v1.2.4", "v1.2.3", false));  // atlanan BASKA surum
  const UpdState yok[] = {UpdState::Disabled, UpdState::Idle, UpdState::Checking, UpdState::UpToDate};
  for (UpdState s : yok) CHECK(!app::upd_badge_visible(s, true, "v1.2.3", "", true));
  const UpdState var[] = {UpdState::Downloading, UpdState::Verifying, UpdState::Extracting, UpdState::Staged, UpdState::Installed};
  for (UpdState s : var) CHECK(app::upd_badge_visible(s, true, "v1.2.3", "v1.2.3", false));
  char b[64];
  app::upd_badge_text(UpdState::Available, "v1.2.3", -1.0f, b, sizeof b);
  CHECK(!std::strcmp(b, "\xE2\xAC\x86 v1.2.3"));
  app::upd_badge_text(UpdState::Downloading, "v1.2.3", 0.4f, b, sizeof b);
  CHECK(!std::strcmp(b, "\xE2\xAC\x86 v1.2.3  %40"));
  app::upd_badge_text(UpdState::Staged, "v1.2.3", -1.0f, b, sizeof b);
  CHECK(!std::strcmp(b, "\xE2\xAC\x86 v1.2.3 haz\xC4\xB1r"));
  char d[32];
  app::upd_date_short("2026-09-25T09:44:12Z", d, sizeof d);
  CHECK(!std::strcmp(d, "2026-09-25"));
  app::upd_date_short("dun", d, sizeof d);
  CHECK(!std::strcmp(d, "dun"));
}

ENGINE_TEST(editor_update_restart_argv_keeps_args) {
  char a0[] = "./engine_editor", a1[] = "--size", a2[] = "1600x900", a3[] = "--scene", a4[] = "eski.sahne", a5[] = "--validation";
  char *argv[] = {a0, a1, a2, a3, a4, a5, nullptr};
  const char *out[16];
  // Sahne verilince --scene degeri DEGISIR, geri kalan AYNEN ve AYNI sirada.
  uint32_t n = app::update_restart_argv("/kur/engine_editor", 6, argv, "/ev/yeni.sahne", out, 16);
  CHECK(n == 6 && out[6] == nullptr);
  CHECK(!std::strcmp(out[0], "/kur/engine_editor") && !std::strcmp(out[1], "--size") && !std::strcmp(out[2], "1600x900"));
  CHECK(!std::strcmp(out[3], "--scene") && !std::strcmp(out[4], "/ev/yeni.sahne") && !std::strcmp(out[5], "--validation"));
  // Sahne bos: orijinal --scene kalir.
  n = app::update_restart_argv("/kur/engine_editor", 6, argv, "", out, 16);
  CHECK(n == 6 && !std::strcmp(out[4], "eski.sahne"));
  // --scene yoksa sona eklenir.
  char *argv2[] = {a0, a5, nullptr};
  n = app::update_restart_argv("/kur/engine_editor", 2, argv2, "/ev/yeni.sahne", out, 16);
  CHECK(n == 4 && !std::strcmp(out[1], "--validation") && !std::strcmp(out[2], "--scene") && !std::strcmp(out[3], "/ev/yeni.sahne"));
  // KONTROL: sigmayan liste KIRPILMAZ (yarim argv baska bir komuttur).
  CHECK(app::update_restart_argv("/kur/engine_editor", 6, argv, nullptr, out, 4) == 0);

  // Tuzaklar 8cl: exe disindaki argumanlar UTF-8'e cevrilir. ASCII fikstur bu
  // farki OLCEMEZ; olcum icin kod sayfasinda ayrisan bir harf (c-cedilla).
  char u8[64];
  CHECK(app::update_arg_to_utf8("duz/yol.sahne", u8, sizeof u8) && !std::strcmp(u8, "duz/yol.sahne"));
#if defined(_WIN32)
  const uint32_t acp = app::update_acp();
  if (acp == 1252 || acp == 1254) { // ikisinde de 0xE7 = U+00E7
    CHECK(app::update_arg_to_utf8("sahne_\xE7" "a.sahne", u8, sizeof u8) && !std::strcmp(u8, "sahne_\xC3\xA7" "a.sahne"));
    std::printf("    [bilgi] ACP %u: 0xE7 -> UTF-8 C3 A7\n", acp);
  } else {
    char why[96];
    std::snprintf(why, sizeof why, "ACP %u'da 0xE7 c-cedilla degil — argv cevirme kapisi olculemedi", acp);
    skip(why);
  }
#else
  CHECK(app::update_acp() == 65001);
  CHECK(app::update_arg_to_utf8("sahne_\xC3\xA7" "a.sahne", u8, sizeof u8) && !std::strcmp(u8, "sahne_\xC3\xA7" "a.sahne")); // POSIX: bayt aynen
#endif
  CHECK(!app::update_arg_to_utf8("uzun-bir-arguman", u8, 4)); // sigmayan KIRPILMAZ
}

// --- 4) AG KURALI: penceresiz 0 istek; pozitif kontrol pencereli 1 istek ------
ENGINE_TEST(editor_update_headless_never_requests_positive_control_counts) {
  static Fx f;
  if (!make_fx(f, "guncelleme_ag", "v9.9.9")) { CHECK(false); return; }
  // (a) PENCERESIZ: otomatik acik, hic denetlenmemis, env yok — tek engel kip.
  static app::UpdateUi h;
  h = app::UpdateUi{};
  CHECK(app::update_ui_init(h, test_arena(), fx_config(f, true), platform::now_ns()));
  const UpdState hs = app::update_ui_state(h);
  std::printf("    [bilgi] penceresiz: durum %s, karar \"%s\", istek %u\n", app::upd_state_name(hs), app::upd_auto_why_text(h.auto_why), h.requests);
  CHECK(hs != UpdState::Disabled); // fikstur gecerli bir paket: guncelleyici ETKIN
  CHECK(h.auto_why == app::UpdAutoWhy::Headless);
  CHECK(h.requests == 0);
  app::update_ui_check(h, true); // menu komutu: REDDEDILIR
  for (int i = 0; i < 5; i++) app::update_ui_poll(h, platform::now_ns());
  CHECK(h.requests == 0 && h.refused_headless == 1);
  CHECK(app::update_ui_state(h) == hs); // Updater'in durumu hic degismedi
  char disk[256];
  CHECK(slurp(f.settings, disk, sizeof disk) == 0); // penceresiz: ayar dosyasina YAZMAZ

  // MALIYET: editorun HER KARE yaptigi uc cagri (poll + rozet sorgusu + kapali
  // pencerelerle draw) bosta ayirmaz ve sabit zamanli. ImGui baglami yok: draw
  // pencereler kapaliyken ImGui'ye hic dokunmadan donmeli (dokunsaydi cokerdi).
  h.window_open = false;
  constexpr int kN = 1000000;
  const uint64_t now0 = platform::now_ns();
  AllocGate::begin_frame();
  const uint64_t t0 = platform::now_ns();
  for (int i = 0; i < kN; i++) {
    app::update_ui_poll(h, now0 + (uint64_t)i);
    test::escape(app::update_ui_badge(h));
    app::update_ui_draw(h);
    test::escape(&h);
  }
  const uint64_t dt = platform::now_ns() - t0;
  const uint64_t allocs = AllocGate::end_frame();
  std::printf("    [olcum] bosta kare yolu (poll + rozet + draw): %.1f ns/kare (%d kare), ayirma %llu\n", (double)dt / kN, kN,
              (unsigned long long)allocs);
  CHECK(allocs == 0);
  CHECK(h.requests == 0);

  // (b) POZITIF KONTROL: AYNI yapilandirma, pencereli. Karar Go, istek 1.
  static app::UpdateUi w;
  w = app::UpdateUi{};
  CHECK(app::update_ui_init(w, test_arena(), fx_config(f, false), platform::now_ns()));
  std::printf("    [bilgi] pencereli: karar \"%s\", istek %u (0 olsaydi sayac kor)\n", app::upd_auto_why_text(w.auto_why), w.requests);
  CHECK(w.auto_why == app::UpdAutoWhy::Go);
  CHECK(w.requests == 1 && w.refused_headless == 0);
  const UpdState ws = poll_until_idle(w);
  std::printf("    [bilgi] fikstur denetimi: %s (%s), rozet %s\n", app::upd_state_name(ws), app::update_ui_reason(w),
              app::update_ui_badge(w) ? app::update_ui_badge(w) : "(yok)");
  CHECK(ws == UpdState::Available);
  if (ws == UpdState::Available) {
    CHECK(!std::strcmp(app::update_ui_release(w).tag, "v9.9.9"));
    const char *b = app::update_ui_badge(w);
    CHECK(b != nullptr && std::strstr(b, "v9.9.9") != nullptr);
    CHECK(w.settings.last_check > 0); // basarili denetim damgalandi...
    CHECK(slurp(f.settings, disk, sizeof disk) > 0 && std::strstr(disk, "son_denetim ") != nullptr); // ...ve yazildi
    // Atla: rozet kaybolur (otomatik denetim), ayar dosyasina girer.
    std::snprintf(w.settings.skip_tag, sizeof w.settings.skip_tag, "v9.9.9");
    w.manual = false;
    CHECK(app::update_ui_badge(w) == nullptr);
  }
}

// --- 5) Sonda: rozet + pencere ---------------------------------------------------
namespace {
struct DrawCtx {
  app::UpdateUi *u = nullptr;
  app::CommandTable t;
  app::ChromeState s;
  bool with_update = true;  // update_ui_draw cagrilsin mi (maliyet kontrolu)
  bool click_badge = false; // sentetik fare rozete tiklasin
  bool scan_window = false; // pencerenin uzerinde fareyi gezdir (kimlik cakismasi)
  uint32_t badge_frames = 0, clicked = 0, conflicts = 0, scanned = 0;
  float badge[4] = {0, 0, 0, 0};
  bool badge_ok = false;
  float win[4] = {0, 0, 0, 0};
};
void on_badge(void *ctx) {
  DrawCtx *c = static_cast<DrawCtx *>(ctx);
  c->clicked++;
  app::update_ui_open_window(*c->u);
}
void draw_update(void *vctx, uint32_t f) {
  DrawCtx &c = *static_cast<DrawCtx *>(vctx);
  c.s = app::ChromeState{};
  c.s.update_badge = app::update_ui_badge(*c.u);
  c.s.update_badge_tip = c.s.update_badge ? "yeni surum" : nullptr;
  app::ChromeMenuExtra ex;
  ex.ctx = &c;
  ex.on_badge = on_badge;
  app::chrome_menu_bar(c.t, c.s, ex);
  if (app::chrome_stats().badge_submitted) c.badge_frames++;
  if (c.with_update) app::update_ui_draw(*c.u);
  float r[4];
  if (app::chrome_probe_rect(app::ChromeRect::UpdateBadge, r)) {
    for (int i = 0; i < 4; i++) c.badge[i] = r[i];
    c.badge_ok = true;
  }
  // Kimlik cakismasi: onceki karede fare altindaki kimligi bu karede birden cok
  // oge kullandiysa ImGui sayar (HoveredIdPreviousFrameItemCount > 1).
  if (ImGui::GetCurrentContext()->HoveredIdPreviousFrameItemCount > 1) c.conflicts++;
  // Sentetik fare: kare N'de kuyruga girer, N+1'de islenir.
  ImGuiIO &io = ImGui::GetIO();
  if (c.click_badge && c.badge_ok) {
    if (f == 1) io.AddMousePosEvent((c.badge[0] + c.badge[2]) * 0.5f, (c.badge[1] + c.badge[3]) * 0.5f);
    if (f == 2) io.AddMouseButtonEvent(0, true);
    if (f == 3) io.AddMouseButtonEvent(0, false);
  }
  if (c.scan_window) {
    ImGuiWindow *w = ImGui::FindWindowByName("###tulpar_guncelleme");
    if (w && w->Active) {
      c.win[0] = w->Pos.x; c.win[1] = w->Pos.y; c.win[2] = w->Pos.x + w->Size.x; c.win[3] = w->Pos.y + w->Size.y;
      // Izgara: 6 sutun x (kare) satir — her karede bir nokta, pencerenin tamami.
      const uint32_t k = c.scanned++;
      const float gx = (float)(k % 6) / 5.0f, gy = (float)((k / 6) % 8) / 7.0f;
      io.AddMousePosEvent(c.win[0] + 6.0f + gx * (c.win[2] - c.win[0] - 12.0f), c.win[1] + 6.0f + gy * (c.win[3] - c.win[1] - 12.0f));
    }
  }
}
void no_cmd(void *) {}

// Sondaya hazir bir UpdateUi: penceresiz (ag YOK), sahte goruntu enjekte.
app::UpdateUi &probe_ui() {
  static app::UpdateUi u;
  static Fx f;
  static bool made = false;
  if (!made) made = make_fx(f, "guncelleme_sonda", "v9.9.9");
  u = app::UpdateUi{};
  app::update_ui_init(u, test_arena(), fx_config(f, true), platform::now_ns());
  return u;
}

app::UpdRelease &fake_release() {
  static app::UpdRelease r;
  r = app::UpdRelease{};
  std::snprintf(r.tag, sizeof r.tag, "v0.3.0");
  std::snprintf(r.published_at, sizeof r.published_at, "2026-09-25T09:44:12Z");
  std::snprintf(r.html_url, sizeof r.html_url, "https://github.com/hamer1818/tulpar-engine/releases/tag/v0.3.0");
  std::snprintf(r.asset_name, sizeof r.asset_name, "tulpar-engine-v0.3.0-%s.tar.gz", build_platform());
  r.asset_size = 48u << 20;
  std::snprintf(r.notes, sizeof r.notes,
                "## Yenilikler\n- Edit\xC3\xB6r i\xC3\xA7i g\xC3\xBC" "ncelleme: Yard\xC4\xB1m > G\xC3\xBC" "ncellemeleri denetle\n"
                "- \xC3\x87ok uzun bir sat\xC4\xB1r: sarma dogru calisiyor mu diye yazilmis, pencerenin genisligini birkac kez asan "
                "bir aciklama metni; kaydirilabilir alanin icinde kalmali ve yatay kaydirma cubugu URETMEMELI.\n"
                "- Sat\xC4\xB1r 3\n- Sat\xC4\xB1r 4\n- Sat\xC4\xB1r 5\n- Sat\xC4\xB1r 6\n- Sat\xC4\xB1r 7\n- Sat\xC4\xB1r 8\n- Sat\xC4\xB1r 9\n");
  r.notes_truncated = true;
  return r;
}

void probe_out(char *buf, size_t n, const char *name) {
  const char *d = std::getenv("TULPAR_PROBE_DIR");
  std::snprintf(buf, n, "%s/%s.ppm", (d && *d) ? d : tmp_dir(), name);
}

ProbeStatus run(DrawCtx &c, uint32_t frames, const char *name, EditorProbe *out = nullptr) {
  EditorProbe p;
  p.width = 1280;
  p.height = 720;
  p.frames = frames;
  p.draw = draw_update;
  p.ctx = &c;
  char path[600];
  probe_out(path, sizeof path, name);
  p.out_ppm = path;
  const ProbeStatus st = editor_probe_render(p);
  if (out) *out = p;
  return st;
}
} // namespace

ENGINE_TEST(editor_update_badge_and_window_render_headless) {
  // Tablo baglamalari: menu cubugu tiklanmayan olu satir cizmesin.
  static DrawCtx c;
  c = DrawCtx{};
  for (uint32_t i = 1; i <= app::kCommandCount; i++) c.t.bind((app::CommandId)i, no_cmd, nullptr);

  // (a) Available + rozete TIKLA -> pencere acilir ve cizilir; fare pencereyi
  // tarar (kimlik cakismasi).
  app::UpdateUi &u = probe_ui();
  app::update_ui_test_inject(u, UpdState::Available, &fake_release());
  c.u = &u;
  c.click_badge = true;
  c.scan_window = true;
  EditorProbe p;
  ProbeStatus st = run(c, 60, "guncelleme_available", &p);
  if (probe_not_ok(st, p.err, __FILE__, __LINE__)) return;
  std::printf("    [bilgi] Available: rozet %u karede, tiklama %u, pencere govdesi %u kare, taranan nokta %u, kimlik cakismasi %u, vertex %u\n",
              c.badge_frames, c.clicked, u.draw_update_win, c.scanned, c.conflicts, p.vertices);
  CHECK(c.badge_ok && c.badge[2] > c.badge[0] && c.badge[3] > c.badge[1]);
  CHECK(c.badge_frames == 60);
  CHECK(c.clicked == 1);
  CHECK(u.window_open && u.draw_update_win > 40);
  CHECK(c.scanned > 40);
  CHECK(c.conflicts == 0);
  // Rozet sagda ve menu cubugunun icinde (urun adinin solunda).
  float bar[4];
  CHECK(app::chrome_probe_rect(app::ChromeRect::MenuBar, bar));
  CHECK(c.badge[0] > (bar[0] + bar[2]) * 0.5f && c.badge[2] < bar[2] && c.badge[1] >= bar[1] && c.badge[3] <= bar[3]);
  static uint8_t rozetli[1280 * 720 * 4];
  std::memcpy(rozetli, p.pixels, sizeof rozetli);
  const float badge_a[4] = {c.badge[0], c.badge[1], c.badge[2], c.badge[3]};

  // (b) KONTROL: Available DEGIL (Idle) -> rozet YOK, ayni yerde piksel farkli.
  app::update_ui_test_inject(u, UpdState::Idle, nullptr);
  u.window_open = false;
  c = DrawCtx{};
  for (uint32_t i = 1; i <= app::kCommandCount; i++) c.t.bind((app::CommandId)i, no_cmd, nullptr);
  c.u = &u;
  st = run(c, 3, "guncelleme_idle", &p);
  if (probe_not_ok(st, p.err, __FILE__, __LINE__)) return;
  CHECK(c.badge_frames == 0 && !c.badge_ok);
  uint32_t fark = 0, top = 0;
  for (uint32_t y = (uint32_t)badge_a[1]; y < (uint32_t)badge_a[3]; y++)
    for (uint32_t x = (uint32_t)badge_a[0]; x < (uint32_t)badge_a[2]; x++) {
      const uint8_t *a = rozetli + ((size_t)y * 1280 + x) * 4, *b = p.pixels + ((size_t)y * 1280 + x) * 4;
      top++;
      if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) fark++;
    }
  std::printf("    [bilgi] rozet alani: %u pikselin %u'i rozetsiz karede farkli\n", top, fark);
  CHECK(top > 0 && fark * 4 > top); // rozetin dolgusu + metni alanin en az ceyregi

  // (c) MALIYET: pencereler kapali, rozet yok -> update_ui_draw ImGui'ye hic
  // dokunmaz: vertex sayisi, hic cagrilmamis karenin AYNISI.
  const uint32_t v_with = p.vertices;
  c = DrawCtx{};
  for (uint32_t i = 1; i <= app::kCommandCount; i++) c.t.bind((app::CommandId)i, no_cmd, nullptr);
  c.u = &u;
  c.with_update = false;
  st = run(c, 3, "guncelleme_hic", &p);
  if (probe_not_ok(st, p.err, __FILE__, __LINE__)) return;
  std::printf("    [bilgi] kapali pencereler: vertex %u (update_ui_draw ile) == %u (onsuz)\n", v_with, p.vertices);
  CHECK(v_with == p.vertices);

  // (d) Staged (kullanici dosyalari listesiyle), Failed ve Disabled pencereleri:
  // GORMEK icin PPM + ImGui hatasi yok (sonda kapisi) + govde cizildi.
  static const char *kept[] = {"tests/assets/editor.sahne", "assets/fonts/OKUBENI.txt"};
  app::UpdPlanSummary plan;
  plan.add = 3; plan.replace = 41; plan.remove = 1; plan.same = 120; plan.kept_user = 2;
  struct Vaka { UpdState s; const char *ad; const char *sebep; };
  const Vaka vakalar[] = {{UpdState::Staged, "guncelleme_staged", ""},
                          {UpdState::Downloading, "guncelleme_indiriliyor", ""},
                          {UpdState::Failed, "guncelleme_failed", "SHA-256 tutmadi: tulpar-engine-v0.3.0-linux-x86_64.tar.gz"},
                          {UpdState::Disabled, "guncelleme_disabled", "kaynak derlemesi (build_version bos)"}};
  for (const Vaka &v : vakalar) {
    app::update_ui_test_inject(u, v.s, &fake_release(), 0.4f, &plan, kept, 2, v.sebep);
    // Disabled vakasi kaynak derlemesini canlandirir: surum bos -> yeniden derleme yolu gorunur.
    std::snprintf(u.current_version, sizeof u.current_version, "%s", v.s == UpdState::Disabled ? "" : "v0.2.9");
    app::update_ui_open_window(u);
    const uint32_t w0 = u.draw_update_win;
    c = DrawCtx{};
    for (uint32_t i = 1; i <= app::kCommandCount; i++) c.t.bind((app::CommandId)i, no_cmd, nullptr);
    c.u = &u;
    c.scan_window = true;
    st = run(c, 30, v.ad, &p);
    if (probe_not_ok(st, p.err, __FILE__, __LINE__)) return;
    std::printf("    [bilgi] %s: govde %u kare, kimlik cakismasi %u, rozet %s\n", v.ad, u.draw_update_win - w0, c.conflicts,
                c.badge_frames ? "var" : "yok");
    CHECK(u.draw_update_win - w0 >= 29); // ilk kare otomatik boyutlanma icin gizli olabilir
    CHECK(c.conflicts == 0);
    CHECK((c.badge_frames > 0) == (v.s != UpdState::Disabled)); // Disabled: rozet yok; digerleri: var
  }
  // Hakkinda penceresi de cizilir.
  app::update_ui_test_inject(u, UpdState::UpToDate, nullptr);
  u.window_open = false;
  app::update_ui_open_about(u);
  const uint32_t a0 = u.draw_about_win;
  c = DrawCtx{};
  for (uint32_t i = 1; i <= app::kCommandCount; i++) c.t.bind((app::CommandId)i, no_cmd, nullptr);
  c.u = &u;
  st = run(c, 3, "guncelleme_hakkinda", &p);
  if (probe_not_ok(st, p.err, __FILE__, __LINE__)) return;
  CHECK(u.draw_about_win - a0 >= 2);
  app::update_ui_test_clear(u);
}

// --- 6) Gercek ikili: --guncelleme / --surum / penceresiz kapi ------------------
namespace {
bool editor_exe(char *out, size_t cap) {
  char d[1024];
  if (!platform::exe_dir(d, sizeof d)) return false;
#if defined(_WIN32)
  const int n = std::snprintf(out, cap, "%s\\engine_editor.exe", d);
#else
  const int n = std::snprintf(out, cap, "%s/engine_editor", d);
#endif
  if (n <= 0 || (size_t)n >= cap) return false;
  FILE *f = std::fopen(out, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// engine_editor'u fikstur ortamiyla kostur, cikis kodunu ve gunlugu dondur.
// inst: kurulum dizini (TULPAR_GUNCELLEME_DIZIN), work: calisma dizini + HOME/
// USERPROFILE (kullanicinin ~/.tulpar_guncelleme'sine dokunulmaz) + gunluk.
int run_editor_in(const char *exe, const char *inst, const char *url, const char *work, const char *const *args, char *log_buf,
                  size_t log_cap, uint32_t timeout_ms) {
  static char e_url[1500], e_dir[1100], e_ver[64], e_home[1100], e_prof[1100];
  std::snprintf(e_url, sizeof e_url, "TULPAR_GUNCELLEME_URL=%s", url);
  std::snprintf(e_dir, sizeof e_dir, "TULPAR_GUNCELLEME_DIZIN=%s", inst);
  std::snprintf(e_ver, sizeof e_ver, "TULPAR_GUNCELLEME_SURUM=v0.0.1");
  std::snprintf(e_home, sizeof e_home, "HOME=%s", work);
  std::snprintf(e_prof, sizeof e_prof, "USERPROFILE=%s", work);
  const char *env[] = {e_url, e_dir, e_ver, e_home, e_prof, nullptr};
  const char *argv[16];
  int n = 0;
  argv[n++] = exe;
  for (int i = 0; args[i] && n < 15; i++) argv[n++] = args[i];
  argv[n] = nullptr;
  char log[1200];
  std::snprintf(log, sizeof log, "%s/cikti.txt", work);
  platform::ProcessSpec s;
  s.argv = argv;
  s.env = env;
  s.cwd = work;
  s.log_path = log;
  platform::Process p;
  char err[256];
  if (!platform::process_start(p, s, err, sizeof err)) {
    std::printf("    FAIL baslatilamadi: %s\n", err);
    return -1002;
  }
  int code = -1000;
  for (uint32_t i = 0; i < timeout_ms / 5; i++) {
    const platform::ProcessState ps = platform::process_poll(p, &code);
    if (ps == platform::ProcessState::Exited) break;
    if (ps == platform::ProcessState::Failed) { code = -1001; break; }
    platform::thread_sleep_us(5000);
  }
  if (code == -1000) platform::process_kill(p);
  slurp(log, log_buf, log_cap);
  return code;
}
int run_editor(const char *exe, const Fx &f, const char *const *args, char *log_buf, size_t log_cap, uint32_t timeout_ms = 60000) {
  return run_editor_in(exe, f.dir, f.url, f.dir, args, log_buf, log_cap, timeout_ms);
}

// --- `--guncelleme kur` icin GERCEK bir surum: arsiv + ozet + latest.json ----
// Kurulum (v0.0.1): OKUBENI.md degismemis (-> degisir), sahne.sahne KULLANICI
// degistirmis (-> yerinde kalir, yenisi .yeni), SURUM.txt. Surum v0.3.0 ayni
// uc dosyanin yenisi. Bicimler paketlemeyle ayni (tools/paket_manifest.py).
struct KurFx {
  char base[512] = {0}, inst[600] = {0}, rel[600] = {0}, url[1400] = {0}, why[256] = {0};
};
bool make_kur_fx(KurFx &k) {
  char tar[1024], curl[1024];
  if (!platform::fs_system_tool("tar", tar, sizeof tar) || !platform::fs_system_tool("curl", curl, sizeof curl)) {
    std::snprintf(k.why, sizeof k.why, "curl/tar bulunamadi — --guncelleme kur uctan uca olculemedi");
    return false;
  }
  if (!tmp_mkdir(k.base, sizeof k.base, "guncelleme_kur")) return false;
  std::snprintf(k.inst, sizeof k.inst, "%s/kurulum", k.base);
  std::snprintf(k.rel, sizeof k.rel, "%s/yayin", k.base);
  const char *plat = build_platform();
  const bool win = std::strncmp(plat, "windows", 7) == 0;
  char s_old[96], s_new[96], man[2048];
  std::snprintf(s_old, sizeof s_old, "v0.0.1 %s\n", plat);
  std::snprintf(s_new, sizeof s_new, "v0.3.0 %s\n", plat);
  // Kurulum.
  size_t n = 0;
  bool ok = platform::fs_mkdir_p(k.inst);
  // Satirlar BAYT sirali: OKUBENI.md < SURUM.txt < sahne.sahne.
  ok = ok && write_rel(k.inst, "OKUBENI.md", "eski okubeni\n") && manifest_line(man, sizeof man, &n, "eski okubeni\n", "OKUBENI.md");
  ok = ok && write_rel(k.inst, "SURUM.txt", s_old) && manifest_line(man, sizeof man, &n, s_old, "SURUM.txt");
  ok = ok && write_rel(k.inst, "sahne.sahne", "kullanicinin sahnesi\n") && manifest_line(man, sizeof man, &n, "paketin sahnesi\n", "sahne.sahne");
  man[n] = 0;
  ok = ok && write_rel(k.inst, "DOSYALAR.txt", man);
  // Surum paketi: tulpar-engine-v0.3.0-<plat>/...
  char pkg_name[160], pkg[800];
  std::snprintf(pkg_name, sizeof pkg_name, "tulpar-engine-v0.3.0-%s", plat);
  std::snprintf(pkg, sizeof pkg, "%s/%s", k.rel, pkg_name);
  n = 0;
  ok = ok && platform::fs_mkdir_p(pkg);
  ok = ok && write_rel(pkg, "OKUBENI.md", "yeni okubeni\n") && manifest_line(man, sizeof man, &n, "yeni okubeni\n", "OKUBENI.md");
  ok = ok && write_rel(pkg, "SURUM.txt", s_new) && manifest_line(man, sizeof man, &n, s_new, "SURUM.txt");
  ok = ok && write_rel(pkg, "sahne.sahne", "yeni sahne\n") && manifest_line(man, sizeof man, &n, "yeni sahne\n", "sahne.sahne");
  man[n] = 0;
  ok = ok && write_rel(pkg, "DOSYALAR.txt", man);
  if (!ok) return false;
  // Arsiv (alt surec, GORELI yollar; Windows'ta System32 tar ile zip).
  char arc_name[200];
  std::snprintf(arc_name, sizeof arc_name, "%s.%s", pkg_name, win ? "zip" : "tar.gz");
  const char *targv_w[] = {tar, "-a", "-cf", arc_name, pkg_name, nullptr};
  const char *targv_p[] = {tar, "-czf", arc_name, pkg_name, nullptr};
  platform::ProcessSpec ts;
  ts.argv = win ? targv_w : targv_p;
  ts.cwd = k.rel;
  platform::Process tp;
  char err[256];
  if (!platform::process_start(tp, ts, err, sizeof err)) return false;
  int code = -1;
  for (int i = 0; i < 6000 && platform::process_poll(tp, &code) == platform::ProcessState::Running; i++) platform::thread_sleep_us(5000);
  if (code != 0) { std::snprintf(k.why, sizeof k.why, "arsivleme basarisiz (tar %d)", code); return false; }
  // Ozet + latest.json.
  char arc[1000], sums_name[160], sums_path[1000], arc_url[1400], sums_url[1400];
  std::snprintf(arc, sizeof arc, "%s/%s", k.rel, arc_name);
  uint64_t arc_size = 0;
  if (!platform::fs_size(arc, &arc_size) || arc_size == 0 || arc_size > (4u << 20)) return false;
  static char arc_bytes[4u << 20];
  bool trunc = false;
  const int64_t got = platform::fs_read_all(arc, arc_bytes, sizeof arc_bytes, &trunc);
  if (got != (int64_t)arc_size || trunc) return false;
  uint8_t d[32];
  sha256(arc_bytes, (size_t)got, d);
  char h[65], sums[400];
  sha256_to_hex(d, h);
  std::snprintf(sums, sizeof sums, "%s  %s\n", h, arc_name);
  std::snprintf(sums_name, sizeof sums_name, "tulpar-engine-v0.3.0-SHA256SUMS.txt");
  std::snprintf(sums_path, sizeof sums_path, "%s/%s", k.rel, sums_name);
  if (!write_file(sums_path, sums)) return false;
  file_url(arc, arc_url, sizeof arc_url);
  file_url(sums_path, sums_url, sizeof sums_url);
  static char json[4096];
  std::snprintf(json, sizeof json,
                "{\"tag_name\":\"v0.3.0\",\"published_at\":\"2026-09-25T12:00:00Z\","
                "\"html_url\":\"https://github.com/hamer1818/tulpar-engine/releases/tag/v0.3.0\",\"body\":\"kur fiksturu\","
                "\"assets\":[{\"name\":\"%s\",\"size\":%zu,\"browser_download_url\":\"%s\"},"
                "{\"name\":\"%s\",\"size\":%llu,\"browser_download_url\":\"%s\"}]}",
                sums_name, std::strlen(sums), sums_url, arc_name, (unsigned long long)arc_size, arc_url);
  char lj[800];
  std::snprintf(lj, sizeof lj, "%s/latest.json", k.rel);
  if (!write_file(lj, json)) return false;
  file_url(lj, k.url, sizeof k.url);
  return true;
}
} // namespace

ENGINE_TEST(editor_update_cli_exit_codes) {
  char exe[1100];
  if (!editor_exe(exe, sizeof exe)) { skip("engine_editor ikilisi engine_tests'in yaninda yok (Android/paket disi)"); return; }
  static char out[8192];
  // --surum: pencere/Vulkan acmadan, "<surum|kaynak derlemesi> <platform>".
  static Fx f0;
  CHECK(make_fx(f0, "guncelleme_cli0", "v9.9.9"));
  const char *a_surum[] = {"--surum", nullptr};
  int rc = run_editor(exe, f0, a_surum, out, sizeof out);
  std::printf("    [bilgi] --surum -> %d: %s", rc, out);
  CHECK(rc == 0 && std::strstr(out, build_platform()) != nullptr);

  // Yeni surum var: 10.
  const char *a_denetle[] = {"--guncelleme", "denetle", nullptr};
  rc = run_editor(exe, f0, a_denetle, out, sizeof out);
  std::printf("    [bilgi] denetle (fikstur v9.9.9, kurulu v0.0.1) -> %d\n%s", rc, out);
  CHECK(rc == app::kUpdCliAvailable);
  CHECK(std::strstr(out, "v9.9.9") != nullptr && std::strstr(out, "v0.0.1") != nullptr);
  // KONTROL: en son surum kurulu olanin aynisi -> 0 (cikis kodu gercekten olcuyor).
  static Fx f1;
  CHECK(make_fx(f1, "guncelleme_cli1", "v0.0.1"));
  rc = run_editor(exe, f1, a_denetle, out, sizeof out);
  std::printf("    [bilgi] denetle (fikstur v0.0.1 = kurulu) -> %d\n%s", rc, out);
  CHECK(rc == app::kUpdCliUpToDate);
  // Bilinmeyen fiil: 2 (sessiz 0 degil).
  const char *a_kotu[] = {"--guncelleme", "belki", nullptr};
  rc = run_editor(exe, f1, a_kotu, out, sizeof out);
  CHECK(rc == app::kUpdCliUsage);
  // CLI ayar dosyasina yazmaz (kullanici komutu; otomatik denetimin damgasi degil).
  char disk[64];
  char ayar[700];
  std::snprintf(ayar, sizeof ayar, "%s/.tulpar_guncelleme", f0.dir);
  CHECK(slurp(ayar, disk, sizeof disk) == 0);
}

// `--guncelleme kur` uctan uca: GERCEK ikili, file:// surum (tar arsivi +
// SHA256SUMS + latest.json). Olculen sey arayuzun CLI yolu: denetle -> indir ->
// dogrula -> ac -> plan ozeti (korunan kullanici dosyasi listelenir) -> kur, cikis
// 0; kurulum dizininde degismemis dosya yenilendi, kullanicinin dosyasi yerinde,
// yenisi `.yeni`. KONTROL: bilinmeyen fiil 2 doner ve kurulum dizinine dokunmaz.
// Geri alma / bozuk arsiv / manifest kacisi cekirdegin kendi kapilarinda
// (tests/test_updater.cpp); burada olculen, CLI'nin o yolu dogru SURMESI.
ENGINE_TEST(editor_update_cli_kur_end_to_end) {
  char exe[1100];
  if (!editor_exe(exe, sizeof exe)) { skip("engine_editor ikilisi engine_tests'in yaninda yok (Android/paket disi)"); return; }
  static KurFx k;
  if (!make_kur_fx(k)) {
    if (k.why[0]) { skip(k.why); return; }
    std::printf("    FAIL kur fiksturu kurulamadi (%s)\n", k.base);
    CHECK(false);
    return;
  }
  static char out[16384];
  const char *a_kur[] = {"--guncelleme", "kur", nullptr};
  const int rc = run_editor_in(exe, k.inst, k.url, k.base, a_kur, out, sizeof out, 120000);
  std::printf("    [bilgi] kur -> %d\n%s", rc, out);
  CHECK(rc == app::kUpdCliUpToDate);
  CHECK(std::strstr(out, "kuruldu: v0.0.1 -> v0.3.0") != nullptr);
  CHECK(std::strstr(out, "korunuyor: sahne.sahne") != nullptr);
  char buf[256], p[800];
  std::snprintf(p, sizeof p, "%s/OKUBENI.md", k.inst);
  CHECK(slurp(p, buf, sizeof buf) > 0 && !std::strcmp(buf, "yeni okubeni\n"));
  std::snprintf(p, sizeof p, "%s/sahne.sahne", k.inst);
  CHECK(slurp(p, buf, sizeof buf) > 0 && !std::strcmp(buf, "kullanicinin sahnesi\n"));
  std::snprintf(p, sizeof p, "%s/sahne.sahne.yeni", k.inst);
  CHECK(slurp(p, buf, sizeof buf) > 0 && !std::strcmp(buf, "yeni sahne\n"));
  // Bilinmeyen fiilde kurulum dizinine DOKUNULMAZ (kullanim hatasi, 2).
  const char *a_kotu[] = {"--guncelleme", "kurr", nullptr};
  CHECK(run_editor_in(exe, k.inst, k.url, k.base, a_kotu, out, sizeof out, 30000) == app::kUpdCliUsage);
  std::snprintf(p, sizeof p, "%s/OKUBENI.md", k.inst);
  CHECK(slurp(p, buf, sizeof buf) > 0 && !std::strcmp(buf, "yeni okubeni\n"));
}

// Penceresiz editorun KENDI kapisi, ETKIN bir guncelleyiciyle (fikstur paketi,
// otomatik acik, hic denetlenmemis): istek 0, menu yolunun reddi 1.
ENGINE_TEST(editor_update_headless_editor_gate) {
  char exe[1100];
  if (!editor_exe(exe, sizeof exe)) { skip("engine_editor ikilisi engine_tests'in yaninda yok (Android/paket disi)"); return; }
  static Fx f;
  CHECK(make_fx(f, "guncelleme_kapi", "v9.9.9"));
  char ppm[700];
  std::snprintf(ppm, sizeof ppm, "%s/k.ppm", f.dir);
  const char *args[] = {"--headless", "2", "--out", ppm, nullptr};
  static char out[65536];
  const int rc = run_editor(exe, f, args, out, sizeof out, 120000);
  const char *line = std::strstr(out, "[engine_editor] guncelleme kapisi:");
  if (!line) {
    // Editor Vulkan'siz makinede sahneye varmadan cikar: olculemez, GORUNUR atla.
    if (std::strstr(out, "Vulkan loader yok") || std::strstr(out, "instance:") || std::strstr(out, "cihaz:")) {
      skip("penceresiz editor Vulkan olmadan kosamadi (guncelleme kapisi olculemedi)");
      return;
    }
    std::printf("    FAIL kapi satiri yok (rc %d):\n%s\n", rc, out);
    CHECK(false);
    return;
  }
  char satir[512];
  std::snprintf(satir, sizeof satir, "%.*s", (int)(std::strcspn(line, "\n")), line);
  std::printf("    [bilgi] rc %d: %s\n", rc, satir);
  CHECK(rc == 0);
  CHECK(std::strstr(satir, "Updater istegi 0") != nullptr);
  CHECK(std::strstr(satir, "reddedildi evet") != nullptr);
  CHECK(std::strstr(satir, "penceresiz") != nullptr); // karar: penceresiz kip (Disabled DEGIL: fikstur etkin)
  CHECK(std::strstr(satir, " OK") != nullptr);
}
