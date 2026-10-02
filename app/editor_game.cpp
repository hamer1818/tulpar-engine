#include "app/editor_game.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/thread.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::app {

bool game_find_extension(const char *exe_dir, char *out, uint32_t cap, char *why, uint32_t why_cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (why && why_cap) why[0] = 0;
  const char *env = std::getenv("TULPAR_MOTOR_EKLENTI");
  char aday[1024];
  int n;
  if (env && *env) n = std::snprintf(aday, sizeof aday, "%s", env);
  else n = std::snprintf(aday, sizeof aday, "%s/tulpar-ext", exe_dir ? exe_dir : ".");
  if (n <= 0 || (size_t)n >= sizeof aday) return false;
  char bildirim[1100];
  std::snprintf(bildirim, sizeof bildirim, "%s/tulpar-ext.json", aday);
  if (FILE *f = std::fopen(bildirim, "rb")) {
    std::fclose(f);
    if (std::snprintf(out, cap, "%s", aday) < (int)cap) return true;
  }
  if (why && why_cap)
    std::snprintf(why, why_cap,
                  "motorun Tulpar eklenti paketi yok (%s). Motoru derleyin (cmake --build yapi; paket yapi/tulpar-ext) "
                  "ya da TULPAR_MOTOR_EKLENTI verin",
                  bildirim);
  return false;
}

namespace {
// `tulpar --ext <paket> version` 0 ile cikiyor mu: derleyici yerel eklentiyi
// (K303) taniyor ve paketin bildirimi gecerli. Eski bir tulpar `--ext`i
// tanimaz ve paket yolunu kaynak dosya sanip duser. Ust sinir 5 s.
bool compiler_takes_extension(const char *comp, const char *ext_dir, const char *exe_dir, int *exit_code) {
  *exit_code = -1;
  char log[1100];
  std::snprintf(log, sizeof log, "%s/tulpar_eklenti_sonda.log", exe_dir ? exe_dir : ".");
  const char *argv[] = {comp, "--ext", ext_dir, "version", nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  s.log_path = log;
  platform::Process p;
  char err[256];
  if (!platform::process_start(p, s, err, sizeof err)) return false;
  for (int i = 0; i < 5000; i++) {
    const platform::ProcessState st = platform::process_poll(p, exit_code);
    if (st == platform::ProcessState::Exited) return *exit_code == 0;
    if (st == platform::ProcessState::Failed) return false;
    platform::thread_sleep_us(1000);
  }
  platform::process_kill(p);
  for (int i = 0; i < 1000 && platform::process_poll(p, exit_code) == platform::ProcessState::Running; i++) platform::thread_sleep_us(1000);
  return false;
}
} // namespace

bool game_find_compiler(const char *exe_dir, const char *ext_dir, char *out, uint32_t cap, char *why, uint32_t why_cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (why && why_cap) why[0] = 0;
  const char *env = std::getenv("TULPAR_MOTOR_DERLEYICI");
  if (env && *env) {
    // Acikca istenen derleyici bulunamazsa SESSIZCE digerine gecilmez: kullanici
    // bunu sectiyse neden calismadigini bilmeli. Sinanmaz: kullanicinin secimi.
    if (platform::process_find_in_path(env, out, cap)) return true;
    if (why && why_cap) std::snprintf(why, why_cap, "TULPAR_MOTOR_DERLEYICI=%s bulunamadi ya da calistirilamaz", env);
    return false;
  }
  if (!platform::process_find_in_path("tulpar", out, cap)) {
    if (why && why_cap)
      std::snprintf(why, why_cap,
                    "PATH'te `tulpar` yok. TulparLang'i kurun (https://tulparlang.dev; yerel eklenti destekli bir surum) "
                    "ya da TULPAR_MOTOR_DERLEYICI verin");
    return false;
  }
  if (!ext_dir || !*ext_dir) return true;
  int kod = -1;
  if (compiler_takes_extension(out, ext_dir, exe_dir, &kod)) return true;
  if (why && why_cap)
    std::snprintf(why, why_cap,
                  "PATH'teki tulpar (%s) motorun eklenti paketini kullanamiyor (`tulpar --ext %s version` cikis %d; "
                  "ayrinti %s/tulpar_eklenti_sonda.log). Yerel eklenti (--ext) destekli bir TulparLang gerekir: `tulpar update`",
                  out, ext_dir, kod, exe_dir ? exe_dir : ".");
  out[0] = 0;
  return false;
}

GameFindResult game_find_for_scene(const char *tulpar_root, const char *scene_path, char (*out)[content::kScenePathLen], uint32_t cap,
                                   FileEntry *scratch, uint32_t scratch_cap, char *text, uint32_t text_cap) {
  GameFindResult r{};
  if (!tulpar_root || !scene_path || !scratch || !text || text_cap < 2) return r;
  // Aranan: "<taban>.sahneb" — oyun sahneyi derlenmis blob olarak yukler
  // (sahne_yukle("examples/assets/betik_dagitimi.sahneb")). Tirnakla bitmesi
  // SART: "arena.sahneb" araninca "yeni_arena.sahneb" da eslesirdi; onundeki
  // karakter ayrica denetleniyor.
  const char *b = file_path_base(scene_path);
  char igne[160];
  const char *nokta = std::strrchr(b, '.');
  const int bn = nokta ? (int)(nokta - b) : (int)std::strlen(b);
  const int in = std::snprintf(igne, sizeof igne, "%.*s.sahneb\"", bn, b);
  if (in <= 0 || (size_t)in >= sizeof igne) return r;
  const FileTreeResult t = file_list_tree(tulpar_root, ".tpr", scratch, scratch_cap);
  r.ok = t.ok;
  if (!t.ok) return r;
  for (uint32_t i = 0; i < t.count; i++) {
    // `*.test.tpr` oyun degil: kopru testi de sahneleri yukler (arena, salon1)
    // ve sayilsaydi her sahne "iki aday" deyip gereksiz yere soru sorardi.
    const size_t nl = std::strlen(scratch[i].name);
    if (nl >= 9 && !std::strcmp(scratch[i].name + nl - 9, ".test.tpr")) continue;
    char yol[kFilePathLen];
    if (!file_path_join(tulpar_root, scratch[i].name, yol, sizeof yol)) continue;
    FILE *f = std::fopen(yol, "rb");
    if (!f) continue;
    const size_t got = std::fread(text, 1, text_cap - 1, f);
    const bool fazla = got == text_cap - 1 && std::fgetc(f) != EOF;
    std::fclose(f);
    if (fazla) { r.too_big++; continue; } // yarim dosyada aramak yanlis "yok" derdi
    text[got] = 0;
    r.scanned++;
    bool eslesti = false;
    for (const char *p = std::strstr(text, igne); p && !eslesti; p = std::strstr(p + 1, igne))
      if (p == text || p[-1] == '"' || p[-1] == '/' || p[-1] == '\\') eslesti = true;
    if (!eslesti) continue;
    if (r.count < cap) std::snprintf(out[r.count], content::kScenePathLen, "%s", scratch[i].name);
    r.count++;
  }
  return r;
}

namespace {
bool start_common(GameRun &r, const char *compiler, const char *ext_dir, const char *tulpar_root, const char *game_rel, const char *log_path,
                  uint32_t w, uint32_t h, bool embedded, char *err, uint32_t err_cap) {
  if (r.state == GameRunState::Running) {
    if (err && err_cap) std::snprintf(err, err_cap, "oyun zaten calisiyor: %s", r.game);
    return false;
  }
  r.chan.close(); // onceki gomulu oynatmanin eslemesi (surec bitti, kanal kaldi)
  r = GameRun{};
  std::snprintf(r.log_path, sizeof r.log_path, "%s", log_path ? log_path : "");
  std::snprintf(r.game, sizeof r.game, "%s", game_rel ? game_rel : "");
  static char env_kanal[128];
  static char env_ext[2200];
  const char *env[3] = {nullptr, nullptr, nullptr};
  int ne = 0;
  if (ext_dir && *ext_dir) {
    // Eklenti paketi ONCE, kullanicinin kendi TULPAR_EXT_PATH'i arkasinda
    // (ayni adli eklentide ilk gelen kazanir — motorun paketi).
#if defined(_WIN32)
    const char ayrac = ';';
#else
    const char ayrac = ':';
#endif
    const char *onceki = std::getenv("TULPAR_EXT_PATH");
    const int n = (onceki && *onceki) ? std::snprintf(env_ext, sizeof env_ext, "TULPAR_EXT_PATH=%s%c%s", ext_dir, ayrac, onceki)
                                      : std::snprintf(env_ext, sizeof env_ext, "TULPAR_EXT_PATH=%s", ext_dir);
    if (n <= 0 || (size_t)n >= sizeof env_ext) {
      if (err && err_cap) std::snprintf(err, err_cap, "eklenti yolu cok uzun: %s", ext_dir);
      r.state = GameRunState::Failed;
      return false;
    }
    env[ne++] = env_ext;
  }
  if (embedded) {
    char cerr[256];
    if (!r.chan.create(w, h, cerr, sizeof cerr)) {
      if (err && err_cap) std::snprintf(err, err_cap, "gomulu kanal acilamadi: %s", cerr);
      r.state = GameRunState::Failed;
      return false;
    }
    std::snprintf(env_kanal, sizeof env_kanal, "%s=%s", platform::kGameChannelEnv, r.chan.name());
    env[ne++] = env_kanal;
    r.embedded = true;
  }
  const char *argv[] = {compiler, game_rel, nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  s.cwd = tulpar_root;
  s.log_path = r.log_path[0] ? r.log_path : nullptr;
  s.env = ne ? env : nullptr;
  if (!platform::process_start(r.proc, s, err, err_cap)) {
    r.chan.close();
    r.state = GameRunState::Failed;
    return false;
  }
  r.chan.beat();
  r.state = GameRunState::Running;
  return true;
}
} // namespace

bool game_run_start(GameRun &r, const char *compiler, const char *ext_dir, const char *tulpar_root, const char *game_rel,
                    const char *log_path, char *err, uint32_t err_cap) {
  return start_common(r, compiler, ext_dir, tulpar_root, game_rel, log_path, 0, 0, false, err, err_cap);
}
bool game_run_start_embedded(GameRun &r, const char *compiler, const char *ext_dir, const char *tulpar_root, const char *game_rel,
                             const char *log_path, uint32_t w, uint32_t h, char *err, uint32_t err_cap) {
  return start_common(r, compiler, ext_dir, tulpar_root, game_rel, log_path, w, h, true, err, err_cap);
}

namespace {
// Gunluge o ana kadar eklenenleri oku ve TAM satirlari ver. `hepsi`: surec
// bitti, sondaki yarim satiri da ver.
void drain(GameRun &r, void (*on_line)(void *, const char *), void *user, bool hepsi) {
  if (!r.log_path[0]) return;
  FILE *f = std::fopen(r.log_path, "rb");
  if (!f) return;
  if (std::fseek(f, r.log_off, SEEK_SET) != 0) { std::fclose(f); return; }
  // Kare basina ust sinir: gurultulu bir oyun editoru dondurmesin. Kalan bir
  // sonraki karede okunur (hepsi=true iken sinir yok: surec bitti, bosalt).
  char buf[8192];
  size_t toplam = 0;
  for (;;) {
    const size_t n = std::fread(buf, 1, sizeof buf, f);
    if (n == 0) break;
    r.log_off += (long)n;
    toplam += n;
    for (size_t i = 0; i < n; i++) {
      const char c = buf[i];
      if (c == '\r') continue;
      if (c == '\n' || r.part_len + 1 >= sizeof r.part) {
        r.part[r.part_len] = 0;
        if (on_line) on_line(user, r.part);
        r.lines++;
        r.part_len = 0;
        if (c == '\n') continue;
      }
      r.part[r.part_len++] = c;
    }
    if (!hepsi && toplam >= 64 * 1024) break;
  }
  std::fclose(f);
  if (hepsi && r.part_len) {
    r.part[r.part_len] = 0;
    if (on_line) on_line(user, r.part);
    r.lines++;
    r.part_len = 0;
  }
}
} // namespace

GameRunState game_run_poll(GameRun &r, void (*on_line)(void *, const char *), void *user) {
  if (r.state != GameRunState::Running) return r.state;
  if (r.embedded && r.chan.ok()) {
    r.chan.beat(); // "editor burada" — durursa oyun kendini kapatir
    // Oyun baglandi: ad artik gereksiz; iki taraf da coksa /dev/shm'de kalmasin.
    if (r.chan.child_state() != platform::GameChildState::None) r.chan.unlink();
    if (r.stop_ns && !r.killed && platform::now_ns() - r.stop_ns > kGameStopGraceNs) {
      platform::process_kill(r.proc);
      r.killed = true;
    }
  }
  int code = 0;
  const platform::ProcessState ps = platform::process_poll(r.proc, &code);
  if (ps == platform::ProcessState::Running) {
    drain(r, on_line, user, false);
    return r.state;
  }
  // Bitti: ONCE cikis kodu, sonra gunlugun kalani (sira onemli degil ama
  // bosaltma surecin YAZMAYI bitirdigi andan sonra olmali — o an bu an).
  drain(r, on_line, user, true);
  r.exit_code = code;
  r.state = ps == platform::ProcessState::Exited ? GameRunState::Finished : GameRunState::Failed;
  // Gomulu: esleme birakilir. Editor kanaldan gelen kareyi HEMEN kendi
  // hazirlama tamponuna kopyaliyor, yani kimse eslemeye isaretci tutmuyor.
  if (r.embedded) r.chan.close();
  return r.state;
}

bool game_run_stop(GameRun &r) {
  if (r.state != GameRunState::Running) return false;
  if (r.embedded && r.chan.ok() && !r.killed) {
    if (!r.stop_ns) r.stop_ns = platform::now_ns();
    r.chan.request_stop();
    return true;
  }
  return platform::process_kill(r.proc);
}

} // namespace tulpar::engine::app
