#include "app/editor_kod_penceresi.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "app/editor_betik.hpp"
#include "app/editor_files.hpp"
#include "app/editor_masaustu.hpp"
#include "app/editor_surucu.hpp"
#include "app/editor_tercih.hpp"
#include "platform/fs_ops.hpp"
#include "platform/process.hpp"
#include "platform/window.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::app {

size_t kod_penceresi_argv(const char *exe, const char *dosya, const char *proje, const char *kutuphane, const char *ek, const char **argv,
                          size_t argv_cap, char *tampon, size_t tampon_cap) {
  size_t n = 0, w = 0;
  auto ekle = [&](const char *s, size_t len) -> bool {
    if (n + 1 >= argv_cap || w + len + 1 > tampon_cap) return false;
    std::memcpy(tampon + w, s, len);
    tampon[w + len] = 0;
    argv[n++] = tampon + w;
    w += len + 1;
    return true;
  };
  auto ekle_s = [&](const char *s) { return ekle(s, std::strlen(s)); };
  if (!exe || !*exe || !dosya || !*dosya) return 0;
  bool ok = ekle_s(exe) && ekle_s("--kod") && ekle_s(dosya);
  if (ok && proje && *proje) ok = ekle_s("--proje") && ekle_s(proje);
  if (ok && kutuphane && *kutuphane) ok = ekle_s("--kutuphane") && ekle_s(kutuphane);
  if (ok) ok = ekle_s("--size") && ekle_s("1100x800");
  for (const char *p = ek; ok && p && *p;) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *e = p;
    while (*e && *e != ' ') e++;
    ok = ekle(p, (size_t)(e - p));
    p = e;
  }
  if (!ok) return 0;
  argv[n] = nullptr;
  return n;
}

bool kod_penceresi_baslat(const char *dosya, const char *proje, const char *kutuphane, char *err, size_t err_cap) {
  char exe[1100];
  if (!masaustu_exe(exe, sizeof exe)) { std::snprintf(err, err_cap, "\xC3\xA7" "al\xC4\xB1\xC5\x9F" "an program yolu bulunamad\xC4\xB1"); return false; }
  const char *argv[24];
  static char tampon[4096];
  const char *ek = std::getenv("TULPAR_KOD_PENCERESI_EK"); // yalniz kapilar: "--headless 300 --out x.ppm"
  if (!kod_penceresi_argv(exe, dosya, proje, kutuphane, ek, argv, 24, tampon, sizeof tampon)) {
    std::snprintf(err, err_cap, "ba\xC5\x9Flatma arg\xC3\xBCmanlar\xC4\xB1 s\xC4\xB1\xC4\x9Fmad\xC4\xB1");
    return false;
  }
  platform::ProcessSpec s;
  s.argv = argv;
  return platform::process_start_detached(s, err, err_cap);
}

bool kod_istek_dizini(char *out, size_t cap) {
  char t[1024];
  if (!editor_ayar_yolu(t, sizeof t, "kod_istekleri/x", nullptr, nullptr)) return false;
  char *sl = std::strrchr(t, '/');
  if (!sl) return false;
  *sl = 0;
  return std::snprintf(out, cap, "%s", t) < (int)cap;
}

bool kod_istegi_yaz(const char *dizin, const char *dosya_yolu, char *istek_yolu, size_t cap) {
  if (!dizin || !*dizin || !dosya_yolu || !*dosya_yolu || !platform::fs_mkdir_p(dizin)) return false;
  static uint32_t n = 0;
  char yol[1200], gecici[1220];
  std::snprintf(yol, sizeof yol, "%s/istek_%lu_%u.txt", dizin, (unsigned long)platform::fs_process_id(), ++n);
  std::snprintf(gecici, sizeof gecici, "%s.yaz", yol); // yarim yazilmis dosya okunmasin: once gecici, sonra yeniden adlandir
  std::FILE *f = std::fopen(gecici, "wb");
  if (!f) return false;
  std::fprintf(f, "%lld\n%s\n", (long long)std::time(nullptr), dosya_yolu);
  std::fclose(f);
  if (!platform::fs_move(gecici, yol)) { platform::fs_remove_file(gecici); return false; }
  if (istek_yolu && cap) std::snprintf(istek_yolu, cap, "%s", yol);
  return true;
}

namespace {
struct IstekGez { const char *dizin; KodIstekFn fn; void *user; uint32_t tuketilen; char adlar[16][200]; uint32_t n; };
bool istek_gez(const char *ad, platform::FsKind k, void *u) {
  IstekGez *g = static_cast<IstekGez *>(u);
  const size_t l = std::strlen(ad);
  if (k == platform::FsKind::File && l > 10 && !std::strncmp(ad, "istek_", 6) && !std::strcmp(ad + l - 4, ".txt") && g->n < 16 && l < 200)
    std::snprintf(g->adlar[g->n++], sizeof g->adlar[0], "%s", ad);
  return true;
}
} // namespace

uint32_t kod_istekleri_al(const char *dizin, KodIstekFn fn, void *user) {
  if (!dizin || !fn) return 0;
  static IstekGez g;
  g = IstekGez{dizin, fn, user, 0, {{0}}, 0};
  platform::fs_list_dir(dizin, istek_gez, &g);
  for (uint32_t i = 0; i < g.n; i++) {
    char yol[1500], satir1[64] = {0}, hedef[1100] = {0};
    std::snprintf(yol, sizeof yol, "%s/%s", dizin, g.adlar[i]);
    std::FILE *f = std::fopen(yol, "rb");
    if (!f) continue;
    const bool ok = std::fgets(satir1, sizeof satir1, f) && std::fgets(hedef, sizeof hedef, f);
    std::fclose(f);
    if (!ok) continue;
    size_t hl = std::strlen(hedef);
    while (hl && (hedef[hl - 1] == '\n' || hedef[hl - 1] == '\r')) hedef[--hl] = 0;
    const long long t = std::atoll(satir1);
    const long long simdi = (long long)std::time(nullptr);
    if (t <= 0 || simdi - t > 60 || t - simdi > 60 || !hl) { platform::fs_remove_file(yol); continue; } // bayat/bozuk: at
    if (fn(hedef, user)) { platform::fs_remove_file(yol); g.tuketilen++; }
  }
  return g.tuketilen;
}

namespace {
struct Ctx { rhi::Device *dev; EditorUi *ui; };
// Ana gecis (iki subpass): ImGui ikinci subpass'ta (editor_app.cpp record_cb ile ayni sebep).
void kayit(VkCommandBuffer cb, void *user) {
  Ctx *c = static_cast<Ctx *>(user);
  c->dev->api().vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
  c->ui->record(cb);
}
} // namespace

int kod_penceresi_calistir(rhi::Device &dev, rhi::Swapchain *swap, rhi::OffscreenTarget *off, const rhi::OffscreenConfig &oc,
                           rhi::OffscreenResult &ores, EditorUi &ui, const EditorHost *host, const EditorOptions &opts, bool headless) {
  static KodPaneli kod;
  char err[400];
  kod = KodPaneli{};
  if (!kod_ac(kod, opts.kod_dosyasi, "", 0, err, sizeof err)) {
    std::fprintf(stderr, "[engine_editor] --kod: %s\n", err);
    return 3;
  }
  kod.tam_pencere = true;
  kod.ana = false;
  std::snprintf(kod.proje_dizin, sizeof kod.proje_dizin, "%s", opts.kod_proje ? opts.kod_proje : "");
  std::snprintf(kod.kutuphane_dizin, sizeof kod.kutuphane_dizin, "%s", opts.kod_kutuphane ? opts.kod_kutuphane : "");
  {
    char t[1024];
    if (editor_ayar_yolu(t, sizeof t, "kod_penceresi/x", nullptr, nullptr)) {
      if (char *sl = std::strrchr(t, '/')) *sl = 0;
      std::snprintf(kod.gecici_dizin, sizeof kod.gecici_dizin, "%s", t);
    }
  }
  {
    // Kullanicinin tema ve yazi boyutu tercihi (ana editorle ayni dosya): ayrilan pencere farkli gorunmesin.
    Tercihler tc;
    char ty[1024];
    if (editor_ayar_yolu(ty, sizeof ty, "tercihler.txt", nullptr, nullptr)) tercih_oku(ty, &tc);
    editor_tema_ayarla(tc.tema);
    if (tc.yazi != 100 || tc.tema != 0) ui.set_ui_scale((float)tc.yazi / 100.0f);
  }
  std::printf("[engine_editor] kod penceresi: %s (%s)\n", kod.yol, headless ? "penceresiz" : "pencere");
  static platform::InputState g_synth;
  bool running = true;
  uint32_t frame = 0, istek_kare = 0;
  char istek_yolu[1200] = {0};
  uint64_t last_ns = platform::now_ns();
  uint32_t fw = oc.width ? oc.width : opts.width, fh = oc.height ? oc.height : opts.height;
  Ctx ctx{&dev, &ui};
  while (running) {
    const uint64_t now = platform::now_ns();
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless) dt = 1.0f / 60.0f;
    const platform::InputState *in = nullptr;
    if (headless) {
      in = &g_synth;
      if (surucu_acik() && !surucu_kare(g_synth, ores.pixels, oc.width, oc.height)) running = false;
    } else {
      if (!host->poll(host->user, &fw, &fh)) {
        // Isletim sistemi kapatma istedi. Kaydedilmemis degisiklik varsa iptal et ve panelde sor (Kaydet/Kaydetmeden/Vazgec).
        if (kod.kirli && host->cancel_close) { host->cancel_close(host->user); kod.kapat_iste = true; }
        else running = false;
      }
      in = host->input ? host->input(host->user) : nullptr;
      if (fw == 0 || fh == 0) continue; // kucultulmus
      if (swap->sync_size(fw, fh)) std::printf("[engine_editor] swapchain %ux%u\n", swap->extent().width, swap->extent().height);
      fw = swap->extent().width;
      fh = swap->extent().height;
      if (host->window_size) {
        uint32_t ww = 0, wh = 0;
        host->window_size(host->user, &ww, &wh);
        if (ww > 0) ui.set_pointer_scale((float)fw / (float)ww);
      }
    }
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    const KodEylem ke = kod_paneli_ciz(kod, nullptr, false);
    if (ke == KodEylem::DisEditor) {
      char kim[1024], e2[640];
      if (!editor_open_in_code_editor_at(kod.yol, (uint32_t)kod.imlec_satir, kim, sizeof kim, e2, sizeof e2)) std::snprintf(kod.uyari_mesaj, sizeof kod.uyari_mesaj, "d\xC4\xB1\xC5\x9F edit\xC3\xB6r a\xC3\xA7\xC4\xB1lamad\xC4\xB1");
    } else if (ke == KodEylem::EditoreDon && !istek_yolu[0]) {
      // Editore geri al: once kaydet (editor dosyayi diskten okur), sonra istek dosyasi birak; editor tuketince pencere kapanir.
      char e2[300] = {0};
      char dizin[1024];
      if (kod.kirli && !kod_kaydet(kod, e2, sizeof e2)) std::snprintf(kod.uyari_mesaj, sizeof kod.uyari_mesaj, "kaydedilemedi: %.100s", e2);
      else if (!kod_istek_dizini(dizin, sizeof dizin) || !kod_istegi_yaz(dizin, kod.yol, istek_yolu, sizeof istek_yolu))
        std::snprintf(kod.uyari_mesaj, sizeof kod.uyari_mesaj, "istek yaz\xC4\xB1lamad\xC4\xB1");
      else { istek_kare = frame; kod.uyari_mesaj[0] = 0; }
    }
    if (istek_yolu[0]) {
      // Editor dosyayi tuketti mi? (kare sayisiyla zaman asimi: ~3 s pencereli, 180 kare penceresiz — belirlenimli)
      std::FILE *var = std::fopen(istek_yolu, "rb");
      if (!var) { std::printf("[engine_editor] kod penceresi: editore geri alindi\n"); running = false; }
      else {
        std::fclose(var);
        if (frame - istek_kare > 180) {
          platform::fs_remove_file(istek_yolu); // editor yok: istegi geri al, pencere acik kalir
          istek_yolu[0] = 0;
          std::snprintf(kod.uyari_mesaj, sizeof kod.uyari_mesaj, "Tulpar Edit\xC3\xB6r a\xC3\xA7\xC4\xB1k de\xC4\x9Fil ya da bu dosyay\xC4\xB1 \xC5\x9F" "u an a\xC3\xA7" "am\xC4\xB1yor");
        }
      }
    }
    if (!kod.acik) running = false;
    ui.end_frame();
    if (headless) {
      if (!rhi::offscreen_render_custom(off, oc, kayit, &ctx, &ores)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
    } else {
      rhi::FrameContext fc;
      if (swap->acquire(&fc)) {
        swap->begin_render_pass(fc);
        kayit(fc.cmd, &ctx);
        swap->end_frame(fc);
      }
    }
    frame++;
    if (headless && !surucu_acik() && frame >= opts.headless_frames) running = false;
  }
  int kod_sonuc = 0;
  if (headless) {
    kod_sonuc = surucu_bitir();
    if (opts.out_path) {
      if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
      else std::fprintf(stderr, "[engine_editor] goruntu yazilamadi: %s\n", opts.out_path);
    }
  }
  std::printf("[engine_editor] kod penceresi kapandi: %u kare\n", frame);
  return kod_sonuc;
}

} // namespace tulpar::engine::app
