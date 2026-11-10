// L6 BRIDGE — Android NativeActivity host: Tulpar oyununun (libtulpargame.so)
// `main`i bu android_main'den cagrilir (raylib'in rcore_android'i gibi).
// stdout/stderr -> boru -> logcat (etiket "tulpar") + files/engine_log.txt;
// APK assets/ -> dahili dizin (cgltf fopen ister), TULPAR_ENGINE_ASSETS.
// Kopru cekirdegi teng_init'te bridge_host_open cagirir: pencere gelene kadar
// olaylar pompalanir, sonra yuzey/poll/dokunmatik geri cagrilari verilir.
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <android/asset_manager.h>
#include <android/configuration.h>
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
  // debug.tulpar.hiz: yalniz debuggable APK (android_main'de bir kez sorulur).
  bool debuggable = false;
  uint32_t speed_poll = 0;
  // Metin girisi (WoE M3): ekran klavyesi acikken tus olaylari kod noktasina cevrilir (JNI KeyEvent.getUnicodeChar)
  bool text_on = false;
  uint32_t text_buf[64] = {0};
  uint32_t text_n = 0;
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
  int32_t cutout[4] = {0, 0, 0, 0};
  int32_t bars[4] = {0, 0, 0, 0};   // N1: sistem cubuklarinin (hareket cubugu, durum cubugu) pencereyle ortusmesi
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
        // getSystemWindowInset* (API 20+; 30'da kullanimdan kalkti ama calisir): gizli cubuk 0 doner
        jclass ci = e->GetObjectClass(ins);
        const char *cad[4] = {"getSystemWindowInsetLeft", "getSystemWindowInsetTop", "getSystemWindowInsetRight", "getSystemWindowInsetBottom"};
        for (int k = 0; k < 4 && j.ok(); k++) {
          jmethodID m = e->GetMethodID(ci, cad[k], "()I");
          if (m && j.ok()) bars[k] = e->CallIntMethod(ins, m);
          if (!j.ok()) bars[k] = 0;
        }
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
          if (m && j.ok()) cutout[k] = e->CallIntMethod(cut, m);
          if (!j.ok()) cutout[k] = 0;
        }
      }
      j.ok();
    }
  }
  const char *eski = std::getenv("TULPAR_ENGINE_INSET_ESKI");
  tulpar::engine::bridge::merge_safe_insets(cutout, bars, !(eski && eski[0] == '1'), in);
  if (in[0] != h->inset[0] || in[1] != h->inset[1] || in[2] != h->inset[2] || in[3] != h->inset[3])
    std::printf("[engine_bridge] android: guvenli alan sol %d ust %d sag %d alt %d (centik %d/%d/%d/%d, cubuk %d/%d/%d/%d%s; pencere %ux%u, API %d)\n",
                in[0], in[1], in[2], in[3], cutout[0], cutout[1], cutout[2], cutout[3], bars[0], bars[1], bars[2], bars[3],
                (eski && eski[0] == '1') ? ", ESKI: yalniz centik" : "", h->inset_w, h->inset_h, android_sdk());
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
// Tus olayinin unicode karsiligi: NDK vermiyor, KeyEvent(downTime, eventTime, action, code, repeat, meta)
// kurulup getUnicodeChar(meta) cagrilir (JNI; yalniz metin girisi acikken, tus basina bir kez). 0 = karakter degil.
uint32_t key_unicode(android_app *app, AInputEvent *ev);
int32_t on_input(android_app *app, AInputEvent *ev) {
  AndroidHost *h = static_cast<AndroidHost *>(app->userData);
  if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY && h->text_on && AKeyEvent_getKeyCode(ev) != AKEYCODE_BACK) {
    if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN) {
      const int32_t kc = AKeyEvent_getKeyCode(ev);
      uint32_t cp = 0;
      if (kc == AKEYCODE_DEL) cp = 8;
      else if (kc == AKEYCODE_ENTER) cp = 10;
      else cp = key_unicode(app, ev);
      if (cp && h->text_n < 64) h->text_buf[h->text_n++] = cp;
    }
    return 1;
  }
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
uint32_t key_unicode(android_app *app, AInputEvent *ev) {
  JniScope j(app);
  JNIEnv *e = j.env;
  if (!e) return 0;
  jclass kc = e->FindClass("android/view/KeyEvent");
  if (!kc || !j.ok()) return 0;
  jmethodID ctor = e->GetMethodID(kc, "<init>", "(JJIIII)V");
  jmethodID uni = e->GetMethodID(kc, "getUnicodeChar", "(I)I");
  if (!ctor || !uni || !j.ok()) return 0;
  const jlong dt = (jlong)(AKeyEvent_getDownTime(ev) / 1000000), et = (jlong)(AKeyEvent_getEventTime(ev) / 1000000);
  const jint meta = AKeyEvent_getMetaState(ev);
  jobject ke = e->NewObject(kc, ctor, dt, et, (jint)AKeyEvent_getAction(ev), (jint)AKeyEvent_getKeyCode(ev), (jint)AKeyEvent_getRepeatCount(ev), meta);
  if (!ke || !j.ok()) return 0;
  const jint cp = e->CallIntMethod(ke, uni, meta);
  e->DeleteLocalRef(ke);
  e->DeleteLocalRef(kc);
  if (!j.ok() || cp <= 0) return 0;
  return (uint32_t)cp;
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

// --- Teshis raporu paylasimi (yayin adimi 6) ---------------------------------
// Rapor metni sistemin paylasim secicisine DUZ METIN olarak verilir (ACTION_SEND,
// text/plain, EXTRA_TEXT): dosya URI'si FileProvider ister, NativeActivity
// derlemesinde manifestte provider yok. Hedefi (e-posta, mesaj, not) OYUNCU
// secer; motor hicbir yere kendiliginden gondermez, ag izni yok. startActivity
// oyun thread'inden cagrilir (Activity bunu ana thread'e kendisi iletir).
android_app *g_share_app = nullptr;
bool android_share_text(const char *subject, const char *text) {
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e || !g_share_app) return false;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return false; }
  bool ok = false;
  jclass ic = e->FindClass("android/content/Intent");
  jmethodID ctor = ic && j.ok() ? e->GetMethodID(ic, "<init>", "(Ljava/lang/String;)V") : nullptr;
  jmethodID set_type = ctor && j.ok() ? e->GetMethodID(ic, "setType", "(Ljava/lang/String;)Landroid/content/Intent;") : nullptr;
  jmethodID put = set_type && j.ok() ? e->GetMethodID(ic, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;") : nullptr;
  jmethodID chooser = put && j.ok() ? e->GetStaticMethodID(ic, "createChooser", "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;") : nullptr;
  if (chooser && j.ok()) {
    jobject it = e->NewObject(ic, ctor, e->NewStringUTF("android.intent.action.SEND"));
    if (it && j.ok()) {
      e->CallObjectMethod(it, set_type, e->NewStringUTF("text/plain"));
      e->CallObjectMethod(it, put, e->NewStringUTF("android.intent.extra.SUBJECT"), e->NewStringUTF(subject ? subject : ""));
      e->CallObjectMethod(it, put, e->NewStringUTF("android.intent.extra.TEXT"), e->NewStringUTF(text ? text : ""));
      jobject ch = j.ok() ? e->CallStaticObjectMethod(ic, chooser, it, e->NewStringUTF(subject ? subject : "")) : nullptr;
      jobject act = g_share_app->activity->clazz;
      jmethodID start = ch && j.ok() ? e->GetMethodID(e->GetObjectClass(act), "startActivity", "(Landroid/content/Intent;)V") : nullptr;
      if (start && j.ok()) {
        e->CallVoidMethod(act, start, ch);
        ok = j.ok();
      }
    }
  }
  if (!ok) j.ok();
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: rapor paylasimi %s\n", ok ? "secici acildi" : "ACILAMADI (JNI)");
  return ok;
}
// --- Yerel bildirim (teng_notify, WoE M6 2026-11-09) ---------------------------
// Oyun arka plandayken (pencere yok ama surec yasiyor; kare dongusu NoWindow ile
// surer) "rapor geldi / insaat bitti" gibi olaylari bildirim cubuguna yazar.
// Notification.Builder + NotificationManager.notify; Android 8+ kanal ister
// ("tulpar_oyun", IMPORTANCE_DEFAULT). Android 13+ POST_NOTIFICATIONS calisma
// zamani izni ister (manifest: tulpar.toml [android] bildirim = true); izin yoksa
// notify istisna ATMAZ, sessizce gostermez — bu yuzden once izin bakilir ve
// false donulur. Dokununca uygulama acilir (getLaunchIntentForPackage +
// PendingIntent.FLAG_IMMUTABLE). Uzak (push) bildirim DEGIL: surec oldurulmusse
// gelmez (FCM, Gradle + Google hesabi ister).
constexpr const char *kBildirimKanal = "tulpar_oyun";
bool g_kanal_kuruldu = false;
jobject app_context(JniScope &j, android_app *app) {
  JNIEnv *e = j.env;
  jobject act = app->activity->clazz;
  jmethodID gac = e->GetMethodID(e->GetObjectClass(act), "getApplicationContext", "()Landroid/content/Context;");
  return gac && j.ok() ? e->CallObjectMethod(act, gac) : nullptr;
}
// 1 izin var, 0 yok. request: yoksa Activity.requestPermissions ile sor (API 33+; sonuc
// asenkron — oyun sonra yeniden bakar). API < 33: izin kurulumda verilmis sayilir;
// yine de NotificationManager.areNotificationsEnabled (oyuncu ayarlardan kapatmis olabilir).
int android_notify_permission(bool request) {
  if (!g_share_app) return 0;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return 0;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return 0; }
  int sonuc = 0;
  jobject ctx = app_context(j, g_share_app);
  if (ctx && j.ok()) {
    if (android_sdk() >= 33) {
      jmethodID csp = e->GetMethodID(e->GetObjectClass(ctx), "checkSelfPermission", "(Ljava/lang/String;)I");
      jstring izin = e->NewStringUTF("android.permission.POST_NOTIFICATIONS");
      const jint r = csp && j.ok() ? e->CallIntMethod(ctx, csp, izin) : -1;
      sonuc = j.ok() && r == 0 ? 1 : 0; // PackageManager.PERMISSION_GRANTED = 0
      if (!sonuc && request) {
        jobject act = g_share_app->activity->clazz;
        jclass sc = e->FindClass("java/lang/String");
        jobjectArray arr = sc && j.ok() ? e->NewObjectArray(1, sc, izin) : nullptr;
        jmethodID rp = arr && j.ok() ? e->GetMethodID(e->GetObjectClass(act), "requestPermissions", "([Ljava/lang/String;I)V") : nullptr;
        if (rp && j.ok()) { e->CallVoidMethod(act, rp, arr, (jint)7301); j.ok(); }
        std::printf("[engine_bridge] android: POST_NOTIFICATIONS izni soruldu\n");
      }
    } else {
      sonuc = 1;
    }
    if (sonuc) {
      jmethodID gss = e->GetMethodID(e->GetObjectClass(ctx), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
      jobject nm = gss && j.ok() ? e->CallObjectMethod(ctx, gss, e->NewStringUTF("notification")) : nullptr;
      jmethodID ane = nm && j.ok() ? e->GetMethodID(e->GetObjectClass(nm), "areNotificationsEnabled", "()Z") : nullptr;
      if (ane && j.ok()) sonuc = e->CallBooleanMethod(nm, ane) && j.ok() ? 1 : 0;
    }
  }
  j.ok();
  e->PopLocalFrame(nullptr);
  return sonuc;
}
bool android_notify(const char *title, const char *text, int id) {
  if (!g_share_app) return false;
  if (!android_notify_permission(false)) {
    std::printf("[engine_bridge] android: bildirim GOSTERILMEDI — izin yok (POST_NOTIFICATIONS / ayarlardan kapali)\n");
    return false;
  }
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return false;
  if (e->PushLocalFrame(32) != 0) { j.ok(); return false; }
  bool ok = false;
  const int sdk = android_sdk();
  jobject ctx = app_context(j, g_share_app);
  jclass cc = ctx && j.ok() ? e->GetObjectClass(ctx) : nullptr;
  jmethodID gss = cc ? e->GetMethodID(cc, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") : nullptr;
  jobject nm = gss && j.ok() ? e->CallObjectMethod(ctx, gss, e->NewStringUTF("notification")) : nullptr;
  jclass nmc = nm && j.ok() ? e->GetObjectClass(nm) : nullptr;
  // Kanal (API 26+): ayni kimlikle yeniden yaratmak zararsiz ama surec basina bir kez yeter.
  if (nmc && sdk >= 26 && !g_kanal_kuruldu) {
    jclass chc = e->FindClass("android/app/NotificationChannel");
    jmethodID chi = chc && j.ok() ? e->GetMethodID(chc, "<init>", "(Ljava/lang/String;Ljava/lang/CharSequence;I)V") : nullptr;
    jobject ch = chi && j.ok() ? e->NewObject(chc, chi, e->NewStringUTF(kBildirimKanal), e->NewStringUTF("Oyun"), (jint)3) : nullptr;
    jmethodID cnc = ch && j.ok() ? e->GetMethodID(nmc, "createNotificationChannel", "(Landroid/app/NotificationChannel;)V") : nullptr;
    if (cnc && j.ok()) { e->CallVoidMethod(nm, cnc, ch); g_kanal_kuruldu = j.ok(); }
  }
  // Kucuk simge: uygulama simgesi (ApplicationInfo.icon); yoksa android.R.drawable.ic_dialog_info.
  jint simge = 0;
  if (nmc) {
    jmethodID gai = e->GetMethodID(cc, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    jobject ai = gai && j.ok() ? e->CallObjectMethod(ctx, gai) : nullptr;
    jfieldID fi = ai && j.ok() ? e->GetFieldID(e->GetObjectClass(ai), "icon", "I") : nullptr;
    if (fi && j.ok()) simge = e->GetIntField(ai, fi);
    j.ok();
    if (!simge) {
      jclass rd = e->FindClass("android/R$drawable");
      jfieldID f = rd && j.ok() ? e->GetStaticFieldID(rd, "ic_dialog_info", "I") : nullptr;
      if (f && j.ok()) simge = e->GetStaticIntField(rd, f);
      j.ok();
    }
  }
  jclass bc = nmc ? e->FindClass("android/app/Notification$Builder") : nullptr;
  jobject b = nullptr;
  if (bc && j.ok()) {
    if (sdk >= 26) {
      jmethodID bi = e->GetMethodID(bc, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V");
      b = bi && j.ok() ? e->NewObject(bc, bi, ctx, e->NewStringUTF(kBildirimKanal)) : nullptr;
    } else {
      jmethodID bi = e->GetMethodID(bc, "<init>", "(Landroid/content/Context;)V");
      b = bi && j.ok() ? e->NewObject(bc, bi, ctx) : nullptr;
    }
  }
  if (b && j.ok()) {
    const char *R = "Landroid/app/Notification$Builder;";
    char sig[160];
    std::snprintf(sig, sizeof sig, "(I)%s", R);
    jmethodID m_icon = e->GetMethodID(bc, "setSmallIcon", sig);
    std::snprintf(sig, sizeof sig, "(Ljava/lang/CharSequence;)%s", R);
    jmethodID m_title = j.ok() ? e->GetMethodID(bc, "setContentTitle", sig) : nullptr;
    jmethodID m_text = j.ok() ? e->GetMethodID(bc, "setContentText", sig) : nullptr;
    std::snprintf(sig, sizeof sig, "(Z)%s", R);
    jmethodID m_auto = j.ok() ? e->GetMethodID(bc, "setAutoCancel", sig) : nullptr;
    std::snprintf(sig, sizeof sig, "(Landroid/app/PendingIntent;)%s", R);
    jmethodID m_pi = j.ok() ? e->GetMethodID(bc, "setContentIntent", sig) : nullptr;
    jmethodID m_build = j.ok() ? e->GetMethodID(bc, "build", "()Landroid/app/Notification;") : nullptr;
    if (m_build && j.ok()) {
      e->CallObjectMethod(b, m_icon, simge);
      e->CallObjectMethod(b, m_title, e->NewStringUTF(title));
      e->CallObjectMethod(b, m_text, e->NewStringUTF(text ? text : ""));
      e->CallObjectMethod(b, m_auto, (jboolean)JNI_TRUE);
      j.ok();
      // Dokununca uygulamayi ac (yoksa bildirim yalniz silinir; zararsiz).
      jmethodID gpm = e->GetMethodID(cc, "getPackageManager", "()Landroid/content/pm/PackageManager;");
      jobject pm = gpm && j.ok() ? e->CallObjectMethod(ctx, gpm) : nullptr;
      jmethodID gpn = pm && j.ok() ? e->GetMethodID(cc, "getPackageName", "()Ljava/lang/String;") : nullptr;
      jobject pn = gpn && j.ok() ? e->CallObjectMethod(ctx, gpn) : nullptr;
      jmethodID gli = pn && j.ok() ? e->GetMethodID(e->GetObjectClass(pm), "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;") : nullptr;
      jobject it = gli && j.ok() ? e->CallObjectMethod(pm, gli, pn) : nullptr;
      jclass pic = it && j.ok() ? e->FindClass("android/app/PendingIntent") : nullptr;
      jmethodID ga = pic && j.ok() ? e->GetStaticMethodID(pic, "getActivity", "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;") : nullptr;
      // FLAG_IMMUTABLE (0x04000000, API 31+ zorunlu) | FLAG_UPDATE_CURRENT (0x08000000)
      jobject pi = ga && j.ok() ? e->CallStaticObjectMethod(pic, ga, ctx, (jint)0, it, (jint)(0x04000000 | 0x08000000)) : nullptr;
      if (pi && j.ok()) e->CallObjectMethod(b, m_pi, pi);
      j.ok();
      jobject n = e->CallObjectMethod(b, m_build);
      jmethodID nf = n && j.ok() ? e->GetMethodID(nmc, "notify", "(ILandroid/app/Notification;)V") : nullptr;
      if (nf && j.ok()) { e->CallVoidMethod(nm, nf, (jint)id, n); ok = j.ok(); }
    }
  }
  if (!ok) j.ok();
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: bildirim #%d %s\n", id, ok ? "gonderildi" : "GONDERILEMEDI (JNI)");
  return ok;
}
// --- Zamanli bildirim (teng_notify_at/_cancel/_due, KOPRU §15.1) -------------------
// Surec olse de gelen bildirim: Java alicisi dev.tulparlang.engine.ZamanliBildirim (AlarmManager +
// SharedPreferences; yeniden baslatmada BOOT_COMPLETED ile yeniden kurulur). Sinif APK'ya motorun Java
// paketiyle girer (tools/android_google.py); yoksa at 0 ("bu APK'da yok"), oyun surecteki teng_notify ile
// yetinir. FindClass yerel is parcacigindan uygulama siniflarini GORMEZ: etkinligin sinif yukleyicisi.
jclass g_zamanli_sinif = nullptr;
bool g_zamanli_yok = false;
jclass zamanli_sinif(JNIEnv *e, JniScope &j) {
  if (g_zamanli_sinif || g_zamanli_yok) return g_zamanli_sinif;
  jobject act = g_share_app->activity->clazz;
  jmethodID gcl = e->GetMethodID(e->GetObjectClass(act), "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject cl = gcl && j.ok() ? e->CallObjectMethod(act, gcl) : nullptr;
  jclass clc = cl && j.ok() ? e->FindClass("java/lang/ClassLoader") : nullptr;
  jmethodID load = clc && j.ok() ? e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
  jobject c = load && j.ok() ? e->CallObjectMethod(cl, load, e->NewStringUTF("dev.tulparlang.engine.ZamanliBildirim")) : nullptr;
  if (!c || !j.ok()) {
    g_zamanli_yok = true;
    std::printf("[engine_bridge] android: zamanli bildirim YOK (dev.tulparlang.engine.ZamanliBildirim bu APK'da degil — tools/android_google.py)\n");
    return nullptr;
  }
  g_zamanli_sinif = (jclass)e->NewGlobalRef(c);
  return g_zamanli_sinif;
}
int android_notify_at(const char *title, const char *text, int id, int delay_s) {
  if (!g_share_app) return 0;
  if (!android_notify_permission(false)) {
    std::printf("[engine_bridge] android: zamanli bildirim #%d KURULMADI — izin yok\n", id);
    return 0;
  }
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return 0;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return 0; }
  int r = 0;
  jclass c = zamanli_sinif(e, j);
  jmethodID m = c ? e->GetStaticMethodID(c, "kur", "(Landroid/content/Context;IJLjava/lang/String;Ljava/lang/String;)I") : nullptr;
  if (m && j.ok()) {
    r = e->CallStaticIntMethod(c, m, g_share_app->activity->clazz, (jint)id, (jlong)delay_s * 1000,
                               e->NewStringUTF(title), e->NewStringUTF(text ? text : ""));
    if (!j.ok()) r = 0;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: zamanli bildirim #%d %d sn sonra %s\n", id, delay_s, r == 1 ? "kuruldu" : "KURULAMADI");
  return r;
}
int android_notify_cancel(int id) {
  if (!g_share_app) return -1;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return -1;
  if (e->PushLocalFrame(8) != 0) { j.ok(); return -1; }
  int r = 0; // sinif yoksa iptal edilecek kayit da yok
  jclass c = zamanli_sinif(e, j);
  jmethodID m = c ? e->GetStaticMethodID(c, "iptal", "(Landroid/content/Context;I)I") : nullptr;
  if (m && j.ok()) {
    r = e->CallStaticIntMethod(c, m, g_share_app->activity->clazz, (jint)id);
    if (!j.ok()) r = -1;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  return r;
}
int android_notify_due(int id) {
  if (!g_share_app) return -1;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return -1;
  if (e->PushLocalFrame(8) != 0) { j.ok(); return -1; }
  long long ms = -1;
  jclass c = zamanli_sinif(e, j);
  jmethodID m = c ? e->GetStaticMethodID(c, "kalan", "(Landroid/content/Context;I)J") : nullptr;
  if (m && j.ok()) {
    ms = e->CallStaticLongMethod(c, m, g_share_app->activity->clazz, (jint)id);
    if (!j.ok()) ms = -1;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  return ms < 0 ? -1 : (int)((ms + 999) / 1000);
}
// --- Baglanti ac (teng_open_url, WoE 2026-11-09) ---------------------------------
// Intent(ACTION_VIEW, Uri.parse(url)) -> sistem tarayicisi (ya da o baglantiyi tutan uygulama).
// Sema denetimi kopruda (yalniz http/https). Tarayici yoksa ActivityNotFoundException -> false.
bool android_open_url(const char *url) {
  if (!g_share_app) return false;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return false;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return false; }
  bool ok = false;
  jclass uc = e->FindClass("android/net/Uri");
  jmethodID parse = uc && j.ok() ? e->GetStaticMethodID(uc, "parse", "(Ljava/lang/String;)Landroid/net/Uri;") : nullptr;
  jobject uri = parse && j.ok() ? e->CallStaticObjectMethod(uc, parse, e->NewStringUTF(url)) : nullptr;
  jclass ic = uri && j.ok() ? e->FindClass("android/content/Intent") : nullptr;
  jmethodID ctor = ic && j.ok() ? e->GetMethodID(ic, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V") : nullptr;
  jobject it = ctor && j.ok() ? e->NewObject(ic, ctor, e->NewStringUTF("android.intent.action.VIEW"), uri) : nullptr;
  jobject act = g_share_app->activity->clazz;
  jmethodID start = it && j.ok() ? e->GetMethodID(e->GetObjectClass(act), "startActivity", "(Landroid/content/Intent;)V") : nullptr;
  if (start && j.ok()) { e->CallVoidMethod(act, start, it); ok = j.ok(); }
  if (!ok) j.ok();
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: baglanti %s\n", ok ? "acildi" : "ACILAMADI (tarayici yok / JNI)");
  return ok;
}
// --- Google ile giris (teng_google_*, KOPRU §16) ----------------------------------
// Java sinifi dev.tulparlang.engine.GoogleGiris (Credential Manager) APK'ya ancak oyun Google paketini
// isterse girer (tools/android_google.py). NativeActivity'nin yerel is parcacigindaki FindClass sistem sinif
// yukleyicisini kullanir, uygulama siniflarini GORMEZ: sinif etkinligin getClassLoader()'indan yuklenir ve
// GlobalRef'te tutulur. Bulunamazsa (paket yok) start -1: oyun tarayici yoluna duser.
jclass g_google_sinif = nullptr;
bool g_google_yok = false;   // bir kez arandi, yok: her tiklamada istisna uretme
jclass google_sinif(JNIEnv *e, JniScope &j) {
  if (g_google_sinif || g_google_yok) return g_google_sinif;
  jobject act = g_share_app->activity->clazz;
  jmethodID gcl = e->GetMethodID(e->GetObjectClass(act), "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject cl = gcl && j.ok() ? e->CallObjectMethod(act, gcl) : nullptr;
  jclass clc = cl && j.ok() ? e->FindClass("java/lang/ClassLoader") : nullptr;
  jmethodID load = clc && j.ok() ? e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
  jobject c = load && j.ok() ? e->CallObjectMethod(cl, load, e->NewStringUTF("dev.tulparlang.engine.GoogleGiris")) : nullptr;
  if (!c || !j.ok()) {
    g_google_yok = true;
    std::printf("[engine_bridge] android: google girisi YOK (dev.tulparlang.engine.GoogleGiris bu APK'da degil — tools/android_google.py)\n");
    return nullptr;
  }
  g_google_sinif = (jclass)e->NewGlobalRef(c);
  return g_google_sinif;
}
int android_google_start(const char *client) {
  if (!g_share_app) return -1;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return -1;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return -1; }
  int r = -1;
  jclass c = google_sinif(e, j);
  jmethodID m = c ? e->GetStaticMethodID(c, "basla", "(Landroid/app/Activity;Ljava/lang/String;)I") : nullptr;
  if (m && j.ok()) {
    r = e->CallStaticIntMethod(c, m, g_share_app->activity->clazz, e->NewStringUTF(client));
    if (!j.ok()) r = -1;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: google hesap secici %s\n", r == 1 ? "acildi" : r == 0 ? "zaten acik" : "ACILAMADI");
  return r;
}
static void jkopya(JNIEnv *e, jstring s, char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!s) return;
  const char *u = e->GetStringUTFChars(s, nullptr);
  if (u) { std::snprintf(out, cap, "%s", u); e->ReleaseStringUTFChars(s, u); }
}
int android_google_poll(char *tok, size_t tcap, char *err, size_t ecap) {
  if (!g_share_app || !g_google_sinif) return 4;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return 1;
  if (e->PushLocalFrame(8) != 0) { j.ok(); return 1; }
  jmethodID md = e->GetStaticMethodID(g_google_sinif, "durum", "()I");
  int d = md && j.ok() ? e->CallStaticIntMethod(g_google_sinif, md) : 1;
  if (!j.ok()) d = 1;
  if (d >= 2) {
    jmethodID mt = e->GetStaticMethodID(g_google_sinif, "jeton", "()Ljava/lang/String;");
    jmethodID mh = mt && j.ok() ? e->GetStaticMethodID(g_google_sinif, "hata", "()Ljava/lang/String;") : nullptr;
    if (mt && mh && j.ok()) {
      jkopya(e, (jstring)e->CallStaticObjectMethod(g_google_sinif, mt), tok, tcap);
      if (j.ok()) jkopya(e, (jstring)e->CallStaticObjectMethod(g_google_sinif, mh), err, ecap);
    }
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  return d;
}
void android_google_reset() {
  if (!g_share_app || !g_google_sinif) return;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return;
  jmethodID m = e->GetStaticMethodID(g_google_sinif, "sifirla", "()V");
  if (m && j.ok()) e->CallStaticVoidMethod(g_google_sinif, m);
  j.ok();
}
// --- Google Play Oyunlar: giris + kayitli oyun (teng_pgs_*, KOPRU §17) ------------------------------
// Java sinifi dev.tulparlang.engine.PlayOyunlar (play-services-games-v2) APK'ya ancak oyun "oyunlar" profilini
// isterse girer (tools/android_google.py --profil oyunlar). Sinif etkinligin sinif yukleyicisinden (GoogleGiris
// ile ayni sebep), yoksa signin -1: oyun bulut kaydini gizler.
jclass g_pgs_sinif = nullptr;
bool g_pgs_yok = false;
jclass pgs_sinif(JNIEnv *e, JniScope &j) {
  if (g_pgs_sinif || g_pgs_yok) return g_pgs_sinif;
  jobject act = g_share_app->activity->clazz;
  jmethodID gcl = e->GetMethodID(e->GetObjectClass(act), "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject cl = gcl && j.ok() ? e->CallObjectMethod(act, gcl) : nullptr;
  jclass clc = cl && j.ok() ? e->FindClass("java/lang/ClassLoader") : nullptr;
  jmethodID load = clc && j.ok() ? e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
  jobject c = load && j.ok() ? e->CallObjectMethod(cl, load, e->NewStringUTF("dev.tulparlang.engine.PlayOyunlar")) : nullptr;
  if (!c || !j.ok()) {
    g_pgs_yok = true;
    std::printf("[engine_bridge] android: Play Oyunlar YOK (dev.tulparlang.engine.PlayOyunlar bu APK'da degil — android_google.py --profil oyunlar)\n");
    return nullptr;
  }
  g_pgs_sinif = (jclass)e->NewGlobalRef(c);
  return g_pgs_sinif;
}
int android_pgs_signin(int interactive) {
  if (!g_share_app) return -1;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return -1;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return -1; }
  int r = -1;
  jclass c = pgs_sinif(e, j);
  jmethodID m = c ? e->GetStaticMethodID(c, "giris", "(Landroid/app/Activity;I)I") : nullptr;
  if (m && j.ok()) {
    r = e->CallStaticIntMethod(c, m, g_share_app->activity->clazz, (jint)interactive);
    if (!j.ok()) r = -1;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  std::printf("[engine_bridge] android: Play Oyunlar girisi %s (%s)\n", r == 1 ? "basladi" : r == 0 ? "zaten suruyor" : "BASLAMADI",
              interactive ? "etkilesimli" : "sessiz");
  return r;
}
// Statik int yontemi + (durum >= 2 ise) iki dizge (veri, hata) — giris ve islem yoklamasinin ortak yolu.
static int pgs_yokla(const char *durum_ad, const char *veri_ad, char *out, size_t ocap, char *err, size_t ecap) {
  if (!g_share_app || !g_pgs_sinif) return 4;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return 1;
  if (e->PushLocalFrame(8) != 0) { j.ok(); return 1; }
  jmethodID md = e->GetStaticMethodID(g_pgs_sinif, durum_ad, "()I");
  int d = md && j.ok() ? e->CallStaticIntMethod(g_pgs_sinif, md) : 1;
  if (!j.ok()) d = 1;
  if (d >= 2) {
    jmethodID mv = e->GetStaticMethodID(g_pgs_sinif, veri_ad, "()Ljava/lang/String;");
    jmethodID mh = mv && j.ok() ? e->GetStaticMethodID(g_pgs_sinif, "hata", "()Ljava/lang/String;") : nullptr;
    if (mv && mh && j.ok()) {
      jkopya(e, (jstring)e->CallStaticObjectMethod(g_pgs_sinif, mv), out, ocap);
      if (j.ok()) jkopya(e, (jstring)e->CallStaticObjectMethod(g_pgs_sinif, mh), err, ecap);
    }
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  return d;
}
int android_pgs_poll_signin(char *player, size_t pcap, char *err, size_t ecap) {
  return pgs_yokla("girisDurum", "oyuncu", player, pcap, err, ecap);
}
int android_pgs_open(const char *name, const char *data, const char *desc, int progress) {
  if (!g_share_app || !g_pgs_sinif) return -1;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return -1;
  if (e->PushLocalFrame(16) != 0) { j.ok(); return -1; }
  int r = -1;
  jmethodID m = data ? e->GetStaticMethodID(g_pgs_sinif, "yaz", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I")
                     : e->GetStaticMethodID(g_pgs_sinif, "oku", "(Landroid/app/Activity;Ljava/lang/String;)I");
  if (m && j.ok()) {
    jobject act = g_share_app->activity->clazz;
    r = data ? e->CallStaticIntMethod(g_pgs_sinif, m, act, e->NewStringUTF(name), e->NewStringUTF(data), e->NewStringUTF(desc ? desc : ""), (jint)progress)
             : e->CallStaticIntMethod(g_pgs_sinif, m, act, e->NewStringUTF(name));
    if (!j.ok()) r = -1;
  } else {
    j.ok();
  }
  e->PopLocalFrame(nullptr);
  return r;
}
int android_pgs_poll_op(char *data, size_t dcap, char *err, size_t ecap) {
  return pgs_yokla("islemDurum", "veri", data, dcap, err, ecap);
}
void android_pgs_op_reset() {
  if (!g_share_app || !g_pgs_sinif) return;
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e) return;
  jmethodID m = e->GetStaticMethodID(g_pgs_sinif, "islemSifirla", "()V");
  if (m && j.ok()) e->CallStaticVoidMethod(g_pgs_sinif, m);
  j.ok();
}
// --- Ag: Wi-Fi yayin kilidi (teng_net_wifi_lock, N2a 2026-10-23) ----------------
// Cogu telefonun Wi-Fi surucusu guc tasarrufu icin coklu yayin VE yayin paketlerini
// kilit yokken suzer: ayni agdaki rakibi bulan UDP yayini (oyun TASARIM §21) hic
// gelmez, hata da vermez. WifiManager.MulticastLock bunu acar; manifestte
// CHANGE_WIFI_MULTICAST_STATE ister (yoksa acquire SecurityException -> false).
// Kilit uygulama baglamindan (Activity degil) yaratilir ve GlobalRef'te tutulur:
// etkinlik ayni surecte yeniden yaratilinca (Tuzaklar 8ct) de gecerli kalir.
// Referans sayimsiz: iki kez al tek kez birak yeter, alinmamisi birakmak istisna atmaz.
jobject g_ag_kilit = nullptr;
bool android_wifi_lock(bool on) {
  JniScope j(g_share_app);
  JNIEnv *e = j.env;
  if (!e || !g_share_app) return false;
  if (!on && !g_ag_kilit) return true; // alinmamis kilidi birakmak: yapacak is yok
  if (e->PushLocalFrame(16) != 0) { j.ok(); return false; }
  const char *asama = "baglam";
  if (!g_ag_kilit) {
    jobject act = g_share_app->activity->clazz;
    jmethodID gac = e->GetMethodID(e->GetObjectClass(act), "getApplicationContext", "()Landroid/content/Context;");
    jobject ctx = gac && j.ok() ? e->CallObjectMethod(act, gac) : nullptr;
    jmethodID gss = ctx && j.ok() ? e->GetMethodID(e->GetObjectClass(ctx), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;") : nullptr;
    if (gss && j.ok()) asama = "WifiManager";
    jobject wm = gss && j.ok() ? e->CallObjectMethod(ctx, gss, e->NewStringUTF("wifi")) : nullptr;
    jmethodID cml = wm && j.ok() ? e->GetMethodID(e->GetObjectClass(wm), "createMulticastLock",
                                                  "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;")
                                 : nullptr;
    if (cml && j.ok()) asama = "createMulticastLock";
    jobject kilit = cml && j.ok() ? e->CallObjectMethod(wm, cml, e->NewStringUTF("tulpar_ag")) : nullptr;
    if (kilit && j.ok()) {
      jmethodID rc = e->GetMethodID(e->GetObjectClass(kilit), "setReferenceCounted", "(Z)V");
      if (rc && j.ok()) e->CallVoidMethod(kilit, rc, JNI_FALSE);
      if (j.ok()) g_ag_kilit = e->NewGlobalRef(kilit);
    }
  }
  bool ok = false;
  if (g_ag_kilit) {
    asama = on ? "acquire (izin: CHANGE_WIFI_MULTICAST_STATE?)" : "release";
    jclass kc = e->GetObjectClass(g_ag_kilit);
    jmethodID m = e->GetMethodID(kc, on ? "acquire" : "release", "()V");
    if (m && j.ok()) {
      e->CallVoidMethod(g_ag_kilit, m);
      ok = j.ok(); // SecurityException burada temizlenir -> false
    }
    jmethodID held = e->GetMethodID(kc, "isHeld", "()Z");
    const bool tutuluyor = held && j.ok() && e->CallBooleanMethod(g_ag_kilit, held) == JNI_TRUE && j.ok();
    std::printf("[engine_bridge] android: Wi-Fi yayin kilidi %s %s (isHeld=%d)\n", on ? "al" : "birak", ok ? "tamam" : "BASARISIZ",
                tutuluyor ? 1 : 0);
    if (ok && on && !tutuluyor) ok = false;
  }
  if (!ok) std::printf("[engine_bridge] android: Wi-Fi yayin kilidi dustu: asama %s\n", asama);
  e->PopLocalFrame(nullptr);
  return ok;
}

// Cihaz tanimi (rapora girer): uretici, model, Android surumu, API, ABI.
// Kisisel veri yok (seri numarasi / reklam kimligi OKUNMAZ).
void report_device(char *out, size_t cap) {
  char ur[PROP_VALUE_MAX] = {0}, md[PROP_VALUE_MAX] = {0}, rel[PROP_VALUE_MAX] = {0}, abi[PROP_VALUE_MAX] = {0};
  __system_property_get("ro.product.manufacturer", ur);
  __system_property_get("ro.product.model", md);
  __system_property_get("ro.build.version.release", rel);
  __system_property_get("ro.product.cpu.abi", abi);
  std::snprintf(out, cap, "%s %s, Android %s (API %d), %s", ur, md, rel, android_sdk(), abi);
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
  // Hata ayiklama hizi: ~30 karede bir ozellik okunur (ucuz, JNI yok). Yayin
  // derlemesinde hic okunmaz: debug_speed 0 kalir, cekirdek dokunmaz.
  if (h->debuggable && (h->speed_poll++ % 30) == 0) {
    char v[PROP_VALUE_MAX] = {0};
    h->sys.debug_speed = __system_property_get("debug.tulpar.hiz", v) > 0 ? (float)std::atof(v) : 0.0f;
  }
  return &h->sys;
}
void a_back_to_game(void *user, bool on) { static_cast<AndroidHost *>(user)->back_to_game = on; }
void a_soft_keyboard(void *user, bool on) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  h->text_on = on;
  h->text_n = 0;
  if (on) ANativeActivity_showSoftInput(h->app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
  else ANativeActivity_hideSoftInput(h->app->activity, 0);
}
uint32_t a_text_chars(void *user, uint32_t *out, uint32_t cap) {
  AndroidHost *h = static_cast<AndroidHost *>(user);
  uint32_t n = h->text_n < cap ? h->text_n : cap;
  for (uint32_t i = 0; i < n; i++) out[i] = h->text_buf[i];
  h->text_n = 0;
  return n;
}
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
  out->soft_keyboard = a_soft_keyboard;
  out->text_chars = a_text_chars;
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
  // Teshis raporu (teng_report_*): cokme raporlariyla AYNI dizin + cihaz + paylasim.
  g_share_app = app;
  {
    char cihaz[192];
    report_device(cihaz, sizeof cihaz);
    bridge::bridge_report_host(dir, cihaz, android_share_text);
  }
  bridge::bridge_net_host(android_wifi_lock); // teng_net_wifi_lock: MulticastLock (g_share_app ile ayni Activity)
  bridge::bridge_notify_host(android_notify, android_notify_permission); // teng_notify (WoE M6)
  bridge::bridge_notify_sched_host(android_notify_at, android_notify_cancel, android_notify_due); // teng_notify_at (§15.1)
  bridge::bridge_url_host(android_open_url);                              // teng_open_url
  bridge::bridge_google_host(android_google_start, android_google_poll, android_google_reset); // teng_google_*
  bridge::bridge_pgs_host(android_pgs_signin, android_pgs_poll_signin, android_pgs_open, android_pgs_poll_op, android_pgs_op_reset); // teng_pgs_*
  char lv[PROP_VALUE_MAX] = {0};
  if (__system_property_get("debug.tulpar.log", lv) > 0) setenv("TULPAR_ENGINE_LOG", lv, 1);
  // Cihaz dili: Android'de LANG yok, oyun Tulpar'dan JNI'ye inemez. Glue'nun
  // AConfiguration'indan iki harfli ISO 639 kodu ("tr", "en"...) TULPAR_ENGINE_LANG
  // olur (masaustundeki LANG'in karsiligi). Her android_main'de yeniden yazilir:
  // etkinlik ayni surecte yeniden yaratilinca (Tuzaklar 8ct) dil degismis olabilir.
  // Asagidaki debug.tulpar.env bunu ezebilir (debug APK'da dil denemesi).
  {
    char dil[3] = {0};
    if (app->config) AConfiguration_getLanguage(app->config, dil);
    if (dil[0]) {
      setenv("TULPAR_ENGINE_LANG", dil, 1);
      std::printf("[engine_bridge] android: cihaz dili %s (TULPAR_ENGINE_LANG)\n", dil);
    } else {
      unsetenv("TULPAR_ENGINE_LANG");
      std::printf("[engine_bridge] android: cihaz dili okunamadi (AConfiguration bos)\n");
    }
  }
  g_host.debuggable = app_debuggable(app);
  // Para test saglayicisi (sahte reklam, bedava satin alma) YALNIZ debug APK'da:
  // yayin APK'sinda teng_ads_init("test") false doner, oyun dugmeleri gostermez.
  bridge::bridge_store_test_allowed(g_host.debuggable);
  // debug.tulpar.env = "AD=deger;AD2=deger2": YALNIZ hata ayiklanabilir APK'da
  // oyunun ortam degiskenleri (masaustundeki `AD=deger tulpar oyun.tpr`un
  // telefondaki karsiligi; ornek: oyunun otopilotu). Yayin APK'si okumaz.
  // Ortam degiskeni SURECE aittir: Android etkinligi ayni surecte yeniden yaratinca
  // (Tuzaklar 8ct) bir onceki oturumun debug.tulpar.env'inden kalan ad (ornek
  // TULPAR_KKS_TEST) yeni ozellikte yoksa bile yasardi — emulatorde 2026-10-09'da
  // yalniz OTOPILOT=1 verilen oturum eski TEST ile hikaye taramasi kostu. Onceki
  // oturumun koydugu adlar burada tutulup her girisin basinda silinir.
  static char s_env_onceki[PROP_VALUE_MAX] = {0};
  for (char *ad = s_env_onceki; *ad;) {
    char *son = std::strchr(ad, ';');
    if (son) *son = 0;
    unsetenv(ad);
    std::printf("[engine_bridge] android: debug.tulpar.env onceki oturumdan %s silindi\n", ad);
    if (!son) break;
    ad = son + 1;
  }
  s_env_onceki[0] = 0;
  char env_v[PROP_VALUE_MAX] = {0};
  if (__system_property_get("debug.tulpar.env", env_v) > 0) {
    if (!g_host.debuggable) {
      std::printf("[engine_bridge] android: debug.tulpar.env YOK SAYILDI — APK hata ayiklanabilir degil\n");
    } else {
      char *save = nullptr;
      for (char *tok = strtok_r(env_v, ";", &save); tok; tok = strtok_r(nullptr, ";", &save)) {
        char *eq = std::strchr(tok, '=');
        if (!eq || eq == tok) continue;
        *eq = 0;
        setenv(tok, eq + 1, 1);
        const size_t dolu = std::strlen(s_env_onceki), n = std::strlen(tok);
        if (dolu + n + 2 <= sizeof(s_env_onceki)) {
          if (dolu) s_env_onceki[dolu] = ';';
          std::memcpy(s_env_onceki + dolu + (dolu ? 1 : 0), tok, n + 1);
        }
        std::printf("[engine_bridge] android: debug.tulpar.env %s=%s\n", tok, eq + 1);
      }
    }
  }
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
