// L6 BRIDGE — Android NativeActivity host: Tulpar oyununun (libtulpargame.so)
// `main`i bu android_main'den cagrilir (raylib'in rcore_android'i gibi).
// stdout/stderr -> boru -> logcat (etiket "tulpar") + files/engine_log.txt;
// APK assets/ -> dahili dizin (cgltf fopen ister), TULPAR_ENGINE_ASSETS.
// Kopru cekirdegi teng_init'te bridge_host_open cagirir: pencere gelene kadar
// olaylar pompalanir, sonra yuzey/poll/dokunmatik geri cagrilari verilir.
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <android/asset_manager.h>
#include <android/input.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/android_lifecycle.hpp"
#include "bridge/asset_filter.hpp"
#include "bridge/bridge_host.hpp"
#include "platform/crash.hpp"
#include "platform/memory.hpp"
#include "platform/time.hpp"

// Tulpar tarafindan uretilen oyun giris noktasi (libtulpargame.so icinde).
extern "C" int main(int argc, char **argv);

using namespace tulpar::engine;

namespace {
struct LogPump { int fd = -1; FILE *file = nullptr; };
void *log_pump(void *p) {
  LogPump *lp = static_cast<LogPump *>(p);
  char buf[1024];
  size_t n = 0;
  for (;;) {
    ssize_t r = read(lp->fd, buf + n, sizeof buf - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        buf[i] = 0;
        __android_log_write(ANDROID_LOG_INFO, "tulpar", buf + start);
        if (lp->file) { std::fputs(buf + start, lp->file); std::fputc('\n', lp->file); std::fflush(lp->file); }
        start = i + 1;
      }
    }
    if (start < n) std::memmove(buf, buf + start, n - start);
    n -= start;
    if (n == sizeof buf - 1) { buf[n] = 0; __android_log_write(ANDROID_LOG_INFO, "tulpar", buf); n = 0; }
  }
  return nullptr;
}
void redirect_output(const char *dir) {
  static LogPump lp;
  char path[512];
  std::snprintf(path, sizeof path, "%s/engine_log.txt", dir);
  lp.file = std::fopen(path, "w");
  int fds[2];
  if (pipe(fds) != 0) return;
  dup2(fds[1], 1);
  dup2(fds[1], 2);
  close(fds[1]);
  lp.fd = fds[0];
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  pthread_t t;
  pthread_create(&t, nullptr, log_pump, &lp);
  pthread_detach(t);
}

struct AndroidHost {
  android_app *app = nullptr;
  platform::TouchState touch;
  bool window_ready = false, had_window = false, window_changed = false, quit = false;
  uint32_t low_memory = 0; // APP_CMD_LOW_MEMORY sayisi (kapanista basilir)
  // Geri tusu (Geri bildirim #8). `back_to_game`i cekirdek kurar (oyun geri
  // tusunu ilk sordugunda): o andan sonra BACK olayi tuketilir ve sistem
  // etkinligi KAPATMAZ. Kurulmadiysa eski davranis: sistem kapatir (geri tusunu
  // hic sormayan eski bir oyun telefonda kapanabilir kalsin).
  tulpar::engine::bridge::BackKeyLatch back;
  bool back_to_game = false;
  bool paused = false; // APP_CMD_PAUSE .. APP_CMD_RESUME
  // Guvenli alan (Geri bildirim #17): pencere/icerik degisince kirli, cekirdek
  // sorunca (a_system) JNI ile BIR kez yeniden okunur — kare basina JNI yok.
  bool insets_dirty = true;
  int32_t inset[4] = {0, 0, 0, 0}; // sol, ust, sag, alt (pencere pikseli)
  uint32_t inset_w = 0, inset_h = 0;
  tulpar::engine::bridge::HostSystem sys;
};
AndroidHost g_host;

// --- JNI yardimcisi: bu thread'i JVM'e bagla (zaten bagliysa dokunma) --------
struct JniScope {
  JavaVM *vm = nullptr;
  JNIEnv *env = nullptr;
  bool attached_here = false;
  explicit JniScope(android_app *app) {
    vm = app && app->activity ? app->activity->vm : nullptr;
    if (!vm) return;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) == JNI_OK) return;
    env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached_here = true;
    else env = nullptr;
  }
  ~JniScope() {
    if (env && env->ExceptionCheck()) env->ExceptionClear();
    if (attached_here) vm->DetachCurrentThread();
  }
  // Istisna atildiysa temizle ve false don (zincirdeki sonraki cagri yapilmasin).
  bool ok() { if (!env) return false; if (env->ExceptionCheck()) { env->ExceptionClear(); return false; } return true; }
};
int android_sdk() {
  char v[PROP_VALUE_MAX] = {0};
  return __system_property_get("ro.build.version.sdk", v) > 0 ? std::atoi(v) : 0;
}
// DisplayCutout'un guvenli bosluklari (API 28+): Activity.getWindow()
// .getDecorView().getRootWindowInsets().getDisplayCutout().getSafeInset*().
// Pencere centige uzanmiyorsa (varsayilan tema: sistem centik seridini siyah
// birakir) cutout null ya da bosluklar 0 doner. Herhangi bir halka eksikse 0.
void query_insets(AndroidHost *h) {
  h->insets_dirty = false;
  int32_t in[4] = {0, 0, 0, 0};
  if (h->app->window) { h->inset_w = (uint32_t)ANativeWindow_getWidth(h->app->window); h->inset_h = (uint32_t)ANativeWindow_getHeight(h->app->window); }
  if (android_sdk() >= 28) {
    JniScope j(h->app);
    JNIEnv *e = j.env;
    if (e) {
      jobject act = h->app->activity->clazz;
      jclass ca = e->GetObjectClass(act);
      jmethodID m_win = j.ok() ? e->GetMethodID(ca, "getWindow", "()Landroid/view/Window;") : nullptr;
      jobject win = m_win && j.ok() ? e->CallObjectMethod(act, m_win) : nullptr;
      jobject decor = nullptr, ins = nullptr, cut = nullptr;
      if (win && j.ok()) {
        jmethodID m = e->GetMethodID(e->GetObjectClass(win), "getDecorView", "()Landroid/view/View;");
        decor = m && j.ok() ? e->CallObjectMethod(win, m) : nullptr;
      }
      if (decor && j.ok()) {
        jmethodID m = e->GetMethodID(e->GetObjectClass(decor), "getRootWindowInsets", "()Landroid/view/WindowInsets;");
        ins = m && j.ok() ? e->CallObjectMethod(decor, m) : nullptr;
      }
      if (ins && j.ok()) {
        jmethodID m = e->GetMethodID(e->GetObjectClass(ins), "getDisplayCutout", "()Landroid/view/DisplayCutout;");
        cut = m && j.ok() ? e->CallObjectMethod(ins, m) : nullptr;
      }
      if (cut && j.ok()) {
        jclass cc = e->GetObjectClass(cut);
        const char *ad[4] = {"getSafeInsetLeft", "getSafeInsetTop", "getSafeInsetRight", "getSafeInsetBottom"};
        for (int k = 0; k < 4; k++) {
          jmethodID m = e->GetMethodID(cc, ad[k], "()I");
          if (m && j.ok()) in[k] = e->CallIntMethod(cut, m);
          if (!j.ok()) in[k] = 0;
        }
      }
      j.ok();
    }
  }
  if (in[0] != h->inset[0] || in[1] != h->inset[1] || in[2] != h->inset[2] || in[3] != h->inset[3])
    std::printf("[engine_bridge] android: guvenli alan sol %d ust %d sag %d alt %d (pencere %ux%u, API %d)\n", in[0], in[1], in[2], in[3], h->inset_w,
                h->inset_h, android_sdk());
  for (int k = 0; k < 4; k++) h->inset[k] = in[k];
}

// Ad tablosu bridge/android_lifecycle.hpp'de (masaustu kapisi onu olcer). Sayilar
// glue'nun enum'una KILITLI: glue sirayi degistirir ya da araya komut eklerse
// motor derlenmez — sessizce yanlis adla loglamaz.
using namespace tulpar::engine::bridge;
static_assert(APP_CMD_INPUT_CHANGED == kAndroidCmdInputChanged && APP_CMD_INIT_WINDOW == kAndroidCmdInitWindow &&
                  APP_CMD_TERM_WINDOW == kAndroidCmdTermWindow && APP_CMD_WINDOW_RESIZED == kAndroidCmdWindowResized &&
                  APP_CMD_WINDOW_REDRAW_NEEDED == kAndroidCmdWindowRedrawNeeded &&
                  APP_CMD_CONTENT_RECT_CHANGED == kAndroidCmdContentRectChanged && APP_CMD_GAINED_FOCUS == kAndroidCmdGainedFocus &&
                  APP_CMD_LOST_FOCUS == kAndroidCmdLostFocus && APP_CMD_CONFIG_CHANGED == kAndroidCmdConfigChanged &&
                  APP_CMD_LOW_MEMORY == kAndroidCmdLowMemory && APP_CMD_START == kAndroidCmdStart && APP_CMD_RESUME == kAndroidCmdResume &&
                  APP_CMD_SAVE_STATE == kAndroidCmdSaveState && APP_CMD_PAUSE == kAndroidCmdPause && APP_CMD_STOP == kAndroidCmdStop &&
                  APP_CMD_DESTROY == kAndroidCmdDestroy,
              "native_app_glue APP_CMD_* sayilari bridge/android_lifecycle.hpp ile ayni olmali");

void on_cmd(android_app *app, int32_t cmd) {
  AndroidHost *h = static_cast<AndroidHost *>(app->userData);
  if (const char *ad = android_cmd_name(cmd)) std::printf("[engine_bridge] android cmd %s\n", ad);
  else std::printf("[engine_bridge] android cmd ?%d (glue'nun bilinmeyen komutu: bridge/android_lifecycle.hpp)\n", (int)cmd);
  switch (cmd) {
  case APP_CMD_INIT_WINDOW:
    h->window_ready = app->window != nullptr;
    if (h->window_ready && h->had_window) h->window_changed = true;
    h->insets_dirty = true;
    break;
  // Pencere / icerik dikdortgeni / yon degisti: centik baska kenara gecmis olabilir.
  case APP_CMD_WINDOW_RESIZED:
  case APP_CMD_CONTENT_RECT_CHANGED:
  case APP_CMD_CONFIG_CHANGED: h->insets_dirty = true; break;
  case APP_CMD_TERM_WINDOW: h->window_ready = false; break;
  case APP_CMD_DESTROY: h->quit = true; break;
  // Arka plan: PAUSE..RESUME (STOP/START bunlarin icinde). Cekirdek bir sonraki
  // kare basinda gorur — arka planda dongu ~10 Hz suruyor (pencere yokken
  // pompa 100 ms bekler), yani ses en gec ~100 ms icinde durur.
  case APP_CMD_PAUSE: h->paused = true; break;
  case APP_CMD_RESUME: h->paused = false; break;
  // Sistem bellegi daraliyor (onTrimMemory/onLowMemory). Motor bugun bir sey
  // BIRAKMIYOR (butun kapasiteler init'te, A2) — ama olgu sessiz kalmasin:
  // sayilir, o anki RSS ile loglanir ve kapanista toplam basilir. Bir oyunun
  // arka planda oldurulmesinin (LMK) sebebi ararken ilk bakilacak satir bu.
  case APP_CMD_LOW_MEMORY:
    h->low_memory++;
    std::printf("[engine_bridge] android: DUSUK BELLEK uyarisi #%u (RSS %zu MB)\n", h->low_memory,
                platform::os_resident_bytes() / (1024 * 1024));
    break;
  default: break;
  }
}
int32_t on_input(android_app *app, AInputEvent *ev) {
  AndroidHost *h = static_cast<AndroidHost *>(app->userData);
  if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
    if (AKeyEvent_getKeyCode(ev) != AKEYCODE_BACK) return 0; // ses tuslari vb. sistemin
    const int32_t a = AKeyEvent_getAction(ev);
    if (a == AKEY_EVENT_ACTION_DOWN || a == AKEY_EVENT_ACTION_UP) h->back.on_key(a == AKEY_EVENT_ACTION_DOWN, AKeyEvent_getRepeatCount(ev));
    // 1 = tuketildi: NativeActivity onBackPressed'e (finish) gitmez.
    return h->back_to_game ? 1 : 0;
  }
  if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
  const int32_t action = AMotionEvent_getAction(ev);
  const int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
  const size_t idx = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
  platform::TouchState &t = h->touch;
  t.time_ns = platform::now_ns();
  switch (kind) {
  case AMOTION_EVENT_ACTION_DOWN:
  case AMOTION_EVENT_ACTION_POINTER_DOWN:
    t.begin(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx));
    break;
  case AMOTION_EVENT_ACTION_MOVE:
    for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); i++)
      t.move(AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
    break;
  case AMOTION_EVENT_ACTION_UP:
  case AMOTION_EVENT_ACTION_POINTER_UP:
    t.end(AMotionEvent_getPointerId(ev, idx));
    break;
  case AMOTION_EVENT_ACTION_CANCEL: t.clear(); break;
  default: break;
  }
  return 1;
}
// --- Kayit dosyasi dis dizinde (Geri bildirim #19) ---------------------------
// Kayit varsayilan olarak cikarma kokunde (/data/user/0/<paket>/files/assets/
// tulpar_kayit.txt): adb okuyamaz, Huawei'de `run-as` de calismiyor (Tuzaklar
// 8n) — ayar ekrani yalniz dokunarak sinanabiliyordu. TANI SECENEGI:
//   adb shell setprop debug.tulpar.kayit dis
// ve APK HATA AYIKLANABILIR ise (TulparLang: TULPAR_ANDROID_DEBUGGABLE=1 tulpar
// build ...) kayit dis uygulama dizinine gider: /sdcard/Android/data/<paket>/
// files/tulpar_kayit.txt — `adb pull`/`adb push` ile okunur/yazilir. Ilk
// acilista ic kayit oraya KOPYALANIR (mevcut ayarlar gorunsun). Yayin (hata
// ayiklanamaz) APK'da ozellik YOK SAYILIR: oyuncunun kaydi baska uygulamalarin
// okuyabildigi dizine tasinmaz. Oyunun kendi kayit_ac(yol)'u ve
// TULPAR_ENGINE_SAVE her seyi ezer (yalniz varsayilan yol degisir).
bool app_debuggable(android_app *app) {
  JniScope j(app);
  JNIEnv *e = j.env;
  if (!e) return false;
  jobject act = app->activity->clazz;
  jmethodID m = e->GetMethodID(e->GetObjectClass(act), "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
  jobject ai = m && j.ok() ? e->CallObjectMethod(act, m) : nullptr;
  if (!ai || !j.ok()) return false;
  jfieldID f = e->GetFieldID(e->GetObjectClass(ai), "flags", "I");
  if (!f || !j.ok()) return false;
  const jint flags = e->GetIntField(ai, f);
  return j.ok() && (flags & 0x2) != 0; // ApplicationInfo.FLAG_DEBUGGABLE
}
void save_mirror_external(android_app *app, const char *internal_assets) {
  char v[PROP_VALUE_MAX] = {0};
  if (__system_property_get("debug.tulpar.kayit", v) <= 0 || std::strcmp(v, "dis") != 0) return;
  if (std::getenv("TULPAR_ENGINE_SAVE")) { std::printf("[engine_bridge] android: debug.tulpar.kayit=dis — TULPAR_ENGINE_SAVE zaten kurulu, dokunulmadi\n"); return; }
  if (!app_debuggable(app)) {
    std::printf("[engine_bridge] android: debug.tulpar.kayit=dis YOK SAYILDI — APK hata ayiklanabilir degil (yalniz debug derlemede; "
                "TULPAR_ANDROID_DEBUGGABLE=1 ile derle)\n");
    return;
  }
  const char *ext = app->activity->externalDataPath;
  if (!ext || !*ext) { std::printf("[engine_bridge] android: debug.tulpar.kayit=dis — dis dizin yok, ic kayit kullaniliyor\n"); return; }
  static char dis[600];
  std::snprintf(dis, sizeof dis, "%s/tulpar_kayit.txt", ext);
  char ic[600];
  std::snprintf(ic, sizeof ic, "%s/tulpar_kayit.txt", internal_assets);
  struct stat st;
  bool kopya = false;
  if (stat(dis, &st) != 0) {
    if (FILE *src = std::fopen(ic, "rb")) {
      if (FILE *dst = std::fopen(dis, "wb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, src)) > 0) std::fwrite(buf, 1, n, dst);
        std::fclose(dst);
        kopya = true;
      }
      std::fclose(src);
    }
  }
  setenv("TULPAR_ENGINE_SAVE", dis, 1);
  std::printf("[engine_bridge] android: kayit DIS dizinde (hata ayiklama tanisi, debug.tulpar.kayit=dis): %s%s\n", dis,
              kopya ? " — ic kayit kopyalandi" : "");
}

// --- APK varlik cikarimi (alt dizinler dahil) ------------------------------
struct AssetWalk {
  AAssetManager *mgr = nullptr;
  const char *dest_root = nullptr;
  JavaVM *vm = nullptr;
  JNIEnv *env = nullptr;
  jobject asset_mgr_obj = nullptr;
  jmethodID list_mid = nullptr;
  uint32_t files = 0, dirs = 0;
};
// Uzanti suzgeci: Huawei'de kok listesi sistem kaplamalarini da getiriyor.
// Liste bridge/asset_filter.hpp'de: masaustu kapisi onu olcer (eskiden burada,
// yalniz Android'de derleniyordu ve `.mp3`/`.flac` eksikti).
bool asset_wanted(const char *name) { return tulpar::engine::bridge::android_asset_wanted(name); }
bool write_asset(AssetWalk *w, const char *rel) {
  AAsset *as = AAssetManager_open(w->mgr, rel, AASSET_MODE_BUFFER);
  if (!as) return false;
  char path[1024];
  std::snprintf(path, sizeof path, "%s/%s", w->dest_root, rel);
  bool ok = false;
  if (FILE *f = std::fopen(path, "wb")) {
    const off_t n = AAsset_getLength(as);
    ok = std::fwrite(AAsset_getBuffer(as), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    if (ok) w->files++;
  }
  AAsset_close(as);
  return ok;
}
bool jni_assets_begin(android_app *app, AssetWalk *w) {
  w->vm = app->activity->vm;
  if (!w->vm || w->vm->AttachCurrentThread(&w->env, nullptr) != JNI_OK || !w->env) return false;
  jobject act = app->activity->clazz;
  jclass act_cls = w->env->GetObjectClass(act);
  jmethodID get_assets = act_cls ? w->env->GetMethodID(act_cls, "getAssets", "()Landroid/content/res/AssetManager;") : nullptr;
  if (!get_assets) { w->env->ExceptionClear(); return false; }
  jobject am = w->env->CallObjectMethod(act, get_assets);
  if (!am) { w->env->ExceptionClear(); return false; }
  jclass am_cls = w->env->GetObjectClass(am);
  w->list_mid = am_cls ? w->env->GetMethodID(am_cls, "list", "(Ljava/lang/String;)[Ljava/lang/String;") : nullptr;
  if (!w->list_mid) { w->env->ExceptionClear(); return false; }
  w->asset_mgr_obj = w->env->NewGlobalRef(am);
  return w->asset_mgr_obj != nullptr;
}
void jni_assets_end(AssetWalk *w) {
  if (w->env && w->asset_mgr_obj) w->env->DeleteGlobalRef(w->asset_mgr_obj);
  if (w->vm) w->vm->DetachCurrentThread();
  w->env = nullptr;
}
// `rel` altindaki girdileri gezer: listesi bos olmayan = dizin (ozyinele),
// digerleri dosya (suzgecten geciyorsa cikar). Derinlik siniri: sonsuz dongu yok.
void extract_dir(AssetWalk *w, const char *rel, int depth) {
  if (depth > 6) return;
  jstring jrel = w->env->NewStringUTF(rel);
  jobjectArray arr = (jobjectArray)w->env->CallObjectMethod(w->asset_mgr_obj, w->list_mid, jrel);
  w->env->DeleteLocalRef(jrel);
  if (w->env->ExceptionCheck()) { w->env->ExceptionClear(); return; }
  if (!arr) return;
  const jsize n = w->env->GetArrayLength(arr);
  for (jsize i = 0; i < n; i++) {
    jstring js = (jstring)w->env->GetObjectArrayElement(arr, i);
    if (!js) continue;
    const char *name = w->env->GetStringUTFChars(js, nullptr);
    char child[1024];
    if (rel[0]) std::snprintf(child, sizeof child, "%s/%s", rel, name);
    else std::snprintf(child, sizeof child, "%s", name);
    // Dizin mi? list() bos olmayan dizi donduruyorsa evet.
    jstring jchild = w->env->NewStringUTF(child);
    jobjectArray sub = (jobjectArray)w->env->CallObjectMethod(w->asset_mgr_obj, w->list_mid, jchild);
    w->env->DeleteLocalRef(jchild);
    if (w->env->ExceptionCheck()) w->env->ExceptionClear();
    const bool is_dir = sub && w->env->GetArrayLength(sub) > 0;
    if (sub) w->env->DeleteLocalRef(sub);
    if (is_dir) {
      char dpath[1024];
      std::snprintf(dpath, sizeof dpath, "%s/%s", w->dest_root, child);
      mkdir(dpath, 0700);
      w->dirs++;
      extract_dir(w, child, depth + 1);
    } else if (asset_wanted(child)) {
      write_asset(w, child);
    }
    w->env->ReleaseStringUTFChars(js, name);
    w->env->DeleteLocalRef(js);
  }
  w->env->DeleteLocalRef(arr);
}
// JNI yoksa (olmamali): yalniz kok dizindeki dosyalar.
void extract_flat(AssetWalk *w) {
  if (AAssetDir *ad = AAssetManager_openDir(w->mgr, "")) {
    while (const char *name = AAssetDir_getNextFileName(ad))
      if (asset_wanted(name)) write_asset(w, name);
    AAssetDir_close(ad);
  }
}

void pump_events(AndroidHost *h, int timeout_ms) {
  int events;
  android_poll_source *src;
  while (ALooper_pollOnce(timeout_ms, nullptr, &events, (void **)&src) >= 0) {
    if (src) src->process(h->app, src);
    if (h->app->destroyRequested) h->quit = true;
    timeout_ms = 0;
  }
}
const char *const *a_exts(void *, uint32_t *n) {
  static const char *exts[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
  *n = 2;
  return exts;
}
bool a_surface(void *user, rhi::VkApi &api, VkInstance inst, VkSurfaceKHR *out) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  auto fn = (PFN_vkCreateAndroidSurfaceKHR)api.vkGetInstanceProcAddr(inst, "vkCreateAndroidSurfaceKHR");
  if (!fn) { std::printf("[engine_bridge] android: vkCreateAndroidSurfaceKHR yok\n"); return false; }
  VkAndroidSurfaceCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  ci.window = h->app->window;
  const VkResult r = fn(inst, &ci, nullptr, out);
  if (r != VK_SUCCESS) std::printf("[engine_bridge] android: vkCreateAndroidSurfaceKHR = %d\n", (int)r);
  else h->had_window = true;
  return r == VK_SUCCESS;
}
bridge::HostPoll a_poll(void *user, uint32_t *w, uint32_t *h_) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  pump_events(h, h->window_ready ? 0 : 100);
  if (h->quit) return bridge::HostPoll::Quit;
  if (!h->window_ready) return bridge::HostPoll::NoWindow;
  *w = (uint32_t)ANativeWindow_getWidth(h->app->window);
  *h_ = (uint32_t)ANativeWindow_getHeight(h->app->window);
  if (h->window_changed) { h->window_changed = false; return bridge::HostPoll::WindowChanged; }
  return bridge::HostPoll::Run;
}
const bridge::HostSystem *a_system(void *user) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  h->sys.back_presses = h->back.presses;
  h->sys.back_down = h->back.down;
  h->sys.paused = h->paused;
  h->sys.low_memory = h->low_memory;
  if (h->insets_dirty && h->window_ready) query_insets(h);
  h->sys.inset_l = h->inset[0]; h->sys.inset_t = h->inset[1]; h->sys.inset_r = h->inset[2]; h->sys.inset_b = h->inset[3];
  h->sys.inset_w = h->inset_w; h->sys.inset_h = h->inset_h;
  return &h->sys;
}
void a_back_to_game(void *user, bool on) { static_cast<AndroidHost *>(user)->back_to_game = on; }
const platform::TouchState *a_touch(void *user) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  if (h->app->window) {
    h->touch.width = (float)ANativeWindow_getWidth(h->app->window);
    h->touch.height = (float)ANativeWindow_getHeight(h->app->window);
  }
  return &h->touch;
}
} // namespace

namespace tulpar::engine::bridge {
bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap) {
  (void)title; (void)w; (void)h;
  if (!g_host.app) { std::snprintf(err, err_cap, "android_app yok (android_main calismadi)"); return false; }
  std::printf("[engine_bridge] android host: pencere bekleniyor\n");
  uint32_t waited = 0;
  while (!g_host.window_ready && !g_host.quit && waited < 100) { pump_events(&g_host, 100); waited++; }
  if (!g_host.window_ready) { std::snprintf(err, err_cap, "ANativeWindow gelmedi (%s)", g_host.quit ? "cikis istendi" : "10 s zaman asimi"); return false; }
  out->user = &g_host;
  out->instance_extensions = a_exts;
  out->create_surface = a_surface;
  out->poll = a_poll;
  out->touch = a_touch;
  out->input = nullptr;
  out->close = nullptr;
  out->system = a_system;
  out->back_to_game = a_back_to_game;
  std::printf("[engine_bridge] android host: pencere %dx%d\n", ANativeWindow_getWidth(g_host.app->window), ANativeWindow_getHeight(g_host.app->window));
  return true;
}
void bridge_host_close(BridgeHost *) {
  // Ayni surecte sonraki oturum geri tusunu yeniden sormadikca sistem kapatir.
  g_host.back_to_game = false;
  std::printf("[engine_bridge] android host: kapatildi (dusuk bellek uyarisi %u)\n", g_host.low_memory);
}
} // namespace tulpar::engine::bridge

extern "C" void android_main(android_app *app) {
  g_host = AndroidHost{};
  g_host.app = app;
  app->userData = &g_host;
  app->onAppCmd = on_cmd;
  app->onInputEvent = on_input;
  const char *dir = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
  redirect_output(dir);
  setenv("TMPDIR", dir, 1);
  static char assets[512];
  std::snprintf(assets, sizeof assets, "%s/assets", app->activity->internalDataPath);
  mkdir(assets, 0700);
  // Varliklari APK'dan DAHILI dizine cikar (cgltf/stb fopen ister). ALT DIZINLER
  // dahil: `tulpar build --target=android` varlik dizinini KAYNAK YOLUYLA montaj
  // ediyor (TULPAR_ANDROID_ASSETS=examples/assets -> APK assets/examples/assets/),
  // oysa AAssetManager_openDir yalniz VERILEN dizindeki DOSYALARI listeler, alt
  // dizin adlarini vermez. Dizin adlarini Java tarafindaki AssetManager.list()
  // veriyor; asagidaki ozyineleme onu kullanir.
  AssetWalk walk;
  walk.mgr = app->activity->assetManager;
  walk.dest_root = assets;
  if (jni_assets_begin(app, &walk)) {
    extract_dir(&walk, "", 0);
    jni_assets_end(&walk);
  } else {
    std::printf("[engine_bridge] android: JNI yok, yalniz kok dizin cikarilir\n");
    extract_flat(&walk);
  }
  setenv("TULPAR_ENGINE_ASSETS", assets, 1);
  // Oyun dosya yollarini "examples/assets/x" gibi GORECELI yazar: calisma dizini
  // cikarma kokunun ustu olsun ki ayni yol masaustunde de cihazda da tutsun.
  if (chdir(assets) != 0) std::printf("[engine_bridge] android: chdir basarisiz\n");
  std::printf("[engine_bridge] android: varlik %u dosya, %u dizin -> %s (cwd), cikti dizini %s\n", walk.files, walk.dirs, assets, dir);
  platform::CrashConfig cc;
  cc.report_dir = dir;
  cc.build_id = "tulpar_engine_android";
  platform::crash_reporter_install(cc);
  char lv[PROP_VALUE_MAX] = {0};
  if (__system_property_get("debug.tulpar.log", lv) > 0) setenv("TULPAR_ENGINE_LOG", lv, 1);
  save_mirror_external(app, assets);

  // Oyunun giris noktasi: Tulpar objesinin `main`i (raylib'in rcore_android'i
  // ile ayni desen). Dogrudan bildirim -> sembol yoksa LINK'te patlar
  // (-Wl,--no-undefined), cihazda sessiz "hicbir sey olmadi" yerine.
  std::printf("[engine_bridge] android: oyun main() basliyor\n");
  static char arg0[] = "tulpargame";
  char *argv[2] = {arg0, nullptr};
  const int rc = main(1, argv);
  std::printf("[engine_bridge] android: oyun main() = %d\n", rc);
  std::printf("[engine_bridge] android: bitti rc=%d\n", rc);
  ANativeActivity_finish(app->activity);
  while (!app->destroyRequested) pump_events(&g_host, 100);
}
#endif
