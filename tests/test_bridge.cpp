// Tulpar kopru (bridge/engine_api): Tulpar betiginin gordugu C ABI'nin kendisi
// kosturulur — kurulum (headless), varlik kurma, fizik (zeminli oturur /
// zeminsiz duser: KONTROL), nesil etiketli id (silinen id hata loglar ve 0 doner;
// canli id loglamaz: KONTROL), kare disi HUD cagrisinin reddi, tus adi eslemesi,
// ve PPM ciktisinin gercekten SAHNE icermesi (bos kareyle piksel farki; iki bos
// kare arasinda 0 fark: KONTROL).
//
// Sonradan eklenen yetenekler, hepsi KONTROLLU:
//   sahne kuvveti   dinamik govde durtuyle yer degistirir; KONTROL: sabit govde
//                   kimildamaz ve cagri hata sayacini bir artirir.
//   bolum gecisi    eng_scene_unload sonrasi sahne varligi 0, govde duser, cizim
//                   azalir; ardindan yeniden yukleme basarili (govde geri gelir).
//   animasyon       pozlu cizim statik cizimden FARKLI piksel verir; KONTROL: ayni
//                   poz iki kez cizilince fark 0.
//   ses             calan ses sayaci ve tepe deger artar; KONTROL: durdurunca
//                   ikisi de sifirlanir. Cihaz yoksa GORUNUR atlanir.
//   kodla betik     teng_script_attach (10d): kanca sirasi ve argumanlari, kanca
//                   icinden sil/bagla/uret, havuz tavani; KONTROL: kancasiz ad ve
//                   olu id HATA sayar, sahne sayaci etkilenmez, kapanista
//                   toplam baslat == toplam bitir.
//   ozellikler      teng_scene_prop_* (4.8): ustune yazilmis vs varsayilan (4 tur),
//                   baslat icinden okuma, nokta dunya uzayinda; KONTROL: donuk
//                   ebeveynli nokta donussuzden farkli, eksik ozellik hata SAYMAZ,
//                   tur uyusmazligi / ad kurali / sinir disi sayar, AllocGate 0.
//
// Tek surecte tek motor ornegi var (global baglam): kapilar BIR oturumun icinde
// kosar. Kapanistan sonra ayni surecte yeni oturum kurulabilir (Tuzaklar 8ct);
// onu sondaki bridge_second_session_in_same_process_starts_clean olcer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "bridge/engine_api.h"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_compile.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/memory.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"
#include "rhi/vk_api.hpp"
#if defined(_WIN32)
#include <cstdlib>
// Windows CRT'sinde setenv/unsetenv yok; _putenv_s ayni isi gorur
// ("DEGISKEN=" bos deger = silme).
static inline int setenv(const char *k, const char *v, int) { return _putenv_s(k, v ? v : ""); }
static inline int unsetenv(const char *k) { return _putenv_s(k, ""); }
#endif

#include "bridge/android_lifecycle.hpp"
#include "bridge/asset_filter.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
// PPM (P6) oku: basligi atla, pikselleri say. Donus: piksel sayisi (0 = hata).
uint32_t read_ppm(const char *path, uint8_t *out, uint32_t max_px, uint32_t *w_out, uint32_t *h_out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  char magic[3] = {0};
  unsigned w = 0, h = 0, maxv = 0;
  if (std::fscanf(f, "%2s %u %u %u", magic, &w, &h, &maxv) != 4 || std::strcmp(magic, "P6") != 0 || maxv != 255) { std::fclose(f); return 0; }
  std::fgetc(f); // tek bosluk
  const uint32_t n = w * h;
  if (n == 0 || n > max_px) { std::fclose(f); return 0; }
  const size_t got = std::fread(out, 3, n, f);
  std::fclose(f);
  if (w_out) *w_out = w;
  if (h_out) *h_out = h;
  return got == n ? n : 0;
}
uint32_t diff_px(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t d = 0;
  for (uint32_t i = 0; i < n; i++) {
    const int dr = a[i * 3] - b[i * 3], dg = a[i * 3 + 1] - b[i * 3 + 1], db = a[i * 3 + 2] - b[i * 3 + 2];
    if (dr * dr + dg * dg + db * db > 64) d++;
  }
  return d;
}
void run_frames(int n) {
  for (int i = 0; i < n; i++) { teng_frame_begin(); teng_frame_end(); }
}
constexpr uint32_t kW = 320, kH = 200;
// Varlik dizini: kaynak yollari SAHNE DOSYASININ yanindan cozulur, bu yuzden
// gecici blob da oraya yazilir (/tmp'ye yazsak glTF'ler bulunamazdi).
const char *assets_dir() {
  const char *d = std::getenv("TULPAR_ENGINE_ASSETS");
  if (d && *d) return d;
  static char buf[512];
  std::snprintf(buf, sizeof buf, "%s/tests/assets", ENGINE_SOURCE_DIR);
  return buf;
}

// KODLA BAGLANAN betikler icin sahte VM (10d). Tulpar'a gerek yok: olculen
// sey MOTORUN kimi, ne zaman, hangi argumanlarla cagirdigi. Her cagri sabit bir
// kayda duser (STL yok, ayirma yok); birkac "betik" motoru kanca ICINDEN
// yeniden cagirir — sil / bagla / uret yeniden giris yollarini kosturmak icin.
namespace kod {
struct Cagri {
  char fn[40];
  double a[8];
  int argc;
  int kare;  // teng_frame() cagri aninda
  int canli; // teng_alive(id) cagri aninda (bitir'de 1 olmali)
};
constexpr int kMax = 8192; // ~0.9 MB statik; tasma sayilir ve kapi onu 0 bekler
Cagri kayit[kMax];
int n = 0, tasma = 0;
bool kaydet = true;
int baslat = 0, bitir = 0; // kayittan bagimsiz toplamlar (her baslat'a bir bitir)
int dogan = 0, dogurgan_kare = -1, intihar_ata = -1;
// Her betikte baslat + bitir var: "her baslat'a tam bir bitir" invaryanti
// toplamlarla olculebilsin (kapanista baslat == bitir).
const char *const kVar[] = {"dusman_baslat",     "dusman_guncelle",   "dusman_carpisma", "dusman_bitir",     "dusman_bolge_girdi",
                            "dusman_bolge_cikti", "tuzak_baslat",      "tuzak_tetik_girdi", "tuzak_tetik_cikti", "tuzak_bitir",
                            "iz_baslat",          "iz_bitir",          "intihar_baslat",  "intihar_guncelle",  "intihar_bitir",
                            "dogurgan_baslat",    "dogurgan_guncelle", "dogurgan_bitir"};
int has(const char *fn) {
  for (const char *v : kVar)
    if (std::strcmp(fn, v) == 0) return 1;
  return 0; // "yok_*" ve digerleri: kanca YOK yolu
}
int call(const char *fn, const double *a, int argc) {
  const int id = argc > 0 ? (int)a[0] : 0;
  if (std::strstr(fn, "_baslat")) baslat++;
  if (std::strstr(fn, "_bitir")) bitir++;
  if (kaydet) {
    if (n < kMax) {
      Cagri &c = kayit[n++];
      std::snprintf(c.fn, sizeof c.fn, "%s", fn);
      for (int i = 0; i < 8; i++) c.a[i] = i < argc ? a[i] : -999;
      c.argc = argc;
      c.kare = teng_frame();
      c.canli = teng_alive(id);
    } else tasma++;
  }
  // Yeniden giris yollari (kanca icinden motor):
  if (!std::strcmp(fn, "intihar_guncelle")) teng_despawn(id);            // guncelle dongusunde kendini sil
  else if (!std::strcmp(fn, "intihar_bitir")) {                           // bitir icinde: kendine bagla (RED) + kendini sil (yok sayilir)
    intihar_ata = teng_script_attach(id, "dusman");
    teng_despawn(id);
  } else if (!std::strcmp(fn, "dogurgan_guncelle") && !dogan) {           // guncelle dongusunde URET + BAGLA
    dogan = teng_spawn_sphere(teng_x(id) + 2.0, 0.9, teng_z(id), 0.4, 1, 0x30c030ff);
    teng_script_attach(dogan, "dusman");
    dogurgan_kare = teng_frame();
  }
  return 1;
}
int say(const char *fn, int id) {
  int k = 0;
  for (int i = 0; i < n; i++)
    if (!std::strcmp(kayit[i].fn, fn) && (id == 0 || (int)kayit[i].a[0] == id)) k++;
  return k;
}
int ilk(const char *fn, int id, int from = 0) {
  for (int i = from; i < n; i++)
    if (!std::strcmp(kayit[i].fn, fn) && (id == 0 || (int)kayit[i].a[0] == id)) return i;
  return -1;
}
// Eski yol (resolve/invoke YOK): motor kancayi `has` ile sorar, adla `call` eder.
const TengScriptVm vm{has, call, nullptr, nullptr};
} // namespace kod

// KANCA HIZLI YOLU icin sahte VM'ler (10c2). Ayni "betikler" iki VM'den gecer:
//   eski  {has, call}                  motor adi her cagrida kurar
//   hizli {has, call, resolve, invoke}  motor YUKLEMEDE cozer, kare icinde isaretciyle cagirir
// `resolve` tablo indisini opak isaretci olarak verir, `invoke` onu ada geri
// cevirip AYNI kayda yazar: iki kosumun kaydi ayni olmali. Kayit STL'siz,
// sabit dizi; tasma sayilir ve kapi onu 0 bekler.
namespace hiz {
struct Kayit {
  char fn[32];
  double a[8];
  int argc;
  int kare; // kosumun ilk karesine gore
};
constexpr int kMax = 3000;
struct Iz {
  Kayit k[kMax];
  int n = 0, tasma = 0;
  int ids[16]; // bu kosumda uretilen kopru id'leri (normallestirme)
  int id_n = 0;
};
Iz iz[3];     // 0 eski, 1 hizli, 2 hizli + BIR kanca dusuruldu (pozitif kontrol)
int aktif = 0;
int kare0 = 0;
// Hangi kanca "var" ve hizli yolda hangi ariteyle. -1: arite bilinmiyor (dlsym
// yedegi), motor argc'yi gecirir. Aritenin cagriyi etkilemesi sahte VM'in isi
// degil (uretilmis eng_script_invoke'un isi); burada yalniz motorun resolve'un
// verdigi ariteyi invoke'a AYNEN tasidigi olculur.
struct Tanim { const char *ad; int arite; };
const Tanim kTanim[] = {
    {"hdev_baslat", 1},      {"hdev_guncelle", 2},     {"hdev_carpisma", 7},    {"hdev_bolge_girdi", 2},
    {"hdev_bolge_cikti", 2}, {"hdev_bitir", 1},        {"halarm_baslat", 1},    {"halarm_tetik_girdi", 3},
    {"halarm_tetik_cikti", 3}, {"halarm_bitir", -1},   {"kdev_baslat", 1},      {"kdev_guncelle", 2},
    {"kdev_carpisma", 8},    {"kdev_bolge_girdi", 3},  {"kdev_bolge_cikti", 3}, {"kdev_bitir", -1},
    {"ktuzak_tetik_girdi", 3}, {"ktuzak_tetik_cikti", 3}, {"ktuzak_bitir", 1},
    {"hdokuz_guncelle", 9}, // tavan disi: motor hizli yolda REDDETMELI (HATA)
};
constexpr int kTanimN = (int)(sizeof kTanim / sizeof kTanim[0]);
const char *dusur = nullptr; // pozitif kontrol: hizli VM bu kancayi "yok" der
int has_n = 0, call_n = 0, resolve_n = 0, invoke_n = 0, arite_uyumsuz = 0;
int bul(const char *fn) {
  for (int i = 0; i < kTanimN; i++)
    if (!std::strcmp(kTanim[i].ad, fn)) return i;
  return -1;
}
void kaydet(const char *fn, const double *a, int argc) {
  Iz &z = iz[aktif];
  if (z.n >= kMax) { z.tasma++; return; }
  Kayit &c = z.k[z.n++];
  std::snprintf(c.fn, sizeof c.fn, "%s", fn);
  for (int i = 0; i < 8; i++) c.a[i] = i < argc ? a[i] : -999;
  c.argc = argc;
  c.kare = teng_frame() - kare0;
}
int has(const char *fn) { has_n++; return bul(fn) >= 0 ? 1 : 0; }
int call(const char *fn, const double *a, int argc) { call_n++; kaydet(fn, a, argc); return 1; }
void *resolve(const char *fn, int *arity) {
  resolve_n++;
  const int i = bul(fn);
  if (i < 0 || (dusur && !std::strcmp(dusur, fn))) { if (arity) *arity = -1; return nullptr; }
  if (arity) *arity = kTanim[i].arite;
  return (void *)(uintptr_t)(i + 1); // opak: motor ICINE bakmaz
}
int invoke(void *fn, int arity, const double *a, int argc) {
  invoke_n++;
  const int i = (int)(uintptr_t)fn - 1;
  if (i < 0 || i >= kTanimN) { arite_uyumsuz++; return 0; }
  if (arity != kTanim[i].arite) arite_uyumsuz++;
  kaydet(kTanim[i].ad, a, argc);
  return 1;
}
const TengScriptVm eski{has, call, nullptr, nullptr};
const TengScriptVm hizli{has, call, resolve, invoke};
// Karsilastirma bicimi: kopru id'leri (nesil kosumdan kosuma degisir) kosumun
// uretim sirasina, carpisma `olay` indisi (halka SIRASI belirlenimli degil,
// Tuzaklar 8cc) 0'a cekilir; sonra her kare kendi icinde (ad, argumanlar) ile
// siralanir — ayni karedeki sahne carpisma kancalari halka sirasiyla gelir.
void normallestir(Iz &z) {
  for (int r = 0; r < z.n; r++) {
    Kayit &c = z.k[r];
    for (int i = 0; i < c.argc && i < 8; i++) {
      if (c.a[i] < 65536.0) continue;
      for (int k = 0; k < z.id_n; k++)
        if ((int)c.a[i] == z.ids[k]) { c.a[i] = 1e7 + k; break; }
    }
    if (std::strstr(c.fn, "_carpisma") && c.argc > 2) c.a[2] = 0;
  }
  std::sort(z.k, z.k + z.n, [](const Kayit &x, const Kayit &y) {
    if (x.kare != y.kare) return x.kare < y.kare;
    const int c = std::strcmp(x.fn, y.fn);
    if (c) return c < 0;
    if (x.argc != y.argc) return x.argc < y.argc;
    for (int i = 0; i < 8; i++)
      if (x.a[i] != y.a[i]) return x.a[i] < y.a[i];
    return false;
  });
}
// Ilk farkli kaydin sirasi; -1 ayni.
int ilk_fark(const Iz &x, const Iz &y) {
  const int n = x.n < y.n ? x.n : y.n;
  for (int r = 0; r < n; r++) {
    const Kayit &p = x.k[r], &q = y.k[r];
    if (p.kare != q.kare || std::strcmp(p.fn, q.fn) || p.argc != q.argc) return r;
    for (int i = 0; i < 8; i++)
      if (p.a[i] != q.a[i]) return r;
  }
  return x.n == y.n ? -1 : n;
}
int say(const Iz &z, const char *onek) {
  int k = 0;
  for (int r = 0; r < z.n; r++)
    if (!std::strncmp(z.k[r].fn, onek, std::strlen(onek))) k++;
  return k;
}
// Tek kosum: VM'i kur, sahneyi yukle, kopru varliklarini uret + bagla, `kare`
// kare kostur, sil + bosalt. Kayit iz[hangi]'ye. Uc kosum AYNI sirayla ayni
// cagrilari yapar; sahne ve kopru govdeleri her kosumda sifirdan kurulur.
// Dinamik-dinamik temas BILEREK yok: Jolt o ciftte govde1'i kimlik sirasiyla
// secer ve kimlikler kosumdan kosuma degisir (temas noktasi karsi yuzeye
// gecerdi). Butun temaslar dinamik kure -> sabit sahne zemini.
void kosum(int hangi, const TengScriptVm *vm, const char *blob, int kare) {
  aktif = hangi;
  Iz &z = iz[hangi];
  z.n = z.tasma = z.id_n = 0;
  kare0 = teng_frame();
  if (vm->resolve) teng_set_script_vm_v2(vm); // uretilmis baglamanin kurulumu
  else teng_set_script_vm(vm);
  CHECK(teng_scene_load(blob) == 1);
  // K1 sahne kulesinden (halarm) gecip sahne zeminine duser; K3 kodla
  // kurulan tetigin (K2, ktuzak) icinden gecip zemine duser.
  const int k1 = teng_spawn_sphere(304, 4.0, 300, 0.4, 1, 0xff0000ff);
  const int k2 = teng_spawn_trigger_box(296, 1.5, 300, 1.0, 0.5, 1.0);
  const int k3 = teng_spawn_sphere(296, 4.5, 300, 0.4, 1, 0x00ff00ff);
  z.ids[z.id_n++] = k1;
  z.ids[z.id_n++] = k2;
  z.ids[z.id_n++] = k3;
  CHECK(k1 && k2 && k3);
  CHECK(teng_script_attach(k1, "kdev") == 1 && teng_script_attach(k2, "ktuzak") == 1 && teng_script_attach(k3, "kdev") == 1);
  run_frames(kare);
  teng_despawn(k1);
  teng_despawn(k2);
  teng_despawn(k3);
  teng_scene_unload();
}
} // namespace hiz
} // namespace

ENGINE_TEST(bridge_runs_a_scripted_game_headless) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  char shot_scene[512], shot_empty[512], shot_empty2[512];
  tmp_template(shot_scene, sizeof shot_scene, "kopru_sahne");
  tmp_template(shot_empty, sizeof shot_empty, "kopru_bos");
  tmp_template(shot_empty2, sizeof shot_empty2, "kopru_bos2");
  char *shots[3] = {shot_scene, shot_empty, shot_empty2};
  for (int i = 0; i < 3; i++) { const int fd = mkstemp(shots[i]); if (fd >= 0) close(fd); }

  teng_log_level(1); // kapi ciktisi sessiz kalsin; HATA yine gorunur
  teng_set_headless(100000, nullptr); // kare sinirina takilma: donguyu test surer
  // Govde eslemesi DENETIMI bu oturumun TAMAMINDA acik: her carpisma / tetik /
  // isin eslemesi eski dogrusal taramayla da yapilir ve fark HATA sayar — yani
  // asagidaki butun bolumler eslemeyi de olcuyor (10c2 toplami okur).
  setenv("TULPAR_ENGINE_GOVDE_DENETIM", "1", 1);
  const int ok = teng_init("kopru kapisi", kW, kH);
  unsetenv("TULPAR_ENGINE_GOVDE_DENETIM"); // yalniz teng_init'te okunur
  if (!ok) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  CHECK(teng_headless() == 1);
  CHECK(teng_running() == 1);
  CHECK(teng_width() == (int)kW && teng_height() == (int)kH);
  std::printf("    [bilgi] GPU: %s, %dx%d\n", teng_gpu_name(), teng_width(), teng_height());
  const int err0 = teng_error_count();

  // --- 1) Bos kare (kontrol icin): hicbir varlik yok ---
  teng_camera(0, 4, 10, 0, 0.5, 0);
  run_frames(2);
  CHECK(teng_screenshot(shot_empty) == 1);
  run_frames(1);
  CHECK(teng_screenshot(shot_empty2) == 1);

  // --- 2) Sahne: zemin + dusen kutu + kure + isik ---
  const int zemin = teng_spawn_ground(8.0, 0xCED4DAFF);
  const int kutu = teng_spawn_box(0, 4.0, 0, 0.5, 0.5, 0.5, 1, 0xE63946FF);
  const int kure = teng_spawn_sphere(2.0, 1.0, 0, 0.5, 0, 0x2A9D8FFF);
  const int isik = teng_spawn_light(0, 3, 2, 0xF77F00FF, 3.0, 8.0);
  CHECK(zemin > 0 && kutu > 0 && kure > 0 && isik > 0);
  CHECK(teng_count() == 4);
  CHECK(teng_alive(kutu) == 1);
  CHECK(teng_is_dynamic(kutu) == 1 && teng_is_dynamic(kure) == 0);
  // Ayni yuva, farkli varlik: id'ler cakismaz.
  CHECK(kutu != kure && kutu != zemin);

  // Fizik: 2 s (120 kare) sonra kutu zemine oturur (yarim kenar 0.5 -> y ~0.5).
  run_frames(120);
  const double y_zeminli = teng_y(kutu);
  CHECK(y_zeminli > 0.3 && y_zeminli < 0.8);
  CHECK(teng_body_count() == 3); // zemin + kutu + kure (isik govdesiz)
  CHECK(teng_light_count() == 1);
  CHECK(teng_draw_count() == 3); // zemin + kutu + kure (isik cizim degil)
  CHECK(teng_screenshot(shot_scene) == 1);

  // --- 3) KONTROL: zemin silinince ayni kutu duser ---
  teng_despawn(zemin);
  CHECK(teng_alive(zemin) == 0);
  CHECK(teng_count() == 3);
  teng_set_pos(kutu, 0, 4.0, 0); // isinla: govde yeniden kurulur, hiz sifir
  CHECK(std::fabs(teng_vy(kutu)) < 0.001);
  run_frames(120);
  const double y_zeminsiz = teng_y(kutu);
  CHECK(y_zeminsiz < -3.0);
  std::printf("    [bilgi] kutu y: zeminli %.3f, zeminsiz %.3f (2 s)\n", y_zeminli, y_zeminsiz);

  // --- 4) Hiz / itme ---
  teng_set_velocity(kutu, 0, 0, 0);
  teng_set_pos(kutu, 0, 2.0, 0);
  teng_impulse(kutu, 3.0, 0, 0);
  CHECK(teng_vx(kutu) > 2.5);
  run_frames(30);
  CHECK(teng_x(kutu) > 0.5); // +x'e gitti

  // --- 4.5) Derlenmis sahne (.sahneb): editor -> engine_sahnec -> oyun yolu ---
  // Sahne metni burada derlenir (engine_sahnec'in yaptigi is), varlik dizinine
  // yazilir ve KOPRU uzerinden yuklenir: modeller, isiklar ve govdeler gelir.
  // Kapi bayraklari: kasitli hata sayisi hangi bolumlerin kostuguna bagli.
  bool sahne_kapisi = false, anim_kapisi = false, ses_kapisi = false;
  int ortam_hatasi = 0; // ortamdan gelen (kasitsiz) hatalar: cihaz/kaynak yok
  int oz_hata = 0;      // 4.8 nesne ozellikleri: kasitli hatalar (orada tek tek sayilir)
  const TengScriptVm *sahte_vm = nullptr; // 4.5c'nin VM'i (4.8 kendi VM'ini kurup geri koyar)
  {
    static SystemArena arena;
    if (arena.capacity() == 0) arena.reserve(64u << 20, "bridge_scene_test");
    char src[768], blob[768];
    std::snprintf(src, sizeof src, "%s/editor.sahne", assets_dir());
    std::snprintf(blob, sizeof blob, "%s/_kopru_test.sahneb", assets_dir());
    static content::SceneDesc desc;
    content::SceneError serr{};
    const bool authored = content::scene_load(arena, src, &desc, &serr);
    if (!authored) std::printf("    [bilgi] sahne kaynagi yok (%s): %s\n", src, serr.msg);
    // KAPALI bir betik BELLEKTE ekleniyor, editor.sahne'ye DEGIL: o dosya
    // "kanonik metin" kapisinin oznesi ve buraya bir satir daha koymak bu
    // testin ihtiyacini paylasilan bir fixture'a tasirdi. Amac dar: "atanmis
    // ama kapali" durumunu kosturmak.
    for (uint32_t i = 0; i < desc.entity_count; i++) {
      if (std::strcmp(desc.entities[i].name, "kure_1") != 0) continue;
      desc.entities[i].components |= content::kSceneScript;
      std::snprintf(desc.entities[i].script_file, sizeof desc.entities[i].script_file, "tulpar/examples/engine_arena.tpr");
      desc.entities[i].script_enabled = false;
    }
    if (authored && content::scene_blob_save(arena, desc, blob, &serr)) {
      sahne_kapisi = true;
      const int draws_before = teng_draw_count();
      const int bodies_before = teng_body_count();
      const int errs_scene = teng_error_count();
      CHECK(teng_scene_load(blob) == 1);
      CHECK(teng_error_count() == errs_scene); // KONTROL: basarili yukleme hata uretmez
      CHECK(teng_scene_count() == (int)desc.entity_count);
      const int kup = teng_scene_find("kup_dusen");
      const int duvar = teng_scene_find("duvar_sabit");
      CHECK(kup >= 0 && duvar >= 0);
      CHECK(teng_scene_find("olmayan_varlik") == -1);
      CHECK(std::strcmp(teng_scene_name(kup), "kup_dusen") == 0);
      if (kup >= 0 && duvar >= 0) {
        const double kup_y0 = teng_scene_y(kup), duvar_y0 = teng_scene_y(duvar);
        run_frames(90);
        const double kup_y1 = teng_scene_y(kup), duvar_y1 = teng_scene_y(duvar);
        // Dinamik sahne govdesi sim'den gelir (duser), sabit olan yazar konumunda kalir.
        CHECK(kup_y1 < kup_y0 - 1.0);
        CHECK(duvar_y1 == duvar_y0);
        std::printf("    [bilgi] sahne: %d varlik, kup_dusen y %.2f -> %.2f, duvar sabit %.2f\n", teng_scene_count(), kup_y0, kup_y1, duvar_y1);
      }
      // Sahne cizimleri ve govdeleri kopruye eklendi.
      run_frames(1);
      CHECK(teng_draw_count() > draws_before);
      CHECK(teng_body_count() > bodies_before);
      // KONTROL: ikinci yukleme ve olmayan dosya reddedilir (her biri bir hata).
      const int errs_dup = teng_error_count();
      CHECK(teng_scene_load(blob) == 0);
      CHECK(teng_error_count() == errs_dup + 1);
      CHECK(teng_scene_load("/olmayan/dizin/x.sahneb") == 0);
      CHECK(teng_error_count() == errs_dup + 2);
      // Sinir disi sahne dizini de hata verir, 0 doner.
      CHECK(teng_scene_x(9999) == 0.0);
      CHECK(teng_error_count() == errs_dup + 3);

      // --- 4.5c) BETIK YASAM DONGUSU: motor betigi CAGIRIYOR mu? ---------
      // Sahte bir betik VM'i kuruluyor: Tulpar'a hic ihtiyac yok, olculen sey
      // MOTORUN davranisi. Kayit tutuyor, boylece "cagrildi mi" sorusu
      // dolayli degil DOGRUDAN yanitlaniyor.
      {
        static int n_baslat = 0, n_guncelle = 0, n_bitir = 0, n_carpisma = 0, n_yok = 0;
        static char son[160];
        static double son_carp[8] = {0};
        static int son_carp_argc = -1, carp_indis_hatali = 0;
        // Tetik: sira + argumanlar. "T+"/"T-" bolgenin, "B+"/"B-" girenin kancasi.
        static char tetik_iz[128];
        static double tetik_arg[4][3];
        static int n_tetik = 0;
        tetik_iz[0] = 0; n_tetik = 0;
        n_baslat = 0; n_guncelle = 0; n_bitir = 0; n_carpisma = 0; n_yok = 0; son[0] = 0;
        son_carp_argc = -1; carp_indis_hatali = 0;
        struct Sahte {
          static int has(const char *fn) {
            // "henuz_yazilmadi_*" BILEREK yok: eksik kanca yolunu olcuyoruz.
            if (std::strstr(fn, "henuz_yazilmadi")) { n_yok++; return 0; }
            // tetik_* YALNIZ "alarm" tabaninda: baska (tetik olmayan) bir
            // varlikta tetik_* bulunsaydi motor bunu hata olarak bildirir —
            // dogru davranis, ama bu kapinin kasitli hata sayacini bozardi.
            if (std::strstr(fn, "_tetik_")) return std::strncmp(fn, "alarm_", 6) == 0 ? 1 : 0;
            if (std::strstr(fn, "_bolge_")) return 1;
            return std::strstr(fn, "_baslat") || std::strstr(fn, "_guncelle") || std::strstr(fn, "_bitir") || std::strstr(fn, "_carpisma") ? 1 : 0;
          }
          static int call(const char *fn, const double *args, int argc) {
            std::snprintf(son, sizeof son, "%s(%d)", fn, argc);
            if (std::strstr(fn, "_baslat")) { n_baslat++; if (argc != 1) { std::printf("    FAIL baslat argc=%d (1 olmali)\n", argc); Registry::failures++; } }
            if (std::strstr(fn, "_guncelle")) { n_guncelle++; if (argc != 2) { std::printf("    FAIL guncelle argc=%d (2 olmali)\n", argc); Registry::failures++; } }
            if (std::strstr(fn, "_bitir")) { n_bitir++; if (argc != 1) { std::printf("    FAIL bitir argc=%d (1 olmali)\n", argc); Registry::failures++; } }
            if (std::strstr(fn, "_carpisma")) {
              n_carpisma++;
              son_carp_argc = argc;
              for (int i = 0; i < argc && i < 8; i++) son_carp[i] = args[i];
              if (argc != 7) { std::printf("    FAIL carpisma argc=%d (7 olmali)\n", argc); Registry::failures++; }
              // POZITIF KONTROL, kanca ICINDEN: `olay` bir halka indisi ve
              // SOZLESME o indisin kanca suresince okunabilir olmasi. Ayni
              // temas noktasi iki yoldan geliyor (arguman + halka); tutmazsa
              // indis yanlis ve `eng_carpisma_nx(olay)` alakasiz bir olayi
              // okurdu — sessizce, cunku her ikisi de gecerli sayi.
              else if (teng_collision_x((int)args[2]) != args[3]) carp_indis_hatali++;
            }
            const bool tg = std::strstr(fn, "_tetik_girdi"), tc = std::strstr(fn, "_tetik_cikti");
            const bool bg = std::strstr(fn, "_bolge_girdi"), bc = std::strstr(fn, "_bolge_cikti");
            if (tg || tc || bg || bc) {
              const size_t l = std::strlen(tetik_iz);
              std::snprintf(tetik_iz + l, sizeof tetik_iz - l, "%s%s ", tg || tc ? "T" : "B", tg || bg ? "+" : "-");
              if (n_tetik < 4) for (int i = 0; i < 3; i++) tetik_arg[n_tetik][i] = i < argc ? args[i] : -99;
              n_tetik++;
              const int bekle = tg || tc ? 3 : 2;
              if (argc != bekle) { std::printf("    FAIL %s argc=%d (%d olmali)\n", fn, argc, bekle); Registry::failures++; }
            }
            (void)args;
            return 1;
          }
        };
        // STATIK: motor isaretciyi kopyalamiyor ve bu blok bitince de kurulu
        // kaliyor ("sonraki bolumler icin geri kur"). Ilk yazimda yigindaydi:
        // blok kapaninca sonraki her kare, kapsami bitmis bir yapinin islev
        // isaretcilerini okuyordu (tanimsiz davranis; sans eseri bozulmadi).
        static const TengScriptVm vm{Sahte::has, Sahte::call, nullptr, nullptr}; // eski yol: resolve/invoke yok
        sahte_vm = &vm;
        const int errs_hook = teng_error_count();
        teng_scene_unload();
        teng_set_script_vm(&vm);
        CHECK(teng_scene_load(blob) == 1);
        // kup_dusen ETKIN betikli, kure_1 KAPALI, digerleri bilesensiz.
        // Yani tam BIR varlik icin baslat beklenir.
        CHECK(n_baslat == 1);
        CHECK(teng_script_hooks_active() == 1);
        // KONTROL: kapali betik cozulmuyor — kure_1 icin hic sorulmadi.
        // (Sorulsaydi n_baslat 2 olurdu.)
        const int b0 = n_guncelle;
        run_frames(5);
        std::printf("    [bilgi] betik yasam dongusu: baslat %d, guncelle %d (5 kare), eksik %d, son cagri \"%s\"\n", n_baslat,
                    n_guncelle - b0, teng_script_missing_count(), son);
        // Her kare TAM bir guncelle: az olsa kancalar duruyor, cok olsa
        // birden fazla yerden cagriliyor demektir.
        CHECK(n_guncelle - b0 == 5);
        CHECK(n_baslat == 1); // baslat TEKRARLANMADI
        CHECK(teng_script_call_count() >= 6);

        // --- carpisma kancasi: fizik olayi betige ULASIYOR mu? ------------
        // editor.sahne'de zemin YOK (kup_dusen sonsuza duser), o yuzden
        // carpacak seyi TEST KURUYOR: olayin ne zaman olustugu sahnenin
        // tesadufune degil buradaki duzene bagli olsun.
        const int kupi = teng_scene_find("kup_dusen");
        CHECK(kupi >= 0);
        CHECK(n_carpisma == 0); // KONTROL: carpmadan ONCE sifir
        const int zemin = teng_spawn_box(teng_scene_x(kupi), teng_scene_y(kupi) - 1.5, teng_scene_z(kupi), 1.5, 0.5, 1.5, 0, 0x606060);
        CHECK(zemin != 0);
        teng_scene_set_velocity(kupi, 0, -20, 0); // 1 m'lik bosluk birkac karede kapanir
        int carp_kare = 0;
        while (n_carpisma == 0 && carp_kare < 240) { teng_frame_begin(); teng_frame_end(); carp_kare++; }
        std::printf("    [bilgi] carpisma kancasi: %d karede %d cagri, argc %d, id %.0f, diger %.0f, olay %.0f, nokta y %.2f, hiz %.2f\n",
                    carp_kare, n_carpisma, son_carp_argc, son_carp[0], son_carp[1], son_carp[2], son_carp[4], son_carp[6]);
        CHECK(n_carpisma > 0);
        CHECK(son_carp_argc == 7);
        CHECK((int)son_carp[0] == kupi); // kanca DOGRU varliga gitti
        // Karsi taraf bir KOPRU varligi (sahnede degil) -> -1. Bu ayni
        // zamanda "her govde sahne varligidir" varsayiminin kontrolu.
        CHECK((int)son_carp[1] == -1);
        CHECK(carp_indis_hatali == 0);
        teng_despawn(zemin);
        // Guncelle sayaci carpisma karelerinde de artti; sonraki olcumler
        // taze bir tabandan baslasin.
        run_frames(1);
        // KONTROL: EKSIK kanca sessiz kalmiyor — hata sayaci artti.
        // editor.sahne'de bilerek yazilmamis bir betik YOK, o yuzden bu
        // kontrol bellekte eklenen varlikla yapiliyor (asagida).
        CHECK(teng_error_count() == errs_hook);
        // --- bitir kancasi: sahne bosalirken betik haber aliyor mu? ------
        // Bosaltma YIKIMDAN once cagirmali; kanca icinde sahne hala okunur
        // olmali. Burada olculen sey sayilar, o sozlesmenin kendisi degil —
        // onu motor tarafindaki yorum tasiyor.
        const int bitir0 = n_bitir;
        teng_scene_unload();
        std::printf("    [bilgi] bitir kancasi: bosaltmada %d cagri (1 olmali: tek etkin betikli varlik)\n", n_bitir - bitir0);
        CHECK(n_bitir - bitir0 == 1);
        CHECK(teng_script_hooks_active() == 0); // tablo KAPANDI
        // VM'i KALDIR: kancalar susmali (eski davranis geri gelmeli).
        teng_set_script_vm(nullptr);
        CHECK(teng_scene_load(blob) == 1);
        const int b1 = n_guncelle;
        run_frames(5);
        std::printf("    [bilgi] KONTROL VM kaldirildi: 5 karede guncelle %d (0 olmali), kancalar etkin %d\n", n_guncelle - b1,
                    teng_script_hooks_active());
        CHECK(n_guncelle - b1 == 0 && teng_script_hooks_active() == 0);
        // KONTROL: VM yokken bosaltma `bitir` de CAGIRMAZ (kanca cozulmedi).
        // Bu, yukaridaki 1'in "her bosaltmada bir kez" degil "cozulmus kanca
        // varsa bir kez" oldugunu gosteriyor.
        const int bitir1 = n_bitir;
        // SIRA ONEMLI: once bosalt, SONRA VM'i kur. Tersi motorun kendi
        // uyarisini tetikliyor ("sahne zaten yuklu, kancalar cozulmedi") —
        // ve bu testin ilk yaziminda tam olarak oyle oldu, kasitli hata
        // sayaci 16/15 dedi. Kapi kendi kuralini kendi uzerimde olctu.
        teng_scene_unload();
        CHECK(n_bitir == bitir1);

        // --- tetik / bolge kancalari -------------------------------------
        // kup_dusen'in dusus yoluna bir TETIK hacmi (y 1.5..2.5) BELLEKTE
        // ekleniyor (editor.sahne kanonik metin kapisinin oznesi). Kup
        // (yarim kenar 0.5) y=6'dan duser: alt yuzu 2.5'e inince girer, ust
        // yuzu 1.5'in altina inince cikar. Zemin yok, kup durmadan gecer —
        // sensor tepki vermiyorsa.
        {
          static content::SceneDesc tet;
          tet = desc;
          content::SceneEntity al{};
          std::snprintf(al.name, sizeof al.name, "alarm");
          al.components = content::kSceneBody | content::kSceneScript;
          al.pos = Vec3{6, 2, 3};
          al.shape = content::SceneShape::Box;
          al.half = Vec3{1.5f, 0.5f, 1.5f};
          al.body_sensor = true;
          al.script_enabled = true;
          std::snprintf(al.script_file, sizeof al.script_file, "tulpar/examples/davranis/alarm.tpr");
          CHECK(tet.insert_entity(tet.entity_count, al));
          char tblob[800];
          std::snprintf(tblob, sizeof tblob, "%s/_kopru_tetik.sahneb", assets_dir());
          CHECK(content::scene_blob_save(arena, tet, tblob, &serr));
          const int errs_t = teng_error_count();
          teng_set_script_vm(&vm);
          CHECK(teng_scene_load(tblob) == 1);
          const int ai = teng_scene_find("alarm"), ki = teng_scene_find("kup_dusen");
          int kare = 0;
          for (; kare < 150 && n_tetik < 4; kare++) { teng_frame_begin(); teng_frame_end(); }
          std::printf("    [bilgi] tetik kancalari: %d karede \"%s\" (alarm=%d kup=%d); ilk T+ (%.0f, %.0f, %.0f), ilk B+ (%.0f, %.0f)\n", kare,
                      tetik_iz, ai, ki, tetik_arg[0][0], tetik_arg[0][1], tetik_arg[0][2], tetik_arg[1][0], tetik_arg[1][1]);
          // Sira: once giris (bolge sonra giren), sonra cikis. Ayni olayda
          // bolgenin kancasi girenin kancasindan ONCE — motordaki sira.
          CHECK(!std::strcmp(tetik_iz, "T+ B+ T- B- "));
          CHECK((int)tetik_arg[0][0] == ai && (int)tetik_arg[0][1] == ki && (int)tetik_arg[0][2] == 0); // kopru varligi degil -> 0
          CHECK((int)tetik_arg[1][0] == ki && (int)tetik_arg[1][1] == ai);
          CHECK(teng_scene_y(ki) < 1.0); // kup sensorun icinden GECTI (tepki yok)
          CHECK(teng_error_count() == errs_t);
          teng_scene_unload();
          std::remove(tblob);
        }
        teng_set_script_vm(&vm); // sonraki bolumler icin geri kur
        CHECK(teng_scene_load(blob) == 1);
      }

      // --- 4.5d) SAHNE KARAKTERI: editorde yerlestirilen karakter dogar mi? ---
      // Bellekte (editor.sahne kanonik metin kapisinin oznesi): uzakta bir zemin
      // kutusu, ustunde editorun "Karakter Kontrolcusu" hazir nesnesinin AYNISI
      // (karakter + dinamik kutu govdesi) ve yolunda bir tetik. Olculen: govde
      // DOGURULMADI (karakter yerini aldi), ilk konum yazar konumunda (kapsul
      // ORTALI), sahne API'siyle yurume, tetik kuyrugunda SAHNE dizini, hiz_ver HATA.
      {
        static content::SceneDesc kd;
        kd = desc;
        content::SceneEntity z{};
        std::snprintf(z.name, sizeof z.name, "k_zemin");
        z.components = content::kSceneBody;
        z.pos = Vec3{100, -0.5f, 100};
        z.half = Vec3{10, 0.5f, 10}; // ust yuz y=0
        CHECK(kd.insert_entity(kd.entity_count, z));
        content::SceneEntity nb{};
        std::snprintf(nb.name, sizeof nb.name, "nobetci");
        nb.components = content::kSceneCharacter | content::kSceneBody; // editor hazir nesnesiyle ayni ikili
        nb.pos = Vec3{100, 0.9f, 100};                                   // ORTA nokta: ayak y=0
        nb.half = Vec3{0.4f, 0.9f, 0.4f};
        nb.dynamic = true;
        nb.char_radius = 0.4f;
        nb.char_height = 1.8f;
        CHECK(kd.insert_entity(kd.entity_count, nb));
        content::SceneEntity kt{};
        std::snprintf(kt.name, sizeof kt.name, "k_gecit");
        kt.components = content::kSceneBody;
        kt.pos = Vec3{102, 1, 100};
        kt.half = Vec3{0.5f, 1, 2};
        kt.body_sensor = true;
        CHECK(kd.insert_entity(kd.entity_count, kt));
        char kblob[800];
        std::snprintf(kblob, sizeof kblob, "%s/_kopru_karakter.sahneb", assets_dir());
        CHECK(content::scene_blob_save(arena, kd, kblob, &serr));
        teng_scene_unload();
        const int govde0 = teng_body_count();
        CHECK(teng_scene_load(kblob) == 1);
        const int ni = teng_scene_find("nobetci"), gi = teng_scene_find("k_gecit");
        // editor.sahne'nin 3 govdesi (kup, duvar) + k_zemin + k_gecit + karakterin IC govdesi;
        // nobetci'nin KUTU govdesi YOK.
        const int govde1 = teng_body_count();
        std::printf("    [bilgi] sahne karakteri: nobetci=%d karakter %d (k_zemin karakter %d), fizik govdesi %d -> %d, ilk merkez y %.3f\n", ni,
                    teng_scene_is_character(ni), teng_scene_is_character(teng_scene_find("k_zemin")), govde0, govde1, teng_scene_y(ni));
        CHECK(ni >= 0 && gi >= 0 && teng_scene_is_character(ni) == 1 && teng_scene_is_character(gi) == 0);
        // 5 = kup_dusen + duvar_sabit + k_zemin + k_gecit + karakterin ic govdesi.
        // Nobetci'nin kutu govdesi de dogsaydi 6 olurdu (karakter kendi kutusuna takilirdi).
        CHECK(govde1 - govde0 == 5);
        CHECK(std::fabs(teng_scene_y(ni) - 0.9) < 1e-3 && std::fabs(teng_scene_x(ni) - 100.0) < 1e-3); // kapsul ORTALI
        const double x0 = teng_scene_x(ni);
        teng_scene_character_move(ni, 2.0, 0.0, 0);
        int gir = 0, cik = 0;
        for (int f = 0; f < 90; f++) {
          teng_frame_begin(); teng_frame_end();
          for (int i = 0; i < teng_trigger_count(); i++)
            if (teng_trigger_zone_scene(i) == gi && teng_trigger_other_scene(i) == ni) (teng_trigger_entered(i) ? gir : cik)++;
        }
        const double yol = teng_scene_x(ni) - x0;
        std::printf("    [bilgi] sahne karakteri yurudu %.2f m (analitik 3.00), merkez y %.3f, zeminde %d, hiz x %.2f; gecit giris %d cikis %d\n", yol,
                    teng_scene_y(ni), teng_scene_character_grounded(ni), teng_scene_vx(ni), gir, cik);
        CHECK(yol > 2.7 && yol < 3.1);
        CHECK(teng_scene_character_grounded(ni) == 1 && std::fabs(teng_scene_y(ni) - 0.9) < 0.05);
        CHECK(gir == 1 && cik == 1);
        const int errs_k = teng_error_count();
        teng_scene_set_velocity(ni, 5, 0, 0); // KONTROL: karakterde HATA
        CHECK(teng_error_count() == errs_k + 1);
        teng_scene_unload();
        CHECK(teng_body_count() == govde0); // karakter ic govdesiyle birlikte gitti
        std::remove(kblob);
        CHECK(teng_scene_load(blob) == 1); // sonraki bolumler icin geri
      }

      // --- 4.5b) BETIK ATAMASI: editorde atanan .tpr oyuna ulasiyor mu? ---
      // Motor betigi CALISTIRMIYOR; tasidigi sey bir ETIKET. Kapi dort durumu
      // birden ayirt ediyor, cunku ucu ayni goruntuyu verebilir ve oyun
      // yanlis dallanir.
      const int kure = teng_scene_find("kure_1");
      CHECK(kure >= 0);
      if (kup >= 0 && kure >= 0 && duvar >= 0) {
        // 1) ETKIN: yol okunuyor, bayrak 1.
        CHECK(std::strcmp(teng_scene_script(kup), "tulpar/examples/engine_ilk_oyun.tpr") == 0);
        CHECK(teng_scene_script_enabled(kup) == 1);
        // 2) KAPALI: yol YINE okunuyor ama bayrak 0. Ikisini bos metne
        //    dusurmek tasarimcinin kapattigi betigi gorunmez yapardi.
        CHECK(std::strcmp(teng_scene_script(kure), "tulpar/examples/engine_arena.tpr") == 0);
        CHECK(teng_scene_script_enabled(kure) == 0);
        // 3) KONTROL — bileseni YOK: bos metin ve bu bir HATA DEGIL.
        const int errs_before_script = teng_error_count();
        CHECK(teng_scene_script(duvar)[0] == 0);
        CHECK(teng_scene_script_enabled(duvar) == 0);
        CHECK(teng_error_count() == errs_before_script);
        // 4) KONTROL — sinir disi: yine bos metin AMA hata sayaci artiyor.
        //    Bu olmadan (3)'teki bos donusun sinir denetiminden mi yoksa
        //    tesaduften mi geldigi anlasilmazdi.
        CHECK(teng_scene_script(9999)[0] == 0);
        CHECK(teng_error_count() == errs_before_script + 1);
        std::printf("    [bilgi] betik atamasi: kup=\"%s\"(etkin %d) kure_1=\"%s\"(etkin %d) duvar=\"%s\" — bileseni yok HATA degil, sinir disi hata\n",
                    teng_scene_script(kup), teng_scene_script_enabled(kup), teng_scene_script(kure), teng_scene_script_enabled(kure),
                    teng_scene_script(duvar));
      }

      // --- 4.6) SAHNE VARLIGINA KUVVET: dinamik govde itilir ---
      // (Sahne bugune kadar yalniz okunuyordu; kuvvet olmadan bolum icinde
      // oynanis yapilamiyordu.) KONTROL: sabit govde kimildamaz + hata sayar.
      if (kup >= 0 && duvar >= 0) {
        CHECK(teng_scene_is_dynamic(kup) == 1);
        CHECK(teng_scene_is_dynamic(duvar) == 0);
        teng_scene_set_velocity(kup, 0, 0, 0);
        CHECK(std::fabs(teng_scene_vx(kup)) < 0.001);
        const double kx0 = teng_scene_x(kup);
        teng_scene_impulse(kup, 4.0, 0, 0);
        CHECK(teng_scene_vx(kup) > 3.0);
        run_frames(30);
        const double kx1 = teng_scene_x(kup);
        CHECK(kx1 > kx0 + 0.5); // durtu tasidi
        // KONTROL: sabit govde — konum AYNI kalir, cagri bir hata uretir.
        const double dx0 = teng_scene_x(duvar);
        const int errs_static = teng_error_count();
        teng_scene_impulse(duvar, 9.0, 0, 0);
        CHECK(teng_error_count() == errs_static + 1);
        run_frames(30);
        CHECK(teng_scene_x(duvar) == dx0);
        std::printf("    [bilgi] sahne kuvveti: kup x %.2f -> %.2f (durtu 4.0), duvar sabit %.2f (durtu yok sayildi)\n", kx0, kx1, dx0);
      }

      // --- 4.7) BOLUM GECISI: bosalt -> sayaclar duser -> yeniden yukle ---
      {
        run_frames(1);
        const int draws_scene = teng_draw_count();
        const int bodies_scene = teng_body_count();
        CHECK(teng_scene_loaded() == 1);
        CHECK(teng_scene_unload() == 1);
        CHECK(teng_scene_loaded() == 0);
        CHECK(teng_scene_count() == 0);
        const int bodies_empty = teng_body_count();
        CHECK(bodies_empty < bodies_scene); // sahne govdeleri fizikten cikti
        run_frames(1);
        const int draws_empty = teng_draw_count();
        CHECK(draws_empty < draws_scene); // cizim de durdu
        // KONTROL: bos sahnede bosaltma hata verir (sessiz gecmez).
        const int errs_unload = teng_error_count();
        CHECK(teng_scene_unload() == 0);
        CHECK(teng_error_count() == errs_unload + 1);
        // Bolum gecisi: ayni blob yeniden yuklenir (birinci yuklemede reddediliyordu).
        CHECK(teng_scene_load(blob) == 1);
        CHECK(teng_scene_loaded() == 1);
        CHECK(teng_scene_count() == (int)desc.entity_count);
        run_frames(1);
        CHECK(teng_body_count() == bodies_scene);
        CHECK(teng_draw_count() == draws_scene);
        std::printf("    [bilgi] bolum gecisi: cizim %d -> %d -> %d, govde %d -> %d -> %d\n", draws_scene, draws_empty, teng_draw_count(),
                    bodies_scene, bodies_empty, teng_body_count());
      }
      // --- 4.8) NESNE OZELLIKLERI (E4): editorde verilen deger oyuna ulasiyor mu? ---
      // Bellekte kucuk bir sahne (editor.sahne kanonik metin kapisinin oznesi):
      //   kaide    kok, DONUK (0 90 0), olcekli (2 2 2); ozelliksiz
      //   muhafiz  kaide'nin COCUGU, betikli ("ozbekci"); dort tur ustune yazilmis
      //   er       kok, donussuz; ozelliksiz (her okuma varsayilan)
      //   zemin    sabit kutu govde: navmesh BAKE edilir (editorun Derle'si gibi)
      // Olculen: ustune yazilmis vs varsayilan (4 tur + has); eksik ozellik HATA
      // SAYMAZ, varsayilan DEGISMEDEN doner; tur uyusmazligi / uzun ad / gecersiz
      // ad / sinir disi HATA sayar; donuk ebeveynli noktanin dunya degeri
      // donussuz hesaptan FARKLI (pozitif kontrol); ayni yerel ofsetli varsayilan
      // ile ustune yazilmis nokta BIT-TAM ayni (tek kural); baslat kancasi
      // ozelligi GORUYOR (aralik kancalardan once kuruldu) ve navmesh'i de
      // (Tuzaklar 8cg: nav eskiden kancalardan SONRA kuruluyordu); okuyan karelerde
      // AllocGate 0; okuma basina ns (1e5 okuma, 5 kosumun medyani).
      {
        static content::SceneDesc od;
        content::scene_desc_reset(od);
        content::SceneEntity ka{};
        std::snprintf(ka.name, sizeof ka.name, "kaide");
        ka.pos = Vec3{10, 0, -4}; ka.rot_deg = Vec3{0, 90, 0}; ka.scale = Vec3{2, 2, 2};
        content::SceneEntity mu{};
        std::snprintf(mu.name, sizeof mu.name, "muhafiz");
        mu.parent = 0;
        mu.pos = Vec3{1, 0, 0};
        mu.components = content::kSceneScript;
        mu.script_enabled = true;
        std::snprintf(mu.script_file, sizeof mu.script_file, "davranis/ozbekci.tpr");
        const float can[3] = {250, 0, 0}, hiz[3] = {5.5f, 0, 0}, evet[3] = {1, 0, 0}, dev[3] = {-2.0f, 0.0f, -1.5f};
        CHECK(content::scene_prop_set(mu, "can", content::kScenePropTam, can) && content::scene_prop_set(mu, "hiz", content::kScenePropSayi, hiz) &&
              content::scene_prop_set(mu, "kalkan", content::kScenePropBayrak, evet) &&
              content::scene_prop_set(mu, "devriye_a", content::kScenePropNokta, dev));
        content::SceneEntity er{};
        std::snprintf(er.name, sizeof er.name, "er");
        er.pos = Vec3{-3, 0, 2};
        content::SceneEntity ze{};
        std::snprintf(ze.name, sizeof ze.name, "zemin");
        ze.components = content::kSceneBody;
        ze.pos = Vec3{0, -0.5f, 0};
        ze.half = Vec3{20, 0.5f, 20}; // ust yuz y=0, sabit: navmesh bake girdisi
        CHECK(od.insert_entity(0, ka) && od.insert_entity(1, mu) && od.insert_entity(2, er) && od.insert_entity(3, ze));
        char oblob[800];
        std::snprintf(oblob, sizeof oblob, "%s/_kopru_ozellik.sahneb", assets_dir());
        content::SceneCompileOptions copt;
        copt.measure_resident = false; // kaynak yok; yalniz navmesh (editorun Derle'si ile ayni ayar)
        copt.bake_nav = true;
        content::SceneBlobExtras ox;
        content::SceneCompileReport orep;
        CHECK(content::scene_compile(arena, od, assets_dir(), copt, &ox, &orep) && orep.nav_ok);
        CHECK(content::scene_blob_save_ex(arena, od, &ox, oblob, &serr));
        // Betik VM'i: yalniz ozbekci_baslat var; icinden ozellik okur. Aralik
        // kancalardan SONRA kurulsaydi burada -1 (varsayilan) okunurdu — sessizce.
        static int oz_baslat_n = 0, oz_baslat_can = 0, oz_baslat_nav = -1;
        oz_baslat_n = 0; oz_baslat_can = 0; oz_baslat_nav = -1;
        struct OzVm {
          static int has(const char *fn) { return std::strcmp(fn, "ozbekci_baslat") == 0 ? 1 : 0; }
          static int call(const char *fn, const double *args, int argc) {
            if (!std::strcmp(fn, "ozbekci_baslat") && argc == 1) {
              oz_baslat_n++;
              oz_baslat_can = teng_scene_prop_int((int)args[0], "can", -1);
              oz_baslat_nav = teng_nav_ok();
            }
            return 1;
          }
        };
        static const TengScriptVm ozvm{OzVm::has, OzVm::call, nullptr, nullptr}; // eski yol
        teng_scene_unload();
        teng_set_script_vm(&ozvm);
        const int e0 = teng_error_count();
        CHECK(teng_scene_load(oblob) == 1);
        CHECK(teng_error_count() == e0);
        const int ki = teng_scene_find("kaide"), mi = teng_scene_find("muhafiz"), ei = teng_scene_find("er");
        CHECK(ki == 0 && mi == 1 && ei == 2);
        std::printf("    [bilgi] ozellik: baslat %d kez, baslat icinde can = %d (250 olmali; -1 = aralik kancalardan SONRA kurulmus), "
                    "nav_ok = %d (1 olmali; yuklemeden sonra %d)\n",
                    oz_baslat_n, oz_baslat_can, oz_baslat_nav, teng_nav_ok());
        CHECK(oz_baslat_n == 1 && oz_baslat_can == 250);
        CHECK(teng_nav_ok() == 1 && oz_baslat_nav == 1); // Tuzaklar 8cg: baslat navmesh'i GORUYOR
        // (a) ustune yazilmis vs varsayilan — HATA YOK.
        CHECK(teng_scene_prop_int(mi, "can", 100) == 250 && teng_scene_prop_int(ei, "can", 100) == 100);
        CHECK(teng_scene_prop_num(mi, "hiz", 3.0) == 5.5);
        CHECK(teng_scene_prop_num(ei, "hiz", 0.1) == 0.1); // varsayilan DEGISMEDEN (float'a yuvarlanmadan) doner
        CHECK(teng_scene_prop_flag(mi, "kalkan", 0) == 1 && teng_scene_prop_flag(ei, "kalkan", 0) == 0 && teng_scene_prop_flag(ei, "kalkan", 1) == 1);
        CHECK(teng_scene_prop_has(mi, "can") == 1 && teng_scene_prop_has(mi, "devriye_a") == 1 && teng_scene_prop_has(ei, "can") == 0 &&
              teng_scene_prop_has(mi, "yok") == 0);
        CHECK(teng_scene_prop_int(mi, "abcdefghijklmnopqrstuvw", 7) == 7); // 23 karakter: gecerli ad, yok -> hata DEGIL (sinir kontrolu)
        // nokta: ustune yazilmis -> blob'daki DUNYA degeri = scene_prop_point_world (bit-tam).
        float wexp[3];
        content::scene_prop_point_world(od, (uint32_t)mi, dev, wexp);
        CHECK(teng_scene_prop_point(mi, "devriye_a", 0, 0, 0) == 1);
        const double mx = teng_scene_prop_px(), my = teng_scene_prop_py(), mz = teng_scene_prop_pz();
        CHECK(mx == (double)wexp[0] && my == (double)wexp[1] && mz == (double)wexp[2]);
        // varsayilan nokta, donussuz kok: dunya = konum + yerel, TAM.
        CHECK(teng_scene_prop_point(ei, "devriye_a", -2.0, 0.0, -1.5) == 0);
        CHECK(teng_scene_prop_px() == -5.0 && teng_scene_prop_py() == 0.0 && teng_scene_prop_pz() == 0.5);
        // (b) TEK KURAL: ayni yerel ofsetli VARSAYILAN (muhafiz'da "yok_nokta")
        // ustune yazilmis devriye_a ile BIT-TAM ayni dunya noktasini verir.
        CHECK(teng_scene_prop_point(mi, "yok_nokta", -2.0, 0.0, -1.5) == 0);
        const bool ayni = teng_scene_prop_px() == mx && teng_scene_prop_py() == my && teng_scene_prop_pz() == mz;
        CHECK(ayni);
        CHECK(teng_error_count() == e0); // eksik ozellik HATA degil (8 okuma yukarida)
        // (c) POZITIF KONTROL: donuk ebeveyn noktayi dondurdu. Donussuz hesap
        // (varligin dunya konumu + yerel) ve yerelin kendisi FARKLI olmali.
        const double wpx = teng_scene_x(mi), wpy = teng_scene_y(mi), wpz = teng_scene_z(mi);
        const double d_donussuz = std::sqrt((mx - (wpx - 2.0)) * (mx - (wpx - 2.0)) + (my - wpy) * (my - wpy) + (mz - (wpz - 1.5)) * (mz - (wpz - 1.5)));
        const double d_boy = std::sqrt((mx - wpx) * (mx - wpx) + (my - wpy) * (my - wpy) + (mz - wpz) * (mz - wpz));
        std::printf("    [bilgi] ozellik nokta: muhafiz (%.4f %.4f %.4f) @ (%.4f %.4f %.4f), donussuzden %.3f m, varliga %.4f (yerel boy 2.5; olcek 2 "
                    "uygulansaydi 5.0); er varsayilani (-5 0 0.5) tam; ayni ofset varsayilan == ustune yazilmis: %s\n",
                    mx, my, mz, wpx, wpy, wpz, d_donussuz, d_boy, ayni ? "bit-tam" : "FARKLI");
        CHECK(d_donussuz > 1.0);
        CHECK(std::fabs(d_boy - 2.5) < 1e-5);
        // (d) HATALAR: sayilir, varsayilan doner.
        int e = teng_error_count();
        CHECK(teng_scene_prop_point(mi, "can", -2.0, 0.0, -1.5) == 0); // tam'i nokta olarak
        CHECK(teng_scene_prop_px() == mx && teng_scene_prop_pz() == mz); // varsayilan YINE dunyaya cevrildi (oyun calismaya devam)
        CHECK(teng_scene_prop_int(mi, "hiz", 7) == 7);        // sayi'yi tam olarak
        CHECK(teng_scene_prop_num(mi, "kalkan", 1.5) == 1.5); // bayrak'i sayi olarak
        CHECK(teng_scene_prop_flag(mi, "devriye_a", 1) == 1); // nokta'yi bayrak olarak
        CHECK(teng_error_count() == e + 4);
        e = teng_error_count();
        CHECK(teng_scene_prop_int(mi, "abcdefghijklmnopqrstuvwx", 9) == 9); // 24 karakter
        CHECK(teng_scene_prop_num(mi, "Can", 2.0) == 2.0);                  // editor boyle ad yazamaz
        CHECK(teng_scene_prop_has(mi, "") == 0);
        CHECK(teng_error_count() == e + 3);
        e = teng_error_count();
        CHECK(teng_scene_prop_int(9999, "can", 4) == 4 && teng_scene_prop_has(-1, "can") == 0);
        CHECK(teng_scene_prop_point(9999, "devriye_a", 1.0, 2.0, 3.0) == 0);
        CHECK(teng_scene_prop_px() == 1.0 && teng_scene_prop_py() == 2.0 && teng_scene_prop_pz() == 3.0); // cevrilemez: oldugu gibi
        CHECK(teng_error_count() == e + 3);
        oz_hata += 10;
        // (e) AllocGate: ozellik okuyan kareler. KONTROL penceresi ayni kareleri
        // okumadan kosar — kare kendisi ayiriyorsa fark orada gorunur.
        uint64_t al_kontrol = 0, al_okuma = 0, al_dongu = 0;
        double acc = 0;
        {
          AllocGate::begin_frame();
          for (int f = 0; f < 10; f++) { teng_frame_begin(); teng_frame_end(); }
          al_kontrol = AllocGate::end_frame();
          AllocGate::begin_frame();
          for (int f = 0; f < 10; f++) {
            teng_frame_begin();
            for (int i = 0; i < 3; i++) {
              acc += teng_scene_prop_num(i, "hiz", 1.0) + teng_scene_prop_int(i, "can", 1) + teng_scene_prop_flag(i, "kalkan", 0) +
                     teng_scene_prop_has(i, "can");
              teng_scene_prop_point(i, "devriye_a", 0.5, 0, 0);
              acc += teng_scene_prop_px();
            }
            teng_frame_end();
          }
          al_okuma = AllocGate::end_frame();
        }
        // (f) okuma basina maliyet: 1e5 okuma x 5 kosum, medyan. Uc bicim:
        // bulunan (aralikta ilk kayit), eksik (araligin tamami taranir),
        // varsayilan nokta (tarama + dunya cevirisi).
        constexpr int kOkuma = 100000;
        double ns_bul[5], ns_yok[5], ns_nok[5];
        AllocGate::begin_frame();
        for (int r = 0; r < 5; r++) {
          uint64_t t0 = platform::now_ns();
          for (int k = 0; k < kOkuma; k++) acc += teng_scene_prop_int(mi, "can", k);
          uint64_t t1 = platform::now_ns();
          for (int k = 0; k < kOkuma; k++) acc += teng_scene_prop_num(mi, "zzz_yok", (double)k);
          uint64_t t2 = platform::now_ns();
          for (int k = 0; k < kOkuma; k++) { teng_scene_prop_point(mi, "yok_nokta", (double)k, 0, 0); acc += teng_scene_prop_px(); }
          uint64_t t3 = platform::now_ns();
          ns_bul[r] = (double)(t1 - t0) / kOkuma;
          ns_yok[r] = (double)(t2 - t1) / kOkuma;
          ns_nok[r] = (double)(t3 - t2) / kOkuma;
        }
        al_dongu = AllocGate::end_frame();
        std::sort(ns_bul, ns_bul + 5);
        std::sort(ns_yok, ns_yok + 5);
        std::sort(ns_nok, ns_nok + 5);
        std::printf("    [bilgi] ozellik okuma maliyeti (1e5 x 5, medyan): bulunan %.1f ns, eksik (4 kayit taranir) %.1f ns, varsayilan nokta %.1f ns "
                    "[%s]; AllocGate: kontrol 10 kare %llu, okuyan 10 kare %llu, 1.5e6 okuma %llu (toplam %.0f)\n",
                    ns_bul[2], ns_yok[2], ns_nok[2], teng_gpu_name(), (unsigned long long)al_kontrol, (unsigned long long)al_okuma,
                    (unsigned long long)al_dongu, acc);
        // Iddia ikiye ayrilir (test_rhi ile ayni sinif): (a) BIZIM kod — okuma
        // dongusu karesiz, surucusuz: 0, her cihazda; (b) okuyan KARELER surucuyu
        // da sayar (global new ayni surec). Surucu kare icinde ayirmiyorsa
        // (olculdu: NVIDIA 0) okuyan kareler de 0 olmali; ayiriyorsa (test_rhi:
        // MoltenVK 28/kare) sayi CIHAZ VERISI — basilir, iddia edilmez.
        CHECK(al_dongu == 0);
        if (al_kontrol == 0) CHECK(al_okuma == 0);
        else skip("surucu kare icinde operator new cagiriyor (test_rhi: MoltenVK sinifi): okuyan-kare sayisi cihaz verisi; okuma dongusu 0 olculdu");
        CHECK(teng_error_count() == e + 3); // olcum dongulerinde hata yok
        teng_scene_unload();
        std::remove(oblob);
        teng_set_script_vm(sahte_vm); // 4.5c'nin VM'i: onceki durum aynen geri
        CHECK(teng_scene_load(blob) == 1);
      }
      unlink(blob);
    } else if (authored) {
      std::printf("    [bilgi] blob yazilamadi (%s): %s\n", blob, serr.msg);
      CHECK(false);
    } else skip("sahne varligi yok (editor.sahne)");
  }

  // --- 5) Nesil etiketli id: silinen id HATA loglar ve 0 doner ---
  const int errs_before = teng_error_count();
  CHECK(teng_x(kure) != 0.0 || teng_y(kure) != 0.0); // canli id: sorun yok
  CHECK(teng_error_count() == errs_before);          // KONTROL: canli id hata URETMEZ
  teng_despawn(kure);
  CHECK(teng_alive(kure) == 0);
  const double olu_x = teng_x(kure);
  CHECK(olu_x == 0.0);
  CHECK(teng_error_count() == errs_before + 1); // tam bir hata (sessiz yutma yok)
  teng_set_velocity(kure, 1, 1, 1);             // olu id: bir hata daha
  CHECK(teng_error_count() == errs_before + 2);

  // --- 6) Kare disi HUD cagrisi reddedilir (kare icinde kabul) ---
  const int errs_hud = teng_error_count();
  teng_text("kare disi", 10, 10, 1.0, 0xFFFFFFFF);
  CHECK(teng_error_count() == errs_hud + 1);
  teng_frame_begin();
  teng_text("kare ici", 10, 10, 1.0, 0xFFFFFFFF);
  teng_rect(10, 40, 100, 20, 0x000000AA);
  teng_frame_end();
  CHECK(teng_error_count() == errs_hud + 1); // KONTROL: kare icinde hata yok

  // --- 7) Tus adi eslemesi: bilinmeyen ad hata, bilinen ad sessiz ---
  const int errs_key = teng_error_count();
  CHECK(teng_key_down("W") == 0); // headless: girdi yok ama ad gecerli
  CHECK(teng_key_down("SPACE") == 0);
  CHECK(teng_key_down("7") == 0);
  CHECK(teng_error_count() == errs_key); // KONTROL: gecerli adlar hata uretmez
  CHECK(teng_key_down("YOKTUS") == 0);
  CHECK(teng_error_count() == errs_key + 1);

  // --- 8) Kare sayaci ve zaman ilerledi ---
  CHECK(teng_frame() > 250);
  CHECK(teng_time() > 4.0); // headless dt = 1/60
  CHECK(teng_dt() > 0.0);

  // --- 9) Piksel: sahne karesi bos kareden FARKLI, iki bos kare AYNI (kontrol) ---
  static uint8_t px_scene[kW * kH * 3], px_empty[kW * kH * 3], px_empty2[kW * kH * 3];
  uint32_t w = 0, h = 0;
  const uint32_t n1 = read_ppm(shot_scene, px_scene, kW * kH, &w, &h);
  const uint32_t n2 = read_ppm(shot_empty, px_empty, kW * kH, nullptr, nullptr);
  const uint32_t n3 = read_ppm(shot_empty2, px_empty2, kW * kH, nullptr, nullptr);
  CHECK(n1 > 0 && n1 == n2 && n2 == n3 && w == kW && h == kH);
  if (n1 > 0 && n1 == n2 && n2 == n3) {
    const uint32_t d_scene = diff_px(px_scene, px_empty, n1), d_ctrl = diff_px(px_empty, px_empty2, n1);
    std::printf("    [bilgi] piksel farki: sahne-bos %u, bos-bos %u (%ux%u)\n", d_scene, d_ctrl, w, h);
    // Sanal GPU'da (Apple Paravirtual, CI macOS) sahne karesi bos cikiyor —
    // ayni sinif test.hpp'de 2026-09-14'te belgelendi. ATLAMA YALNIZ PIKSEL
    // BLOGUNA: kaparin geri kalani (yasam dongusu, varlik, fizik, girdi, hata
    // sayaci, kare/zaman) macOS'ta da KOSUYOR. Butun kapiyi atlamak o kapsami
    // bedavaya kaybetmek olurdu.
    if (test::gpu_is_virtual(teng_gpu_name())) {
      skip("sanal GPU (Apple Paravirtual, CI macOS): kaparin PIKSEL blogu gercek cihazda olculur");
    } else {
      CHECK(d_scene > 1000);
      CHECK(d_ctrl == 0);
    }
  }
  unlink(shot_scene); unlink(shot_empty); unlink(shot_empty2);

  // --- 10) MODEL ANIMASYONU: pozlu cizim statikten FARKLI piksel verir -------
  // KONTROL: ayni poz iki kez cizilince fark 0 (yazici sabit cikti veriyor).
  // Sahne bosaltilir ve butun kopru varliklari silinir: kare yalniz modeli
  // icersin, yoksa dusen kutu farki kendi basina uretir (yanlis yesil).
  {
    // KONTROL (once): animasyon model OLMAYAN varlikta hata verir.
    const int errs_anim0 = teng_error_count();
    teng_set_anim(kutu, 0, 1.0, 1);
    CHECK(teng_error_count() == errs_anim0 + 1);

    if (teng_scene_loaded()) teng_scene_unload();
    teng_despawn(kutu);
    teng_despawn(isik);
    CHECK(teng_count() == 0);
    char model_path[768];
    std::snprintf(model_path, sizeof model_path, "%s/skin_tube.gltf", assets_dir());
    const int model = teng_load_model(model_path);
    if (model < 0) {
      ortam_hatasi++; // yuklenemeyen kaynak = ortam hatasi, kasitli degil
      skip("skin_tube.gltf yok — animasyon kapisi kosmadi");
    } else {
      anim_kapisi = true;
      CHECK(teng_model_clip_count(model) == 1);
      CHECK(std::strcmp(teng_model_clip_name(model, 0), "bend") == 0);
      const double sure = teng_model_clip_duration(model, 0);
      CHECK(sure > 0.5);
      // KONTROL: olmayan klip hata verir ve 0 doner.
      const int errs_clip = teng_error_count();
      CHECK(teng_model_clip_duration(model, 9) == 0.0);
      CHECK(teng_error_count() == errs_clip + 1);

      char shot_stat[512], shot_stat2[512], shot_anim[512];
      tmp_template(shot_stat, sizeof shot_stat, "kopru_stat");
      tmp_template(shot_stat2, sizeof shot_stat2, "kopru_stat2");
      tmp_template(shot_anim, sizeof shot_anim, "kopru_anim");
      char *ashots[3] = {shot_stat, shot_stat2, shot_anim};
      for (int i = 0; i < 3; i++) { const int fd = mkstemp(ashots[i]); if (fd >= 0) close(fd); }

      const int ent = teng_spawn_model(model, 0, 0, 0, 1.0, 0xFFFFFFFF);
      CHECK(ent > 0);
      teng_camera(0, 1.2, 3.2, 0, 1.0, 0);
      run_frames(2);
      CHECK(teng_screenshot(shot_stat) == 1);
      run_frames(1);
      CHECK(teng_screenshot(shot_stat2) == 1); // KONTROL karesi: ayni (statik) poz
      // Klip ata: klibin ortasina kadar ilerlet (headless dt = 1/60).
      teng_set_anim(ent, 0, 1.0, 1);
      CHECK(teng_anim_time(ent) == 0.0);
      run_frames(30);
      const double t_anim = teng_anim_time(ent);
      CHECK(t_anim > 0.4 && t_anim < 0.6);
      CHECK(teng_anim_done(ent) == 0); // dongulu klip bitmez
      CHECK(teng_screenshot(shot_anim) == 1);

      static uint8_t px_s[kW * kH * 3], px_s2[kW * kH * 3], px_a[kW * kH * 3];
      const uint32_t m1 = read_ppm(shot_stat, px_s, kW * kH, nullptr, nullptr);
      const uint32_t m2 = read_ppm(shot_stat2, px_s2, kW * kH, nullptr, nullptr);
      const uint32_t m3 = read_ppm(shot_anim, px_a, kW * kH, nullptr, nullptr);
      CHECK(m1 > 0 && m1 == m2 && m2 == m3);
      if (m1 > 0 && m1 == m2 && m2 == m3) {
        const uint32_t d_anim = diff_px(px_a, px_s, m1), d_ctrl = diff_px(px_s, px_s2, m1);
        std::printf("    [bilgi] animasyon: klip \"%s\" %.2f s, t=%.2f; piksel farki pozlu-statik %u, statik-statik %u\n",
                    teng_model_clip_name(model, 0), sure, t_anim, d_anim, d_ctrl);
        if (test::gpu_is_virtual(teng_gpu_name())) {
          skip("sanal GPU (Apple Paravirtual, CI macOS): animasyon PIKSEL karsilastirmasi gercek cihazda");
        } else {
          CHECK(d_anim > 100);
          CHECK(d_ctrl == 0);
        }
      }
      // Tek sefer calan klip: sure dolunca biter (dongulude bitmiyordu).
      teng_set_anim(ent, 0, 4.0, 0);
      run_frames(60);
      CHECK(teng_anim_done(ent) == 1);
      teng_set_anim(ent, -1, 1.0, 0); // statige don (hata degil)
      CHECK(teng_anim_time(ent) == 0.0);
      teng_despawn(ent);
      unlink(shot_stat); unlink(shot_stat2); unlink(shot_anim);
    }
  }

  // --- 10b) KODLA TETIK: gorunmez bolge, kuyruk, tasima -----------------------
  // Sahnenin uzaginda (30, *, 30): sabit bir zemin kutusu, onun ustune binen
  // bir TETIK kutusu ve yukaridan birakilan bir kure. Kure tetigin icinden
  // gecip zemine iner ve ICERDE kalir. Olculen: giris olayinin kimlikleri,
  // tetigin cizilmemesi ve yakinlik sorgusunda gorunmemesi, TASIMANIN
  // icerde duran kure icin olay uretmemesi, disari tasimanin tek cikis.
  {
    const int errs0 = teng_error_count();
    run_frames(1);
    const int cizim0 = teng_draw_count();
    const int zem = teng_spawn_box(30, 0, 30, 3, 0.5, 3, 0, 0x404040);       // ust yuz y=0.5
    const int tet = teng_spawn_trigger_box(30, 1.5, 30, 2, 1, 2);            // y 0.5..2.5
    const int top = teng_spawn_sphere(30, 5, 30, 0.5, 1, 0xff8800);
    CHECK(zem && tet && top && teng_alive(tet));
    CHECK(teng_trigger_count() == 0); // KONTROL: henuz adim yok
    run_frames(1);
    const int cizim1 = teng_draw_count();
    std::printf("    [bilgi] tetik cizilmiyor: 3 varlik eklendi, cizim %d -> %d (+2 olmali: zemin + kure)\n", cizim0, cizim1);
    CHECK(cizim1 - cizim0 == 2);
    int gir = 0, cik = 0, bilinmeyen = 0, gir_kare = -1;
    int bolge_sahne = 99, diger_sahne = 99;
    auto tara = [&](int kare) {
      for (int i = 0; i < teng_trigger_count(); i++) {
        if (teng_trigger_zone(i) != tet || teng_trigger_other(i) != top) { bilinmeyen++; continue; }
        bolge_sahne = teng_trigger_zone_scene(i);
        diger_sahne = teng_trigger_other_scene(i);
        if (teng_trigger_entered(i)) { gir++; if (gir_kare < 0) gir_kare = kare; }
        else cik++;
      }
    };
    for (int f = 0; f < 150; f++) { teng_frame_begin(); teng_frame_end(); tara(f); }
    const double top_y = teng_y(top);
    std::printf("    [bilgi] kodla tetik: giris %d (kare %d), cikis %d, bilinmeyen %d; kure y %.2f (zeminde, tetigin ICINDE)\n", gir, gir_kare, cik,
                bilinmeyen, top_y);
    CHECK(gir == 1 && cik == 0 && bilinmeyen == 0);
    CHECK(bolge_sahne == -1 && diger_sahne == -1); // ikisi de kopru varligi
    CHECK(top_y > 0.8 && top_y < 1.2);             // tetik TUTMADI, zemin tuttu
    // Yakinlik: tetik merkezinde 0.1'lik sorgu tetigi DONDURMEZ (hedef degil).
    const int yakin = teng_nearest(30, 1.5, 30, 3.0, 0);
    CHECK(yakin != tet);
    // Tasima: kure icerde kalacak kadar kaydir -> olay YOK (silip kursaydi sahte giris).
    teng_set_pos(tet, 30.4, 1.5, 30);
    int g1 = gir, c1 = cik;
    for (int f = 0; f < 10; f++) { teng_frame_begin(); teng_frame_end(); tara(1000 + f); }
    const int tasi_olay = (gir - g1) + (cik - c1);
    // Disari tasi -> tam bir cikis.
    teng_set_pos(tet, 40, 1.5, 30);
    g1 = gir; c1 = cik;
    for (int f = 0; f < 10; f++) { teng_frame_begin(); teng_frame_end(); tara(2000 + f); }
    std::printf("    [bilgi] tetik tasima: icerde kaydirinca %d olay (0 olmali), disari tasiyinca giris %d cikis %d\n", tasi_olay, gir - g1, cik - c1);
    CHECK(tasi_olay == 0);
    CHECK(gir - g1 == 0 && cik - c1 == 1);
    CHECK(teng_error_count() == errs0);
    // KONTROL: sinir disi olay dizini hata loglar (sessiz 0 "olay yok" gibi okunurdu).
    CHECK(teng_trigger_zone(9999) == 0);
    CHECK(teng_error_count() == errs0 + 1);
    teng_despawn(top);
    teng_despawn(tet);
    teng_despawn(zem);
  }

  // --- 10c) KARAKTER DENETLEYICISI --------------------------------------------
  // Sahnenin uzaginda (70, *, 70): zemin kutusu (ust yuz y=0.5), yolun ustunde
  // bir tetik. Karakter havada dogar, iner, +x'e 2 m/s yurur, tetigi gecer,
  // zipar, isinlanir. Olculen: iniste zemin, yurume mesafesi (hiz x zaman),
  // tetik kuyrugunda KARAKTERIN id'si (ic govde eslemesi), isinin karaktere
  // carpmasi ve skip_id ile karakterin icinden atilan isinin ayagin altindaki
  // zemini BULMASI (eski yaklasik atlama bunu kacirirdi), en_yakin'da gorunmesi,
  // hiz_ver'in karakterde HATA vermesi.
  {
    const int errs0 = teng_error_count();
    const int zem = teng_spawn_box(70, 0, 70, 20, 0.5, 20, 0, 0x404040);
    const int tet = teng_spawn_trigger_box(72, 1.5, 70, 0.5, 1, 2); // x 71.5..72.5 (yurume 70 -> 73)
    const int ch = teng_spawn_character(70, 2.0, 70, 0.3, 1.8, 0x3399ff);
    CHECK(zem && tet && ch);
    int inis = -1;
    for (int f = 0; f < 120 && inis < 0; f++) { teng_frame_begin(); teng_frame_end(); if (teng_character_grounded(ch)) inis = f; }
    const double ayak = teng_y(ch);
    std::printf("    [bilgi] karakter: %d. karede indi, ayak y %.3f (zemin 0.5), zemin durumu %d\n", inis, ayak, teng_character_ground_state(ch));
    CHECK(inis > 0 && ayak > 0.45 && ayak < 0.55);
    CHECK(teng_character_ground_state(ch) == 0);
    // Yuru: 2 m/s x 90 kare (1.5 s) -> ~3 m. Tetik yolun ustunde.
    const double x0 = teng_x(ch);
    teng_character_move(ch, 2.0, 0.0, 0);
    int gir = 0, cik = 0;
    for (int f = 0; f < 90; f++) {
      teng_frame_begin(); teng_frame_end();
      for (int i = 0; i < teng_trigger_count(); i++)
        if (teng_trigger_zone(i) == tet && teng_trigger_other(i) == ch) (teng_trigger_entered(i) ? gir : cik)++;
    }
    const double yol = teng_x(ch) - x0;
    std::printf("    [bilgi] karakter yurudu: %.2f m (analitik 3.00), hiz x %.2f; tetik giris %d cikis %d\n", yol, teng_vx(ch), gir, cik);
    CHECK(yol > 2.7 && yol < 3.1);
    CHECK(gir == 1 && cik == 1); // tetigin icinden gecti, kuyrukta KARAKTERIN id'si
    // Isin: tepeden asagi karaktere carpar; skip_id ile karakterin ICINDEN
    // atilan isin ayagin altindaki zemini bulur.
    const double cx = teng_x(ch), cz = teng_z(ch);
    const double d_ust = teng_raycast(cx, 5.0, cz, 0, -1, 0, 10, 0);
    const int vurulan = teng_ray_id();
    const double d_ic = teng_raycast(cx, ayak + 0.9, cz, 0, -1, 0, 3, ch);
    const int vurulan_ic = teng_ray_id();
    std::printf("    [bilgi] isin: tepeden %.2f m -> #%d (karakter #%d); icinden skip ile %.2f m -> #%d (zemin #%d)\n", d_ust, vurulan, ch, d_ic,
                vurulan_ic, zem);
    CHECK(vurulan == ch);
    CHECK(vurulan_ic == zem && d_ic > 0.85 && d_ic < 0.95);
    CHECK(teng_nearest(cx, 1.0, cz, 2.0, 0) == ch);
    // Zipla: dur, bir kez zipla, havaya kalk, geri in.
    teng_character_move(ch, 0.0, 0.0, 1);
    double tepe = ayak;
    int havada = 0, yere = -1;
    for (int f = 0; f < 120; f++) {
      teng_frame_begin(); teng_frame_end();
      if (f == 0) teng_character_move(ch, 0.0, 0.0, 0); // kenar-tetikli: istek bir kez
      if (teng_y(ch) > tepe) tepe = teng_y(ch);
      if (!teng_character_grounded(ch)) havada++;
      else if (havada > 0 && yere < 0) yere = f;
    }
    std::printf("    [bilgi] ziplama: tepe %.2f m (v=4 -> analitik 0.82 m), %d kare havada, %d. karede indi\n", tepe - ayak, havada, yere);
    CHECK(tepe - ayak > 0.7 && tepe - ayak < 0.95);
    CHECK(yere > 0);
    // Isinla: konum + hiz sifir.
    teng_set_pos(ch, 80, 3.0, 70);
    CHECK(std::fabs(teng_x(ch) - 80.0) < 1e-3 && teng_vx(ch) == 0.0);
    CHECK(teng_error_count() == errs0);
    // KONTROL: karakterde hiz_ver HATA (sessizce yok sayilsaydi "karakter neden itilmiyor" diye aranirdi).
    teng_set_velocity(ch, 5, 0, 0);
    CHECK(teng_error_count() == errs0 + 1);
    const int govde_once = teng_body_count();
    teng_despawn(ch);
    CHECK(!teng_alive(ch) && teng_body_count() == govde_once - 1); // ic govde de gitti
    teng_despawn(tet);
    teng_despawn(zem);
  }

  // --- 10c2) KANCA HIZLI YOLU + GOVDE ESLEMESI --------------------------------
  // Kancalar artik YUKLEMEDE cozulup kare icinde isaretciyle cagriliyor
  // (TengScriptVm::resolve/invoke); eski yol her cagrida adi kuruyordu ve
  // Tulpar tarafinda her cagri yeni bir dizgi ayiriyordu. Iddia: AYNI betikli
  // sahne eski ve hizli VM'le AYNI cagri izini uretir — her kanca turu, sahne
  // ve kodla baglanan, arguman arguman. Pozitif kontrol: hizli VM'den BIR kanca
  // dusurulunce iz ayrisir ve kapi bunu gorur. Ayrica: hizli yol gercekten
  // kullanildi (hizli kosumda `has` ve `call` 0 kez), VM degisince isaretci
  // yeni VM'e verilmez, 8'den fazla parametreli kanca reddedilir. Govde
  // eslemesi (O(1)) bu oturum boyunca TULPAR_ENGINE_GOVDE_DENETIM ile her
  // sorguda eski dogrusal taramaya karsi denetleniyor; pozitif kontrol bir
  // girdiyi bozup uyusmazligin sayildigini olcer.
  int hiz_hata = 0;
  {
    static SystemArena harena;
    if (harena.capacity() == 0) harena.reserve(16u << 20, "bridge_hizli_yol");
    static content::SceneDesc hd;
    content::SceneEntity zm{};
    std::snprintf(zm.name, sizeof zm.name, "h_zemin");
    zm.components = content::kSceneBody;
    zm.pos = Vec3{300, -0.5f, 300};
    zm.half = Vec3{8, 0.5f, 8}; // ust yuz y=0
    content::SceneEntity tp{};
    std::snprintf(tp.name, sizeof tp.name, "h_top");
    tp.components = content::kSceneBody | content::kSceneScript;
    tp.pos = Vec3{300, 3, 300};
    tp.shape = content::SceneShape::Sphere;
    tp.radius = 0.4f;
    tp.dynamic = true;
    std::snprintf(tp.script_file, sizeof tp.script_file, "davranis/hdev.tpr");
    content::SceneEntity bl{};
    std::snprintf(bl.name, sizeof bl.name, "h_bolge");
    bl.components = content::kSceneBody | content::kSceneScript;
    bl.pos = Vec3{300, 1.5f, 300};
    bl.half = Vec3{1, 0.5f, 1}; // y 1..2: h_top icinden gecer
    bl.body_sensor = true;
    std::snprintf(bl.script_file, sizeof bl.script_file, "davranis/halarm.tpr");
    content::SceneEntity ku = bl;
    std::snprintf(ku.name, sizeof ku.name, "h_kule");
    ku.pos = Vec3{304, 1.5f, 300}; // K1 icinden gecer
    CHECK(hd.insert_entity(0, zm) && hd.insert_entity(1, tp) && hd.insert_entity(2, bl) && hd.insert_entity(3, ku));
    char hblob[800];
    std::snprintf(hblob, sizeof hblob, "%s/_kopru_hizli_yol.sahneb", assets_dir());
    content::SceneError he{};
    CHECK(content::scene_blob_save(harena, hd, hblob, &he));
    if (teng_scene_loaded()) teng_scene_unload();

    const int errs0 = teng_error_count();
    const int denetim0 = teng_body_map_audit_checks();
    constexpr int kKare = 90;
    hiz::has_n = hiz::call_n = hiz::resolve_n = hiz::invoke_n = hiz::arite_uyumsuz = 0;
    hiz::kosum(0, &hiz::eski, hblob, kKare);
    const int e_has = hiz::has_n, e_call = hiz::call_n, e_res = hiz::resolve_n, e_inv = hiz::invoke_n;
    hiz::has_n = hiz::call_n = hiz::resolve_n = hiz::invoke_n = 0;
    hiz::kosum(1, &hiz::hizli, hblob, kKare);
    const int h_has = hiz::has_n, h_call = hiz::call_n, h_res = hiz::resolve_n, h_inv = hiz::invoke_n;
    hiz::dusur = "kdev_bolge_girdi"; // POZITIF KONTROL: hizli VM bu kancayi "yok" der
    hiz::kosum(2, &hiz::hizli, hblob, kKare);
    hiz::dusur = nullptr;
    CHECK(teng_error_count() == errs0); // uc kosum da temiz
    std::remove(hblob);
    for (int i = 0; i < 3; i++) {
      CHECK(hiz::iz[i].tasma == 0);
      hiz::normallestir(hiz::iz[i]);
    }
    const hiz::Iz &e = hiz::iz[0], &h = hiz::iz[1], &d = hiz::iz[2];
    const int fark = hiz::ilk_fark(e, h), fark_kontrol = hiz::ilk_fark(e, d);
    std::printf("    [bilgi] kanca izi (%d kare): eski %d cagri (has %d, call %d), hizli %d cagri (resolve %d, invoke %d, has %d, call %d); "
                "ilk fark %d (-1 olmali)\n",
                kKare, e.n, e_has, e_call, h.n, h_res, h_inv, h_has, h_call, fark);
    std::printf("    [bilgi] kanca turleri (eski): baslat %d guncelle %d carpisma %d tetik %d bolge %d bitir %d; hdev_carpisma %d kdev_carpisma %d\n",
                hiz::say(e, "hdev_baslat") + hiz::say(e, "kdev_baslat"), hiz::say(e, "hdev_guncelle") + hiz::say(e, "kdev_guncelle"),
                hiz::say(e, "hdev_carpisma") + hiz::say(e, "kdev_carpisma"), hiz::say(e, "halarm_tetik") + hiz::say(e, "ktuzak_tetik"),
                hiz::say(e, "hdev_bolge") + hiz::say(e, "kdev_bolge"), hiz::say(e, "hdev_bitir") + hiz::say(e, "halarm_bitir") +
                    hiz::say(e, "kdev_bitir") + hiz::say(e, "ktuzak_bitir"),
                hiz::say(e, "hdev_carpisma"), hiz::say(e, "kdev_carpisma"));
    std::printf("    [bilgi] POZITIF KONTROL (kdev_bolge_girdi dusuruldu): %d cagri, eskiyle ilk fark %d (>= 0 olmali), eksik %d\n", d.n,
                fark_kontrol, e.n - d.n);
    // Kapsam: her kanca turu iki tarafta da (sahne + kod) en az bir kez kostu.
    CHECK(hiz::say(e, "hdev_baslat") == 1 && hiz::say(e, "halarm_baslat") == 2 && hiz::say(e, "kdev_baslat") == 2);
    CHECK(hiz::say(e, "hdev_guncelle") == kKare && hiz::say(e, "kdev_guncelle") == 2 * kKare);
    CHECK(hiz::say(e, "hdev_carpisma") >= 1 && hiz::say(e, "kdev_carpisma") >= 2);
    CHECK(hiz::say(e, "halarm_tetik_girdi") >= 2 && hiz::say(e, "halarm_tetik_cikti") >= 2 && hiz::say(e, "ktuzak_tetik_girdi") >= 1);
    CHECK(hiz::say(e, "hdev_bolge_girdi") >= 1 && hiz::say(e, "kdev_bolge_girdi") >= 2 && hiz::say(e, "kdev_bolge_cikti") >= 2);
    CHECK(hiz::say(e, "hdev_bitir") == 1 && hiz::say(e, "halarm_bitir") == 2 && hiz::say(e, "kdev_bitir") == 2 && hiz::say(e, "ktuzak_bitir") == 1);
    // Asil iddia: iki yol ayni izi uretti.
    CHECK(e.n > 0 && fark == -1);
    // POZITIF KONTROL: dusurulen kanca izi ayirdi ve eksik tam o kancanin sayisi.
    CHECK(fark_kontrol >= 0 && e.n - d.n == hiz::say(e, "kdev_bolge_girdi") && hiz::say(d, "kdev_bolge_girdi") == 0);
    // Yol gercekten ayriydi: eski kosum yalniz has/call, hizli kosum yalniz resolve/invoke.
    CHECK(e_has > 0 && e_res == 0 && e_inv == 0 && e_call == e.n);
    CHECK(h_res > 0 && h_has == 0 && h_call == 0 && h_inv == h.n && hiz::arite_uyumsuz == 0);

    // SURUM KAYMASI: 4 alanli yapi ESKI setter'la kurulursa motor resolve/invoke'a
    // DOKUNMAZ (eski baglama 2 alanli; sonrasi cop olabilirdi) — adla calisir.
    {
      teng_set_script_vm(&hiz::hizli);
      hiz::aktif = 0;
      hiz::iz[0].n = 0;
      hiz::has_n = hiz::call_n = hiz::resolve_n = hiz::invoke_n = 0;
      const int k = teng_spawn_light(300, -50, 300, 0xffffffff, 0.0, 1.0);
      CHECK(teng_script_attach(k, "kdev") == 1);
      run_frames(1);
      std::printf("    [bilgi] eski setter + 4 alanli yapi: has %d call %d, resolve %d invoke %d (0/0 olmali)\n", hiz::has_n, hiz::call_n,
                  hiz::resolve_n, hiz::invoke_n);
      CHECK(hiz::has_n > 0 && hiz::call_n >= 2 && hiz::resolve_n == 0 && hiz::invoke_n == 0);
      teng_despawn(k);
    }
    // VM kurulumdan sonra degisirse isaretci YENI VM'e verilmez: cagri adla gider.
    {
      teng_set_script_vm_v2(&hiz::hizli);
      hiz::aktif = 0;
      hiz::iz[0].n = 0;
      const int k = teng_spawn_light(300, -50, 300, 0xffffffff, 0.0, 1.0);
      CHECK(teng_script_attach(k, "kdev") == 1); // hizli yolda cozuldu (baslat invoke ile)
      hiz::call_n = hiz::invoke_n = 0;
      teng_set_script_vm(&hiz::eski);
      run_frames(1);
      const int eski_call = hiz::call_n, eski_inv = hiz::invoke_n;
      teng_set_script_vm_v2(&hiz::hizli);
      run_frames(1);
      std::printf("    [bilgi] VM degisimi: eski VM'le 1 kare call %d invoke %d (1/0 olmali), cozen VM geri gelince invoke %d (1 olmali)\n", eski_call,
                  eski_inv, hiz::invoke_n - eski_inv);
      CHECK(eski_call == 1 && eski_inv == 0 && hiz::invoke_n - eski_inv == 1 && hiz::call_n == eski_call);
      teng_despawn(k);
    }
    // Tavan: 9 parametreli kanca hizli yolda REDDEDILIR (eski yol 8'de sessizce keserdi).
    {
      const int e9 = teng_error_count();
      const int k = teng_spawn_light(300, -50, 300, 0xffffffff, 0.0, 1.0);
      CHECK(teng_script_attach(k, "hdokuz") == 0);
      CHECK(teng_error_count() == e9 + 2); // arite HATA + "kanca YOK" HATA
      hiz_hata += 2;
      teng_despawn(k);
    }

    // Govde eslemesi: denetim bu oturumda KOSTU (olcu kor degil) ve su ana kadar temiz.
    const int denetim1 = teng_body_map_audit_checks(), uyusmaz1 = teng_body_map_audit_mismatches();
    std::printf("    [bilgi] govde eslemesi denetimi: oturum basindan %d sorgu (bu bolumde %d), %d uyusmazlik\n", denetim1, denetim1 - denetim0, uyusmaz1);
    CHECK(denetim1 - denetim0 > 100 && uyusmaz1 == 0);
    // POZITIF KONTROL: bir girdiyi boz -> isin sonucu "kopru varligi degil" der,
    // denetim tam BIR uyusmazlik ve BIR hata sayar; geri alinca temiz.
    {
      const int kutu = teng_spawn_box(320, 0, 320, 1, 0.5, 1, 0, 0x808080ff);
      run_frames(1); // genis faz agaci
      const int e1 = teng_error_count();
      teng_raycast(320, 5, 320, 0, -1, 0, 10, 0);
      const int saglam = teng_ray_id();
      CHECK(teng_debug_body_map_corrupt(kutu) == 1);
      teng_raycast(320, 5, 320, 0, -1, 0, 10, 0);
      const int bozuk = teng_ray_id();
      const int uyusmaz2 = teng_body_map_audit_mismatches(), e2 = teng_error_count();
      CHECK(teng_debug_body_map_corrupt(kutu) == 1); // geri al
      teng_raycast(320, 5, 320, 0, -1, 0, 10, 0);
      const int onarilmis = teng_ray_id();
      std::printf("    [bilgi] govde eslemesi POZITIF KONTROL: isin #%d -> bozuk girdiyle #%d (0 olmali), uyusmazlik +%d hata +%d (1/1), onarinca #%d\n",
                  saglam, bozuk, uyusmaz2 - uyusmaz1, e2 - e1, onarilmis);
      CHECK(saglam == kutu && bozuk == 0 && onarilmis == kutu);
      CHECK(uyusmaz2 - uyusmaz1 == 1 && e2 - e1 == 1 && teng_body_map_audit_mismatches() == uyusmaz2 && teng_error_count() == e2);
      hiz_hata += 1;
      teng_despawn(kutu);
    }
  }

  // --- 10d) KODLA URETILEN VARLIGA BETIK BAGLAMA -----------------------------
  // Sahne kancalarinin kod ikizi (teng_script_attach). Olculen: `id` KOPRU id'si
  // (sahne indisi degil), guncelle YUVA sirasiyla (baglama sirasiyla degil),
  // carpisma argumanlari (diger = kopru id, diger_sahne = -1 / sahne dizini),
  // tetik: once bolgenin kancasi sonra girenin, sil/coz/yeniden bagla/kapanis
  // hepsinde TAM BIR bitir, bitir sirasinda varlik canli, kanca icinden sil /
  // bagla / uret, havuz tavani, sahne sayaclarinin ETKILENMEMESI. Kasitli
  // hatalar tek tek sayilir (kod_hata), kapanis toplamina eklenir.
  int kod_hata = 0;
  {
    teng_set_script_vm(&kod::vm);
    const int errs0 = teng_error_count();
    const int zem = teng_spawn_box(110, 0, 110, 20, 0.5, 20, 0, 0x404040); // ust yuz y=0.5
    // A, B, C sirali uretilir (artan yuva); BAGLAMA sirasi TERS. SABIT kureler:
    // zeminle temas olayi uretmesinler (guncelle sayimi yalniz guncelle olsun).
    const int A = teng_spawn_sphere(100, 0.9, 100, 0.4, 0, 0xff0000ff);
    const int B = teng_spawn_sphere(102, 0.9, 100, 0.4, 0, 0x00ff00ff);
    const int C = teng_spawn_sphere(104, 0.9, 100, 0.4, 0, 0x0000ffff);
    CHECK(zem && A && B && C);
    const int yA = A & 0xFFFF, yB = B & 0xFFFF, yC = C & 0xFFFF;
    CHECK(yA < yB && yB < yC);
    CHECK(teng_script_count() == 0);
    CHECK(teng_script_attach(C, "dusman") == 1);
    CHECK(teng_script_attach(A, "davranis/dusman.tpr") == 1); // yol verilirse taban ad
    CHECK(teng_script_attach(B, "dusman.tpr") == 1);
    CHECK(teng_script_count() == 3);
    CHECK(std::strcmp(teng_script_name(A), "dusman") == 0 && std::strcmp(teng_script_name(zem), "") == 0);
    // baslat HEMEN, baglama sirasiyla; `id` kopru id'si (nesil<<16 -> >= 65536:
    // sahne indisi olsaydi < 256 olurdu).
    CHECK(kod::n == 3 && kod::say("dusman_baslat", 0) == 3);
    if (kod::n == 3) {
      CHECK((int)kod::kayit[0].a[0] == C && (int)kod::kayit[1].a[0] == A && (int)kod::kayit[2].a[0] == B);
      CHECK(kod::kayit[0].argc == 1 && kod::kayit[0].a[0] >= 65536.0);
    }
    // guncelle: her kare TAM BIR, YUVA sirasi (A, B, C), dt headless 1/60.
    const int g0 = kod::n;
    run_frames(3);
    CHECK(kod::n - g0 == 9);
    bool sira_ok = kod::n - g0 == 9;
    for (int f = 0; f < 3 && sira_ok; f++) {
      const kod::Cagri *c = &kod::kayit[g0 + f * 3];
      sira_ok = !std::strcmp(c[0].fn, "dusman_guncelle") && (int)c[0].a[0] == A && (int)c[1].a[0] == B && (int)c[2].a[0] == C &&
                c[0].argc == 2 && std::fabs(c[0].a[1] - 1.0 / 60.0) < 1e-6 && c[0].kare == c[2].kare;
    }
    std::printf("    [bilgi] kodla betik: baslat sirasi C,A,B (baglama), guncelle sirasi A,B,C (yuva %d<%d<%d): %s\n", yA, yB, yC,
                sira_ok ? "dogru" : "YANLIS");
    CHECK(sira_ok);

    // --- carpisma: kure havadan zemine; diger = zeminin KOPRU id'si ------
    const int D = teng_spawn_sphere(115, 3.0, 110, 0.4, 1, 0xffaa00ff);
    const int Z = teng_spawn_trigger_box(115, 2.0, 110, 1.0, 0.5, 1.0); // y 1.5..2.5: D icinden gecer
    CHECK(teng_script_attach(D, "dusman") == 1 && teng_script_attach(Z, "tuzak") == 1);
    // KONTROL: carpmadan once sifir carpisma cagrisi.
    CHECK(kod::say("dusman_carpisma", D) == 0);
    int kare = 0;
    while (kod::say("dusman_carpisma", D) == 0 && kare < 180) { run_frames(1); kare++; }
    const int ic = kod::ilk("dusman_carpisma", D);
    CHECK(ic >= 0);
    if (ic >= 0) {
      const kod::Cagri &c = kod::kayit[ic];
      std::printf("    [bilgi] kodla carpisma: %d karede, argc %d, id #%.0f diger #%.0f (zemin #%d) olay %.0f nokta y %.2f hiz %.2f diger_sahne %.0f\n",
                  kare, c.argc, c.a[0], c.a[1], zem, c.a[2], c.a[4], c.a[6], c.a[7]);
      CHECK(c.argc == 8 && (int)c.a[1] == zem && (int)c.a[7] == -1);
      CHECK(c.a[6] > 3.0 && c.a[6] < 9.0); // ~2.1 m serbest dusus: v ~ 6.4 m/s
    }
    // tetik: D, Z'nin icinden gecti. Her olayda ONCE bolgenin kancasi.
    const int tg = kod::ilk("tuzak_tetik_girdi", Z), bg = kod::ilk("dusman_bolge_girdi", D);
    const int tc = kod::ilk("tuzak_tetik_cikti", Z), bc = kod::ilk("dusman_bolge_cikti", D);
    std::printf("    [bilgi] kodla tetik: T+ %d B+ %d T- %d B- %d (kayit sirasi)\n", tg, bg, tc, bc);
    CHECK(tg >= 0 && bg == tg + 1 && tc > bg && bc == tc + 1);
    if (tg >= 0 && bg >= 0) {
      CHECK(kod::kayit[tg].argc == 3 && (int)kod::kayit[tg].a[1] == D && (int)kod::kayit[tg].a[2] == -1);
      CHECK(kod::kayit[bg].argc == 3 && (int)kod::kayit[bg].a[1] == Z && (int)kod::kayit[bg].a[2] == -1);
    }

    // --- sahne tarafi: diger = 0, SON arguman sahne dizini ----------------
    // Bellekte kucuk bir sahne: sabit blok + tetik bolge + dinamik kup.
    // Kodla uretilen F sahne bolgesinden gecip sahne blogunun ustune duser;
    // sahne kupu KODLA uretilen bir bolgeden (Z2) gecer.
    {
      static SystemArena sarena;
      if (sarena.capacity() == 0) sarena.reserve(16u << 20, "bridge_kod_betik");
      static content::SceneDesc sd; // buyuk: yigina sigmaz, tek kullanim
      content::SceneEntity blok{};
      std::snprintf(blok.name, sizeof blok.name, "s_blok");
      blok.components = content::kSceneBody;
      blok.pos = Vec3{155, 0, 150};
      blok.half = Vec3{12, 0.5f, 2}; // ust yuz y=0.5, x 143..167
      content::SceneEntity bolge{};
      std::snprintf(bolge.name, sizeof bolge.name, "s_bolge");
      bolge.components = content::kSceneBody;
      bolge.pos = Vec3{150, 2, 150};
      bolge.half = Vec3{1, 0.5f, 1};
      bolge.body_sensor = true;
      content::SceneEntity kup{};
      std::snprintf(kup.name, sizeof kup.name, "s_kup");
      kup.components = content::kSceneBody;
      kup.pos = Vec3{160, 4, 150};
      kup.half = Vec3{0.3f, 0.3f, 0.3f};
      kup.dynamic = true;
      CHECK(sd.insert_entity(0, blok) && sd.insert_entity(1, bolge) && sd.insert_entity(2, kup));
      char sblob[800];
      std::snprintf(sblob, sizeof sblob, "%s/_kopru_kod_betik.sahneb", assets_dir());
      content::SceneError se{};
      CHECK(content::scene_blob_save(sarena, sd, sblob, &se));
      const int Z2 = teng_spawn_trigger_box(160, 2.0, 150, 1.0, 0.5, 1.0);
      CHECK(Z2 && teng_script_attach(Z2, "tuzak") == 1);
      if (teng_scene_loaded()) teng_scene_unload();
      CHECK(teng_scene_load(sblob) == 1);
      const int iblok = teng_scene_find("s_blok"), ibolge = teng_scene_find("s_bolge"), ikup = teng_scene_find("s_kup");
      const int F = teng_spawn_sphere(150, 3.0, 150, 0.4, 1, 0xaa00ffff);
      CHECK(F && teng_script_attach(F, "dusman") == 1);
      kare = 0;
      while ((kod::say("dusman_carpisma", F) == 0 || kod::say("tuzak_tetik_girdi", Z2) == 0) && kare < 240) { run_frames(1); kare++; }
      const int fb = kod::ilk("dusman_bolge_girdi", F), fc = kod::ilk("dusman_carpisma", F), zg = kod::ilk("tuzak_tetik_girdi", Z2);
      CHECK(fb >= 0 && fc >= 0 && zg >= 0);
      if (fb >= 0 && fc >= 0 && zg >= 0) {
        const kod::Cagri &b1 = kod::kayit[fb], &c1 = kod::kayit[fc], &z1 = kod::kayit[zg];
        std::printf("    [bilgi] kod<->sahne: F bolge_girdi(bolge #%.0f, bolge_sahne %.0f; s_bolge=%d), carpisma(diger #%.0f, diger_sahne %.0f; s_blok=%d), "
                    "Z2 tetik_girdi(diger #%.0f, diger_sahne %.0f; s_kup=%d)\n",
                    b1.a[1], b1.a[2], ibolge, c1.a[1], c1.a[7], iblok, z1.a[1], z1.a[2], ikup);
        CHECK((int)b1.a[1] == 0 && (int)b1.a[2] == ibolge);
        CHECK((int)c1.a[1] == 0 && (int)c1.a[7] == iblok);
        CHECK((int)z1.a[1] == 0 && (int)z1.a[2] == ikup);
      }
      // Sahne sayaci kodla baglanan cagrilari SAYMIYOR: yukleme onu sifirladi,
      // sahnede betik yok, bu karelerde yalniz kopru kancalari kostu.
      std::printf("    [bilgi] sahne kanca sayaci bu karelerde %d (0 olmali: kopru kancalari ayri sayilir)\n", teng_script_call_count());
      CHECK(teng_script_call_count() == 0);
      teng_scene_unload();
      std::remove(sblob);
      teng_despawn(F);
      teng_despawn(Z2);
      CHECK(kod::say("dusman_bitir", F) == 1 && kod::say("tuzak_bitir", Z2) == 1);
    }

    // --- belirlenimli carpisma sirasi: 32 kure AYNI adimda zemine ----------
    // Halka is parcaciklarindan yaziliyor (Tuzaklar 8cc); kancalar YUVA sirasiyla
    // gelmeli. Baglama sirasi yine TERS.
    {
      int kk[32];
      for (int i = 0; i < 32; i++) kk[i] = teng_spawn_sphere(112 + (i % 8) * 1.5, 1.2, 116 + (i / 8) * 1.5, 0.4, 1, 0x8080ffff);
      for (int i = 31; i >= 0; i--) CHECK(teng_script_attach(kk[i], "dusman") == 1);
      int halka_sirasiz = 0, kanca_sirasiz = 0, toplam = 0;
      for (int f = 0; f < 30; f++) {
        const int f0 = kod::n;
        run_frames(1);
        // bu karenin kopru carpisma kancalari: yuva sirasi azalmamali
        int son = -1;
        for (int i = f0; i < kod::n; i++) {
          if (std::strcmp(kod::kayit[i].fn, "dusman_carpisma")) continue;
          const int y = (int)kod::kayit[i].a[0] & 0xFFFF;
          if (y < son) kanca_sirasiz++;
          son = y;
          toplam++;
        }
        // KONTROL (bilgi): ayni karenin HALKA sirasi — siralama olmasaydi kancalar bunu izlerdi.
        int hs = -1;
        for (int i = 0; i < teng_collision_count(); i++) {
          const int a = teng_collision_a(i), b = teng_collision_b(i);
          const int y = ((a > b ? a : b) & 0xFFFF);
          if (y < hs) halka_sirasiz++;
          hs = y;
        }
      }
      std::printf("    [bilgi] carpisma sirasi: %d kopru kancasi, yuva sirasini bozan %d (0 olmali); ayni karelerin HALKA sirasinda %d ters adim\n",
                  toplam, kanca_sirasiz, halka_sirasiz);
      CHECK(toplam >= 32 && kanca_sirasiz == 0);
      for (int i = 0; i < 32; i++) teng_despawn(kk[i]);
    }

    // --- hatalar: gorunur, onceki baglanti DEGISMEZ -------------------------
    int e = teng_error_count();
    const int n0 = kod::n;
    CHECK(teng_script_attach(A, "yok_boyle") == 0); // kanca YOK
    CHECK(teng_error_count() == e + 1 && std::strcmp(teng_script_name(A), "dusman") == 0 && kod::n == n0); // bitir/baslat yok
    kod_hata++;
    const int E = teng_spawn_sphere(90, 0.9, 90, 0.4, 1, 0xffffffff);
    teng_despawn(E);
    e = teng_error_count();
    CHECK(teng_script_attach(E, "dusman") == 0 && teng_script_detach(E) == 0 && teng_script_name(E)[0] == 0); // olu id: uc hata
    CHECK(teng_script_attach(0, "dusman") == 0);                                                           // id 0: hata
    CHECK(teng_error_count() == e + 4);
    kod_hata += 4;
    // KONTROL: coz/ad'da id 0 ailenin "yok"u — sessiz (kanca icinde betik_adi(diger), diger = 0).
    CHECK(teng_script_detach(0) == 0 && teng_script_name(0)[0] == 0 && teng_error_count() == e + 4);
    teng_set_script_vm(nullptr);
    e = teng_error_count();
    CHECK(teng_script_attach(B, "iz") == 0 && teng_error_count() == e + 1); // VM yok
    kod_hata++;
    teng_set_script_vm(&kod::vm);
    // tetik_* kancali betik TETIK olmayan varliga: hata, ama baglanir (sahneyle ayni kural).
    const int K = teng_spawn_sphere(92, 0.9, 92, 0.4, 1, 0xffffffff);
    e = teng_error_count();
    CHECK(teng_script_attach(K, "tuzak") == 1 && teng_error_count() == e + 1);
    kod_hata++;
    teng_despawn(K);

    // --- yeniden baglama: ONCE eskinin bitir'i, SONRA yeninin baslat'i ------
    int r0 = kod::n;
    CHECK(teng_script_attach(A, "iz") == 1);
    CHECK(kod::n == r0 + 2 && kod::n >= 2 && !std::strcmp(kod::kayit[r0].fn, "dusman_bitir") && !std::strcmp(kod::kayit[r0 + 1].fn, "iz_baslat"));
    CHECK(kod::n > r0 && kod::kayit[r0].canli == 1); // bitir sirasinda varlik canli
    CHECK(std::strcmp(teng_script_name(A), "iz") == 0 && teng_script_count() == 5); // A B C D Z
    // --- coz: bitir; ikinci coz sessiz 0 (hata degil) ------------------------
    e = teng_error_count();
    r0 = kod::n;
    CHECK(teng_script_detach(A) == 1 && kod::n == r0 + 1 && !std::strcmp(kod::kayit[r0].fn, "iz_bitir"));
    CHECK(teng_script_detach(A) == 0 && teng_error_count() == e && teng_script_name(A)[0] == 0);
    CHECK(teng_alive(A) == 1); // coz varligi SILMEZ
    // --- sil: bitir bir kez, varlik bitir sirasinda canli --------------------
    r0 = kod::n;
    teng_despawn(B);
    CHECK(kod::say("dusman_bitir", B) == 1 && kod::n > r0 && kod::kayit[kod::n - 1].canli == 1);
    CHECK(teng_script_count() == 3); // C D Z

    // --- kanca icinden: guncelle'de kendini sil, bitir'de kendine bagla + sil --
    const int X = teng_spawn_sphere(94, 0.9, 94, 0.4, 1, 0xffffffff);
    CHECK(teng_script_attach(X, "intihar") == 1);
    e = teng_error_count();
    run_frames(1);
    CHECK(teng_alive(X) == 0 && kod::say("intihar_guncelle", X) == 1 && kod::say("intihar_bitir", X) == 1);
    CHECK(kod::intihar_ata == 0 && teng_error_count() == e + 1); // bitir icinden kendine bagla REDDEDILDI
    kod_hata++;
    CHECK(teng_script_count() == 3);
    // --- kanca icinden URET + BAGLA: yeni betik o karede GUNCELLENMEZ -------
    const int Y = teng_spawn_sphere(96, 0.9, 96, 0.4, 1, 0xffffffff);
    CHECK(teng_script_attach(Y, "dogurgan") == 1);
    run_frames(3);
    CHECK(kod::dogan != 0);
    const int dg = kod::ilk("dusman_guncelle", kod::dogan);
    std::printf("    [bilgi] kanca icinden baglanan #%d: dogdugu kare %d, ilk guncelle kare %d (bir sonraki olmali)\n", kod::dogan, kod::dogurgan_kare,
                dg >= 0 ? kod::kayit[dg].kare : -1);
    CHECK(dg >= 0 && kod::kayit[dg].kare == kod::dogurgan_kare + 1);
    CHECK(kod::say("dusman_baslat", kod::dogan) == 1);

    // --- havuz tavani: dolunca HATA, sessiz buyume yok ----------------------
    {
      static int isiklar[600];
      int ok = 0, red = 0;
      kod::kaydet = false;
      const int once = teng_script_count();
      const int m = 512 - once + 3; // tavanin 3 fazlasi: tam 3 red beklenir
      e = teng_error_count();
      for (int i = 0; i < m && i < 600; i++) {
        isiklar[i] = teng_spawn_light(0, -50, 0, 0xffffffff, 0.0, 1.0);
        if (teng_script_attach(isiklar[i], "iz")) ok++;
        else red++;
      }
      std::printf("    [bilgi] betik havuzu: %d bagli + %d yeni = %d (tavan), %d reddedildi, hata +%d\n", once, ok, teng_script_count(), red,
                  teng_error_count() - e);
      CHECK(once + ok == 512 && teng_script_count() == 512 && red == 3 && teng_error_count() == e + red);
      kod_hata += red;
      for (int i = 0; i < m && i < 600; i++) teng_despawn(isiklar[i]);
      kod::kaydet = true;
    }
    CHECK(kod::tasma == 0);
    CHECK(teng_error_count() - errs0 == kod_hata);
    teng_despawn(D);
    teng_despawn(Z);
    teng_despawn(zem);
    std::printf("    [bilgi] kodla betik: %d cagri kaydedildi, baslat %d / bitir %d, bagli %d (kapanista bitir alacak), kasitli hata %d\n", kod::n,
                kod::baslat, kod::bitir, teng_script_count(), kod_hata);
    // C, Y ve dogan BAGLI kaliyor: kapanis bitir'i asagida olculur.
  }

  // --- 11) SES: calan ses sayaci ve tepe deger; KONTROL: durdurunca sifir ----
  {
    int a_ok = teng_audio_open(0, 0);
    bool null_backend = false;
    if (!a_ok) {
      // Gercek cihaz yok (CI konteyneri): null arka uc POZITIF KONTROL olarak
      // kosar — callback thread'i gercekten calisir, sayaclar gercektir.
      ortam_hatasi++;
      setenv("TULPAR_ENGINE_AUDIO_NULL", "1", 1);
      a_ok = teng_audio_open(0, 0);
      null_backend = a_ok != 0;
      if (!a_ok) ortam_hatasi++;
      unsetenv("TULPAR_ENGINE_AUDIO_NULL");
    }
    if (!a_ok) {
      skip("ses cihazi yok (gercek de null arka uc da acilmadi)");
    } else {
      ses_kapisi = true;
      std::printf("    [bilgi] ses: %s%s\n", teng_audio_backend(), null_backend ? "  (null arka uc)" : "");
      CHECK(teng_audio_ok() == 1);
      const int klip = teng_audio_tone(440.0, 1.0);
      CHECK(klip >= 0);
      CHECK(teng_audio_tone(440.0, 1.0) == klip); // ayni ton = ayni tutamac (arena buyumez)
      // KONTROL: hicbir ses calmiyorken sayac ve tepe 0.
      platform::thread_sleep_us(150000);
      CHECK(teng_audio_playing() == 0);
      CHECK(teng_audio_peak() == 0.0);
      const int ses = teng_audio_play(klip, 0.5, 1);
      CHECK(ses != 0);
      platform::thread_sleep_us(250000);
      const int calan = teng_audio_playing();
      const double tepe = teng_audio_peak();
      CHECK(calan >= 1);
      CHECK(tepe > 0.05);
      // KONTROL: durdurulunca ikisi de sifira doner.
      teng_audio_stop(ses);
      platform::thread_sleep_us(250000);
      const int calan_sonra = teng_audio_playing();
      const double tepe_sonra = teng_audio_peak();
      std::printf("    [bilgi] ses kapisi: calan %d tepe %.2f -> durdurunca calan %d tepe %.2f\n", calan, tepe, calan_sonra, tepe_sonra);
      CHECK(calan_sonra == 0);
      CHECK(tepe_sonra == 0.0);
      // Gecersiz argumanlar: iki kasitli hata.
      const int errs_audio = teng_error_count();
      CHECK(teng_audio_play(999, 1.0, 0) == 0);
      CHECK(teng_audio_tone(-5.0, 1.0) == -1);
      CHECK(teng_error_count() == errs_audio + 2);
      teng_audio_master(0.5); // ana seviye: calan ses yokken de kabul edilir
      teng_audio_stop_all();
      teng_audio_close();
      CHECK(teng_audio_ok() == 0);
      // Cihaz kapaliyken cagri COKMEZ: hata loglanir, cagri yok sayilir.
      const int errs_off = teng_error_count();
      CHECK(teng_audio_play(klip, 1.0, 0) == 0);
      CHECK(teng_audio_playing() == 0);
      CHECK(teng_error_count() == errs_off + 1);
    }
  }

  // --- 12) Kapanis: kasitli hata sayisi (kosan kapilara gore) ----------------
  // Ortamdan gelen hatalar (ses cihazi yok, kaynak yok) ayri sayilir; onlar
  // kasitli degil ve makineye gore degisir.
  int beklenen = 2 /*olu id*/ + 1 /*kare disi HUD*/ + 1 /*gecersiz tus adi*/ + 1 /*model olmayan varlikta animasyon*/ + 1 /*sinir disi tetik olayi*/ + 1 /*karakterde hiz_ver*/;
  beklenen += kod_hata; // 10d: kodla betik baglamanin kasitli reddleri (orada tek tek sayildi)
  beklenen += hiz_hata; // 10c2: 9 parametreli kanca (2) + bozulan esleme girdisi (1)
  if (sahne_kapisi) beklenen += 3 /*ikinci yukleme, olmayan dosya, sinir disi dizin*/ + 1 /*sabit govdeye durtu*/ + 1 /*bos sahnede bosaltma*/ + 1 /*sinir disi betik erisimi*/ + 1 /*sahne karakterine hiz_ver*/;
  beklenen += oz_hata; // 4.8: nesne ozelliklerinin kasitli hatalari (tur uyusmazligi, ad kurali, sinir disi)
  if (anim_kapisi) beklenen += 1 /*olmayan klip*/;
  if (ses_kapisi) beklenen += 3 /*olmayan klip, negatif frekans, kapali cihazda cal*/;
  const int errs_total = teng_error_count() - err0 - ortam_hatasi;
  std::printf("    [bilgi] kasitli hata %d/%d (sahne kapisi %d, animasyon %d, ses %d), ortam hatasi %d, uyari %d\n", errs_total, beklenen,
              (int)sahne_kapisi, (int)anim_kapisi, (int)ses_kapisi, ortam_hatasi, teng_warning_count());
  CHECK(errs_total == beklenen);
  // Govde eslemesi: oturumun TAMAMINDA tek uyusmazlik, 10c2'nin kasitli bozdugu.
  std::printf("    [bilgi] govde eslemesi denetimi (oturum): %d sorgu, %d uyusmazlik (1 olmali: pozitif kontrol)\n", teng_body_map_audit_checks(),
              teng_body_map_audit_mismatches());
  CHECK(teng_body_map_audit_mismatches() == 1);
  teng_close();
  CHECK(teng_running() == 0);
  // Kapanis: hala bagli betiklere (10d'den C, Y, dogan) TAM BIR bitir, yuva
  // sirasiyla. Invaryant: toplam baslat == toplam bitir (her baslat'a bir bitir).
  const int bagli = teng_script_count(), bitir0 = kod::bitir, kn0 = kod::n;
  teng_shutdown();
  int kap_sira = 1, son_yuva = -1;
  for (int i = kn0; i < kod::n; i++) {
    const int y = (int)kod::kayit[i].a[0] & 0xFFFF;
    if (y < son_yuva || !std::strstr(kod::kayit[i].fn, "_bitir") || kod::kayit[i].canli != 1) kap_sira = 0;
    son_yuva = y;
  }
  std::printf("    [bilgi] kapanis: %d bagli -> %d bitir (yuva sirasi %s); toplam baslat %d / bitir %d\n", bagli, kod::bitir - bitir0,
              kap_sira ? "dogru" : "YANLIS", kod::baslat, kod::bitir);
  CHECK(bagli == 3 && kod::bitir - bitir0 == bagli && kap_sira);
  CHECK(kod::baslat == kod::bitir);
  CHECK(teng_script_count() == 0);
}

// --- Yarim kurulum GERI ALINIYOR mu (Tuzaklar 8cq) -----------------------------
// teng_init dusunce (yukleyici yok / yukleyici var ama ICD yok) o ana kadar
// aldigi her seyi birakmali: 512 MB arena rezervi, worker thread'leri, acilan
// Vulkan yukleyicisi. Olcu: ayni surecte N kez "dusen init + shutdown"
// dongusunden sonra sanal boyut, RSS ve thread sayisi dongu ONCESIYLE ayni.
// Isinma turu olcumun disinda: ilk dusen init surec basina bir kez olan
// seyleri (cokme raporlayicisi, log halkasi, Bridge nesnesinin sayfalari) da
// dokundurur.
//
// POZITIF KONTROL (ayni iki test, DUZELTMESIZ bridge/engine_api.cpp; RTX 5080 /
// Linux 7.2.8, 16 cekirdek, 2026-10-05): 8 dongu -> iki senaryoda da sanal
// +5316 MB, thread +120 (dongu basina 15 worker), RSS +33,8 MB; uc kontrol
// KIRMIZI. Duzeltmeyle: +0 MB, +0 thread, RSS +0..8 KB. Esikler (64 MB sanal,
// 4 MB RSS) tek bir kacak arenanin (512 MB) / tek dongunun worker yiginlarinin
// altinda.
namespace {
struct SurecOlcumu {
  size_t rss, sanal;
  uint32_t thread;
};
SurecOlcumu surec_olc() { return {platform::os_resident_bytes(), platform::os_virtual_bytes(), platform::os_thread_count()}; }
// Bir "dusen kurulum + kapanis" dongusu. Donus: kurulum beklenmedik sekilde
// BASARILI oldu mu (o zaman olculen yol kosmamistir).
bool dusen_kurulum(const char *baslik) {
  teng_set_headless(3, nullptr); // pencere ACILMAZ
  const int ok = teng_init(baslik, 64, 64);
  if (ok) { teng_shutdown(); return true; }
  CHECK(teng_running() == 0);
  CHECK(teng_last_error()[0] != 0);
  teng_shutdown(); // kurulmamis motorda guvenli (sozlesme)
  return false;
}
// N dongu kosturur, uc olcuyu karsilastirir. `beklenen_hata`: eng_last_error
// bu dizgiyi icermeli — dogru ASAMADA dustugumuzun kaniti; baska bir sebeple
// dusen kurulum baska bir yolu olcer. Donus: olcum yapilabildi mi (kurulum
// basarili olursa yapilamaz; cagiran ne diyecegine karar verir).
bool kacak_kapisi(const char *senaryo, const char *beklenen_hata, int n) {
  teng_log_level(1);
  if (dusen_kurulum("isinma")) return false;
  if (!std::strstr(teng_last_error(), beklenen_hata)) {
    std::printf("    [bilgi] %s: kurulum beklenen yerden dusmedi: '%s' (beklenen '*%s*')\n", senaryo, teng_last_error(), beklenen_hata);
    CHECK(false);
    return true;
  }
  const SurecOlcumu a = surec_olc();
  for (int i = 0; i < n; i++)
    if (dusen_kurulum("yeniden deneme")) { CHECK(false); return true; }
  const SurecOlcumu b = surec_olc();
  const long long d_rss = (long long)b.rss - (long long)a.rss, d_sanal = (long long)b.sanal - (long long)a.sanal;
  const int d_thread = (int)b.thread - (int)a.thread;
  std::printf("    [bilgi] %s, %d dusen kurulum ('%s'): RSS %zu -> %zu KB (%+lld KB), sanal %+lld MB, thread %u -> %u (%+d)\n", senaryo, n,
              teng_last_error(), a.rss / 1024, b.rss / 1024, d_rss / 1024, d_sanal / (1024 * 1024), a.thread, b.thread, d_thread);
  CHECK(a.rss > 0 && a.sanal > 0 && a.thread > 0); // olcu aletleri bu platformda calisiyor
  // Worker'lar JOIN edildi. Esik "deneme basina en az bir sizan thread"in
  // altinda (kacak sinifi her denemede cekirdek-1 >= 1 worker birakir), sifir
  // DEGIL: tam kosumda onceki testlerin surucu/ses thread'leri es zamanli
  // bitip baslayabiliyor — olculdu CI ubuntu-latest lavapipe 2026-10-05:
  // 41 -> 42 (+1), ayni kosumda ICD senaryosu +0.
  CHECK(d_thread < n);
  CHECK(d_sanal < 64ll * 1024 * 1024);             // tek bir kacak arena bile 512 MB olurdu
  CHECK(d_rss < 4ll * 1024 * 1024);                // dokunulan arena sayfalari + thread yiginlari geri dondu
  return true;
}
} // namespace

ENGINE_TEST(bridge_failed_init_without_loader_leaves_no_residue) {
  // Yukleyici kapatilir (TULPAR_ENGINE_NO_VULKAN, vk_api_load'un pozitif
  // kontrol anahtari): kurulum arena + is sistemi + profiler'dan SONRA,
  // yukleyicide duser. Her makinede kosar.
  setenv("TULPAR_ENGINE_NO_VULKAN", "1", 1);
  const bool olculdu = kacak_kapisi("yukleyici yok", "Vulkan loader yok", 8);
  unsetenv("TULPAR_ENGINE_NO_VULKAN");
  if (!olculdu) {
    std::printf("    [bilgi] TULPAR_ENGINE_NO_VULKAN=1 iken kurulum BASARILI — anahtar calismiyor\n");
    CHECK(false);
  }
}

ENGINE_TEST(bridge_failed_init_without_icd_leaves_no_residue) {
  // Windows CI'nin gercek yolu: yukleyici VAR, ICD YOK -> vkCreateInstance
  // VK_ERROR_INCOMPATIBLE_DRIVER. Burada ICD listesi olmayan bir dosyaya
  // yonlendirilerek uretilir (VK_DRIVER_FILES; eski yukleyiciler icin
  // VK_ICD_FILENAMES). Yukleyici yoksa olculemez (ATLANDI); yukleyici
  // dogrudan MoltenVK ise degisken yok sayilir ve kurulum basarili olur — o
  // da ATLANDI (olculen yol kosmadi). Bu yolda yukleyici dlopen EDILMISTIR:
  // geri alma onu da kapatmali.
#if defined(__APPLE__)
  // macOS'ta bu yol YOK: ICD gizlenince rhi/device.cpp'nin yedegi MoltenVK'yi
  // DOGRUDAN yukler ve kurulum basarir (olculdu CI macos-latest 2026-10-05:
  // "[rhi] loader ATLANDI: MoltenVK DOGRUDAN yuklendi", GPU Apple Paravirtual).
  // Dusen kurulumun geri alinmasi orada yukleyici-yok senaryosuyla olculur.
  skip("macOS: ICD gizlenince kurulum MoltenVK'yi dogrudan yukleyip basarir — 'yukleyici var, ICD yok' yolu bu platformda yok");
  return;
#endif
  {
    rhi::VkApi api;
    if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok — 'yukleyici var, ICD yok' yolu olculemez"); return; }
    rhi::vk_api_unload(api);
  }
  char yok[512];
  std::snprintf(yok, sizeof yok, "%s/tulpar_icd_yok.json", tmp_dir());
  setenv("VK_DRIVER_FILES", yok, 1);
  setenv("VK_ICD_FILENAMES", yok, 1);
  // Gizleme GERCEKTEN tuttu mu? Motor kurulmadan ham bir vkCreateInstance ile
  // sorulur: tutmadiysa kurulum basarir ve bu kapi olculmek istenen yolu degil,
  // basarili bir ikinci oturumu kostururdu (o yolu ayrica
  // bridge_second_session_in_same_process_starts_clean olcer, 8ct). Windows'ta YONETICI surec
  // bu degiskenleri YOK SAYAR (CI windows-latest, lavapipe kayit defterinden;
  // olculdu 2026-10-05) — orada bu yol yalniz yonetici olmayan surecte olculur.
  {
    rhi::VkApi api;
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    if (rhi::vk_api_load(api)) {
      VkApplicationInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      ai.apiVersion = VK_API_VERSION_1_1;
      VkInstanceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      ci.pApplicationInfo = &ai;
      VkInstance inst = VK_NULL_HANDLE;
      r = api.vkCreateInstance(&ci, nullptr, &inst);
      if (r == VK_SUCCESS) {
        rhi::vk_api_load_instance(api, inst);
        api.vkDestroyInstance(inst, nullptr);
      }
      rhi::vk_api_unload(api);
    }
    if (r == VK_SUCCESS) {
      unsetenv("VK_DRIVER_FILES");
      unsetenv("VK_ICD_FILENAMES");
      skip("yukleyici VK_DRIVER_FILES/VK_ICD_FILENAMES'i yok sayiyor (Windows yonetici sureci) — ICD gizlenemedi, 'yukleyici var, ICD yok' yolu olculmedi");
      return;
    }
  }
  const bool olculdu = kacak_kapisi("yukleyici var, ICD yok", "INCOMPATIBLE_DRIVER", 8);
  unsetenv("VK_DRIVER_FILES");
  unsetenv("VK_ICD_FILENAMES");
  if (!olculdu) skip("ICD gizlenemedi (yukleyici VK_DRIVER_FILES'i yok sayiyor; dogrudan MoltenVK?) — kurulum basarili oldu, yol olculmedi");
}

// --- Ayni surecte IKINCI (ve N.) basarili oturum (Tuzaklar 8ct) ---------------
// teng_init -> kareler -> teng_shutdown, ayni surecte tekrar tekrar. Eskiden
// kapanis yalniz `inited = false` diyordu: 512 MB arena rezervi, profiler ve
// Bridge'in butun alanlari (varliklar, sahne, betik havuzlari, sayaclar) kaliyor,
// ikinci teng_init eskisinin ustune yeni arena rezerv ediyor ve bayat
// tablolarla kuruluyordu (CI macOS 2026-10-05: ikinci oturum SIGSEGV).
// Olculen, her oturumda: taze baslangic (varlik 0, kare 0), sahne gercekten
// ciziliyor (bos kareyle piksel farki) ve ILK oturumun karesiyle AYNI (ayni
// sahne + ayni kare sayisi; bayat durum farki gosterirdi), onceki oturumun
// id'si yeni oturumda olu (nesil korunuyor), hata sayaci artmiyor. Isinma
// oturumundan sonra N oturum: sanal boyut, RSS, thread sayisi ve kapanista
// canli Vulkan nesnesi (teng_vk_live) BUYUMUYOR.
// POZITIF KONTROL (RTX 5080 / Linux, 2026-10-05): duzeltmesiz kopru ->
// ikinci oturumda 5 kontrol KIRMIZI (varlik 3, kare 32, eski id CANLI, cizim
// sayisi yanlis) ve ikinci kapanis SIGSEGV.
// Bu, editorun F5'i DEGIL: F5 oyunu her seferinde AYRI SURECTE baslatir
// (app/editor_game.hpp). Bu yol: oyunun "motoru kapat / yeniden ac" akisi
// (ayar degisince yeniden kurulum, Android'de surec canliyken etkinlik yeniden
// yaratilinca android_main'in yeniden cagrilmasi).
namespace {
constexpr int kOturumKare = 30;
alignas(16) uint8_t g_ilk_sahne[kW * kH * 3];
alignas(16) uint8_t g_oturum_bos[kW * kH * 3];
alignas(16) uint8_t g_oturum_sahne[kW * kH * 3];
struct OturumOlcu {
  bool kuruldu = false;
  int vk_canli = -1;           // kapanistan sonra teng_vk_live
  uint32_t sahne_bos_fark = 0; // sahne karesi - bos kare (piksel)
  uint32_t ilk_fark = 0;       // sahne karesi - ilk oturumun sahne karesi
  int kutu = 0;                // bu oturumun kutu id'si
  bool sanal_gpu = false;      // test::gpu_is_virtual (kurulu iken okunur)
};
// teng_init'in stdout'a bastigi satirlari yakala (kurulum satirinin kare
// oneki olculsun). Yakalanan metin gercek stdout'a da aynen basilir.
char g_init_log[4096];
int init_yakala(const char *baslik) {
  g_init_log[0] = 0;
  char yol[512];
  tmp_template(yol, sizeof yol, "oturum_log");
  const int fd = mkstemp(yol);
  if (fd < 0) return teng_init(baslik, kW, kH);
  std::fflush(stdout);
  const int eski = dup(1);
  dup2(fd, 1);
  const int ok = teng_init(baslik, kW, kH);
  std::fflush(stdout);
  dup2(eski, 1);
  close(eski);
  lseek(fd, 0, SEEK_SET);
  const long n = (long)read(fd, g_init_log, sizeof g_init_log - 1);
  g_init_log[n > 0 ? n : 0] = 0;
  close(fd);
  unlink(yol);
  std::fputs(g_init_log, stdout);
  return ok;
}
// tur 0: ilk oturum (sahne karesi g_ilk_sahne'ye). `onceki_kutu`: bir onceki
// oturumun id'si — bu oturumda OLU olmali.
OturumOlcu oturum_kos(int tur, int onceki_kutu, const char *yol_bos, const char *yol_sahne) {
  OturumOlcu o;
  teng_set_headless(100000, nullptr); // ayarlar oturuma tasinmaz: her kurulumdan ONCE
  if (!init_yakala("ikinci oturum")) return o;
  o.kuruldu = true;
  // Kurulum satirinin kare oneki bu oturumun karesi (0): eskiden ayni
  // surecteki ikinci oturum onceki oturumun son karesini basiyordu (Android
  // emulatorunde etkinlik yeniden yaratilinca `k60 bilgi kurulum`, olculdu
  // 2026-10-06). Log seviyesi 1: bilgi satirlari basilir.
  CHECK(std::strstr(g_init_log, "[engine_bridge] k0 bilgi kurulum") != nullptr);
  o.sanal_gpu = test::gpu_is_virtual(teng_gpu_name());
  CHECK(teng_running() == 1);
  CHECK(teng_count() == 0); // onceki oturumun varliklari yok
  CHECK(teng_frame() == 0); // sayaclar sifirdan
  CHECK(teng_scene_count() == 0);
  teng_camera(0, 4, 10, 0, 0.5, 0);
  run_frames(2);
  CHECK(teng_screenshot(yol_bos) == 1);
  const int zemin = teng_spawn_ground(8.0, 0xCED4DAFF);
  o.kutu = teng_spawn_box(0, 2.0, 0, 0.5, 0.5, 0.5, 1, 0xE63946FF);
  const int isik = teng_spawn_light(0, 3, 2, 0xF77F00FF, 3.0, 8.0);
  CHECK(zemin > 0 && o.kutu > 0 && isik > 0);
  CHECK(teng_count() == 3);
  if (onceki_kutu) {
    CHECK(teng_alive(onceki_kutu) == 0); // ayni yuva, yeni nesil: eski id ona DENK GELMEZ
    CHECK(o.kutu != onceki_kutu);
  }
  run_frames(kOturumKare);
  CHECK(teng_draw_count() == 2); // zemin + kutu
  CHECK(teng_screenshot(yol_sahne) == 1);
  teng_shutdown();
  CHECK(teng_running() == 0);
  o.vk_canli = teng_vk_live();
  uint32_t w = 0, h = 0;
  const uint32_t nb = read_ppm(yol_bos, g_oturum_bos, kW * kH, &w, &h);
  uint8_t *hedef = tur == 0 ? g_ilk_sahne : g_oturum_sahne;
  const uint32_t ns = read_ppm(yol_sahne, hedef, kW * kH, &w, &h);
  CHECK(nb == kW * kH && ns == kW * kH);
  if (nb == kW * kH && ns == kW * kH) {
    o.sahne_bos_fark = diff_px(hedef, g_oturum_bos, ns);
    o.ilk_fark = tur == 0 ? 0 : diff_px(hedef, g_ilk_sahne, ns);
  }
  return o;
}
} // namespace

ENGINE_TEST(bridge_second_session_in_same_process_starts_clean) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  char yol_bos[512], yol_sahne[512];
  tmp_template(yol_bos, sizeof yol_bos, "oturum_bos");
  tmp_template(yol_sahne, sizeof yol_sahne, "oturum_sahne");
  char *yollar[2] = {yol_bos, yol_sahne};
  for (int i = 0; i < 2; i++) { const int fd = mkstemp(yollar[i]); if (fd >= 0) close(fd); }
  teng_log_level(1);
  const int err0 = teng_error_count();
  // Isinma: surec basina bir kez olan seyler (surucunun ilk yuklenisi,
  // shader/PSO onbellegi, cokme raporlayicisi) olcumun disinda kalsin.
  const OturumOlcu ilk = oturum_kos(0, 0, yol_bos, yol_sahne);
  if (!ilk.kuruldu) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  // Oturum basina ARTIS olculur, uc fark degil (Tuzaklar 8cs dersi): olculdu
  // RTX 5080 / Linux, 2026-10-05, 10 ek oturum — ikinci oturumda TEK SEFERLIK
  // ~+19-21 MB RSS ve +86..150 MB sanal (glibc ana yigininin tepe noktasi:
  // mallinfo2 "kullanimda" 0,76 -> 0,88 MB, "arena" 17 -> 40 MB; bosaltilan ama
  // sisteme geri verilmeyen yigin), sonra duz (0-4 KB/oturum); sanal boyutta
  // ara sira +64 MB basamak (glibc'nin thread basina malloc arenasi rezervi;
  // RSS'e yansimiyor). CI ubuntu lavapipe (ayni gun): oturum basina +1,2..2,9 MB
  // RSS SURUYOR (canli vk sabit 4; ayni kod RTX 5080'de 0-4 KB, MoltenVK'de
  // 80-272 KB — surucu davranisi, olculur ve basilir, iddia edilmez).
  // Kacak sinifi ise HER oturumda artar: arena +512 MB sanal, worker'lar
  // cekirdek-1 thread. Olcu bu yuzden oturum artislarinin EN KUCUGU (tek
  // seferlik ya da ara sira basamaklar onu oynatmaz): en kucuk sanal artis
  // < 256 MB, en kucuk RSS artisi < 4 MB, thread toplami < kN.
  // Esiklerin pozitif kontrolu (RTX 5080, 2026-10-05): teste oturum basina
  // 512 MB'lik kacak rezerv enjekte edilince her oturum +512..576 MB -> KIRMIZI.
  constexpr int kN = 5;
  SurecOlcumu onceki_olcu = surec_olc();
  const SurecOlcumu a = onceki_olcu;
  int onceki = ilk.kutu, vk_esit = 1;
  long long sanal_max = 0, rss_max = 0, sanal_min = 1ll << 62, rss_min = 1ll << 62;
  uint32_t ilk_fark_max = 0, sahne_bos_min = ilk.sahne_bos_fark;
  bool hepsi_kuruldu = true;
  for (int i = 0; i < kN; i++) {
    const OturumOlcu o = oturum_kos(i + 1, onceki, yol_bos, yol_sahne);
    if (!o.kuruldu) {
      std::printf("    [bilgi] oturum %d KURULAMADI: %s\n", i + 2, teng_last_error());
      hepsi_kuruldu = false;
      break;
    }
    onceki = o.kutu;
    if (o.vk_canli != ilk.vk_canli) vk_esit = 0;
    ilk_fark_max = std::max(ilk_fark_max, o.ilk_fark);
    sahne_bos_min = std::min(sahne_bos_min, o.sahne_bos_fark);
    const SurecOlcumu m = surec_olc();
    const long long ds = (long long)m.sanal - (long long)onceki_olcu.sanal, dr = (long long)m.rss - (long long)onceki_olcu.rss;
    onceki_olcu = m;
    sanal_min = std::min(sanal_min, ds);
    rss_min = std::min(rss_min, dr);
    sanal_max = std::max(sanal_max, ds);
    rss_max = std::max(rss_max, dr);
    std::printf("    [bilgi] oturum %d: sahne-bos %u px, ilk oturumdan fark %u px, kapanista canli vk %d; RSS %+lld KB, sanal %+lld MB, thread %u\n",
                i + 2, o.sahne_bos_fark, o.ilk_fark, o.vk_canli, dr / 1024, ds / (1024 * 1024), m.thread);
  }
  const SurecOlcumu b = surec_olc();
  const int d_thread = (int)b.thread - (int)a.thread;
  std::printf("    [bilgi] %d ek oturum (isinmadan sonra): RSS %zu -> %zu KB, sanal %zu -> %zu MB, thread %u -> %u (%+d); oturum basina RSS "
              "%+lld..%+lld KB, sanal %+lld..%+lld MB (en kucuk..en buyuk); kapanista canli vk %d (hepsi esit %d)\n",
              kN, a.rss / 1024, b.rss / 1024, a.sanal >> 20, b.sanal >> 20, a.thread, b.thread, d_thread, rss_min / 1024, rss_max / 1024,
              sanal_min / (1024 * 1024), sanal_max / (1024 * 1024), ilk.vk_canli, vk_esit);
  std::remove(yol_bos);
  std::remove(yol_sahne);
  CHECK(hepsi_kuruldu);
  CHECK(teng_error_count() == err0);
  // Sahne gercekten cizildi (her oturumda) ve ilk oturumla ayni kare. Olculdu
  // RTX 5080 2026-10-05: sahne-bos 37402 px, ilk oturumdan fark 0 px; bayat
  // durumla (duzeltmesiz kod) ikinci oturum eski varliklari da cizer.
  // Sanal GPU'da (Apple Paravirtual, CI macOS) PIKSEL blogu atlanir — olculdu
  // CI macos-latest 2026-10-05: sahne-bos 0 px (dosyanin diger piksel
  // kapilariyla ayni sinif); taze durum, olu id, vk ve surec kapilari orada da kosar.
  if (ilk.sanal_gpu) {
    skip("sanal GPU (Apple Paravirtual, CI macOS): ikinci oturumun PIKSEL blogu gercek cihazda olculur");
  } else {
    CHECK(sahne_bos_min > 500);
    CHECK(ilk_fark_max <= 64);
  }
  // Vulkan: kapanista cihazin yalniz kendi nesneleri; oturumdan oturuma ayni.
  CHECK(ilk.vk_canli >= 0);
  CHECK(vk_esit == 1);
  CHECK(a.rss > 0 && a.sanal > 0 && a.thread > 0);
  CHECK(sanal_min < 256ll * 1024 * 1024);
  CHECK(rss_min < 4ll * 1024 * 1024);
  CHECK(d_thread < kN);
}

// Kapanis raporunun "hata N, uyari N"si oturuma ait (8ct'nin devami): ayni
// surecte ikinci oturumun raporu onceki oturumun hatalarini da sayiyordu
// (Huawei P20 Pro, Android 10, 2026-10-07: pencere gelmeyen 5 oturumun
// raporlari "uyari 1..5"). teng_error_count() SUREC toplami olarak kalir.
namespace {
char g_kapanis_log[8192];
void kapanis_yakala() {
  g_kapanis_log[0] = 0;
  char yol[512];
  tmp_template(yol, sizeof yol, "kapanis_log");
  const int fd = mkstemp(yol);
  if (fd < 0) { teng_shutdown(); return; }
  std::fflush(stdout);
  const int eski = dup(1);
  dup2(fd, 1);
  teng_shutdown();
  std::fflush(stdout);
  dup2(eski, 1);
  close(eski);
  lseek(fd, 0, SEEK_SET);
  const long n = (long)read(fd, g_kapanis_log, sizeof g_kapanis_log - 1);
  g_kapanis_log[n > 0 ? n : 0] = 0;
  close(fd);
  unlink(yol);
  std::fputs(g_kapanis_log, stdout);
}
} // namespace

ENGINE_TEST(bridge_shutdown_report_counts_this_session_only) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  const int err0 = teng_error_count();
  // Oturum 1: bilerek BIR hata (olu id).
  teng_set_headless(100000, nullptr);
  if (!teng_init("rapor sayaci 1", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  (void)teng_x(987654); // KONTROL: olu id hata loglar
  kapanis_yakala();
  CHECK(teng_error_count() == err0 + 1);
  CHECK(std::strstr(g_kapanis_log, ", hata 1, uyari 0") != nullptr);
  // Oturum 2: hatasiz. Rapor 0 demeli; surec toplami degismemeli.
  teng_set_headless(100000, nullptr);
  CHECK(teng_init("rapor sayaci 2", kW, kH) == 1);
  kapanis_yakala();
  CHECK(std::strstr(g_kapanis_log, ", hata 0, uyari 0") != nullptr); // duzeltmesiz kod "hata 1" basar
  CHECK(teng_error_count() == err0 + 1);
}

// --- Tulpar yerel eklenti bildiriminin ABI kilidi (K303) ----------------------
// TulparLang eng_* cagrisini tulpar-ext.json'daki C tipleriyle DOGRUDAN
// teng_*'e indirir ve statik arsivde tip goremez. Tip uyumunu
// bridge/tulpar_abi.cpp DERLEMEDE kilitler; bu test o tabloyu baglar, yani
// bildirimdeki her sembolun arsivde TANIMLI olmasini da zorlar (yoksa
// engine_tests linklenmez). Sayi bildirimle ayni kaynaktan (SPEC).
namespace tulpar::engine::bridge {
using TulparAbiFn = void (*)(void);
extern const TulparAbiFn kTulparAbiTable[];
extern const int kTulparAbiCount;
} // namespace tulpar::engine::bridge

#define TENG_ABI(R, NAME, SYM, PARAMS)
#include "bridge/tulpar_ext_abi.inc"
#undef TENG_ABI

ENGINE_TEST(bridge_tulpar_abi_lock_links_every_manifest_symbol) {
  std::printf("    [bilgi] Tulpar eklenti bildirimi: %d fonksiyon, ABI tablosu %d\n", TENG_ABI_COUNT,
              tulpar::engine::bridge::kTulparAbiCount);
  CHECK(tulpar::engine::bridge::kTulparAbiCount == TENG_ABI_COUNT);
  int bos = 0;
  for (int i = 0; i < tulpar::engine::bridge::kTulparAbiCount; i++)
    if (!tulpar::engine::bridge::kTulparAbiTable[i]) bos++;
  CHECK(bos == 0);
}

// --- Android APK varlik suzgeci (bridge/asset_filter.hpp, 2026-10-07) ----------
// Suzgec yalniz Android'de derleniyordu ve `.mp3`/`.flac` eksikti: oyunun
// muzigi telefonda CIKARILMIYOR, masaustunde caliyordu (docs/OYUN_GERI_BILDIRIM.md
// madde 1). Kapi: ses kod cozucunun cozdugu her bicim (WAV, MP3, FLAC — audio/
// miniaudio_impl.c) ve oyunlarin kullandigi varlik turleri cikarilir; buyuk harfli
// uzanti da. KONTROL: uzantisiz ad, `.tmp`, `.tpr` (kaynak) ve uzantinin bir
// parcasi (`.mp`) cikarilMAZ — suzgec "hepsini al" diye yesil gecmesin.
ENGINE_TEST(bridge_android_asset_filter_keeps_every_decodable_audio_format) {
  using tulpar::engine::bridge::android_asset_wanted;
  const char *istenen[] = {"assets/sesler/muzik_savas.mp3", "a/b.flac", "ses_ok.wav", "MUZIK.MP3", "x.Flac",
                           "savas.sahneb", "savas.sahne", "modeller/kup.gltf", "fonts/DejaVuSans.ttf", "ikon.png"};
  const char *istenmeyen[] = {"OKUBENI", "kayit.tmp", "oyun.tpr", "x.mp", "mp3", "", "a.mp3.bak"};
  int ok = 0, red = 0;
  for (const char *n : istenen) { const bool w = android_asset_wanted(n); CHECK(w); ok += w ? 1 : 0; }
  for (const char *n : istenmeyen) { const bool w = android_asset_wanted(n); CHECK(!w); red += w ? 0 : 1; }
  CHECK(!android_asset_wanted(nullptr));
  std::printf("    [bilgi] varlik suzgeci: %d/%d istenen cikarilir, %d/%d istenmeyen elenir\n", ok,
              (int)(sizeof istenen / sizeof istenen[0]), red, (int)(sizeof istenmeyen / sizeof istenmeyen[0]));
}

// --- Android yasam dongusu komut adlari (bridge/android_lifecycle.hpp) ---------
// Geri bildirim #18: tablo yalniz Android'de derleniyordu ve 16 glue komutunun
// 12'sini biliyordu; her acilista 4-5 `android cmd ?` satiri. Kapi: 0..15 her
// komutun adi var, adlar BIRBIRINDEN farkli (kopyala-yapistir ayni adi iki
// komuta vermesin), tablo disi sayi nullptr (cagiran sayiyla basar). Sayilarin
// glue'ya esitligi Android derlemesinde static_assert (android_host.cpp).
// KONTROL: ayni olcu, eski 12 adli tabloyu (switch'in bildikleri) 4 eksikle
// kirmizi gorur — kapi "hepsi adli" demeyi gercekten ayirt ediyor.
ENGINE_TEST(bridge_android_lifecycle_names_every_glue_command) {
  using namespace tulpar::engine::bridge;
  int adli = 0, cift = 0;
  for (int32_t c = 0; c < kAndroidCmdCount; c++) {
    const char *a = android_cmd_name(c);
    CHECK(a && a[0]);
    if (a && a[0]) adli++;
    for (int32_t d = 0; d < c; d++)
      if (a && android_cmd_name(d) && std::strcmp(a, android_cmd_name(d)) == 0) cift++;
  }
  CHECK(cift == 0);
  CHECK(android_cmd_name(-1) == nullptr && android_cmd_name(kAndroidCmdCount) == nullptr);
  CHECK(kAndroidCmdCount == 16 && kAndroidCmdDestroy == 15 && kAndroidCmdLowMemory == 9);
  CHECK(std::strcmp(android_cmd_name(kAndroidCmdContentRectChanged), "CONTENT_RECT_CHANGED") == 0);
  // KONTROL: eski host'un switch'inin bildigi 12 komut — ayni olcu eksigi sayar.
  const int32_t eski[] = {kAndroidCmdInitWindow, kAndroidCmdTermWindow, kAndroidCmdWindowResized, kAndroidCmdGainedFocus,
                          kAndroidCmdLostFocus,  kAndroidCmdConfigChanged, kAndroidCmdStart, kAndroidCmdResume,
                          kAndroidCmdPause,      kAndroidCmdStop,          kAndroidCmdDestroy, kAndroidCmdLowMemory};
  int eski_eksik = 0;
  for (int32_t c = 0; c < kAndroidCmdCount; c++) {
    bool var = false;
    for (int32_t e : eski) var = var || e == c;
    if (!var) eski_eksik++;
  }
  CHECK(eski_eksik == 4);
  std::printf("    [bilgi] android komut adlari: %d/%d adli, %d cift; KONTROL: eski tabloda %d adsiz komut (INPUT_CHANGED, "
              "WINDOW_REDRAW_NEEDED, CONTENT_RECT_CHANGED, SAVE_STATE)\n",
              adli, (int)kAndroidCmdCount, cift, eski_eksik);
}

// --- Geri tusu (Geri bildirim #8) ------------------------------------------------
// Android'de geri tusu etkinligi KAPATIYORDU (savas kayboluyordu). Iki parca:
// (1) host mandali: `input keyevent KEYCODE_BACK` DOWN ile UP'i AYNI pompada
// verir — "su an basili mi" tutan bir host basisi kaybeder; mandal SAYAR.
// (2) cekirdek: eng_back_pressed tam BIR kare true, oyun sorunca host'a
// "tuket" der (cihazda: tools/android_yasam_dongusu.sh). KONTROL: ayni olay
// dizisini "kare basinda basili mi" diye ornekleyen saf okuma basisi kaybeder.
ENGINE_TEST(bridge_back_key_latch_keeps_press_released_in_same_pump) {
  using namespace tulpar::engine::bridge;
  BackKeyLatch l;
  BackKeySample smp;
  CHECK(!smp.sample(l.presses));
  // Pompa 1: DOWN + UP (adb keyevent / hizli parmak) — kare ornegi ARADA degil.
  l.on_key(true, 0);
  l.on_key(false, 0);
  const bool saf_okuma = l.down; // KONTROL: eski "basili mi" ornegi
  const bool k1 = smp.sample(l.presses);
  CHECK(k1 && smp.count == 1);
  CHECK(!saf_okuma);
  CHECK(!smp.sample(l.presses)); // sonraki kare: kenar yok
  // Basili tutma: tekrar olaylari yeni basis degil.
  l.on_key(true, 0);
  CHECK(smp.sample(l.presses) && l.down);
  l.on_key(true, 1);
  l.on_key(true, 2);
  CHECK(!smp.sample(l.presses) && l.down);
  l.on_key(false, 0);
  CHECK(!smp.sample(l.presses) && !l.down);
  // Iki basis tek pompada: BIR kenar, sayisi 2.
  l.on_key(true, 0); l.on_key(false, 0); l.on_key(true, 0); l.on_key(false, 0);
  CHECK(smp.sample(l.presses) && smp.count == 2);
  std::printf("    [bilgi] geri mandali: DOWN+UP ayni pompada -> kenar %d (saf 'basili mi' okumasi %d), tekrarlar sayilmaz, cift basis count %u\n",
              (int)k1, (int)saf_okuma, smp.count);
}

ENGINE_TEST(bridge_back_pressed_is_one_frame_edge_headless) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("geri tusu", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  const int e0 = teng_error_count();
  teng_frame_begin();
  CHECK(teng_back_pressed() == 0); // KONTROL: enjeksiyonsuz kare
  teng_frame_end();
  teng_back_test_press();
  teng_frame_begin();
  const int k1 = teng_back_pressed();
  const int k1b = teng_back_pressed(); // ayni kare, ikinci soru: yine true (tuketmez)
  const int esc = teng_key_pressed("ESC"); // masaustu/penceresiz: Esc tusu ayri, eslenmez
  teng_frame_end();
  teng_frame_begin();
  const int k2 = teng_back_pressed();
  teng_frame_end();
  CHECK(k1 == 1 && k1b == 1 && k2 == 0 && esc == 0);
  CHECK(teng_key_pressed("GERI") == 0 && teng_error_count() == e0); // "GERI"/"BACK" gecerli tus adi
  std::printf("    [bilgi] geri: enjekte kare %d (ikinci soru %d), sonraki kare %d, penceresizde ESC %d\n", k1, k1b, k2, esc);
  kapanis_yakala();
  CHECK(std::strstr(g_kapanis_log, "kapanis (geri tusu): 1 basis, oyuna bagli evet") != nullptr);
}

// --- Arka planda ses (Geri bildirim #9) -------------------------------------------
// Android'de ana ekrana donunce oyunun AAudio akisi `state:started` kaliyordu.
// Cekirdek host'un arka plan durumunu kare basinda gorur ve ses cihazini
// DURDURUR, donuste SURDURUR. Penceresizde host yok: teng_debug_app_pause taklit
// eder (cihazda: tools/android_yasam_dongusu.sh `dumpsys audio`). Olcu: mixer
// callback sayaci (NULL arka uc). KONTROL: on plandayken ayni bekleme sayaci artirir.
ENGINE_TEST(bridge_app_pause_stops_and_resumes_audio_headless) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  setenv("TULPAR_ENGINE_AUDIO_NULL", "1", 1);
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("arka plan", kW, kH)) { unsetenv("TULPAR_ENGINE_AUDIO_NULL"); skip("Vulkan cihazi/kurulum yok"); return; }
  const int e0 = teng_error_count();
  CHECK(teng_audio_open(0, 0) == 1);
  const int ton = teng_audio_tone(440, 0.5);
  CHECK(ton >= 0 && teng_audio_play(ton, 0.2, 1) > 0);
  auto kare = [] { teng_frame_begin(); teng_frame_end(); };
  kare();
  platform::thread_sleep_us(100000);
  const long long c0 = teng_debug_audio_callbacks();
  platform::thread_sleep_us(100000);
  const long long c1 = teng_debug_audio_callbacks(); // KONTROL: on planda artar
  CHECK(teng_app_paused() == 0);
  teng_debug_app_pause(1);
  kare(); // gecis kare basinda
  const int p1 = teng_app_paused();
  const long long c2 = teng_debug_audio_callbacks();
  platform::thread_sleep_us(150000);
  kare();
  const long long c3 = teng_debug_audio_callbacks(); // arka planda sabit
  CHECK(teng_audio_play(ton, 0.2, 0) > 0);           // ses cagrilari arka planda da gecerli (hata degil)
  teng_debug_app_pause(0);
  kare();
  const int p2 = teng_app_paused();
  platform::thread_sleep_us(150000);
  const long long c4 = teng_debug_audio_callbacks();
  std::printf("    [bilgi] arka plan: on planda +%lld callback / 100 ms, arka planda +%lld / 150 ms, donuste +%lld / 150 ms; paused %d -> %d\n",
              c1 - c0, c3 - c2, c4 - c3, p1, p2);
  CHECK(c1 > c0 && c3 == c2 && c4 > c3);
  CHECK(p1 == 1 && p2 == 0 && teng_error_count() == e0 && teng_low_memory_count() == 0);
  kapanis_yakala();
  unsetenv("TULPAR_ENGINE_AUDIO_NULL");
  CHECK(std::strstr(g_kapanis_log, "kapanis (yasam dongusu): 1 kez arka plan, ses 1 kez durduruldu") != nullptr);
}

// --- Guvenli alan (Geri bildirim #17) ---------------------------------------------
// Telefonda pencere centige uzaninca (TulparLang [android] cutout = "short_edges")
// HUD centigin altina girmesin diye oyun kenar bosluklarini okur. Host degeri
// kare basinda alinir; penceresizde host yok, teng_debug_safe_insets taklit eder
// (cihazda: tools/android_yasam_dongusu.sh "guvenli alan" satiri). KONTROL:
// taklitsiz ve masaustu/penceresiz 0 — "hep 81" donen bir yol da yesil olmasin.
ENGINE_TEST(bridge_safe_insets_follow_host_each_frame_headless) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("guvenli alan", kW, kH)) { skip("Vulkan cihazi/kurulum yok"); return; }
  auto kare = [] { teng_frame_begin(); teng_frame_end(); };
  kare();
  const int s0 = teng_safe_inset_left() + teng_safe_inset_top() + teng_safe_inset_right() + teng_safe_inset_bottom();
  teng_debug_safe_insets(81, 0, 0, 12);
  const int once = teng_safe_inset_left(); // kare basindan once degismez
  kare();
  const int l = teng_safe_inset_left(), t = teng_safe_inset_top(), r = teng_safe_inset_right(), b = teng_safe_inset_bottom();
  teng_debug_safe_insets(0, 0, 81, 0); // yon degisti: centik saga gecti
  kare();
  const int r2 = teng_safe_inset_right(), l2 = teng_safe_inset_left();
  teng_debug_safe_insets(-1, -1, -1, -1); // taklit kapali: penceresizde 0
  kare();
  const int s3 = teng_safe_inset_left() + teng_safe_inset_right() + teng_safe_inset_bottom();
  std::printf("    [bilgi] guvenli alan: baslangic %d, taklit (81,0,0,12) -> (%d,%d,%d,%d), yon (0,0,81,0) -> sol %d sag %d, kapali %d\n", s0, l, t, r,
              b, l2, r2, s3);
  CHECK(s0 == 0 && once == 0);
  CHECK(l == 81 && t == 0 && r == 0 && b == 12);
  CHECK(l2 == 0 && r2 == 81 && s3 == 0);
  teng_shutdown();
}

// --- Geri bildirim #10: fizik duraklatma + zaman olcegi ------------------------
// Oyunun duraklat menusunde mermiler ucmaya devam ediyordu: teng_frame_end fizigi
// her kare adimliyordu ve kopruden durdurmanin yolu yoktu. Olcu (uc oturum, ayni
// surecte, her biri sifirdan): A = 40 kare + 25 kare DURAKLI + 60 kare; B = 100
// kare duraklamasiz. Duraklatma birikimi DONDURUR (sim::FixedStep), yani A'nin
// son durumu B'ninkiyle BIT-TAM ayni olmali; duraklik boyunca govde KIPIRDAMAZ,
// temas/tetik olayi gelmez, sim adimi sayaci artmaz. KONTROL: govde gercekten
// hareket ediyor (B'de kare 40 ile 100 arasi fark var) -- yoksa "ayni" bir sey
// soylemezdi. Zaman olcegi 0.5: 100 karede 50 sim adimi ve son durum 50 karelik
// olceksiz kosuyla bit-tam ayni (olcek girdiye uygulanir, adima degil).
namespace {
struct FizikIz {
  double x, y, z, vy;
  int tick;
};
// Bir oturum: zemine dusup yuvarlanan kure + yuksekten dusen kutu. `once` kare
// sonra `durakli` kare duraklatilir (negatifse KONTROL: duraklatmadan sayilir).
FizikIz fizik_oturumu(const char *ad, int once, int durakli, int sonra, double olcek, int *durakli_hareket, int *durakli_olay,
                      int *durakli_tick) {
  FizikIz iz{};
  teng_set_headless(100000, nullptr);
  if (!teng_init(ad, kW, kH)) return iz;
  teng_spawn_ground(20, 0xffffffff);
  const int top = teng_spawn_sphere(0.3, 6.0, 0.1, 0.4, 1, 0xff0000ff);
  const int kutu = teng_spawn_box(-0.5, 9.2, 0.0, 0.3, 0.3, 0.3, 1, 0x00ff00ff); // ~kare 81 de zemine: duraklik penceresinde
  teng_set_velocity(top, 1.5, 0, 0.25);
  if (olcek != 1.0) teng_time_scale(olcek);
  run_frames(once);
  if (durakli != 0) {
    // durakli < 0: KONTROL kosusu — ayni pencere DURAKLATMADAN sayilir.
    const bool gercek = durakli > 0;
    if (durakli < 0) durakli = -durakli;
    if (gercek) teng_physics_pause(1);
    CHECK(teng_physics_paused() == (gercek ? 1 : 0));
    const double x0 = teng_x(top), y0 = teng_y(top), k0 = teng_y(kutu);
    const int t0 = teng_sim_tick();
    int olay = 0;
    for (int i = 0; i < durakli; i++) {
      run_frames(1);
      olay += teng_collision_count();
    }
    *durakli_hareket = (teng_x(top) != x0 || teng_y(top) != y0 || teng_y(kutu) != k0) ? 1 : 0;
    *durakli_olay = olay;
    *durakli_tick = teng_sim_tick() - t0;
    teng_physics_pause(0);
    CHECK(teng_physics_paused() == 0);
  }
  run_frames(sonra);
  iz.x = teng_x(top);
  iz.y = teng_y(top);
  iz.z = teng_z(kutu);
  iz.vy = teng_vy(top);
  iz.tick = teng_sim_tick();
  teng_shutdown();
  return iz;
}
} // namespace

ENGINE_TEST(bridge_physics_pause_freezes_and_resumes_deterministically) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("fizik duraklatma sondasi", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  teng_shutdown();
  int hareket = -1, olay = -1, dtick = -1, h2 = 0, o2 = 0, t2 = 0;
  // Kare 70'te top zeminde yuvarlaniyor, kutu ~kare 81'de zemine carpar:
  // duraklik penceresi (70..95) yeni temasin OLDUGU yerde (olay 0 bir sey soylesin).
  int kh = 0, ko = 0, kt = 0;
  const FizikIz a = fizik_oturumu("durakli", 70, 25, 30, 1.0, &hareket, &olay, &dtick);
  const FizikIz k = fizik_oturumu("kontrol", 70, -25, 5, 1.0, &kh, &ko, &kt); // ayni pencere, duraklatmadan
  const FizikIz b = fizik_oturumu("duz", 100, 0, 0, 1.0, &h2, &o2, &t2);
  std::printf("    [bilgi] duraklatma: A (70+25 durakli+30) top (%.6f %.6f) tick %d | B (100) top (%.6f %.6f) tick %d | "
              "pencerede hareket/olay/tick: durakli %d/%d/%d, KONTROL %d/%d/%d\n", a.x, a.y, a.tick, b.x, b.y, b.tick, hareket, olay, dtick, kh,
              ko, kt);
  CHECK(hareket == 0 && olay == 0 && dtick == 0);
  CHECK(kh == 1 && ko > 0 && kt == 25); // KONTROL: ayni pencerede govde hareket eder, temas olayi gelir
  CHECK(a.tick == b.tick && a.tick > 0);
  CHECK(a.x == b.x && a.y == b.y && a.z == b.z && a.vy == b.vy); // BIT-TAM: duraklatma sonucu degistirmez
  CHECK(k.x == b.x && k.y == b.y);                              // kontrol kosusu da ayni 100 adim
  // Zaman olcegi: 0.5 ile 100 kare = 50 adim, 50 olceksiz kareyle bit-tam ayni.
  const FizikIz y = fizik_oturumu("yarim", 100, 0, 0, 0.5, &h2, &o2, &t2);
  const FizikIz d50 = fizik_oturumu("duz50", 50, 0, 0, 1.0, &h2, &o2, &t2);
  std::printf("    [bilgi] zaman olcegi 0.5: 100 kare -> tick %d, top (%.6f %.6f) | olceksiz 50 kare tick %d (%.6f %.6f)\n", y.tick, y.x, y.y,
              d50.tick, d50.x, d50.y);
  CHECK(y.tick == d50.tick && y.tick >= 49 && y.tick <= 50);
  CHECK(y.x == d50.x && y.y == d50.y && y.vy == d50.vy);
}

// --- Ic cozunurluk olcegi (teng_render_scale, 2026-10-07) ---------------------
// Renderer'in dinamik cozunurlugu (set_render_scale) koprude yoktu; ilk gercek
// oyun (Kupler ile Kurelerin Savasi) P20 Pro / Mali-G72'de 2159x1080'de bos
// sahnede bile 60 fps tutamadi: parlama acik 43.8 fps, kapali 55.6 fps;
// parlama acik + olcek 0.7 ile 59.6 fps (docs/OYUN_GERI_BILDIRIM.md madde 2).
// Kapi: (1) olcek ic hedefin KULLANILAN alt-dikdortgenini kucultur ve sahne
// yine tam kareye cizilir (olcekli kare bos kareden belirgin farkli, tam
// cozunurluklu kareden de farkli — yani gercekten ayri bir yol kostu);
// (2) aralik disi deger HATA sayar ve olcegi degistirmez, 0.5'in alti
// kenetlenir; (3) POZITIF KONTROL: parlama KAPALI kurulumda olcek 1.0 doner,
// UYARI sayar ve kare bayt bayt degismez (sessiz "dusurdum" yok).
namespace {
alignas(16) uint8_t g_olcek_bos[kW * kH * 3];
alignas(16) uint8_t g_olcek_tam[kW * kH * 3];
alignas(16) uint8_t g_olcek_yarim[kW * kH * 3];
bool olcek_sahnesi(const char *yol_bos, const char *yol_tam, const char *yol_yarim, double olcek, double *uygulanan) {
  teng_set_headless(100000, nullptr);
  if (!teng_init("olcek", kW, kH)) return false;
  teng_camera(0, 4, 10, 0, 0.5, 0);
  run_frames(2);
  CHECK(teng_screenshot(yol_bos) == 1);
  CHECK(teng_spawn_ground(8.0, 0xCED4DAFF) > 0);
  CHECK(teng_spawn_box(0, 1.0, 0, 0.6, 0.6, 0.6, 0, 0xE63946FF) > 0);
  CHECK(teng_spawn_sphere(-2.0, 1.0, 1.0, 0.7, 0, 0x2A9D8FFF) > 0);
  run_frames(3);
  CHECK(teng_screenshot(yol_tam) == 1);
  *uygulanan = teng_render_scale(olcek);
  run_frames(3);
  CHECK(teng_screenshot(yol_yarim) == 1);
  return true;
}
} // namespace

ENGINE_TEST(bridge_render_scale_draws_scene_smaller_and_reports_when_unavailable) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  char yb[512], yt[512], yy[512];
  tmp_template(yb, sizeof yb, "olcek_bos");
  tmp_template(yt, sizeof yt, "olcek_tam");
  tmp_template(yy, sizeof yy, "olcek_yarim");
  char *yollar[3] = {yb, yt, yy};
  for (int i = 0; i < 3; i++) { const int fd = mkstemp(yollar[i]); if (fd >= 0) close(fd); }
  teng_log_level(1);
  // (1) parlama acik: ic hedef var.
  teng_bloom(1, 1.0, 0.6);
  double uyg = -1.0;
  if (!olcek_sahnesi(yb, yt, yy, 0.5, &uyg)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  const bool sanal = test::gpu_is_virtual(teng_gpu_name());
  CHECK(teng_bloom_on() == 1);
  CHECK(uyg == 0.5);
  const int hata0 = teng_error_count();
  CHECK(teng_render_scale(1.5) == 0.5); // aralik disi: HATA, olcek degismez
  CHECK(teng_render_scale(0.0) == 0.5);
  CHECK(teng_error_count() == hata0 + 2);
  CHECK(teng_render_scale(0.2) == 0.5); // 0.5'e kenetlenir, hata degil
  CHECK(teng_error_count() == hata0 + 2);
  teng_shutdown();
  uint32_t w = 0, h = 0;
  const uint32_t nb = read_ppm(yb, g_olcek_bos, kW * kH, &w, &h);
  const uint32_t nt = read_ppm(yt, g_olcek_tam, kW * kH, &w, &h);
  const uint32_t ny = read_ppm(yy, g_olcek_yarim, kW * kH, &w, &h);
  CHECK(nb == kW * kH && nt == kW * kH && ny == kW * kH);
  uint32_t yarim_bos = 0, yarim_tam = 0;
  if (nb == kW * kH && nt == kW * kH && ny == kW * kH) {
    yarim_bos = diff_px(g_olcek_yarim, g_olcek_bos, ny);
    yarim_tam = diff_px(g_olcek_yarim, g_olcek_tam, ny);
  }
  // (3) pozitif kontrol: parlama KAPALI -> olcek uygulanamaz, gorunur.
  teng_bloom(0, 1.0, 0.0);
  double uyg2 = -1.0;
  const int uyari0 = teng_warning_count();
  CHECK(olcek_sahnesi(yb, yt, yy, 0.5, &uyg2));
  CHECK(teng_bloom_on() == 0);
  CHECK(uyg2 == 1.0);
  CHECK(teng_warning_count() == uyari0 + 1);
  teng_shutdown();
  const uint32_t nt2 = read_ppm(yt, g_olcek_tam, kW * kH, &w, &h);
  const uint32_t ny2 = read_ppm(yy, g_olcek_yarim, kW * kH, &w, &h);
  CHECK(nt2 == kW * kH && ny2 == kW * kH);
  const uint32_t kapali_fark = (nt2 == kW * kH && ny2 == kW * kH) ? diff_px(g_olcek_yarim, g_olcek_tam, ny2) : 9999u;
  for (int i = 0; i < 3; i++) std::remove(yollar[i]);
  std::printf("    [bilgi] olcek 0.5 (parlama acik): olcekli-bos %u px, olcekli-tam %u px; parlama kapali: olcek %.2f, kare farki %u px\n",
              yarim_bos, yarim_tam, uyg2, kapali_fark);
  if (sanal) { skip("sanal GPU (Apple Paravirtual, CI macOS): olcek PIKSEL blogu gercek cihazda olculur"); return; }
  CHECK(yarim_bos > 500);  // sahne olcekli yolda da cizildi
  CHECK(yarim_tam > 0);    // ve gercekten ayri (dusuk cozunurluklu) bir kare
  CHECK(kapali_fark == 0); // ic hedef yoksa kare degismez
}

// --- Geri bildirim #3: sahne parcaciklari derlenmis oyunda --------------------
// Kopru SceneRuntime::update'i HIC cagirmiyordu: `.sahne`deki yayicilar editorde
// doguyor, oyunda (eng_scene_load) hic dogmuyordu; dogsalardi da v8 blobu
// rengi tasimadigi icin beyaz olurlardi. Olcu: yesil (0,1,0) yayicili sahne,
// 30 kare; cizim sayisi + yesil piksel. KONTROL: ayni sahne dogum hizi 0 --
// cizim 0, yesil piksel 0 (kapi "sahnede bir sey var" degil parcacigi olcer).
ENGINE_TEST(bridge_scene_particles_spawn_with_authored_color) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("parcacik kapisi", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  static SystemArena sarena;
  if (sarena.capacity() == 0) sarena.reserve(16u << 20, "bridge_parcacik");
  static content::SceneDesc sd;
  static uint8_t px[kW * kH * 3];
  int draws[2] = {0, 0};
  uint32_t green[2] = {0, 0};
  for (int pass = 0; pass < 2; pass++) {
    content::scene_desc_reset(sd);
    content::SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "yesil_ates");
    e.components = content::kSceneParticle;
    e.pos = Vec3{0, 1, 0};
    e.particle_spawn_rate = pass == 0 ? 120.0f : 0.0f;
    e.particle_lifetime_min = e.particle_lifetime_max = 2.0f;
    e.particle_size_start = e.particle_size_end = 0.35f;
    e.particle_velocity = Vec3{0, 0.5f, 0};
    e.particle_jitter = Vec3{0.6f, 0.3f, 0.6f};
    e.particle_color_start = e.particle_color_end = Vec3{0, 1, 0};
    e.particle_gravity = 0.0f;
    CHECK(sd.insert_entity(0, e));
    char sblob[800], shot[512];
    std::snprintf(sblob, sizeof sblob, "%s/_kopru_parcacik.sahneb", assets_dir());
    content::SceneError se{};
    CHECK(content::scene_blob_save(sarena, sd, sblob, &se));
    if (teng_scene_loaded()) teng_scene_unload();
    CHECK(teng_scene_load(sblob) == 1);
    teng_camera(0, 2, 6, 0, 1, 0);
    run_frames(30);
    draws[pass] = teng_draw_count();
    tmp_template(shot, sizeof shot, "kopru_parcacik");
    const int fd = mkstemp(shot);
    if (fd >= 0) close(fd);
    CHECK(teng_screenshot(shot) == 1);
    uint32_t w = 0, h = 0;
    const uint32_t n = read_ppm(shot, px, kW * kH, &w, &h);
    CHECK(n == kW * kH);
    for (uint32_t i = 0; i < n; i++) {
      const int r = px[i * 3], gg = px[i * 3 + 1], b = px[i * 3 + 2];
      if (gg > 80 && gg > r + 40 && gg > b + 40) green[pass]++;
    }
    std::remove(shot);
    teng_scene_unload();
    std::remove(sblob);
  }
  std::printf("    [bilgi] sahne parcaciklari: cizim %d (kontrol %d), yesil piksel %u (kontrol %u) / %u\n", draws[0], draws[1], green[0], green[1],
              kW * kH);
  CHECK(draws[0] > 0 && draws[1] == 0); // duzeltmesiz kopru: 0 / 0
  // Sanal GPU'da (Apple Paravirtual, CI macOS) sahne karesi bos cikiyor (test.hpp,
  // 2026-09-14). ATLAMA YALNIZ PIKSEL BLOGUNA: cizim sayisi orada da olculur.
  if (test::gpu_is_virtual(teng_gpu_name())) skip("sanal GPU (Apple Paravirtual, CI macOS): parcacik rengi PIKSELDE gercek cihazda olculur");
  else CHECK(green[0] > 100 && green[1] == 0);
  teng_shutdown();
}

// --- Geri bildirim #11: sabit govde YERINDE isinlanir + kinematik govde --------
// Oyun birliklerini sabit govdeyle surseydi her `isinla` govdeyi silip yeniden
// kuracakti (60 birlik x 60 kare = saniyede ~3600 kurma); kinematik tur yoktu.
// Olcu: (1) sabit kutu 2000 kez isinlanir — kimlik ayni, isin YENI yere carpar,
// cagri basina ns; KONTROL ayni sayida dinamik isinlama (yeniden kurma yolu).
// (2) kinematik kutu eng_kinematic_move ile 1 m/s surulur: zemindeki kureyi
// iter, yercekimi onu dusurmez, hedef gelmeyince DURUR; dürtü HATA.
ENGINE_TEST(bridge_kinematic_body_and_in_place_teleport) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("kinematik kapisi", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  teng_spawn_ground(20, 0xffffffff);
  // (1) yerinde isinlama
  const int duvar = teng_spawn_box(0, 1, -5, 0.5, 1, 0.5, 0, 0x808080ff);
  const int dtop = teng_spawn_box(0, 1, 5, 0.5, 0.5, 0.5, 1, 0x808080ff);
  run_frames(1);
  constexpr int kN = 2000;
  uint64_t t0 = platform::now_ns();
  for (int i = 0; i < kN; i++) teng_set_pos(duvar, (double)(i % 9) - 4.0, 1, -5);
  const double ns_sabit = (double)(platform::now_ns() - t0) / kN;
  t0 = platform::now_ns();
  for (int i = 0; i < kN; i++) teng_set_pos(dtop, (double)(i % 9) - 4.0, 3, 5);
  const double ns_dinamik = (double)(platform::now_ns() - t0) / kN;
  teng_set_pos(duvar, 7, 1, -5);
  run_frames(1);
  const double d_yeni = teng_raycast(7, 6, -5, 0, -1, 0, 10, 0);
  const int vurulan = teng_ray_id();
  const double d_eski = teng_raycast(0, 6, -5, 0, -1, 0, 10, 0); // eski yerde yalniz zemin (y=0): 6 m
  std::printf("    [bilgi] isinlama x%d: sabit (yerinde) %.0f ns/cagri, dinamik (yeniden kurma) %.0f ns/cagri; isin yeni yer %.2f m (#%d), eski yer %.2f m\n",
              kN, ns_sabit, ns_dinamik, d_yeni, vurulan, d_eski);
  CHECK(teng_alive(duvar) && vurulan == duvar && std::fabs(d_yeni - 4.0) < 0.01 && std::fabs(d_eski - 6.0) < 0.01);
  teng_despawn(dtop);
  // (2) kinematik
  const int kin = teng_spawn_kinematic_box(-3, 0.6, 0, 0.5, 0.5, 0.5, 0x2080ffff);
  const int top = teng_spawn_sphere(0, 0.4, 0, 0.4, 1, 0xff4040ff);
  CHECK(kin && top && teng_is_kinematic(kin) == 1 && teng_is_dynamic(kin) == 0 && teng_is_kinematic(top) == 0);
  run_frames(60); // yercekimi: kinematik yerinde, top otursun
  const double ky0 = teng_y(kin), tx0 = teng_x(top);
  for (int f = 1; f <= 240; f++) {
    teng_frame_begin();
    teng_kinematic_move(kin, -3.0 + f / 60.0, 0.6, 0);
    teng_frame_end();
  }
  const double kx1 = teng_x(kin), tx1 = teng_x(top), kvx = teng_vx(kin);
  run_frames(30); // hedef yok: DURMALI
  const double kx2 = teng_x(kin);
  const int h0 = teng_error_count();
  teng_impulse(kin, 1, 0, 0);
  const int h1 = teng_error_count();
  std::printf("    [bilgi] kinematik: y %.4f (yercekimsiz), x -3 -> %.3f (hiz %.2f), 30 kare hedefsiz -> %.3f; kure x %.3f -> %.3f; durtu hatasi %d\n", ky0,
              kx1, kvx, kx2, tx0, tx1, h1 - h0);
  CHECK(std::fabs(ky0 - 0.6) < 1e-4);
  CHECK(std::fabs(kx1 - 1.0) < 0.01 && std::fabs(kvx - 1.0) < 0.05);
  CHECK(std::fabs(kx2 - kx1) < 1e-4); // durdu (hiz sifirlandi)
  CHECK(tx1 > tx0 + 0.5);             // itildi
  CHECK(h1 == h0 + 1);
  // Kinematik isinlama: yerinde, hiz sifir.
  teng_set_pos(kin, 5, 0.6, 3);
  run_frames(2);
  CHECK(std::fabs(teng_x(kin) - 5.0) < 1e-4 && std::fabs(teng_z(kin) - 3.0) < 1e-4);
  // Kapanis raporu yolu SAYAR: 2000 + 1 sabit + 1 kinematik yerinde, 2000
  // dinamik yeniden kurma. Duzeltmesiz kopru bu satiri hic basmaz (her
  // isinlama yeniden kurmaydi).
  kapanis_yakala();
  CHECK(std::strstr(g_kapanis_log, "kapanis (isinlama): 2002 yerinde") != nullptr);
  CHECK(std::strstr(g_kapanis_log, "2000 yeniden kurma (dinamik)") != nullptr);
}

// --- Geri bildirim #12: gorus acisi + ekran isini + secme + dunya->ekran -------
// Oyun dokunusla kule yuvasi secmek ve can cubugu cizmek icin izdusumu kendisi
// kuruyordu ve render_frame'in SABIT pi/3.5'ini kopyaliyordu: sabit degisirse
// oyun sessizce yanlis yere dokunur. Olcu: kirmizi kurenin merkezi dunya->ekran
// ile izdusulur; (1) o PIKSEL kirmizi (renderer'in projeksiyonuyla ayni
// gelenek: y asagi, gorunen en-boy), (2) o noktadan secme kureyi bulur, (3)
// ekran isini merkezden < 1 mm gecer. Gorus acisi 30 dereceye cekilince ayni
// uc olcu yeni projeksiyonla tutar. KONTROL: eski sabiti kopyalayan oyunun
// hesapladigi nokta (pi/3.5 ile) kureyi ISKALAR ve pikseli kirmizi degil; bos
// gokyuzunde secme -1.
namespace {
bool kirmizi_mi(const uint8_t *px, uint32_t w, uint32_t h, double sx, double sy) {
  const int x = (int)sx, y = (int)sy;
  if (x < 0 || y < 0 || x >= (int)w || y >= (int)h) return false;
  const uint8_t *p = px + ((size_t)y * w + (size_t)x) * 3;
  return p[0] > 120 && p[0] > p[1] + 60 && p[0] > p[2] + 60;
}
bool kare_kirmizi(const char *ad, double sx, double sy, bool *okundu) {
  static uint8_t px[kW * kH * 3];
  char shot[512];
  tmp_template(shot, sizeof shot, ad);
  const int fd = mkstemp(shot);
  if (fd >= 0) close(fd);
  uint32_t w = 0, h = 0;
  *okundu = teng_screenshot(shot) == 1 && read_ppm(shot, px, kW * kH, &w, &h) == kW * kH;
  std::remove(shot);
  return *okundu && kirmizi_mi(px, w, h, sx, sy);
}
} // namespace

ENGINE_TEST(bridge_camera_fov_screen_ray_pick_world_to_screen) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  teng_log_level(1);
  teng_set_headless(100000, nullptr);
  if (!teng_init("kamera isini kapisi", kW, kH)) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  CHECK(std::fabs(teng_camera_fov_get() - 180.0 / 3.5) < 1e-4);
  teng_spawn_ground(20, 0x404040ff);
  const double cx = 1.6, cy = 1.2, cz = -2.0;
  const int top = teng_spawn_sphere(cx, cy, cz, 0.35, 0, 0xff0000ff);
  teng_camera(0, 3, 8, 0, 1, 0);
  run_frames(2);
  const bool sanal = test::gpu_is_virtual(teng_gpu_name());
  double sx[2], sy[2], d_ray[2];
  int pick_id[2];
  bool kirmizi[2], okundu[2];
  for (int k = 0; k < 2; k++) {
    if (k == 1) { teng_camera_fov(30); run_frames(1); }
    CHECK(teng_world_to_screen(cx, cy, cz) == 1);
    sx[k] = teng_screen_x(); sy[k] = teng_screen_y();
    const double d = teng_pick(sx[k], sy[k], 50);
    pick_id[k] = teng_ray_id();
    CHECK(d > 0);
    CHECK(teng_screen_ray(sx[k], sy[k]) == 1);
    // isin merkezden ne kadar uzak: |(c - o) x d|
    const double ox = teng_screen_ray_ox(), oy = teng_screen_ray_oy(), oz = teng_screen_ray_oz();
    const double dx = teng_screen_ray_dx(), dy = teng_screen_ray_dy(), dz = teng_screen_ray_dz();
    const double vx = cx - ox, vy = cy - oy, vz = cz - oz;
    const double qx = vy * dz - vz * dy, qy = vz * dx - vx * dz, qz = vx * dy - vy * dx;
    d_ray[k] = std::sqrt(qx * qx + qy * qy + qz * qz);
    kirmizi[k] = kare_kirmizi("kamera_isin", sx[k], sy[k], &okundu[k]);
    CHECK(okundu[k]);
  }
  // KONTROL: oyunun eski kopyasi (pi/3.5 sabit) fov 30'da nereye dokunurdu.
  const double t_eski = std::tan(3.14159265358979 / 3.5 * 0.5), t_yeni = std::tan(30.0 * 3.14159265358979 / 180.0 * 0.5);
  const double ex = kW * 0.5 + (sx[1] - kW * 0.5) * t_yeni / t_eski, ey = kH * 0.5 + (sy[1] - kH * 0.5) * t_yeni / t_eski;
  teng_pick(ex, ey, 50);
  const int eski_id = teng_ray_id();
  bool ok_e = false;
  const bool eski_kirmizi = kare_kirmizi("kamera_isin_eski", ex, ey, &ok_e);
  const double bos = teng_pick(2, 2, 50); // sol ust: gokyuzu
  const int h0 = teng_error_count();
  teng_camera_fov(500);
  CHECK(teng_error_count() == h0 + 1 && std::fabs(teng_camera_fov_get() - 120.0) < 1e-4);
  std::printf("    [bilgi] kure merkezi ekranda: fov 51.4 -> (%.1f %.1f) secme #%d (kure #%d) isin %.2e m kirmizi %d | fov 30 -> (%.1f %.1f) secme #%d isin %.2e m kirmizi %d | "
              "KONTROL eski sabit (%.1f %.1f) secme #%d kirmizi %d, gokyuzu %.1f\n",
              sx[0], sy[0], pick_id[0], top, d_ray[0], kirmizi[0], sx[1], sy[1], pick_id[1], d_ray[1], kirmizi[1], ex, ey, eski_id, eski_kirmizi, bos);
  CHECK(pick_id[0] == top && pick_id[1] == top);
  CHECK(d_ray[0] < 1e-3 && d_ray[1] < 1e-3);
  CHECK(eski_id != top && bos < 0);
  CHECK(std::fabs(sx[1] - sx[0]) > 10.0); // fov gercekten degisti
  if (sanal) skip("sanal GPU (Apple Paravirtual, CI macOS): izdusumun PIKSEL dogrulamasi gercek cihazda olculur");
  else CHECK(kirmizi[0] && kirmizi[1] && !eski_kirmizi);
  teng_shutdown();
}
