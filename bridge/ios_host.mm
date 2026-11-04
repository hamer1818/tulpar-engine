// L6 BRIDGE — iOS host (UIKit + CAMetalLayer + statik MoltenVK).
//
// Android host'unun (android_host.cpp) iOS karsiligi. Fark: iOS'ta surecin
// `main`i UIApplicationMain'i ANA iplikte kosturmak zorunda ve hic donmez;
// Tulpar oyununun dongusu (motor_ac .. kare_bitir .. motor_kapat) ise bloklayan
// bir dongu. Bu yuzden:
//   * `main` BURADA: UIApplicationMain -> TulparAppDelegate.
//   * Oyunun giris noktasi `tulpar_main` (TulparLang `--target=ios` oyunun
//     `main`ini boyle yeniden adlandirir) AYRI bir iplikte, buyuk yiginla.
//   * UIKit olaylari (dokunma, yasam dongusu, olcu) ana iplikte gelir; kilitli
//     bir kuyruga yazilir, cekirdegin poll()'u (oyun ipligi) bosaltir.
//   * Vulkan: MoltenVK uygulamanin ICINDE statik; vkGetInstanceProcAddr
//     rhi'ye dogrudan verilir (rhi::vk_api_set_static_loader) — dlopen yok.
//   * Yuzey: VK_EXT_metal_surface, gorunumun katmani CAMetalLayer.
//
// Dosyalar: calisma dizini .app koku (oyun "assets/x" okur — TulparLang
// varliklari oraya koyar), kayit/rapor/log Documents'ta (salt-okunur
// paketin disinda), gecici dosyalar NSTemporaryDirectory.
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define VK_USE_PLATFORM_METAL_EXT 1
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "platform/crash.hpp"
#include "platform/time.hpp"
#include "rhi/vk_api.hpp"
#include <vulkan/vulkan_metal.h>

// TulparLang `--target=ios`: oyunun main'i (bkz. aot_pipeline.cpp iOS yolu).
extern "C" int tulpar_main(int argc, char **argv);
// Statik MoltenVK'nin disari verdigi giris (VK_NO_PROTOTYPES: baslik bildirmez).
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *name);

using namespace tulpar::engine;

namespace {
struct LogPump { int fd = -1; int tee = -1; FILE *file = nullptr; };
void *log_pump(void *p) {
  LogPump *lp = static_cast<LogPump *>(p);
  char buf[1024];
  for (;;) {
    const ssize_t r = read(lp->fd, buf, sizeof buf);
    if (r <= 0) break;
    if (lp->file) { std::fwrite(buf, 1, (size_t)r, lp->file); std::fflush(lp->file); }
    if (lp->tee >= 0) (void)!write(lp->tee, buf, (size_t)r); // Xcode / simctl --console
  }
  return nullptr;
}
// stdout/stderr -> Documents/engine_log.txt + ozgun stderr (konsol).
void redirect_output(const char *dir) {
  static LogPump lp;
  char path[1024];
  std::snprintf(path, sizeof path, "%s/engine_log.txt", dir);
  lp.file = std::fopen(path, "w");
  lp.tee = dup(2);
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

enum : uint8_t { kEvDown = 1, kEvMove = 2, kEvUp = 3, kEvCancel = 4 };
struct TouchEv { uint8_t kind; int32_t id; float x, y; };

struct IosHost {
  pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
  CAMetalLayer *layer = nil; // gorunumun katmani (gorunumle yasar)
  bool ready = false;        // ilk olcu geldi
  bool active = true;        // willResignActive .. didBecomeActive arasi false
  bool was_inactive = false; // geri donuste swapchain'i yeniden kurdur
  uint32_t w = 0, h = 0;     // cizilebilir olcu (piksel)
  int32_t inset[4] = {0, 0, 0, 0};
  // Ana iplik -> oyun ipligi dokunma kuyrugu (sabit, ayirmasiz). Dolarsa
  // olay DUSER ve sayilir (sessiz degil: kapanista basilir).
  static constexpr uint32_t kQ = 512;
  TouchEv q[kQ];
  uint32_t qh = 0, qt = 0, dropped = 0;
  uint32_t low_memory = 0;
  // Yalniz oyun ipliginin gordugu durum.
  platform::TouchState touch;
  bridge::HostSystem sys;
};
IosHost g_host;

void push_ev(uint8_t kind, int32_t id, float x, float y) {
  pthread_mutex_lock(&g_host.mu);
  const uint32_t n = (g_host.qt + 1) % IosHost::kQ;
  if (n == g_host.qh) g_host.dropped++;
  else { g_host.q[g_host.qt] = TouchEv{kind, id, x, y}; g_host.qt = n; }
  pthread_mutex_unlock(&g_host.mu);
}
// Kilit altinda cagrilir. Ayni bosaltmada hem DOWN hem UP alan parmagin UP'u
// sonraki kareye kalir: yoksa kisa dokunus (ikisi bir karede) hic gorulmezdi.
void drain_events_locked() {
  int32_t began[16];
  uint32_t nb = 0;
  while (g_host.qh != g_host.qt) {
    const TouchEv &e = g_host.q[g_host.qh];
    if (e.kind == kEvUp) {
      bool same = false;
      for (uint32_t i = 0; i < nb; i++) if (began[i] == e.id) same = true;
      if (same) break;
    }
    switch (e.kind) {
    case kEvDown: g_host.touch.begin(e.id, e.x, e.y); if (nb < 16) began[nb++] = e.id; break;
    case kEvMove: g_host.touch.move(e.id, e.x, e.y); break;
    case kEvUp: g_host.touch.end(e.id); break;
    case kEvCancel: g_host.touch.clear(); break;
    default: break;
    }
    g_host.qh = (g_host.qh + 1) % IosHost::kQ;
  }
}

const char *const *i_exts(void *, uint32_t *n) {
  static const char *exts[] = {"VK_KHR_surface", "VK_EXT_metal_surface"};
  *n = 2;
  return exts;
}
bool i_surface(void *, rhi::VkApi &api, VkInstance inst, VkSurfaceKHR *out) {
  auto fn = (PFN_vkCreateMetalSurfaceEXT)api.vkGetInstanceProcAddr(inst, "vkCreateMetalSurfaceEXT");
  if (!fn) { std::printf("[engine_bridge] ios: vkCreateMetalSurfaceEXT yok\n"); return false; }
  VkMetalSurfaceCreateInfoEXT ci{};
  ci.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  ci.pLayer = g_host.layer; // MoltenVK orneklerindeki gibi dogrudan (const eklenmesi ortuk)
  const VkResult r = fn(inst, &ci, nullptr, out);
  if (r != VK_SUCCESS) std::printf("[engine_bridge] ios: vkCreateMetalSurfaceEXT = %d\n", (int)r);
  return r == VK_SUCCESS;
}
bridge::HostPoll i_poll(void *, uint32_t *w, uint32_t *h) {
  pthread_mutex_lock(&g_host.mu);
  const bool active = g_host.active;
  if (active) drain_events_locked();
  *w = g_host.w;
  *h = g_host.h;
  const bool back = active && g_host.was_inactive;
  if (back) g_host.was_inactive = false;
  pthread_mutex_unlock(&g_host.mu);
  // Arka planda GPU isi gonderilemez (iOS komut tamponunu reddeder):
  // pencere yokmus gibi davran, kare cizilmez; dongu de bosa donmesin.
  if (!active) { usleep(50000); return bridge::HostPoll::NoWindow; }
  if (back) return bridge::HostPoll::WindowChanged; // yuzey + swapchain taze
  return bridge::HostPoll::Run;
}
const platform::TouchState *i_touch(void *) {
  pthread_mutex_lock(&g_host.mu);
  g_host.touch.width = (float)g_host.w;
  g_host.touch.height = (float)g_host.h;
  pthread_mutex_unlock(&g_host.mu);
  g_host.touch.time_ns = platform::now_ns();
  return &g_host.touch;
}
const bridge::HostSystem *i_system(void *) {
  pthread_mutex_lock(&g_host.mu);
  g_host.sys.paused = !g_host.active;
  g_host.sys.low_memory = g_host.low_memory;
  g_host.sys.inset_l = g_host.inset[0]; g_host.sys.inset_t = g_host.inset[1];
  g_host.sys.inset_r = g_host.inset[2]; g_host.sys.inset_b = g_host.inset[3];
  g_host.sys.inset_w = g_host.w; g_host.sys.inset_h = g_host.h;
  pthread_mutex_unlock(&g_host.mu);
  return &g_host.sys;
}

void *game_thread(void *) {
  // Ilk olcu gelmeden motor_ac pencereyi soramaz (bridge_host_open bekler).
  std::printf("[engine_bridge] ios: oyun tulpar_main() basliyor\n");
  static char arg0[] = "tulpargame";
  char *argv[2] = {arg0, nullptr};
  const int rc = tulpar_main(1, argv);
  std::printf("[engine_bridge] ios: oyun tulpar_main() = %d (dusen dokunma olayi %u)\n", rc, g_host.dropped);
  // iOS uygulamasi kendini kapatmaz (HIG); oyun bittiyse surec biter — ana
  // menuye donus oyunun isi. exit() ana iplikte degil: UIKit'e birakilir.
  dispatch_async(dispatch_get_main_queue(), ^{ exit(rc); });
  return nullptr;
}
} // namespace

// --- UIKit --------------------------------------------------------------------
@interface TulparView : UIView
@end
@implementation TulparView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (instancetype)initWithFrame:(CGRect)f {
  if ((self = [super initWithFrame:f])) {
    self.multipleTouchEnabled = YES;
    self.opaque = YES;
    self.backgroundColor = [UIColor blackColor];
  }
  return self;
}
- (void)layoutSubviews {
  [super layoutSubviews];
  const CGFloat s = self.window ? self.window.screen.nativeScale : [UIScreen mainScreen].nativeScale;
  self.contentScaleFactor = s;
  CAMetalLayer *ml = (CAMetalLayer *)self.layer;
  ml.contentsScale = s;
  const CGSize b = self.bounds.size;
  const uint32_t w = (uint32_t)(b.width * s + 0.5), h = (uint32_t)(b.height * s + 0.5);
  ml.drawableSize = CGSizeMake(w, h);
  const UIEdgeInsets in = self.safeAreaInsets;
  pthread_mutex_lock(&g_host.mu);
  g_host.layer = ml;
  g_host.w = w;
  g_host.h = h;
  g_host.inset[0] = (int32_t)(in.left * s); g_host.inset[1] = (int32_t)(in.top * s);
  g_host.inset[2] = (int32_t)(in.right * s); g_host.inset[3] = (int32_t)(in.bottom * s);
  const bool first = !g_host.ready && w && h;
  if (first) g_host.ready = true;
  pthread_cond_broadcast(&g_host.cv);
  pthread_mutex_unlock(&g_host.mu);
  if (first) std::printf("[engine_bridge] ios: gorunum %ux%u (olcek %.2f, guvenli alan %.0f/%.0f/%.0f/%.0f pt)\n", w, h, (double)s,
                         (double)in.left, (double)in.top, (double)in.right, (double)in.bottom);
}
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self setNeedsLayout]; }
static int32_t touch_id(UITouch *t) { return (int32_t)(((uintptr_t)(__bridge void *)t >> 4) & 0x7fffffff); }
- (void)emit:(NSSet<UITouch *> *)touches kind:(uint8_t)k {
  const CGFloat s = self.contentScaleFactor;
  for (UITouch *t in touches) {
    const CGPoint p = [t locationInView:self];
    push_ev(k, touch_id(t), (float)(p.x * s), (float)(p.y * s));
  }
}
- (void)touchesBegan:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self emit:t kind:kEvDown]; }
- (void)touchesMoved:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self emit:t kind:kEvMove]; }
- (void)touchesEnded:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self emit:t kind:kEvUp]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { push_ev(kEvCancel, 0, 0, 0); }
@end

@interface TulparViewController : UIViewController
@end
@implementation TulparViewController
- (void)loadView { self.view = [[TulparView alloc] initWithFrame:[UIScreen mainScreen].bounds]; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
// Kenardan kaydirma oyun dokunusunu yutmasin (ilk kaydirma oyuna, ikincisi sisteme).
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
@end

@interface TulparAppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow *window;
@end
@implementation TulparAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
  NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  const char *dir = docs.fileSystemRepresentation;
  redirect_output(dir);
  setenv("TMPDIR", NSTemporaryDirectory().fileSystemRepresentation, 1);
  // Oyun yollari GORECELI ("assets/x"): calisma dizini paketin koku.
  NSString *bundle = [NSBundle mainBundle].bundlePath;
  if (chdir(bundle.fileSystemRepresentation) != 0) std::printf("[engine_bridge] ios: chdir basarisiz\n");
  // Motorun kendi varliklari (HUD fontu): TulparLang eklentinin assets/'ini
  // <app>/tulpar-ext/<ad>/assets'e koyar; kopru TULPAR_EXT_PATH'te arar.
  NSString *ext = [bundle stringByAppendingPathComponent:@"tulpar-ext/engine"];
  setenv("TULPAR_EXT_PATH", ext.fileSystemRepresentation, 1);
  char save[1024];
  std::snprintf(save, sizeof save, "%s/tulpar_kayit.txt", dir);
  if (!getenv("TULPAR_ENGINE_SAVE")) setenv("TULPAR_ENGINE_SAVE", save, 1);
  setenv("TULPAR_ENGINE_RAPOR", dir, 1);
  NSString *lang = [NSLocale preferredLanguages].firstObject;
  if (lang.length >= 2) setenv("TULPAR_ENGINE_LANG", [[lang substringToIndex:2] lowercaseString].UTF8String, 1);
  std::printf("[engine_bridge] ios: paket %s (cwd), belgeler %s, dil %s\n", bundle.fileSystemRepresentation, dir,
              getenv("TULPAR_ENGINE_LANG") ? getenv("TULPAR_ENGINE_LANG") : "?");
  platform::CrashConfig cc;
  static char crash_dir[1024];
  std::snprintf(crash_dir, sizeof crash_dir, "%s", dir);
  cc.report_dir = crash_dir;
  cc.build_id = "tulpar_engine_ios";
  platform::crash_reporter_install(cc);
  {
    struct utsname u{};
    uname(&u);
    char cihaz[256];
    std::snprintf(cihaz, sizeof cihaz, "%s iOS %s", u.machine, [UIDevice currentDevice].systemVersion.UTF8String);
    bridge::bridge_report_host(crash_dir, cihaz, nullptr);
  }
  bridge::bridge_store_test_allowed(false);
  // Ses: oyun sesi sessiz anahtarina uyar, baska uygulamanin muzigini kesmez.
  [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
  [[AVAudioSession sharedInstance] setActive:YES error:nil];
  rhi::vk_api_set_static_loader(vkGetInstanceProcAddr);
#if TARGET_OS_SIMULATOR
  // Simulatorun Metal surucusu (MTLSimDriver) argument buffer'da
  // setBuffer:offset:atIndex: icinde abort ediyor — olculdu 2026-11-04, CI
  // macos-15 iOS 26.2 simulatoru, vkAllocateDescriptorSets ilk cagrisi
  // (Renderer::init). MoltenVK'ya klasik baglamayi soyle; cihazda varsayilan kalir.
  setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 0);
#endif

  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  self.window.rootViewController = [[TulparViewController alloc] init];
  [self.window makeKeyAndVisible];
  [UIApplication sharedApplication].idleTimerDisabled = YES; // oyun sirasinda ekran kararmasin

  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 64u << 20); // masaustu ana yigini 8 MB; Tulpar derin ozyinelemeli
  pthread_t t;
  pthread_create(&t, &at, game_thread, nullptr);
  pthread_attr_destroy(&at);
  pthread_detach(t);
  return YES;
}
- (void)applicationWillResignActive:(UIApplication *)app {
  pthread_mutex_lock(&g_host.mu);
  g_host.active = false;
  g_host.was_inactive = true;
  pthread_mutex_unlock(&g_host.mu);
}
- (void)applicationDidBecomeActive:(UIApplication *)app {
  pthread_mutex_lock(&g_host.mu);
  g_host.active = true;
  pthread_mutex_unlock(&g_host.mu);
}
- (void)applicationDidReceiveMemoryWarning:(UIApplication *)app {
  pthread_mutex_lock(&g_host.mu);
  g_host.low_memory++;
  pthread_mutex_unlock(&g_host.mu);
}
@end

namespace tulpar::engine::bridge {
bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap) {
  (void)title; (void)w; (void)h;
  pthread_mutex_lock(&g_host.mu);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += 10;
  while (!g_host.ready) {
    if (pthread_cond_timedwait(&g_host.cv, &g_host.mu, &ts) != 0) break;
  }
  const bool ok = g_host.ready;
  const uint32_t fw = g_host.w, fh = g_host.h;
  pthread_mutex_unlock(&g_host.mu);
  if (!ok) { std::snprintf(err, err_cap, "iOS gorunumu 10 s icinde olculmedi"); return false; }
  out->user = &g_host;
  out->instance_extensions = i_exts;
  out->create_surface = i_surface;
  out->poll = i_poll;
  out->touch = i_touch;
  out->input = nullptr;
  out->close = nullptr;
  out->system = i_system;
  out->back_to_game = nullptr; // iOS'ta geri tusu yok
  std::printf("[engine_bridge] ios host: pencere %ux%u\n", fw, fh);
  return true;
}
void bridge_host_close(BridgeHost *) {
  std::printf("[engine_bridge] ios host: kapatildi (dusuk bellek uyarisi %u, dusen dokunma %u)\n", g_host.low_memory, g_host.dropped);
}
} // namespace tulpar::engine::bridge

int main(int argc, char *argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([TulparAppDelegate class]));
  }
}
#endif
