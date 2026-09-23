#include "app/editor_game.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tulpar::engine::app {

bool game_find_compiler(const char *exe_dir, char *out, uint32_t cap, char *why, uint32_t why_cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (why && why_cap) why[0] = 0;
  const char *env = std::getenv("TULPAR_MOTOR_DERLEYICI");
  if (env && *env) {
    // Acikca istenen derleyici bulunamazsa SESSIZCE digerine gecilmez: kullanici
    // bunu sectiyse neden calismadigini bilmeli.
    if (platform::process_find_in_path(env, out, cap)) return true;
    if (why && why_cap) std::snprintf(why, why_cap, "TULPAR_MOTOR_DERLEYICI=%s bulunamadi ya da calistirilamaz", env);
    return false;
  }
  char aday[1024];
#if defined(_WIN32)
  const int n = std::snprintf(aday, sizeof aday, "%s\\tulpar-motor\\tulpar.exe", exe_dir ? exe_dir : ".");
#else
  const int n = std::snprintf(aday, sizeof aday, "%s/tulpar-motor/tulpar", exe_dir ? exe_dir : ".");
#endif
  if (n > 0 && (size_t)n < sizeof aday && platform::process_find_in_path(aday, out, cap)) return true;
  if (why && why_cap)
    std::snprintf(why, why_cap,
                  "motoru taniyan derleyici yok (%s). Kurulu `tulpar` motoru TANIMIYOR; once tools/motor_derleyici.sh "
                  "calistirin ya da TULPAR_MOTOR_DERLEYICI verin",
                  aday);
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

bool game_run_start(GameRun &r, const char *compiler, const char *tulpar_root, const char *game_rel, const char *log_path, char *err,
                    uint32_t err_cap) {
  if (r.state == GameRunState::Running) {
    if (err && err_cap) std::snprintf(err, err_cap, "oyun zaten calisiyor: %s", r.game);
    return false;
  }
  r = GameRun{};
  std::snprintf(r.log_path, sizeof r.log_path, "%s", log_path ? log_path : "");
  std::snprintf(r.game, sizeof r.game, "%s", game_rel ? game_rel : "");
  const char *argv[] = {compiler, game_rel, nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  s.cwd = tulpar_root;
  s.log_path = r.log_path[0] ? r.log_path : nullptr;
  if (!platform::process_start(r.proc, s, err, err_cap)) {
    r.state = GameRunState::Failed;
    return false;
  }
  r.state = GameRunState::Running;
  return true;
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
  return r.state;
}

bool game_run_stop(GameRun &r) { return r.state == GameRunState::Running && platform::process_kill(r.proc); }

} // namespace tulpar::engine::app
