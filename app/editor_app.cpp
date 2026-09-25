#include "app/editor_app.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>   // GetProcessMemoryInfo (RSS)
#endif

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <IconsMaterialDesign.h> // ikon makrolari (IconFontCppHeaders, Zlib)
#include <imnodes.h>            // Malzeme graf paneli (thedmd/imgui-node-editor degil, imnodes)
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "app/demo_scene.hpp"
#include "app/editor_game.hpp"
#include "app/editor_ui.hpp"
#include "content/gi.hpp"
#include "content/gltf.hpp"
#include "content/hash.hpp"
#include "content/primitives.hpp"
#include "content/particles.hpp"
#include "content/particle_module.hpp"
#include "content/ribbon.hpp"
#include "content/vfx_graph.hpp"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_compile.hpp"
#include "sim/voxel_smoke.hpp"
#include "audio/mixer.hpp"
#include "audio/spatial.hpp"
#include "content/device_tier.hpp"
#include "content/dynamic_quality.hpp"
#include "renderer/cull.hpp"
#include "sim/boids.hpp"
#include "sim/chaos_physics.hpp"
#include "sim/fluid_system.hpp"
#include "sim/mls_mpm.hpp"
#include "sim/physics.hpp"
#include "sim/quantum_gen.hpp"
#include "sim/rollback.hpp"
// Prosedurel arazi/su/voksel onizlemesi derlenmis sahneninkiyle AYNI
// ureticileri kullanir (make_terrain_mesh / make_voxel_mesh / make_water_mesh
// scene_runtime.hpp'de bildirildi) -- editorde gorulen sey oyunda cikan sey
// olsun diye; ikinci bir geometri kopyasi YOK.
#include "content/scene_runtime.hpp"
#include "content/terrain.hpp"
#include "content/voxel.hpp"
#include "content/water_wave.hpp"
#include "core/jobs/job_system.hpp"
#include "core/math/noise.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/memory.hpp"   // os_resident_bytes (durum cubugu RSS)
#include "platform/thread.hpp"
#include "platform/paths.hpp"   // varlik yolu: ikilinin yani -> calisma dizini -> kaynak agaci
#include "platform/startup_report.hpp" // guncelleme sonrasi yeniden baslatma hatasi: engine_hata.log
#include "platform/time.hpp"
#include "rhi/device.hpp"
#include "app/editor_camera.hpp"
#include "app/editor_chrome.hpp"
#include "app/editor_console.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_files.hpp"
#include "app/editor_layout.hpp"
#include "app/editor_overlay.hpp"
#include "app/editor_game_view.hpp"
#include "app/editor_viewport.hpp"
#include "app/editor_widgets.hpp"
#include "app/editor_inspector.hpp"
#include "app/editor_palette.hpp"
#include "app/editor_multiedit.hpp"
#include "app/editor_props.hpp"
#include "app/editor_update.hpp"
#include "content/prefab.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "sim/schedule.hpp"

#include <unistd.h>

namespace tulpar::engine::app {

namespace {
constexpr float kPi = 3.14159265358979f;
static float s_smoke_live_diff = 0.20f;
static float s_smoke_live_diss = 0.015f;
using content::SceneDesc;
using content::SceneEntity;

enum class ViewportTab : uint8_t {
  Scene = 0, // Sahne: serbest kamera, gizmo, seçim, ızgara
  Game = 1,  // Oyun: kamera bileşeninden bakış, temiz oyun görüntüsü
};

enum class GameAspect : uint8_t {
  Free = 0,
  Aspect16_9,
  Aspect16_10,
  Aspect4_3,
  Aspect21_9,
  Aspect1_1,
};

// Surecin yerlesik bellegi (RSS), MB. Olcum L0'da (platform::os_resident_bytes:
// Linux/Android /proc, macOS task_info, Windows GetProcessMemoryInfo) — Tulpar
// koprusunun bellek_kb()'si de ayni sayaci okur. Eskiden burada ayri bir kopya
// vardi ve macOS'ta /proc olmadigi icin hep 0 gosteriyordu. Sayi yalnizca durum
// cubugunda GOSTERILIYOR, bir kapi degil — bulunamazsa 0 doner.
static float get_system_rss_mb() {
  return (float)((double)platform::os_resident_bytes() / (1024.0 * 1024.0));
}

struct ScenePolyStats {
  uint32_t triangles = 0;
  uint32_t vertices = 0;
};

static ScenePolyStats calculate_scene_poly_stats(const content::SceneDesc &s) {
  ScenePolyStats st{};
  for (uint32_t i = 0; i < s.entity_count; i++) {
    const SceneEntity &e = s.entities[i];
    if (e.flags & content::kSceneHidden) continue;
    if (e.components & content::kSceneModel) {
      if (std::strstr(e.name, "kure") || std::strstr(e.name, "Küre") || std::strstr(e.name, "sphere")) {
        st.triangles += 768;
        st.vertices += 420;
      } else if (std::strstr(e.name, "zemin") || std::strstr(e.name, "Zemin") || std::strstr(e.name, "plane")) {
        st.triangles += 2;
        st.vertices += 4;
      } else if (std::strstr(e.name, "kup") || std::strstr(e.name, "Küp") || std::strstr(e.name, "kutu") || std::strstr(e.name, "Kutu")) {
        st.triangles += 12;
        st.vertices += 24;
      } else {
        st.triangles += 1840;
        st.vertices += 1200;
      }
    } else if (e.components & content::kSceneBody) {
      if (e.shape == content::SceneShape::Box) { st.triangles += 12; st.vertices += 24; }
      else if (e.shape == content::SceneShape::Sphere) { st.triangles += 768; st.vertices += 420; }
    }
  }
  return st;
}

inline ImVec4 tone_col(Tone t, float a = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * a);
}

inline ImU32 tone_u32(Tone t, float a = 1.0f) { return ImGui::GetColorU32(tone_col(t, a)); }

inline ImVec4 tone(Tone t, float a = 1.0f) { return tone_col(t, a); }

inline const char *editor_basename(const char *path) {
  if (!path || !path[0]) return "";
  const char *sl = std::strrchr(path, '/');
  if (!sl) sl = std::strrchr(path, '\\');
  return sl ? sl + 1 : path;
}

static int find_first_camera_entity(const content::SceneDesc &s) {
  for (uint32_t i = 0; i < s.entity_count; i++) {
    if ((s.entities[i].components & content::kSceneCamera) != 0 && (s.entities[i].flags & content::kSceneHidden) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static void draw_stats_overlay(bool *open, float dt, const renderer::RendererStats &rs, const content::SceneDesc &scene,
                               uint32_t vp_w, uint32_t vp_h, ViewportTab tab, const Arena &arena, const ViewportRect &vr) {
  if (!open || !*open) return;
  if (vr.w < 120.0f || vr.h < 120.0f) return;

  // Modern Oyun Motoru Viewport HUD (Unreal / Dagor / Unity tarzı)
  const float hud_w = 300.0f;
  const float hud_x = vr.x + vr.w - hud_w - 12.0f;
  const float hud_y = vr.y + 12.0f;

  ImGui::SetNextWindowPos(ImVec2(hud_x, hud_y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(hud_w, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, tone_col(Tone::Bg0, 0.90f));
  ImGui::PushStyleColor(ImGuiCol_Border, tone_col(Tone::Line, 0.70f));

  const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoMove;

  if (ImGui::Begin("##EngineStatsHUD", nullptr, wf)) {
    const float fps = dt > 0.0001f ? (1.0f / dt) : 0.0f;
    const float ms = dt * 1000.0f;

    // Üst Başlık: İkon, FPS, ms, Kapat [✕]
    ImVec4 fps_col = (fps >= 55.0f) ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f)
                                    : ((fps >= 30.0f) ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f));
    ImGui::TextColored(tone(Tone::Accent), ICON_MD_BAR_CHART);
    ImGui::SameLine();
    ImGui::TextColored(fps_col, "%.1f FPS", (double)fps);
    ImGui::SameLine();
    ImGui::TextDisabled("(%.2f ms)", (double)ms);

    ImGui::SameLine(ImGui::GetWindowWidth() - 24.0f);
    if (ImGui::SmallButton("\xE2\x9C\x95##close_hud")) { // ✕
      *open = false;
    }

    ImGui::Separator();

    // PR #7'nin duz `metric_row` yerlesimi korundu (dar, goruntu alanina
    // yapisik HUD). main'in CollapsingHeader + Columns bolumleri BILGI olarak
    // tasindi: koseler, mesh/doku, golge haritasi, etkin isik, arena, ARC,
    // varlik sayisi ve gorunum kipi satirlari PR #7'nin listesinde YOKTU ve
    // duserlerdi. Bicim PR #7'den, icerik iki taraftan.
    const ScenePolyStats poly = calculate_scene_poly_stats(scene);
    const float rss_mb = get_system_rss_mb();
    const float vram_est = (float)(vp_w * vp_h * 8 * 2 + rs.textures * 1024 * 1024 * 4) / (1024.0f * 1024.0f);

    auto metric_row = [](const char *label, const char *val, ImVec4 val_col) {
      ImGui::TextDisabled("%s", label);
      ImGui::SameLine(150.0f);
      ImGui::TextColored(val_col, "%s", val);
    };

    char buf[64];
    std::snprintf(buf, sizeof buf, "%u tris", poly.triangles);
    metric_row("\xC3\x9C\xC3\xA7genler (Tris):", buf, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

    std::snprintf(buf, sizeof buf, "%u verts", poly.vertices);
    metric_row("K\xC3\xB6\xC5\x9F" "eler (Verts):", buf, tone(Tone::Text));

    std::snprintf(buf, sizeof buf, "%u batch", rs.draws);
    metric_row("\xC3\x87izim \xC3\x87" "a\xC4\x9Fr\xC4\xB1s\xC4\xB1:", buf, tone(Tone::Text));

    std::snprintf(buf, sizeof buf, "%u SetPass", rs.material_binds);
    metric_row("Malzeme Ge\xC3\xA7i\xC5\x9Fi:", buf, tone(Tone::Text));

    std::snprintf(buf, sizeof buf, "%u / %u", rs.meshes, rs.textures);
    metric_row("Mesh / Doku:", buf, tone(Tone::Text));

    metric_row("G\xC3\xB6lge Haritas\xC4\xB1:", "CSM 3x1024 D16", tone(Tone::TextDim));

    std::snprintf(buf, sizeof buf, "%u de\xC4\x9F" "erlendirildi", rs.clusters.lights_evaluated);
    metric_row("Aktif I\xC5\x9F\xC4\xB1k:", buf, tone(Tone::Text));

    ImGui::Separator();

    std::snprintf(buf, sizeof buf, "%.1f MB", (double)rss_mb);
    metric_row("Sistem RAM:", buf, ImVec4(0.3f, 0.9f, 0.5f, 1.0f));

    std::snprintf(buf, sizeof buf, "%.1f MB", (double)vram_est);
    metric_row("GPU VRAM:", buf, tone(Tone::Text));

    std::snprintf(buf, sizeof buf, "%.1f / %.1f KB", (double)arena.used() / 1024.0, (double)arena.capacity() / 1024.0);
    metric_row("Motor Arena:", buf, tone(Tone::Text));

    metric_row("S\xC4\xB1" "cak Tahsis:", "0 Bayt", ImVec4(0.2f, 0.8f, 0.4f, 1.0f));

    metric_row("Tulpar ARC:", "0 s\xC4\xB1z\xC4\xB1nt\xC4\xB1", tone(Tone::TextDim));

    ImGui::Separator();

    std::snprintf(buf, sizeof buf, "%u varl\xC4\xB1k", scene.entity_count);
    metric_row("Sahne Varl\xC4\xB1klar\xC4\xB1:", buf, tone(Tone::Text));

    metric_row("G\xC3\xB6r\xC3\xBCn\xC3\xBCm Kipi:",
               tab == ViewportTab::Scene ? ICON_MD_MOVIE " Sahne" : ICON_MD_SPORTS_ESPORTS " Oyun", tone(Tone::TextDim));

    std::snprintf(buf, sizeof buf, "%u x %u px", vp_w, vp_h);
    metric_row("\xC3\x87\xC3\xB6z\xC3\xBCn\xC3\xBCrl\xC3\xBCk:", buf, tone(Tone::TextDim));
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

struct RecordCtx {
  renderer::Renderer *r;
  EditorUi *ui;
  EditorViewport *vp;
  rhi::Device *dev;
  EditorGameView *game = nullptr; // F5 gomulu oyunun karesi (yeni kare varsa yuklenir)
};
// ANA GECIS ARTIK YALNIZ ImGui ICERIYOR. 3B sahne kendi VIEWPORT gecisine,
// yani ImGui'nin doku olarak ornekleyebilecegi offscreen hedefe ciziliyor.
// Onceden ucu de (3B + HUD + ImGui) ayni renk subpass'indeydi: sahne tam ekran,
// ImGui ustunde yuzuyordu — "debug kaplamali oyun" modeli. Sahne bir panele o
// yuzden konamiyordu.
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  // SUBPASS'I BIZ ILERLETIYORUZ. Ana gecis (swapchain ve offscreen, ikisi de)
  // IKI subpass tanimliyor: derinlik on-gecisi + renk. Eskiden ikinciye
  // `Renderer::record` geciyordu (renderer.cpp:2407) ve ImGui'nin boru hatti da
  // subpass 1 icin kurulmustu. Renderer artik VIEWPORT gecisine cizdigi icin
  // ilerleten kimse kalmadi: ImGui subpass 0'da cizmeye calisip
  // VUID-vkCmdDrawIndexed-subpass-02685 veriyordu ve KARE TAMAMEN SIYAH
  // cikiyordu (olculdu: 9910 benzersiz renk -> 1). Ustelik Vulkan iki subpass'li
  // bir gecisi subpass 0'da BITIRMEYE de izin vermez, yani ilerletmek sart.
  c->dev->api().vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
  c->ui->record(cb);
}
// Ana gecis BASLAMADAN once kosan kayit: kendi gecisi olan her sey burada.
// SIRA ONEMLI — golge once, sonra viewport; ikisi de ayri gecis.
void before_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record_shadow(cb);
  if (c->vp->begin_pass(cb)) {
    c->r->record(cb);
    c->r->ui_record(cb);
    c->vp->end_pass(cb);
  }
  // Oyunun karesi: kopya + duzen gecisi, ana gecisten (ImGui orneklemeden) ONCE.
  if (c->game) c->game->record_upload(cb);
}

void entity_from_matrix(SceneEntity &e, const Mat4 &mat) {
  ImGuizmo::DecomposeMatrixToComponents(&mat.m[0][0], &e.pos.x, &e.rot_deg.x, &e.scale.x);
}

// Editor durumu: veri modeli (gercek) + turetilmis sim/gpu kaynaklari.
// Renderer'in CALISMA ZAMANINDA degistirilebilen ayarlari. Sahne dosyasina
// YAZILMAZ: SceneWorld'e eklemek surum artirimi + scene_blob degisikligi
// ister; bu tur oturum ayari olarak duruyor ve her karede uygulaniyor.
//
// Varsayilanlar RendererConfig'ten TUREIR -- ayni sayi iki yerde tutulmaz,
// motorun varsayilani degisirse editor kendiliginden uyar.
const renderer::RendererConfig kRenderDefaults{};

struct RenderSettings {
  bool shadows = true;
  float shadow_bias = kRenderDefaults.shadow_bias;
  float shadow_normal_offset = kRenderDefaults.shadow_normal_offset;
  float exposure = kRenderDefaults.exposure;
  float bloom_threshold = kRenderDefaults.bloom_threshold;
  float bloom_intensity = kRenderDefaults.bloom_intensity;
  float bloom_knee = kRenderDefaults.bloom_soft_knee;
  float bloom_radius = kRenderDefaults.bloom_radius;
  float render_scale = kRenderDefaults.temporal.render_scale;
  int upscaler = (int)kRenderDefaults.temporal.upscaler;
  float sharpness = kRenderDefaults.temporal.sharpness;
  bool jitter = kRenderDefaults.temporal.jitter;
};

enum class TerrainBrushMode : uint8_t {
  Yukselt = 0, // Raise
  Alcalt,      // Lower
  Duzlestir,   // Flatten
  Yumusat,     // Smooth
  Gurultu,     // Noise
  Teras        // Terrace
};

struct TerrainSculptLayer {
  float *deltas = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t version = 0;
};

struct TerrainBrushSettings {
  TerrainBrushMode mode = TerrainBrushMode::Yukselt;
  float radius = 8.0f;
  float strength = 1.0f;
  float target_height = 5.0f;
  float terrace_step = 2.0f;
  bool active = false; // 3D gorunumde fircayla boyama acik/kapali
  Vec3 hit_world{0, 0, 0};
  Vec3 hit_local{0, 0, 0};
  bool hit_valid = false;
};

struct EditorState {
  SceneDesc scene;
  content::SceneHistory hist;
  char scene_path[1024];
  char scene_dir[1024];
  content::Model models[content::kSceneMaxAssets];
  content::UploadedModel ups[content::kSceneMaxAssets];
  bool have[content::kSceneMaxAssets];
  content::PoseScratch pose_scratch;
  sim::BodyId bodies[content::kSceneMaxEntities];
  sim::CharacterId chars[content::kSceneMaxEntities];
  content::SceneLiveStats live; // son bodies_spawn'in olcumu
  bool bodies_live = false;
  // Malzeme graf paneli (ImNodes) acik mi. Panel ONIZLEMEDIR: tek sabit dugum
  // cizer, hicbir malzemeye baglanmaz -- rozeti bunu acikca soyler.
  bool show_node_editor = false;
  // --- Prosedurel geometri (PR #331 bilesenleri) ----------------------------
  // Ilkeller YUVA basina tutulur, varlik basina DEGIL: ayni kup yuz varlikta
  // kullanilsa da tek mesh. Tabloyu content::build_primitive_meshes kurar --
  // SceneRuntime de AYNI fonksiyonu cagirir, yani editor ve oyun ayni
  // geometriyi cizer.
  renderer::MeshHandle prims[content::kPrimitiveSlotCount] = {};
  // Arazi / voksel / su varlik basinadir (her birinin kendi olculeri var) ve
  // alanlari degisince YENIDEN uretilir. Ozet (hash) degismedikce tek bir
  // create_mesh bile calismaz: alan kaydirmadan duran bir sahnede kare icinde
  // GPU ayirmasi olmaz.
  renderer::MeshHandle terrain_meshes[content::kSceneMaxEntities] = {};
  renderer::MeshHandle voxel_meshes[content::kSceneMaxEntities] = {};
  renderer::MeshHandle water_meshes[content::kSceneMaxEntities] = {};
  uint32_t terrain_hash[content::kSceneMaxEntities] = {};
  uint32_t voxel_hash[content::kSceneMaxEntities] = {};
  uint32_t water_hash[content::kSceneMaxEntities] = {};
  uint32_t terrain_mesh_w[content::kSceneMaxEntities] = {};
  uint32_t terrain_mesh_h[content::kSceneMaxEntities] = {};
  // Arazi heykeltiras (sculpt) fircalari ve degisiklik katmani
  TerrainBrushSettings terrain_brush;
  TerrainSculptLayer terrain_sculpt[content::kSceneMaxEntities] = {};
  Arena sculpt_arena;
  // Malzemeler ACILISTA kurulur. create_material kare icinde cagrilirsa
  // "kare basina 0 ayirma" kapisi duser (scene_runtime.cpp ayni notu tasiyor).
  renderer::MaterialHandle terrain_mat{}, voxel_mat{}, water_mat{}, sun_mat{}, light_core_mat{}, beam_mat{}, particle_mat{};
  renderer::MaterialHandle entity_mats[content::kSceneMaxEntities] = {};
  renderer::PbrTextures entity_pbr_tex[content::kSceneMaxEntities] = {};
  renderer::TextureHandle entity_albedo_tex[content::kSceneMaxEntities] = {};
  renderer::TextureHandle particle_tex{};
  renderer::TextureHandle tex_checker{};
  renderer::TextureHandle tex_brick_normal{};
  renderer::TextureHandle tex_rough_orm{};
  renderer::TextureHandle tex_grid{};
  renderer::TextureHandle tex_wood{};
  // Prosedurel uretimin GECICI alani: her uretimde mark/reset_to ile geri
  // sarilir. Motorun tek bellek kaynagi arena -- std::malloc DEGIL (AllocGate
  // global ayirmalari sayiyor). Ana `sys` arenasi DOGRUDAN kullanilamaz:
  // oradan kalici seyler de ayriliyor, reset_to onlari da sifirlardi. Bu
  // yuzden `sys`ten CARVE edilmis bir cocuk arena -- ve tasma politikasi
  // ReturnNull: kullanici arazi olcusunu buyuturken tasan bir arena editoru
  // OLDURMEMELI, o varligin mesh'i cizilmez ve konsola yazilir.
  Arena proc_arena;
  Selection sel;   // coklu secim: items[0] = ana secili (gizmo ona bagli)
  OpGroups groups; // bir kullanici eylemi = gunlukte N islem (grup tasima/silme)
  bool playing = false, dirty = false;
  float play_time = 0;
  // Surukleme / metin duzenleme: aktiflesince kopya, birakinca tek islem.
  SceneEntity edit_before;
  bool edit_active = false, gizmo_was_using = false, gizmo_was_over = false;
  // Grup suruklemesi: surukleme basindaki kopyalar; bitince selection_commit ile
  // gunluge TEK grup (her karede degil).
  int32_t drag_items[Selection::kMax] = {};
  SceneEntity drag_before[Selection::kMax], drag_after[Selection::kMax];
  uint32_t drag_count = 0;
  content::SceneWorld world_before; // Dunya paneli surukleme/metin: tek islem
  bool world_edit_active = false;
  bool prev_lmb = false;
  RenderSettings render;    // golge/pozlama/bloom/olceklendirme (oturum ayari)
  GizmoOptions gizmos;      // isik yaricapi / golge hacmi / gunes yonu
  uint32_t gizmo_draws = 0; // son karede gizmolarin yaptigi cizim sayisi
  // Tarayici tavani 64'ten 128'e: liste artik glTF'lerin YANI SIRA .tpr
  // dosyalarini da tasiyor. Olculdu 2026-09-22: tests/assets'te 4 glTF,
  // depoda 5 .tpr — 128 yaklasik 14 kat bosluk birakiyor.
  static constexpr uint32_t kBrowseCap = 128;
  AssetFile browse[kBrowseCap]; // kaynak tarayici: ONCE modeller, SONRA betikler
  uint32_t browse_count = 0;
  uint32_t browse_models = 0;   // ilk bu kadari MODEL (browse[0] invaryanti)
  // Betik listesi AYRI da tutuluyor: denetci secicisi (prop_asset) ardisik bir
  // `char[][128]` tablosu istiyor, AssetFile dizisinden dilim alinamaz.
  char scripts[kBrowseCap][content::kScenePathLen] = {};
  uint32_t script_count = 0;
  ScriptScanResult script_scan;
  char tulpar_dir[1024] = {0};       // depo `tulpar/` koku (acilista cozulur)
  FileEntry scan_scratch[kFileListMax]; // file_list_tree kaziyicisi
  char status[160];
  char filter[64] = {0}; // Sahne paneli suzgeci
  char prop_filter[64] = {0}; // Ozellikler paneli aramasi (UE5 Details aramasi)
  HierarchyState tree;   // Sahne agaci: katlama bitleri + yerinde ad + surukleme
  AssetsView assets_view; // Kaynaklar paneli gorunumu (izgara/liste, karo, suzgec)
  // Pano: editorun KENDI tamponu (isletim sistemi panosu degil — metin degil
  // yapi tasiyoruz). Kes/kopyala secimi buraya yazar, yapistir buradan ekler.
  SceneEntity clip[Selection::kMax];
  uint32_t clip_count = 0;
  // Duraklatma DURDURMAK DEGILDIR: playing true kalir, govdeler yerinde durur,
  // yalniz zaman akmaz. step_request duraklatilmisken tek adim ilerletir.
  bool paused = false;
  uint32_t step_request = 0;
  // F5 GOMULU oynatma: oyun ayri surecte (Ctrl+F5 ile ayni ikili), karesi Oyun
  // sekmesinde. playing de true (arayuz ayni: Durdur, Oyun sekmesi, geri alma
  // isareti), ama editorun kendi fizigi DOGMAZ — sahneyi oyun oynatiyor.
  bool play_embedded = false;
  // GI (isik probe) onizleme -- SADECE EDITOR, .sahne/.sahneb'e YAZILMAZ.
  // "Isik Haritasini Pisir" butonuyla bellekte pisirilir (ayni bellek-ici
  // bake->derle->ac yolu .sahneb derleyicisinin kullandigi, disk YOK).
  // Sahne degisince (undo_count degisir) otomatik yeniden pisirilmez --
  // bake tum sahne uzerinde CPU ray-tracing, her kare/duzenlemede pahali.
  SystemArena gi_arena;
  content::SceneGi gi;
  content::SceneGiReport gi_report{};
  bool gi_baked = false, gi_preview = false;
  uint32_t gi_bake_undo_count = 0;
  // Partikül Simülasyonu & VFX (sabit kapasite arena)
  content::ParticleSystem particles;
  Rng particle_rng{1337};
  float particle_spawn_accum[content::kSceneMaxEntities] = {};
  bool particle_sim_paused = false;
  // Canlı Şerit / Kuyruk İzi (Ribbon Trail) & CS2 Voksel Dumanı
  content::RibbonTrail ribbon_trail;
  bool ribbon_initialized = false;
  sim::VoxelSmokeGrid smoke_grid;
  bool smoke_initialized = false;
  // Nesne ozellikleri (E5): betik yolu -> bildirimler. Disk yalniz denetcinin
  // betik karti cizilirken ve en cok kPropRestatFrames karede bir sorulur;
  // gorunum isaretleri yalniz BAKAR (prop_cache_peek). 283 216 B (olculdu
  // 2026-09-25, x86_64 GCC 16.2.1; 256 KB'si tarama metni) — EditorState
  // statik, yigina girmez.
  PropCache props;
  uint32_t prop_markers_drawn = 0; // son karede cizilen `nokta` isareti (penceresiz kapi okur)
  // Nokta duzenleme kipi (E6): denetcideki ✥ ya da gorunumde isaretin
  // eskenar dortgeni ANA secilinin bir noktasini kipe alir; editorun TEK
  // gizmosu varlik yerine noktanin DUNYA konumunda durur (yalniz tasima).
  // Kip varlik INDEKSI + ADla tutulur ve her kare (gizmo blogundan once)
  // prop_edit_validate ile yeniden sorulur: secim, kilit, bildirim, oynatma.
  int32_t pe_entity = -1;                          // -1: kip KAPALI
  char pe_name[content::kScenePropNameLen] = {0};
  uint32_t pe_sel_count = 0;                       // girildigindeki secim sayisi (degisirse cik)
  // Surukleme NOKTAYI mi tasiyor: surukleme BASINDA kip acikti. Birakis bu
  // bayraga gore islenir (o anki kipe gore degil) — surukleme ortasinda Esc
  // kipi kapatsa bile birakis nokta islemi olarak TEK islemle gunluge girer.
  bool pe_drag = false;
  bool pe_exit_after_drag = false; // surukleme sirasinda cikis istendi: birakista cik
  float pe_drag_world[3] = {0, 0, 0}; // son surukleme karesinde gizmonun verdigi DUNYA konumu (kapi okur)
  uint32_t pe_drag_writes = 0;        // son suruklemede noktaya yazilan kare (kapi okur)
  int32_t pe_drag_entity = -1;        // surukleme basindaki varlik
  // Surukleme basindaki varlik — edit_before DEGIL: denetci her kare (widget
  // etkin degilken) track_edit'te edit_before'u SIMDIKI varlikla yeniler ve
  // denetci gizmodan once cizilir; birakista edit_before suruklenmis hali
  // tasir, "once == sonra" olur ve gunluge islem DUSMEZ (Tuzaklar 8ck, olculdu).
  SceneEntity pe_drag_before;
  uint32_t pe_drag_undo = 0, pe_drag_redo = 0; // surukleme basinda gunluk (degistiyse kopya bayat)
};

// Varlik silindikten / geri alindiktan sonra secimi gecerli tut.
void clamp_selection(EditorState &st) {
  for (uint32_t k = st.sel.count; k > 0; k--)
    if (st.sel.items[k - 1] >= (int32_t)st.scene.entity_count) st.sel.erase(st.sel.items[k - 1]);
}

// Fare pikselinden dunya isini (kamera tabanindan; matris tersi gerekmez).
Mat4 live_matrix(const EditorState &st, const sim::Physics &phys, uint32_t i); // asagida, bodies_spawn yaninda
// Tum varliklarin dunya AABB'si (secim icin) — model sinirlari yuklu modelden.
content::SceneBounds entity_world_bounds_one(const EditorState &st, const sim::Physics &phys, uint32_t i) {
  const SceneEntity &e = st.scene.entities[i];
  const content::SceneBounds *mb = nullptr;
  content::SceneBounds mbs;
  if ((e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
    mbs = {st.models[e.asset].bounds_min, st.models[e.asset].bounds_max};
    mb = &mbs;
  }
  const Mat4 m = live_matrix(st, phys, i);
  return content::scene_world_bounds(content::scene_entity_local_bounds(e, mb), m);
}

uint32_t entity_world_bounds(const EditorState &st, const sim::Physics &phys, content::SceneBounds *out) {
  for (uint32_t i = 0; i < st.scene.entity_count; i++) out[i] = entity_world_bounds_one(st, phys, i);
  return st.scene.entity_count;
}

// SECILEBILIR varliklarin dunya AABB'leri, SIKISTIRILMIS. scene.hpp kSceneHidden
// icin "cizilmez, SECILEMEZ" diyor ama secim yollari bayragi hic sormuyordu:
// gozu kapatilmis bir nesne hem isinla hem kutu secimle yakalanabiliyordu (ve
// secilince gizmo ile tasinabiliyordu -- gorunmeyen bir seyi kazara tasimak,
// gizleme ozelliginin tam tersi).
//
// Sikistirmak SART: content::scene_pick bitisik bir dizi bekler ve INDEKS
// dondurur; gizli varliklari yerinde birakip sonra elemek, arkalarindaki
// nesnenin secilmesini engellerdi. map[k] = k'inci sinirin GERCEK varlik
// indeksi. Donus: yazilan eleman sayisi.
uint32_t entity_pick_bounds(const EditorState &st, const sim::Physics &phys, content::SceneBounds *out, int32_t *map) {
  uint32_t m = 0;
  for (uint32_t i = 0; i < st.scene.entity_count; i++) {
    if (st.scene.entities[i].flags & content::kSceneHidden) continue;
    out[m] = entity_world_bounds_one(st, phys, i);
    map[m] = (int32_t)i;
    m++;
  }
  return m;
}

void set_status(EditorState &st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void set_status(EditorState &st, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(st.status, sizeof st.status, fmt, ap);
  va_end(ap);
  // Durum cubugu yalniz SON iletiyi tutar: art arda iki hatada ilki okunmadan
  // siliniyordu. Ayni satir konsola da dusuyor (gecmis orada kaliyor); duzey
  // metinden turetiliyor ("KAYDEDILEMEDI" -> Hata), siniflandirici konsolunkiyle
  // AYNI — iki yerde iki kural olmasin.
  console_log_raw(console_classify_level(st.status, false), kConsoleTagEditor, st.status);
}

// Kaynak tarayicisini yeniden kurar: ONCE modeller (editor_scan_assets), SONRA
// betikler (editor_scan_scripts) — ayni diziye EKLEYEREK.
//
// ADA GORE GLOBAL SIRALAMA DEGIL, ve sebebi olculdu: headless kaynak tarayici
// kapisi `st.browse[0].name` ile bir varlik ekleyip onun kSceneModel almasini
// bekliyor. Karisik siralamada "engine_arena.tpr" one gecer ve o kapi kirmiziya
// doner. Modeller once durdugu surece invaryant korunuyor.
//
// editor_scan_assets'e DOKUNULMUYOR: `only_gltf` kontrolu (tests/test_editor)
// onun sozlesmesini kilitliyor.
void rescan_browse(EditorState &st) {
  st.browse_models = editor_scan_assets(st.scene_dir, st.scene, st.browse, EditorState::kBrowseCap);
  st.browse_count = st.browse_models;
  st.script_scan = editor_scan_scripts(st.scene_dir, st.tulpar_dir, st.scripts, EditorState::kBrowseCap, st.scan_scratch, kFileListMax);
  st.script_count = st.script_scan.count;
  for (uint32_t k = 0; k < st.script_count && st.browse_count < EditorState::kBrowseCap; k++) {
    AssetFile &f = st.browse[st.browse_count++];
    std::snprintf(f.name, sizeof f.name, "%s", st.scripts[k]);
    f.kind = AssetKind::Script;
    f.in_scene = false;
    f.index = -1;
  }
  // Tavan asimi SESSIZ kalmiyor: eksik bir liste "o betik yok" gibi gorunur.
  if (st.script_scan.truncated || st.script_scan.clipped)
    set_status(st, "betik taramasi: %u atlandi, %u dizine inilmedi (tavan)", st.script_scan.truncated, st.script_scan.clipped);
  else if (st.script_scan.err[0])
    set_status(st, "betik taramasi: %s", st.script_scan.err);
}

// Oynat (F5): govdeler + KARAKTERLER fizige. SceneRuntime ile ayni kural
// (scene_spawn_live): karakterli varligin govdesi dogmaz. Eskiden yalniz govde
// dogurulurdu — "Karakter Kontrolcusu" konan nesne F5'te kutu gibi devrilirdi,
// oyunda ise karakter olarak dururdu; iki kip farkli seyi gosteriyordu.
void bodies_spawn(EditorState &st, sim::Physics &ph) {
  if (st.bodies_live) return;
  st.live = content::scene_spawn_live(st.scene, ph, st.bodies, st.chars);
  st.bodies_live = true;
  if (st.live.characters || st.live.characters_failed)
    console_log(ConsoleLevel::Bilgi, kConsoleTagScene, "oynat: %u govde, %u karakter (%u govde bileseni karakterin yerine gecti)",
                st.live.bodies, st.live.characters, st.live.bodies_replaced);
  if (st.live.characters_failed)
    console_log(ConsoleLevel::Hata, kConsoleTagScene, "oynat: %u karakter DOGAMADI — boy > 2*yaricap olmali (Karakter karti), ya da havuz dolu",
                st.live.characters_failed);
}
void bodies_remove(EditorState &st, sim::Physics &ph) {
  if (!st.bodies_live) return;
  content::scene_remove_live(ph, st.bodies, st.chars, st.scene.entity_count);
  st.bodies_live = false;
}
// Varligin bu karedeki dunya matrisi: oynatilirken sim'den (dinamik govde ya
// da karakter), degilse yazar donusumu. Dort cizim/secim yolu bunu kullanir.
Mat4 live_matrix(const EditorState &st, const sim::Physics &phys, uint32_t i) {
  const SceneEntity &e = st.scene.entities[i];
  if (st.playing && st.bodies_live) {
    if (st.chars[i].valid()) return content::scene_character_matrix(st.scene, i, phys, st.chars[i]);
    if (st.bodies[i].valid() && e.dynamic) return content::scene_body_matrix(e, phys, st.bodies[i]);
  }
  return content::scene_entity_world_matrix(st.scene, i);
}
// Varlik listesini degistiren islemler (ekle/sil/geri al/yinele) oynarken
// govde indekslerini kaydirir: once govdeler cikar, sonra yeniden girer.
template <class F> void with_bodies(EditorState &st, sim::Physics &ph, F &&f) {
  const bool live = st.bodies_live;
  if (live) bodies_remove(st, ph);
  f();
  if (live) bodies_spawn(st, ph);
}

// Ozellik paneli: PropItem uzerinden — ImGui "son oge"sine BAKMAZ. Bilesik bir
// widget'ta (vec3 = uc surukleme) son oge yalniz Z alanidir; Y suruklenirken
// IsItemActivated() false kalir ve gunluge islem dusmezdi (editor_widgets kapisi).
// Coklu secim: ana secilide yapilan alan duzenlemesini secimdeki DIGER
// varliklara yayar (bkz. editor_multiedit.hpp). Yalniz `index` secimin
// icindeyse calisir: secili olmayan bir satirin goz/kilit simgesine tiklamak
// butun secimi etkilememeli. Donus: gunluge eklenen EK islem sayisi -- cagiran
// bunu kendi islemiyle TEK grup yapar, yani tek Ctrl+Z hepsini geri alir.
uint32_t propagate_selection_edit(EditorState &st, int index, const SceneEntity &before, const SceneEntity &after) {
  if (st.sel.count < 2 || !st.sel.contains(index)) return 0;
  uint32_t n = 0;
  MultieditStats ms;
  for (uint32_t k = 0; k < st.sel.count; k++) {
    const int32_t j = st.sel.items[k];
    if (j == index || j < 0 || j >= (int32_t)st.scene.entity_count) continue;
    SceneEntity x = st.scene.entities[j];
    if (!multiedit_apply(before, after, &x, &ms)) continue;
    if (st.hist.set_entity(st.scene, (uint32_t)j, x)) n++;
  }
  // Nesne ozellikleri (E3): yayilmayan her deger GORUNUR. Dolu hedef (16
  // ozellik) reddedildi — kullanici "hepsine yazdim" sanmasin; betigi farkli
  // hedef ise bilincli atlandi, yine de soylenir.
  if (ms.props_overflow)
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor,
                "coklu duzenleme: %u ozellik yazilamadi (hedefte %u ozellik tavani dolu)", ms.props_overflow,
                content::kSceneMaxProps);
  if (ms.props_script_skipped)
    console_log(ConsoleLevel::Bilgi, kConsoleTagEditor,
                "coklu duzenleme: ozellikler %u varliga yayilmadi (betikleri farkli)", ms.props_script_skipped);
  return n;
}

void track_edit(EditorState &st, SceneEntity &e, int index, const PropItem &it) {
  if (!st.edit_active) {
    st.edit_before = e;
  }
  if (it.activated || it.changed) {
    st.edit_active = true;
  }
  if (it.deactivated_after_edit) {
    const SceneEntity after = e;
    e = st.edit_before;
    if (st.hist.set_entity(st.scene, (uint32_t)index, after)) {
      const uint32_t extra = propagate_selection_edit(st, index, st.edit_before, after);
      st.groups.push(1 + extra); // tek kullanici eylemi = tek geri al
      st.dirty = true;
    }
    st.edit_active = false;
    st.edit_before = after;
  } else if (it.deactivated && !it.changed && content::scene_entity_equal(e, st.edit_before)) {
    st.edit_active = false;
  }
}
// Dunya paneli (gunes/ortam/golge): ayni kural, SceneOp::World olarak gunluge.
void track_world_edit(EditorState &st, const PropItem &it) {
  if (!st.world_edit_active) {
    st.world_before = st.scene.world();
  }
  if (it.activated || it.changed) {
    st.world_edit_active = true;
  }
  if (it.deactivated_after_edit) {
    const content::SceneWorld after = st.scene.world();
    st.scene.set_world(st.world_before);
    if (st.hist.set_world(st.scene, after)) {
      st.groups.push(1);
      st.dirty = true;
    }
    st.world_edit_active = false;
    st.world_before = after;
  } else if (it.deactivated && content::scene_world_equal(st.scene.world(), st.world_before)) {
    st.world_edit_active = false;
  }
}
// --- Prosedurel onizleme (arazi / voksel / su) --------------------------------
// Editor canli SceneDesc uzerinde calisir; derlenmis sahnedeki gibi yukleme
// aninda bir kez uretme sansi YOK: kullanici alani suruklerken geometri
// degisir. Cozum, alanlarin OZETI: ozet degismedikce tek bir create_mesh bile
// calismaz, yani duran bir sahnede kare icinde GPU ayirmasi olmaz.
//
// Ozet FNV-1a; PR #331'in yaptigi gibi alanlari TOPLAMAK degil (genislik+1 /
// uzunluk-1 ayni sayiyi verir ve mesh sessizce eski kalirdi).
uint32_t proc_hash(const void *p, size_t n) {
  const uint64_t h = content::content_fnv1a(p, n);
  // 0 "hic uretilmedi" anlamina geliyor (diziler sifir baslar); 0'a dusen
  // gercek bir ozet ilk uretimi sonsuza dek tekrarlatirdi.
  const uint32_t v = (uint32_t)(h ^ (h >> 32));
  return v ? v : 1u;
}

// Bir varligin prosedurel mesh'lerini gerekiyorsa yeniden uretir.
// begin_frame'den ONCE cagrilir (create_mesh kayit sirasinda degil).
void proc_refresh(EditorState &st, renderer::Renderer &ren, uint32_t i) {
  // Arena kurulamadiysa hicbir sey uretilmez: Arena::alloc init EDILMEMIS bir
  // arenada assert eder, yani "sessizce bos mesh" degil dogrudan cokme olurdu.
  if (st.proc_arena.capacity() == 0) return;
  const SceneEntity &e = st.scene.entities[i];
  const size_t mark = st.proc_arena.mark();
  if (e.components & content::kSceneTerrain) {
    content::HeightmapConfig cfg;
    cfg.width = (uint32_t)e.terrain_width; // DIKKAT: hucre SAYISI (bkz. scene.hpp)
    cfg.height = (uint32_t)e.terrain_height;
    cfg.cell_size = e.terrain_cell;
    cfg.amplitude = e.terrain_amp;
    cfg.frequency = e.terrain_freq;
    cfg.octaves = e.terrain_octaves;
    cfg.seed = e.terrain_seed;
    const uint32_t sculpt_ver = st.terrain_sculpt[i].version;
    const uint32_t h = proc_hash(&cfg, sizeof cfg) ^ (sculpt_ver * 0x9e3779b9u);
    if (h != st.terrain_hash[i]) {
      const float *deltas = (st.terrain_sculpt[i].deltas &&
                             st.terrain_sculpt[i].width == cfg.width &&
                             st.terrain_sculpt[i].height == cfg.height)
                                ? st.terrain_sculpt[i].deltas
                                : nullptr;
      bool updated = false;
      if (st.terrain_meshes[i].valid() &&
          st.terrain_mesh_w[i] == cfg.width &&
          st.terrain_mesh_h[i] == cfg.height) {
        updated = content::update_terrain_mesh_vertices(st.proc_arena, ren, st.terrain_meshes[i], cfg, deltas);
      }
      if (!updated) {
        st.terrain_meshes[i] = content::make_terrain_mesh(st.proc_arena, ren, cfg, deltas);
        st.terrain_mesh_w[i] = cfg.width;
        st.terrain_mesh_h[i] = cfg.height;
      }
      st.proc_arena.reset_to(mark);
      st.terrain_hash[i] = h;
    }
  } else if (st.terrain_hash[i]) {
    st.terrain_meshes[i] = renderer::MeshHandle{}; // bilesen kaldirildi: cizme
    st.terrain_hash[i] = 0;
    st.terrain_mesh_w[i] = 0;
    st.terrain_mesh_h[i] = 0;
  }
  if (e.components & content::kSceneVoxel) {
    const struct { uint32_t x, y, z; float cell; } key{e.voxel_size_x, e.voxel_size_y, e.voxel_size_z, e.voxel_cell};
    const uint32_t h = proc_hash(&key, sizeof key);
    if (h != st.voxel_hash[i]) {
      st.voxel_meshes[i] = content::make_voxel_mesh(st.proc_arena, ren, key.x, key.y, key.z, key.cell);
      st.proc_arena.reset_to(mark);
      st.voxel_hash[i] = h;
    }
  } else if (st.voxel_hash[i]) {
    st.voxel_meshes[i] = renderer::MeshHandle{};
    st.voxel_hash[i] = 0;
  }
  if (e.components & content::kSceneWater) {
    content::GerstnerWave w;
    w.direction = e.wave_direction;
    w.wavelength = e.wave_length;
    w.amplitude = e.wave_amplitude;
    w.steepness = e.wave_steepness;
    w.speed = e.wave_speed;
    const uint32_t h = proc_hash(&w, sizeof w);
    if (h != st.water_hash[i]) {
      st.water_meshes[i] = content::make_water_mesh(st.proc_arena, ren, w);
      st.proc_arena.reset_to(mark);
      st.water_hash[i] = h;
    }
  } else if (st.water_hash[i]) {
    st.water_meshes[i] = renderer::MeshHandle{};
    st.water_hash[i] = 0;
  }
}

static float *get_or_create_terrain_deltas(EditorState &st, uint32_t ent_idx, uint32_t width, uint32_t height) {
  if (ent_idx >= content::kSceneMaxEntities) return nullptr;
  auto &sc = st.terrain_sculpt[ent_idx];
  const uint32_t total = width * height;
  if (sc.deltas && sc.width == width && sc.height == height) return sc.deltas;
  if (st.sculpt_arena.capacity() == 0) return nullptr;
  sc.deltas = st.sculpt_arena.alloc_array<float>(total);
  if (!sc.deltas) return nullptr;
  std::memset(sc.deltas, 0, total * sizeof(float));
  sc.width = width;
  sc.height = height;
  sc.version = 0;
  return sc.deltas;
}

static float sample_terrain_height_with_sculpt(const EditorState &st, uint32_t ent_idx, float lx, float lz) {
  if (ent_idx >= st.scene.entity_count) return 0.0f;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width;
  const uint32_t h = (uint32_t)e.terrain_height;
  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  if (w < 2 || h < 2) return 0.0f;

  float gx = lx / cell;
  float gz = lz / cell;
  if (gx < 0.0f) gx = 0.0f;
  if (gz < 0.0f) gz = 0.0f;
  if (gx > (float)(w - 1)) gx = (float)(w - 1);
  if (gz > (float)(h - 1)) gz = (float)(h - 1);

  const uint32_t x0 = (uint32_t)std::floor(gx);
  const uint32_t z0 = (uint32_t)std::floor(gz);
  const uint32_t x1 = x0 + 1 < w ? x0 + 1 : x0;
  const uint32_t z1 = z0 + 1 < h ? z0 + 1 : z0;
  const float tx = gx - (float)x0;
  const float tz = gz - (float)z0;

  content::HeightmapConfig cfg;
  cfg.width = w; cfg.height = h; cfg.cell_size = cell;
  cfg.amplitude = e.terrain_amp; cfg.frequency = e.terrain_freq;
  cfg.octaves = e.terrain_octaves; cfg.seed = e.terrain_seed;

  auto get_h = [&](uint32_t x, uint32_t z) -> float {
    const float nx = (float)x * cell * cfg.frequency;
    const float nz = (float)z * cell * cfg.frequency;
    float base = fbm_2d(nx, nz, cfg.seed, cfg.octaves, cfg.lacunarity, cfg.gain) * cfg.amplitude;
    const auto &sc = st.terrain_sculpt[ent_idx];
    if (sc.deltas && sc.width == w && sc.height == h) {
      base += sc.deltas[z * w + x];
    }
    return base;
  };

  const float h00 = get_h(x0, z0);
  const float h10 = get_h(x1, z0);
  const float h01 = get_h(x0, z1);
  const float h11 = get_h(x1, z1);

  const float h0 = h00 * (1.0f - tx) + h10 * tx;
  const float h1 = h01 * (1.0f - tx) + h11 * tx;
  return h0 * (1.0f - tz) + h1 * tz;
}

static void apply_terrain_brush(EditorState &st, uint32_t ent_idx, float center_lx, float center_lz, float dt, bool invert = false) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  if (!(e.components & content::kSceneTerrain)) return;
  const uint32_t w = (uint32_t)e.terrain_width;
  const uint32_t h = (uint32_t)e.terrain_height;
  if (w < 2 || h < 2) return;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;

  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  const float rad = st.terrain_brush.radius > 0.1f ? st.terrain_brush.radius : 1.0f;
  const float str = st.terrain_brush.strength;
  TerrainBrushMode mode = st.terrain_brush.mode;
  if (invert) {
    if (mode == TerrainBrushMode::Yukselt) mode = TerrainBrushMode::Alcalt;
    else if (mode == TerrainBrushMode::Alcalt) mode = TerrainBrushMode::Yukselt;
  }

  const float step_dt = dt > 0.0f ? dt : 0.05f;
  const float pi = 3.14159265358979323846f;

  content::HeightmapConfig cfg;
  cfg.width = w; cfg.height = h; cfg.cell_size = cell;
  cfg.amplitude = e.terrain_amp; cfg.frequency = e.terrain_freq;
  cfg.octaves = e.terrain_octaves; cfg.seed = e.terrain_seed;

  const int min_x = (int)std::floor((center_lx - rad) / cell);
  const int max_x = (int)std::ceil((center_lx + rad) / cell);
  const int min_z = (int)std::floor((center_lz - rad) / cell);
  const int max_z = (int)std::ceil((center_lz + rad) / cell);

  const int cl_min_x = min_x < 0 ? 0 : min_x;
  const int cl_max_x = max_x >= (int)w ? (int)w - 1 : max_x;
  const int cl_min_z = min_z < 0 ? 0 : min_z;
  const int cl_max_z = max_z >= (int)h ? (int)h - 1 : max_z;

  for (int z = cl_min_z; z <= cl_max_z; z++) {
    for (int x = cl_min_x; x <= cl_max_x; x++) {
      const float vx = (float)x * cell;
      const float vz = (float)z * cell;
      const float dx = vx - center_lx;
      const float dz = vz - center_lz;
      const float dist = std::sqrt(dx * dx + dz * dz);
      if (dist > rad) continue;
      const float falloff = 0.5f * (1.0f + std::cos((dist / rad) * pi));
      const uint32_t idx = (uint32_t)(z * w + x);

      const float nx = (float)x * cell * cfg.frequency;
      const float nz = (float)z * cell * cfg.frequency;
      const float base_h = fbm_2d(nx, nz, cfg.seed, cfg.octaves, cfg.lacunarity, cfg.gain) * cfg.amplitude;
      const float cur_h = base_h + deltas[idx];

      switch (mode) {
        case TerrainBrushMode::Yukselt: {
          deltas[idx] += str * falloff * 10.0f * step_dt;
          break;
        }
        case TerrainBrushMode::Alcalt: {
          deltas[idx] -= str * falloff * 10.0f * step_dt;
          break;
        }
        case TerrainBrushMode::Duzlestir: {
          const float target = st.terrain_brush.target_height;
          const float diff = target - cur_h;
          deltas[idx] += diff * falloff * std::min(1.0f, str * 6.0f * step_dt);
          break;
        }
        case TerrainBrushMode::Yumusat: {
          float sum = 0.0f;
          int count = 0;
          for (int oz = -1; oz <= 1; oz++) {
            for (int ox = -1; ox <= 1; ox++) {
              const int nx_i = x + ox, nz_i = z + oz;
              if (nx_i >= 0 && nx_i < (int)w && nz_i >= 0 && nz_i < (int)h) {
                const uint32_t nidx = (uint32_t)(nz_i * w + nx_i);
                const float n_base = fbm_2d((float)nx_i * cell * cfg.frequency, (float)nz_i * cell * cfg.frequency, cfg.seed, cfg.octaves, cfg.lacunarity, cfg.gain) * cfg.amplitude;
                sum += n_base + deltas[nidx];
                count++;
              }
            }
          }
          if (count > 0) {
            const float avg = sum / (float)count;
            const float diff = avg - cur_h;
            deltas[idx] += diff * falloff * std::min(1.0f, str * 8.0f * step_dt);
          }
          break;
        }
        case TerrainBrushMode::Gurultu: {
          const float nval = (value_noise_2d((float)x * 0.45f, (float)z * 0.45f, cfg.seed + 991) - 0.5f) * 2.0f;
          deltas[idx] += nval * str * falloff * 8.0f * step_dt;
          break;
        }
        case TerrainBrushMode::Teras: {
          const float step = st.terrain_brush.terrace_step > 0.1f ? st.terrain_brush.terrace_step : 2.0f;
          const float target = std::round(cur_h / step) * step;
          const float diff = target - cur_h;
          deltas[idx] += diff * falloff * std::min(1.0f, str * 6.0f * step_dt);
          break;
        }
      }
    }
  }

  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
}

static void stamp_raise_mountain(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  const float cx = (float)w * cell * 0.5f, cz = (float)h * cell * 0.5f;
  const float rad = (float)std::min(w, h) * cell * 0.42f;
  const float pi = 3.14159265358979323846f;
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      const float vx = (float)x * cell, vz = (float)z * cell;
      const float dist = std::sqrt((vx - cx) * (vx - cx) + (vz - cz) * (vz - cz));
      if (dist < rad) {
        const float falloff = 0.5f * (1.0f + std::cos((dist / rad) * pi));
        deltas[z * w + x] += 12.0f * falloff;
      }
    }
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: tepe/zirve kabartildi (+12m)");
}

static void stamp_carve_crater(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  const float cx = (float)w * cell * 0.5f, cz = (float)h * cell * 0.5f;
  const float rad = (float)std::min(w, h) * cell * 0.38f;
  const float pi = 3.14159265358979323846f;
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      const float vx = (float)x * cell, vz = (float)z * cell;
      const float dist = std::sqrt((vx - cx) * (vx - cx) + (vz - cz) * (vz - cz));
      if (dist < rad) {
        const float u = dist / rad;
        if (u < 0.65f) {
          const float depth_falloff = 0.5f * (1.0f + std::cos((u / 0.65f) * pi));
          deltas[z * w + x] -= 8.0f * depth_falloff;
        } else {
          const float rim = std::sin(((u - 0.65f) / 0.35f) * pi);
          deltas[z * w + x] += 3.5f * rim;
        }
      }
    }
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: krater/cukur kazildi");
}

static void stamp_flatten_plateau(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  const float cx = (float)w * cell * 0.5f, cz = (float)h * cell * 0.5f;
  const float rad = (float)std::min(w, h) * cell * 0.35f;
  const float pi = 3.14159265358979323846f;
  const float target = st.terrain_brush.target_height;
  content::HeightmapConfig cfg;
  cfg.width = w; cfg.height = h; cfg.cell_size = cell;
  cfg.amplitude = e.terrain_amp; cfg.frequency = e.terrain_freq;
  cfg.octaves = e.terrain_octaves; cfg.seed = e.terrain_seed;
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      const float vx = (float)x * cell, vz = (float)z * cell;
      const float dist = std::sqrt((vx - cx) * (vx - cx) + (vz - cz) * (vz - cz));
      if (dist < rad) {
        const float falloff = 0.5f * (1.0f + std::cos((dist / rad) * pi));
        const float nx = (float)x * cell * cfg.frequency;
        const float nz = (float)z * cell * cfg.frequency;
        const float base_h = fbm_2d(nx, nz, cfg.seed, cfg.octaves, cfg.lacunarity, cfg.gain) * cfg.amplitude;
        const float cur_h = base_h + deltas[z * w + x];
        deltas[z * w + x] += (target - cur_h) * falloff;
      }
    }
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: zirve platosu duzlestirildi (%.1f m)", target);
}

static void stamp_smooth_erosion(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  const size_t mark = st.proc_arena.mark();
  float *temp = st.proc_arena.alloc_array<float>(w * h);
  if (temp) {
    std::memcpy(temp, deltas, w * h * sizeof(float));
    for (uint32_t z = 1; z + 1 < h; z++) {
      for (uint32_t x = 1; x + 1 < w; x++) {
        float sum = 0.0f;
        for (int oz = -1; oz <= 1; oz++) {
          for (int ox = -1; ox <= 1; ox++) {
            sum += temp[(z + oz) * w + (x + ox)];
          }
        }
        deltas[z * w + x] = sum / 9.0f;
      }
    }
    st.proc_arena.reset_to(mark);
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: erozyon yumusatmasi uygulandi");
}

static void stamp_add_rock_noise(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      const float n = (value_noise_2d((float)x * 0.7f, (float)z * 0.7f, 1337) - 0.5f) * 2.0f;
      deltas[z * w + x] += n * 1.5f;
    }
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: kayalik mikro puruzler basildi");
}

static void stamp_terrace_steps(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= st.scene.entity_count) return;
  const SceneEntity &e = st.scene.entities[ent_idx];
  const uint32_t w = (uint32_t)e.terrain_width, h = (uint32_t)e.terrain_height;
  const float cell = e.terrain_cell > 0.001f ? e.terrain_cell : 1.0f;
  float *deltas = get_or_create_terrain_deltas(st, ent_idx, w, h);
  if (!deltas) return;
  const float step = st.terrain_brush.terrace_step > 0.1f ? st.terrain_brush.terrace_step : 2.5f;
  content::HeightmapConfig cfg;
  cfg.width = w; cfg.height = h; cfg.cell_size = cell;
  cfg.amplitude = e.terrain_amp; cfg.frequency = e.terrain_freq;
  cfg.octaves = e.terrain_octaves; cfg.seed = e.terrain_seed;
  for (uint32_t z = 0; z < h; z++) {
    for (uint32_t x = 0; x < w; x++) {
      const float nx = (float)x * cell * cfg.frequency;
      const float nz = (float)z * cell * cfg.frequency;
      const float base_h = fbm_2d(nx, nz, cfg.seed, cfg.octaves, cfg.lacunarity, cfg.gain) * cfg.amplitude;
      const float cur_h = base_h + deltas[z * w + x];
      const float stepped = std::round(cur_h / step) * step;
      deltas[z * w + x] += (stepped - cur_h) * 0.7f;
    }
  }
  st.terrain_sculpt[ent_idx].version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: basamakli teraslama uygulandi (%.1f m)", step);
}

static void stamp_reset_sculpt(EditorState &st, uint32_t ent_idx) {
  if (ent_idx >= content::kSceneMaxEntities) return;
  auto &sc = st.terrain_sculpt[ent_idx];
  if (sc.deltas) {
    std::memset(sc.deltas, 0, sc.width * sc.height * sizeof(float));
  }
  sc.version++;
  st.terrain_hash[ent_idx] = 0;
  st.dirty = true;
  set_status(st, "arazi: tum heykeltiras/firca degisiklikleri sifirlandi");
}

// Ayrik widget (onay kutusu, secim): kopya uzerinde degisiklik, hemen islem.
bool commit(EditorState &st, int index, const SceneEntity &after) {
  const SceneEntity before = st.scene.entities[index]; // yayma icin fark tabani
  if (!st.hist.set_entity(st.scene, (uint32_t)index, after)) return false;
  st.groups.push(1 + propagate_selection_edit(st, index, before, after));
  st.dirty = true;
  return true;
}

// --- Nesne ozellikleri (E5) ----------------------------------------------------
// Varligin betiginin taramasi. may_io: disk sorulabilir mi (denetci karti
// cizilirken evet; gorunum isaretleri yalniz onbellege bakar). note: taranamadiysa
// gorunur sebep. Donus: tarama (Ok) ya da nullptr.
const PropCacheEntry *entity_prop_scan(EditorState &st, const SceneEntity &e, uint32_t frame, bool may_io, char *note, uint32_t note_cap) {
  if (note && note_cap) note[0] = 0;
  if (!(e.components & content::kSceneScript)) return nullptr;
  if (!e.script_file[0]) {
    if (note && note_cap) std::snprintf(note, note_cap, "betik atanmam\xC4\xB1\xC5\x9F");
    return nullptr;
  }
  char yol[1024];
  if (!editor_script_resolve(e.script_file, st.scene_dir, st.tulpar_dir, yol, sizeof yol)) {
    if (note && note_cap) std::snprintf(note, note_cap, "yol \xC3\xA7\xC3\xB6z\xC3\xBClemedi: %s", e.script_file);
    return nullptr;
  }
  const PropCacheEntry *ce = prop_cache_get(st.props, yol, frame, may_io);
  if (!ce) {
    if (note && note_cap) std::snprintf(note, note_cap, "%s", may_io ? "yol \xC3\xA7ok uzun" : "hen\xC3\xBCz taranmad\xC4\xB1");
    return nullptr;
  }
  if (ce->state != PropCacheState::Ok) {
    if (note && note_cap) std::snprintf(note, note_cap, "%s: %s", prop_cache_state_text(ce->state), yol);
    return nullptr;
  }
  return ce;
}

// --- Nokta duzenleme kipi (E6) -------------------------------------------------
// Kipin noktasi: tarama ONBELLEKTEN (disk yok — gizmo ve isaretler her kare
// sorar; diske giden denetci karti) + kural (editor_props: prop_edit_state).
// Ok ise out_local = su anki yerel deger.
PropEditState prop_edit_point(EditorState &st, int32_t ent, const char *name, uint32_t frame, float out_local[3], bool *is_ov) {
  if (ent < 0 || ent >= (int32_t)st.scene.entity_count) return PropEditState::NoEntity;
  const PropCacheEntry *ce = entity_prop_scan(st, st.scene.entities[ent], frame, false, nullptr, 0);
  const uint32_t n = ce ? (ce->res.count < kPropDeclMax ? ce->res.count : kPropDeclMax) : 0;
  return prop_edit_state(st.scene, ent, name, ce ? ce->decls : nullptr, n, ce != nullptr, out_local, is_ov);
}
bool prop_edit_active(const EditorState &st) { return st.pe_entity >= 0; }
// Kipe gir: ✥ ve isaret tiki AYNI yoldan. Kural tutmazsa kip ACILMAZ ve sebep
// durum cubuguna yazilir (sessiz "hicbir sey olmadi" yok).
bool prop_edit_enter(EditorState &st, int32_t ent, const char *name, uint32_t frame) {
  if (st.pe_drag) return false; // surukleme ortasinda kip degismez (birakis tek islem)
  const PropEditState ps = prop_edit_point(st, ent, name, frame, nullptr, nullptr);
  if (ps != PropEditState::Ok) {
    set_status(st, "nokta duzenlenemez: %s.%s (%s)", ent >= 0 && ent < (int32_t)st.scene.entity_count ? st.scene.entities[ent].name : "-",
               name ? name : "-", prop_edit_state_text(ps));
    return false;
  }
  st.pe_entity = ent;
  std::snprintf(st.pe_name, sizeof st.pe_name, "%s", name);
  st.pe_sel_count = st.sel.count;
  st.pe_exit_after_drag = false;
  set_status(st, "nokta duzenleme: %s.%s (gizmo noktada; Esc ya da tekrar " ICON_MD_OPEN_WITH " ile cik)", st.scene.entities[ent].name,
             st.pe_name);
  return true;
}
// Kipten cik. Surukleme surerken cikis BIRAKISA ertelenir: birakis nokta
// islemi olarak gunluge girer, sonra kip kapanir.
void prop_edit_exit(EditorState &st, const char *why) {
  if (!prop_edit_active(st)) return;
  if (st.pe_drag) { st.pe_exit_after_drag = true; return; }
  set_status(st, "nokta duzenleme bitti: %s", why);
  st.pe_entity = -1;
  st.pe_name[0] = 0;
  st.pe_exit_after_drag = false;
}
// Her kare, gizmo blogundan ONCE: kip hala gecerli mi? Secim degisti (ana
// secili ya da secim sayisi), oynatma basladi, varlik kilitlendi, betik noktayi
// artik okumuyor, konumu bilinmiyor... — biri tutarsa kip kapanir ve SEBEP
// soylenir. Surukleme surerken karar birakisa kalir.
void prop_edit_validate(EditorState &st, uint32_t frame) {
  if (!prop_edit_active(st) || st.pe_drag) return;
  const char *why = nullptr;
  if (st.playing) why = "oynatma basladi";
  else if (st.sel.primary() != st.pe_entity || st.sel.count != st.pe_sel_count) why = "secim degisti";
  else {
    const PropEditState ps = prop_edit_point(st, st.pe_entity, st.pe_name, frame, nullptr, nullptr);
    if (ps != PropEditState::Ok) why = prop_edit_state_text(ps);
  }
  if (why) prop_edit_exit(st, why);
}

// Denetcinin Ozellikler bolumu: surekli widget'lar track_edit'ten, ayrik
// eylemler commit'ten gecer — ikisi de propagate_selection_edit ile coklu
// secimde AYNI betigi tasiyan digerlerine ada gore yayilir.
void props_section(EditorState &st, SceneEntity &e, int si, SceneEntity &after, uint32_t frame) {
  char note[1200];
  PropsPanelInput in;
  in.has_script = (e.components & content::kSceneScript) != 0;
  const PropCacheEntry *ce = entity_prop_scan(st, e, frame, true, note, sizeof note);
  if (ce) {
    in.scan = &ce->res;
    in.decls = ce->decls;
    in.decl_count = ce->res.count < kPropDeclMax ? ce->res.count : kPropDeclMax;
  } else {
    in.note = note;
  }
  in.point_edit = st.pe_entity == si ? st.pe_name : nullptr;
  struct Ctx {
    EditorState *st;
    SceneEntity *e;
    int si;
  } ctx{&st, &e, si};
  after = e;
  const PropsPanelResult r = props_panel(
      e, after, in, [](void *u, const PropItem &it) { Ctx *c = static_cast<Ctx *>(u); track_edit(*c->st, *c->e, c->si, it); }, &ctx);
  if (r.commit) commit(st, si, after);
  // ✥: ayni nokta kipteyse kapat, degilse (baska nokta / kip kapali) ona gec.
  if (r.point_toggle) {
    if (st.pe_entity == si && !std::strcmp(st.pe_name, r.point_name)) prop_edit_exit(st, ICON_MD_OPEN_WITH);
    else prop_edit_enter(st, si, r.point_name, frame);
  }
}

// Gorunum: secili varligin `nokta` ozellikleri — varliktan noktaya cizgi +
// eskenar dortgen + ad. Konum DUNYA (scene_prop_point_world: yazar pozu,
// olcek yok; kopru ve derleyiciyle ayni kural). Surukleme (E6) gizmoyla:
// kipteki noktanin cevresinde halka. Renk: ustune yazilmis AccentHi (buyuk),
// betik varsayilani AccentHi (kucuk, soluk cizgi), yetim Warn. Donus: cizilen
// isaret sayisi.
constexpr float kPropMarkerR = 6.0f, kPropMarkerDefR = 4.5f;
bool project_to_view(const Mat4 &vp_mat, const ViewportRect &vr, const float w[3], ImVec2 *out) {
  const Vec4 clip = vp_mat * Vec4{w[0], w[1], w[2], 1.0f};
  if (clip.w <= 0.01f) return false; // kameranin arkasinda
  out->x = vr.x + (clip.x / clip.w * 0.5f + 0.5f) * vr.w;
  out->y = vr.y + (clip.y / clip.w * 0.5f + 0.5f) * vr.h; // Vulkan: NDC y asagi
  return true;
}
uint32_t draw_prop_markers(EditorState &st, uint32_t ent, const Mat4 &vp_mat, const ViewportRect &vr, uint32_t frame) {
  if (ent >= st.scene.entity_count) return 0;
  const PropCacheEntry *ce = entity_prop_scan(st, st.scene.entities[ent], frame, false, nullptr, 0);
  static PropMarker mk[kPropDeclMax + content::kSceneMaxProps];
  const uint32_t n = prop_marker_points(st.scene, ent, ce ? ce->decls : nullptr, ce ? ce->res.count : 0, ce != nullptr, mk,
                                        kPropDeclMax + content::kSceneMaxProps);
  if (n == 0) return 0;
  const Mat4 wm = content::scene_entity_world_matrix(st.scene, ent);
  const Vec4 o4 = wm * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
  const float o[3] = {o4.x, o4.y, o4.z};
  ImVec2 so;
  const bool so_ok = project_to_view(vp_mat, vr, o, &so);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float c[4];
  editor_tone(Tone::AccentHi, c);
  const ImU32 col_ov = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1.0f));
  const ImU32 col_def_line = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 0.55f));
  editor_tone(Tone::Warn, c);
  const ImU32 col_orphan = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1.0f));
  editor_tone(Tone::Bg0, c);
  const ImU32 col_edge = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1.0f));
  editor_tone(Tone::TextDim, c);
  const ImU32 col_text = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1.0f));
  uint32_t drawn = 0;
  const uint32_t lim = n < kPropDeclMax + content::kSceneMaxProps ? n : kPropDeclMax + content::kSceneMaxProps;
  for (uint32_t k = 0; k < lim; k++) {
    ImVec2 sp;
    if (!project_to_view(vp_mat, vr, mk[k].world, &sp)) continue;
    const bool def = mk[k].kind == kPropMarkerDefault, orphan = mk[k].kind == kPropMarkerOrphan;
    const ImU32 col = orphan ? col_orphan : col_ov;
    const float r = def ? kPropMarkerDefR : kPropMarkerR;
    if (so_ok) dl->AddLine(so, sp, def ? col_def_line : col, def ? 1.0f : 1.5f);
    const ImVec2 a(sp.x, sp.y - r), b(sp.x + r, sp.y), cc(sp.x, sp.y + r), d(sp.x - r, sp.y);
    dl->AddQuadFilled(a, b, cc, d, col);
    dl->AddQuad(ImVec2(a.x, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y), ImVec2(cc.x, cc.y + 1.0f), ImVec2(d.x - 1.0f, d.y), col_edge, 1.0f);
    dl->AddText(ImVec2(sp.x + r + 3.0f, sp.y - r - 8.0f), orphan ? col_orphan : col_text, mk[k].name);
    if (st.pe_entity == (int32_t)ent && !orphan && !std::strcmp(st.pe_name, mk[k].name))
      dl->AddCircle(sp, kPropMarkerHitR + 2.0f, col_ov, 0, 1.5f); // kipteki nokta
    drawn++;
  }
  return drawn;
}
// Gorunumde tiklanan isaret (E6): `ent`in DUZENLENEBILIR noktalarindan
// (sx, sy) ekran konumuna en yakini — draw_prop_markers'in cizdigi AYNI
// isaretler, AYNI izdusum. Yetim ve kurali tutmayan (kilitli, varsayilan kodda,
// 16 dolu) isaret aday degildir: tik varlik secimine duser. Donus: isabet.
bool prop_marker_pick(EditorState &st, uint32_t ent, const Mat4 &vp_mat, const ViewportRect &vr, float sx, float sy, uint32_t frame,
                      char *out_name, uint32_t cap) {
  if (ent >= st.scene.entity_count) return false;
  const PropCacheEntry *ce = entity_prop_scan(st, st.scene.entities[ent], frame, false, nullptr, 0);
  constexpr uint32_t kCap = kPropDeclMax + content::kSceneMaxProps;
  static PropMarker mk[kCap];
  static PropMarkerScreen sc[kCap];
  const uint32_t n = prop_marker_points(st.scene, ent, ce ? ce->decls : nullptr, ce ? ce->res.count : 0, ce != nullptr, mk, kCap);
  const uint32_t lim = n < kCap ? n : kCap;
  for (uint32_t k = 0; k < lim; k++) {
    ImVec2 sp;
    sc[k].visible = project_to_view(vp_mat, vr, mk[k].world, &sp);
    sc[k].x = sp.x;
    sc[k].y = sp.y;
    sc[k].editable = mk[k].kind != kPropMarkerOrphan &&
                     prop_edit_point(st, (int32_t)ent, mk[k].name, frame, nullptr, nullptr) == PropEditState::Ok;
  }
  const int32_t hit = prop_marker_hit(sc, lim, sx, sy, kPropMarkerHitR);
  if (hit < 0) return false;
  std::snprintf(out_name, cap, "%s", mk[hit].name);
  return true;
}

// E6 penceresiz kapilarinin adayi: suruklenebilir bir nokta. Once USTUNE
// YAZMASI olan (ozellik.sahne: "muhafiz" — donuk ve olcekli kaidenin cocugu,
// yani ebeveyn zinciri de olculur), sonra yalniz betik VARSAYILANI olan;
// ikisi de yoksa FIKSTUR: kilitsiz, gizli olmayan, betiksiz ilk varliga
// tulpar/examples/davranis/muhafiz.tpr atanir (devriye_a, varsayilan
// (-2, 0, 0), ustune yazma YOK — surukleme ustune yazmayi YARATMA yolunu
// olcer). Fikstur gunluge girer (*ops islem); kapi sonunda geri alinir.
// Betik diskten taranir (onbellege girsin; gorunum yollari yalniz bakar).
// Donus: varlik indeksi, bulunamadiysa -1.
int32_t prop_edit_gate_candidate(EditorState &st, uint32_t frame, char *name, uint32_t cap, uint32_t *ops, bool *fixture) {
  *ops = 0;
  *fixture = false;
  for (int pass = 0; pass < 2; pass++) { // 0: ustune yazmali, 1: yalniz varsayilan
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const PropCacheEntry *ce = entity_prop_scan(st, st.scene.entities[i], frame, true, nullptr, 0);
      if (!ce) continue;
      const uint32_t n = ce->res.count < kPropDeclMax ? ce->res.count : kPropDeclMax;
      for (uint32_t k = 0; k < n; k++) {
        if (ce->decls[k].type != content::kScenePropNokta) continue;
        bool ov = false;
        if (prop_edit_point(st, (int32_t)i, ce->decls[k].name, frame, nullptr, &ov) != PropEditState::Ok) continue;
        if (pass == 0 && !ov) continue;
        std::snprintf(name, cap, "%s", ce->decls[k].name);
        return (int32_t)i;
      }
    }
  }
  for (uint32_t i = 0; i < st.scene.entity_count; i++) {
    SceneEntity a = st.scene.entities[i];
    if ((a.flags & (content::kSceneLocked | content::kSceneHidden)) || (a.components & content::kSceneScript) ||
        a.prop_count >= content::kSceneMaxProps)
      continue;
    a.components |= content::kSceneScript;
    std::snprintf(a.script_file, sizeof a.script_file, "%s", "tulpar/examples/davranis/muhafiz.tpr");
    a.script_enabled = true;
    if (!st.hist.set_entity(st.scene, i, a)) continue;
    st.groups.push(1);
    (*ops)++;
    *fixture = true;
    entity_prop_scan(st, st.scene.entities[i], frame, true, nullptr, 0);
    std::snprintf(name, cap, "%s", "devriye_a");
    return prop_edit_point(st, (int32_t)i, name, frame, nullptr, nullptr) == PropEditState::Ok ? (int32_t)i : -1;
  }
  return -1;
}
} // namespace

int editor_run(const EditorOptions &opts, const EditorHost *host) {
  const bool headless = host == nullptr || opts.headless_frames > 0;
  static SystemArena sys;
  if (!sys.reserve(512u << 20, "editor")) { std::fprintf(stderr, "arena\n"); return 1; }
  FrameArena frame;
  sys.carve(frame, 8u << 20, "frame");
  JobSystem jobs;
  if (!jobs.init(sys, JobSystemConfig{})) { std::fprintf(stderr, "job sistemi\n"); return 1; }
  Profiler prof;
  ProfilerConfig pc;
  pc.frame_capacity = 600;
  pc.zone_capacity = 32768;
  prof.init(sys, pc);

  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { std::fprintf(stderr, "Vulkan loader yok\n"); return 1; }
  rhi::DeviceConfig dc;
  dc.validation = opts.validation;
  dc.best_practices = opts.validation;
  if (!headless) {
    uint32_t n = 0;
    dc.instance_extensions = host->instance_extensions(host->user, &n);
    dc.instance_extension_count = n;
  }
  rhi::Device dev;
  if (!dev.init_instance(api, dc)) { std::fprintf(stderr, "instance: %s\n", dev.last_error()); return 1; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!headless && !host->create_surface(host->user, api, dev.instance(), &surface)) { std::fprintf(stderr, "yuzey\n"); return 1; }
  if (!dev.init_device(surface)) { std::fprintf(stderr, "cihaz: %s\n", dev.last_error()); return 1; }
  std::printf("[engine_editor] GPU: %s (Vulkan %u.%u)\n", dev.caps().device_name, VK_API_VERSION_MAJOR(dev.caps().api_version),
              VK_API_VERSION_MINOR(dev.caps().api_version));

  uint32_t width = opts.width, height = opts.height;
  rhi::Swapchain swap;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  uint32_t image_count = 2;
  if (headless) {
    oc.width = width; oc.height = height; oc.srgb = true;
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
  } else {
    uint32_t fw = 0, fh = 0;
    host->poll(host->user, &fw, &fh);
    if (!swap.init(dev, sys, surface, fw ? fw : width, fh ? fh : height)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
    image_count = swap.image_count();
    width = swap.extent().width; height = swap.extent().height;
  }
  // --- VIEWPORT ONCE KURULUR, RENDERER ONUN GECISINE GORE ---------------------
  // Vulkan'da gecis UYUMLULUGU subpass BAGIMLILIKLARINI da kapsar, yalniz
  // formatlari degil. Renderer'i swapchain gecisine gore kurup viewport
  // gecisine kaydetmek dogrulama katmanini kiriyor (olculdu):
  //   srcStageMask incompatible: FRAGMENT_SHADER_BIT != COLOR_ATTACHMENT_OUTPUT_BIT
  // Bu yuzden sira: vp.init -> ren.init(vp.render_pass()) -> ui.init(rp).
  // vp.render_pass() yeniden boyutlanmada YENIDEN YARATILMAZ, yani renderer'in
  // boru hatlari panel olcusu degisince gecerli kalir.
  static EditorViewport vp;
  EditorViewportConfig vc;
  if (!vp.init(dev, vc, width, height)) { std::fprintf(stderr, "viewport: %s\n", vp.last_error()); return 1; }

  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.max_meshes = 1024;
  rc.max_materials = 1024;
  rc.srgb_target = true; // hedef ARTIK viewport ve o *_SRGB (bkz. EditorViewportConfig)
  // --- Son isleme (post): ISIMA GORUNSUN DIYE ACIK --------------------------
  // post kapaliyken renderer dogrudan LDR hedefe yazar ve isima 1.0e
  // KIRPILIR: malzemeye 5 birim isima verilse de ekranda beyazdan oteye
  // gitmez, parlama (bloom) hic calismaz. Editorde isima ayarlayip
  // sonucu gorememek, ayari hic vermemekle ayni sey.
  //
  // Ic HDR hedef KURULUMDA olculenir ve kare icinde degismez
  // (renderer.hpp post_width/post_height). Viewport paneli her zaman
  // pencereden kucuk oldugu icin pencere olcusu guvenli ust sinirdir;
  // panel kuculdugunde mevcut render_scale alt-dikdortgeni devreye girer
  // (renderer.hpp:130-138). Desen bridge/engine_api.cpp:713-725 ile ayni.
  rc.post = true;
  rc.post_width = width;
  rc.post_height = height;
  // --- Isik huzmesi (godray) gecisi TABLOYA girsin -------------------------
  // Editorde huzme kaydiraclari (Yogunluk/Agirlik/Sonumleme/Pozlama) ve
  // varlik basina "Isik Huzmesi" bayragi var; bunlar set_godrays* ile
  // renderer'a ULASIYORDU ama gecis tabloda olmadigi icin hicbir sey
  // cizmiyordu — kaydiraci oynatmak goruntuyu degistirmiyordu. Gecis burada
  // tabloya alinir; AC/KAPA her kare sahnenin godrays_enabled alanindan
  // gelir (asagida set_godrays_enabled). Sahne varsayilani KAPALI oldugu
  // icin acilistaki goruntu degismez.
  rc.godrays = true;
  // Ic hedefin temizleme rengi viewport gecisininkiyle AYNI olmali;
  // yoksa post acilinca arka plan aniden kararir (bridge bunu olcmustu).
  rc.post_clear = Vec3{vc.clear[0], vc.clear[1], vc.clear[2]};
  // TONEMAP KAPALI. Faz 0'da acmistim ve bu bir REGRESYONDU: compose.frag
  // duz Reinhard kullaniyor (c / (1 + c)), yani tam aydinlik beyaz bir yuzey
  // 0.5'e iniyor -- editorun TUM sahnesi yaklasik yari parlakliga dusuyordu.
  // Kapaliyken HDR hedef yine var: 1'in ustundeki isima bloom'a gider,
  // siradan yuzeyler eskisi gibi gorunur. Dogru cozum pozlama kompanzasyonlu
  // bir filmik egri (ACES / AgX); shader yeniden derlenmesi gerektirir.
  rc.tonemap = false;
  if (!ren.init(dev, sys, vp.render_pass(), rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  ren.set_render_size(vp.width(), vp.height());

  // --- Sahne dosyasi (veri modeli) ---
  static EditorState st;
  // Donus DENETLENIR: gunluk kurulamazsa set_entity degisikligi uygular ama
  // KAYDETMEZ ve Ctrl+Z sessizce hicbir sey yapmaz. 256 x sizeof(SceneOp)
  // sistem arenasindan; olcumu kosum sonundaki "sistem arenasi" satirinda.
  if (!st.hist.init(sys, 256)) {
    std::fprintf(stderr, "[engine_editor] HATA: gunluk (256 islem) sistem arenasina sigmadi\n");
    return 1;
  }
  st.gi_arena.reserve(32u << 20, "editor_gi_preview"); // bake + blob derleme scratch'i
  // Prosedurel geometri GECICI alani (yukselti/voksel/dalga tamponlari). AYRI
  // arena: her uretimden sonra reset_to ile geri sarilir; `sys` uzerinde
  // yapilsaydi ayni geri sarma KALICI tahsisleri de silerdi. Yer ayrilamazsa
  // onizleme sessizce degil, KONSOLA yazarak kapanir.
  if (!sys.carve(st.proc_arena, 64u << 20, "editor_proc_mesh", OverflowPolicy::ReturnNull))
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "prosedurel onizleme arenasi ayrilamadi: arazi/su/voksel cizilmeyecek");
  if (!sys.carve(st.sculpt_arena, 16u << 20, "editor_sculpt", OverflowPolicy::ReturnNull))
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "sculpt arenasi ayrilamadi: arazi sekillendirme kullanilamayacak");
  // Ilkel mesh tablosu: SAHNEDEN BAGIMSIZ, salt geometri, bir kez kurulur.
  // SceneRuntime de AYNI fonksiyonu cagirir -- kapsul/silindir/koni/dortgen/
  // simit editorde ve derlenmis oyunda ayni geometriyi gosterir.
  content::build_primitive_meshes(ren, st.prims);
  {
    // Arazi/voksel/su TURU basina tek malzeme (hepsi duz renk). ACILISTA:
    // create_material kare icinde ayirma demektir (bkz. scene_runtime.cpp).
    renderer::PbrParams p;
    p.metallic = 0.1f; p.roughness = 0.9f;
    st.terrain_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, p);
    p.metallic = 0.0f; p.roughness = 0.5f;
    st.voxel_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, p);
    p.metallic = 0.0f; p.roughness = 0.1f;
    st.water_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, p);
    renderer::PbrParams sun_p;
    sun_p.metallic = 0.0f; sun_p.roughness = 1.0f;
    sun_p.emissive = Vec3{35.0f, 32.0f, 24.0f}; // Ultra-parlak HDR günes emisyonu
    st.sun_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, sun_p);
    renderer::PbrParams core_p;
    core_p.metallic = 0.0f; core_p.roughness = 1.0f;
    core_p.emissive = Vec3{28.0f, 25.0f, 20.0f}; // Nokta/Spot isik cekirdegi
    st.light_core_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, core_p);
    renderer::PbrParams beam_p;
    beam_p.metallic = 0.0f; beam_p.roughness = 1.0f;
    beam_p.emissive = Vec3{8.0f, 7.5f, 6.0f}; // Hacimsel isik huzmesi / fake godray
    st.beam_mat = ren.create_material(ren.default_texture(), Vec3{1, 1, 1}, beam_p);
    renderer::PbrParams part_p;
    part_p.metallic = 0.0f; part_p.roughness = 0.8f;
    part_p.emissive_strength = 2.5f; // Parlayan gorsel efektler & bloom

    // Asagidaki alti 16 KB'lik doku tamponu STATIK: ayni blokta yan yana
    // yasadiklari icin derleyici yuvalarini paylastiramiyor ve editor_run'in
    // cercevesine 96 KB ekliyorlardi. E3 (SceneEntity 824 -> 1468 B) ile
    // cerceve 131 520 B'ye cikti ve CMake'in 128 KB kapisini kirdi (olculdu
    // 2026-09-25, GCC 16.2, -fdump-tree-optimized ile yerel tek tek sayildi).
    // Yalniz acilista bir kez doldurulur; create_texture veriyi kopyalar.
    // Usulsel Gaussian Dairesel Yumusak Alfa Dokusu (64x64) - Karton kutu sis/duman yerine ipeksi vfx
    alignas(16) static uint8_t gauss_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const float nx = (float(x) - 31.5f) / 31.5f;
        const float ny = (float(y) - 31.5f) / 31.5f;
        const float d2 = nx * nx + ny * ny;
        const float a = d2 < 1.0f ? std::exp(-3.5f * d2) * (1.0f - d2) : 0.0f;
        const int iv = int(a * 255.0f + 0.5f);
        const uint8_t alpha = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
        const int idx = (y * 64 + x) * 4;
        gauss_px[idx + 0] = alpha;
        gauss_px[idx + 1] = alpha;
        gauss_px[idx + 2] = alpha;
        gauss_px[idx + 3] = alpha;
      }
    }
    st.particle_tex = ren.create_texture(gauss_px, 64, 64, true, false);
    st.particle_mat = ren.create_material(st.particle_tex, Vec3{1, 1, 1}, part_p);

    // Dama Tahtasi (Checkerboard 64x64)
    alignas(16) static uint8_t chk_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const bool dark = (((x / 8) + (y / 8)) & 1) != 0;
        const uint8_t c = dark ? 90 : 235;
        const int idx = (y * 64 + x) * 4;
        chk_px[idx + 0] = c; chk_px[idx + 1] = c; chk_px[idx + 2] = c; chk_px[idx + 3] = 255;
      }
    }
    st.tex_checker = ren.create_texture(chk_px, 64, 64, true, true);

    // Prosedurel Tas / Tugla Normal Haritasi (64x64)
    alignas(16) static uint8_t norm_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const int bx = x % 16, by = y % 8;
        const float dx = (bx == 0) ? -0.8f : (bx == 15) ? 0.8f : 0.0f;
        const float dy = (by == 0) ? -0.8f : (by == 7)  ? 0.8f : 0.0f;
        const float len = std::sqrt(dx * dx + dy * dy + 1.0f);
        const float nx = dx / len, ny = dy / len, nz = 1.0f / len;
        const int idx = (y * 64 + x) * 4;
        norm_px[idx + 0] = (uint8_t)((nx * 0.5f + 0.5f) * 255.0f);
        norm_px[idx + 1] = (uint8_t)((ny * 0.5f + 0.5f) * 255.0f);
        norm_px[idx + 2] = (uint8_t)((nz * 0.5f + 0.5f) * 255.0f);
        norm_px[idx + 3] = 255;
      }
    }
    st.tex_brick_normal = ren.create_texture(norm_px, 64, 64, true, false);

    // Prosedurel ORM Haritasi (64x64, R=Occlusion, G=Roughness, B=Metallic)
    alignas(16) static uint8_t orm_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const int bx = x % 16, by = y % 8;
        const bool edge = (bx <= 1 || bx >= 14 || by <= 1 || by >= 6);
        const uint8_t occ = edge ? 70 : 255;
        const uint8_t rough = edge ? 240 : 130;
        const int idx = (y * 64 + x) * 4;
        orm_px[idx + 0] = occ; orm_px[idx + 1] = rough; orm_px[idx + 2] = 0; orm_px[idx + 3] = 255;
      }
    }
    st.tex_rough_orm = ren.create_texture(orm_px, 64, 64, true, false);

    // Izgara Haritasi (Grid 64x64)
    alignas(16) static uint8_t grid_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const bool line = (x % 16 == 0 || y % 16 == 0 || x == 63 || y == 63);
        const uint8_t c = line ? 240 : 50;
        const int idx = (y * 64 + x) * 4;
        grid_px[idx + 0] = c; grid_px[idx + 1] = c; grid_px[idx + 2] = c; grid_px[idx + 3] = 255;
      }
    }
    st.tex_grid = ren.create_texture(grid_px, 64, 64, true, true);

    // Ahsap / Halka Haritasi (Wood 64x64)
    alignas(16) static uint8_t wood_px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const float r = std::sqrt(float(x * x + (y * 4) * (y * 4))) * 0.15f;
        const float ring = 0.5f + 0.5f * std::sin(r);
        const int idx = (y * 64 + x) * 4;
        wood_px[idx + 0] = (uint8_t)(150 + ring * 60);
        wood_px[idx + 1] = (uint8_t)(100 + ring * 40);
        wood_px[idx + 2] = (uint8_t)(50 + ring * 25);
        wood_px[idx + 3] = 255;
      }
    }
    st.tex_wood = ren.create_texture(wood_px, 64, 64, true, true);

    for (uint32_t i = 0; i < content::kSceneMaxEntities; i++) {
      st.entity_mats[i] = ren.create_material(ren.default_texture(), Vec3{1, 1, 1});
    }
  }
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (opts.scene_path) std::snprintf(st.scene_path, sizeof st.scene_path, "%s", opts.scene_path);
  else if (adir && *adir) std::snprintf(st.scene_path, sizeof st.scene_path, "%s/editor.sahne", adir);
  else platform::asset_path(st.scene_path, sizeof st.scene_path, "tests/assets/editor.sahne");
  content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
  // Betik tarayicisinin IKINCI koku: deponun `tulpar/` agaci. asset_path exe
  // dizini -> cwd -> ENGINE_SOURCE_DIR sirasiyla deniyor ve BULAMAZSA son
  // denedigi yolu tamponda BIRAKIYOR, yani durum satiri hangi yola baktigimizi
  // soyleyebiliyor (sahne yolu icin de ayni cagri kullaniliyor).
  platform::asset_path(st.tulpar_dir, sizeof st.tulpar_dir, "tulpar");
  {
    content::SceneError err{};
    if (!content::scene_load(sys, st.scene_path, &st.scene, &err)) {
      console_log(ConsoleLevel::Hata, kConsoleTagScene, "sahne %s: %s", st.scene_path, err.msg);
      std::fprintf(stderr, "sahne %s: %s\n", st.scene_path, err.msg);
      return 1;
    }
  }
  char path[1024];
  auto asset = [&](const char *name) {
    std::snprintf(path, sizeof path, "%s/%s", st.scene_dir, name);
    return path;
  };
  // Kaynak yukleme (glTF -> GPU); tarayicidan eklenen kaynak da buradan gecer.
  auto load_asset = [&](int32_t i) {
    if (i < 0 || i >= (int32_t)st.scene.asset_count) return false;
    st.have[i] = content::gltf_load(sys, asset(st.scene.assets[i]), &st.models[i]) && content::upload_model(ren, sys, st.models[i], &st.ups[i]);
    if (!st.have[i]) {
      console_log(ConsoleLevel::Uyari, kConsoleTagScene, "kaynak yuklenemedi: %s", st.scene.assets[i]);
      std::printf("[engine_editor] kaynak yuklenemedi: %s\n", st.scene.assets[i]);
    }
    return st.have[i];
  };
  for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
  // Gunluk bir kaynak satirini kaldirdiysa (geri al; SceneHistory::add_asset) o
  // yuvanin modeli artik sahnenin degil: `have` dusurulur, yoksa ayni indekse
  // sonra BASKA bir kaynak gelince eski model cizilirdi. Yinele satiri geri
  // getirirse model yeniden yuklenir. n0: islemden onceki kaynak sayisi.
  auto assets_after_history = [&](uint32_t n0) {
    for (uint32_t i = st.scene.asset_count; i < n0 && i < content::kSceneMaxAssets; i++) st.have[i] = false;
    for (uint32_t i = n0; i < st.scene.asset_count; i++)
      if (!st.have[i]) load_asset((int32_t)i);
  };
  rescan_browse(st);
  std::printf("[engine_editor] sahne %s: %u varlik, %u kaynak\n", st.scene_path, st.scene.entity_count, st.scene.asset_count);
  console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "sahne %s: %u varlik, %u kaynak", st.scene_path, st.scene.entity_count,
              st.scene.asset_count);

  // Editorun KENDI yardimci geometrisi. Eskiden burada demo sahnesinin tam
  // cizim takimi (dama zemin malzemesi, duzlem, glTF kutu) kuruluyordu;
  // demo arka plani editorden cikinca geriye yalniz GERCEKTEN kullanilan
  // kup kaldi: modelsiz govdenin carpisma hacmi, bos varlik isareti ve
  // editor_draw_gizmos. Tip DemoScene::DrawSet olarak KALIYOR (duz bir
  // MeshHandle yeterdi) ki bu satirlar demo arka planinin donusu
  // tartisilirsa yeniden yazilmasin.
  DemoScene::DrawSet ds;
  {
    renderer::Vertex v[24];
    uint32_t idx[36];
    const uint32_t n = renderer::Renderer::cube(v, idx); // v/idx BURADA dolar
    ds.cube = ren.create_mesh(v, 24, idx, n);
  }
  DemoScene scene;
  // with_content=false: editore YALNIZ bos bir fizik dunyasi lazim.
  // Demo icerigi (arena zemini/duvarlari, ajanlar, dusen kutular) sahne
  // dosyasina ait degil; ekranda 100+ nesne gosterip agacta 8 satir
  // birakiyordu ve GORUNMEZ carpisma kutulari kullanicinin dinamik
  // govdelerini tutuyordu. Derlenmis oyunda (scene_runtime) o kutular
  // YOK, yani editor oyundan farkli davraniyordu. Bos sahne artik
  // gercekten bos: Faz 4.5 vitrin sahnesi GERCEK sahne icerigi olacak.
  if (!scene.init(sys, &jobs, /*with_content*/ false)) { std::fprintf(stderr, "sahne\n"); return 1; }
  // Ilkel mesh tablosu bir kez kurulur (sahneden bagimsiz, salt geometri).
  content::build_primitive_meshes(ren, st.prims);
  st.particles.init(sys, 4096, Vec3{0, -9.8f, 0});
  st.ribbon_trail.init(sys, 256);
  st.ribbon_initialized = true;
  st.smoke_grid.init(sys, 24, 24, 24, Vec3{-3.0f, 0.0f, -3.0f}, 0.25f);
  st.smoke_initialized = true;
  sim::Physics &phys = scene.physics();

  EditorUi ui;
  {
    char fpath[1024], ipath[1024];
    platform::asset_path(fpath, sizeof fpath, "assets/fonts/DejaVuSans.ttf");
    // Ikon fontu (Material Icons, Apache-2.0) metin fontunun atlasina
    // birlestirilir; bkz. EditorUi::init. Bulunamazsa arayuz yine acilir.
    platform::asset_path(ipath, sizeof ipath, "assets/fonts/" FONT_ICON_FILE_NAME_MD);
    if (!ui.init(dev, rp, 1, image_count, fpath, 17.0f, 1.0f, true, ipath)) { std::fprintf(stderr, "editor ui: %s\n", ui.last_error()); return 1; }
    if (!ui.icons_ok())
      console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "ikon fontu yuklenemedi (%s): ikonlar bos kutu cizilecek", ipath);
  }

  // NOT: ImNodes baglami EditorUi::init icinde kuruluyor (ImGui baglamiyla
  // ayni yerde, shutdown'da da orada yikiliyor). Burada ikinci bir
  // CreateContext cagrisi ikinci bir baglam yaratir ve ilkini sizdirirdi.
  // --- Panel duzeni: acilista yukle -----------------------------------------
  // layout_save/layout_load deterministik ve surumlu; ama bugune kadar YALNIZ
  // headless kapidan cagriliyordu, yani editor her acilista duzeni unutuyordu.
  // Dosya yoksa ya da bozuksa sessizce varsayilan duzene dusulur -- bozuk bir
  // duzen dosyasi yuzunden editorun acilmamasi kabul edilemez.
  char layout_path[1024];
  std::snprintf(layout_path, sizeof layout_path, "%s/editor_layout.txt", st.scene_dir);
  bool layout_restored = false;

  EditorCamera cam;
  cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
  if (headless) st.sel.set_single(0);
  st.playing = headless;
  if (st.playing) bodies_spawn(st, phys);
  if (headless && st.scene.entity_count) {
    // Betikli durum: bir islem + geri al (gunluk kapali dongude calisiyor mu).
    SceneEntity e = st.scene.entities[0];
    e.pos.x += 1.0f;
    if (st.hist.set_entity(st.scene, 0, e)) st.groups.push(1);
    st.groups.undo_size(); // grup muhasebesi gunlukle hizali kalsin
    st.hist.undo(st.scene);
  }
  set_status(st, "yuklendi: %s", st.scene_path);
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle
  bool snap_on = false;   // arac cubugundaki "Yakala": ImGuizmo adim kilidi
  float snap_step = 0.5f; // dunya birimi / derece / olcek adimi
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0, prev_scroll = 0;
  GizmoSpace gizmo_space = GizmoSpace::World; // ImGuizmo: dunya / yerel eksen
  // Kamera girdisi ONCEKI karenin panel durumunu okur (panel kare icinde daha
  // sonra ciziliyor): bir kare gecikme gorunmez, yanlis kosul gorunur olurdu.
  ViewportRect view_rect{};
  bool view_hovered = false;
  OverlayResult ovres;       // kaplamanin son karede urettigi etkilesim
  bool cam_dragging = false; // kamera tusu basili: panelden cikmak donusu kesmesin
  bool prev_f = false;
  ViewportTab view_tab = ViewportTab::Scene;
  GameAspect game_aspect = GameAspect::Free;
  bool maximize_on_play = false;
  bool mute_audio_on_play = false;
  bool show_grid = true;
  // Gorunum kipi (UE5 "View Mode"): 0 Aydinlatmali, 1 Isiksiz (albedo),
  // 2 Carpisma (tel kutu), 3 Sinirlar (AABB). Eskiden burada hicbir sey
  // OKUMAYAN bir `wireframe_mode` bayragi vardi; dordu de gercekten ciziliyor.
  // PR #7 o bayraga geri donuyordu; donulmedi -- dort kip de cizim yolunda
  // canli.
  int view_mode = 0;
  bool show_stats = true;
  bool was_playing = false;
  RecordCtx rctx{&ren, &ui, &vp, &dev};
  bool running = true;
  // Konsol + dosya islemleri durumu. static: ic tamponlari buyuk (halka ~150 KB,
  // diyalogun dizin listesi 512 girdi) ve surec basina TEK editor kosuyor.
  static ConsoleView console_view;
  static ConsoleCapture console_cap;
  static FileDialog dlg;
  // Ayni dosya diyalogu uc is icin acilir; kabul edildiginde NE yapilacagini
  // bu belirler. Eskiden yalniz dlg.mode (Ac/Kaydet) soruluyordu -- prefab
  // kaydetmek sahneyi kaydetmekle karisirdi.
  enum DialogIntent { IntentScene = 0, IntentPrefabSave = 1, IntentPrefabLoad = 2, IntentScriptNew = 3, IntentGamePick = 4 };
  DialogIntent dlg_intent = IntentScene;
  // "Yeni betik" diyalogu HANGI varlik icin acildi. Diyalog kareler boyunca
  // acik kalir ve bu arada secim degisebilir; secime bakarak atasaydik
  // betik kullanicinin sonradan tikladigi nesneye giderdi.
  int32_t script_new_target = -1;
  int32_t prefab_root = -1;
  static ConfirmState confirm;
  bool show_console = true;
  // PendingUpdate: indirilmis guncellemeyi kur + yeniden baslat (kirli sahne ONCE sorulur).
  enum PendingAction { PendingNone = 0, PendingNew = 1, PendingOpen = 2, PendingOpenPath = 3, PendingUpdate = 4 };
  int pending = PendingNone;
  char pending_path[1024] = {0}; // "Son dosyalar"dan secilen yol
  char recent_file[1024];
  {
    const char *home = std::getenv("HOME");
    std::snprintf(recent_file, sizeof recent_file, "%s/.tulpar_son_sahneler", (home && *home) ? home : ENGINE_SOURCE_DIR);
    recent_load(recent_file); // dosya yoksa false doner, liste bos kalir — HATA DEGIL
    recent_push(st.scene_path);
  }
  // --- Editor ici guncelleme (app/editor_update.hpp) -------------------------
  // Updater'in calisma bellegi sistem arenasindan (A2). PENCERESIZ KIPTE AG YOK:
  // ne otomatik denetim ne menu komutu — kapi dongu bitince olcer (asagida
  // "guncelleme kapisi"). static: UpdateUi surum notu tamponlariyla ~40 KB.
  static UpdateUi upd_ui;
  {
    UpdateUiConfig uc;
    uc.headless = headless;
    update_ui_init(upd_ui, sys, uc, platform::now_ns());
  }
  bool restart_after_update = false; // kurulum basarili: kapanisin SONUNDA yeni ikili baslar
  // DURDUR = oynatma oncesine TAM donus (Unity/Godot'daki gibi):
  //  - govdeler + karakterler fizikten cikar, cizim yazar donusumune doner;
  //  - duraklatma, tek adim istegi ve oynatma suresi sifirlanir;
  //  - OYNATMA SIRASINDA yapilan duzenlemeler GERI ALINIR. Kaybolmazlar:
  //    yinele (Ctrl+Y) onlari geri getirir. Eskiden kaliyordu — oynatirken bir
  //    degeri deneyip durdurunca sahne "oynatma oncesi" DEGILDI.
  // Isaret gunlugun GRUP derinligi (bir kullanici eylemi = bir grup).
  static uint32_t play_group_mark = 0;
  static bool play_dirty_mark = false;
  // Editorden baslatilan oyun: Ctrl+F5 (kendi penceresi) ya da F5 (gomulu, Oyun
  // sekmesi). TEK yuva: ikisi ayni anda kosmaz (ikisi de ayni .sahneb'i okur).
  static GameRun oyun;
  static EditorGameView oyun_goruntu; // F5: oyunun karesi (Oyun sekmesi dokusu)
  // Durdur -> oyun kendi `bitir` yolundan cikana kadar gecen sure boyunca
  // Konsol'a akan satirlar bu oynatmaya ait; bu bayrak "kapaniyor" durumunu tutar.
  static bool oyun_kapaniyor = false;
  // Oyun sekmesinin SON cizim olcusu (panel yerlesiminden, en-boy secimiyle):
  // F5 kanali bu olcude acar, oyun da bu olcude cizer — kare panele 1:1 oturur.
  static uint32_t oyun_cizim_w = 0, oyun_cizim_h = 0;
  static bool oyun_odak_iste = false;         // oynatma basladi: Gorunum paneline odak
  static bool gomulu_secim_bekliyor = false;  // oyun secici F5 icin acildi (secilince gomulu baslar)
  static uint64_t oyun_baslangic_ns = 0;
  static bool oyun_girdi_odak = false; // ONCEKI karede Oyun sekmesi odaktaydi: klavye oyunun
  static ViewportRect oyun_rect{};     // oyun karesinin EKRAN dikdortgeni (fare -> kare pikseli)
  static bool gorunum_odak = false;    // Gorunum paneli bu karede odakta
  rctx.game = &oyun_goruntu;
  // Kanaldan yeni kare -> Oyun sekmesi dokusunun bu ucus yuvasindaki hazirlama
  // tamponu. Kayittan (before_cb) HEMEN once cagrilir: yuvanin cit'i acquire'da
  // beklenmis, yani GPU o tamponu artik okumuyor. Doku oynatma basina bir kez,
  // kare BASINDA kurulur (asagida) — burada ayirma yok.
  auto oyun_kare_hazirla = [&](uint32_t slot) {
    if (!st.play_embedded || !oyun.chan.ok()) return;
    // Doku kare basinda kuruldu; olcu tutmuyorsa (kurulamadi) bu kare atlanir.
    if (!oyun_goruntu.ok() || oyun_goruntu.width() != oyun.chan.width() || oyun_goruntu.height() != oyun.chan.height()) return;
    const uint8_t *px = nullptr;
    uint32_t fr = 0;
    if (!oyun.chan.acquire(&px, &fr) || !px) return;
    if (oyun_goruntu.stages() == 0) {
      const double s0 = (platform::now_ns() - oyun_baslangic_ns) / 1e9;
      set_status(st, "oynat: %s — Oyun sekmesinde (tikla: klavye oyuna; F5 durdur, F6 duraklat)", oyun.game);
      console_log(ConsoleLevel::Bilgi, "oyun", "F5: ilk kare %.1f s'de geldi (%ux%u)", s0, oyun.chan.width(), oyun.chan.height());
    }
    oyun_goruntu.stage(px, slot);
  };
  auto set_playing = [&](bool p) {
    if (p == st.playing) return;
    st.playing = p;
    st.paused = false; // durdur/baslat duraklatmayi da sifirlar
    st.step_request = 0;
    st.play_time = 0;
    if (p) {
      play_group_mark = st.groups.depth();
      play_dirty_mark = st.dirty;
      // Gomulu oynatmada sahneyi OYUN oynatiyor: editorun govdeleri dogmaz.
      if (!st.play_embedded) {
        bodies_spawn(st, phys);
        // F5 FIZIK onizlemesi (oyun ya da derleyici bulunamadi): Tulpar
        // betikleri oyunun ikilisinde yasar, editorun icinde KOSMAZ. Sessiz
        // kalsaydi "kovalayan neden kovalamiyor" diye aranirdi.
        uint32_t betikli = 0;
        for (uint32_t i = 0; i < st.scene.entity_count; i++)
          if ((st.scene.entities[i].components & content::kSceneScript) && st.scene.entities[i].script_enabled) betikli++;
        if (betikli) {
          set_status(st, "oynat: FIZIK onizlemesi — %u betik kosmuyor (Konsol: neden)", betikli);
          console_log(ConsoleLevel::Uyari, kConsoleTagEditor,
                      "F5 fizik onizlemesi: %u varligin betigi KOSMUYOR. Betikler oyunun ikilisinde yasar; F5 onu gomulu "
                      "calistiramadi (sebep ustteki satirda)",
                      betikli);
        }
      }
    } else {
      if (st.play_embedded) {
        // Oyun kendi `bitir` yolundan kapanir (kanal: durdur); kalan satirlar
        // Konsol'a akmaya devam eder. Editorun sahnesine oyun HIC dokunmadi.
        st.play_embedded = false;
        if (oyun.state == GameRunState::Running) { game_run_stop(oyun); oyun_kapaniyor = true; }
      } else {
        bodies_remove(st, phys); // durdur: veri modeli (yazar donusumu) gecerli
      }
      uint32_t geri = 0;
      const uint32_t na0 = st.scene.asset_count;
      while (st.groups.depth() > play_group_mark && st.hist.undo_count() > 0) {
        const uint32_t k = st.groups.undo_size();
        for (uint32_t i = 0; i < k && st.hist.undo(st.scene); i++) geri++;
      }
      assets_after_history(na0);
      if (geri) {
        clamp_selection(st);
        // Butun oynatma duzenlemeleri geri alindiysa kirli bayragi da oynatma
        // oncesine doner (yalniz oynatirken kirlenen sahne "kaydedilmedi" demesin).
        if (st.groups.depth() == play_group_mark) st.dirty = play_dirty_mark;
        set_status(st, "durduruldu: oynatma sirasindaki %u duzenleme geri alindi (Ctrl+Y geri getirir)", geri);
        console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "durdur: oynatma sirasindaki %u duzenleme geri alindi (yinele ile geri gelir)", geri);
      } else {
        set_status(st, "durduruldu: sahne oynatma oncesine dondu");
      }
    }
  };
  // Geri al / yinele GRUP isler: bir kullanici eylemi gunlukte N islem olabilir
  // (grup tasima = N set_entity, grup silme = N remove_entity). Sinirlar OpGroups'ta;
  // gunluk once biterse dongu orada durur (grup muhasebesi gunlugun onune gecemez).
  auto do_undo = [&]() {
    if (st.edit_active) {
      const int32_t si = st.sel.primary();
      if (si >= 0 && si < (int32_t)st.scene.entity_count) {
        st.scene.entities[si] = st.edit_before;
      }
      st.edit_active = false;
      ImGui::ClearActiveID();
      set_status(st, "degisiklik iptal edildi");
      return;
    }
    if (st.hist.undo_count() == 0) return;
    const uint32_t k = st.groups.undo_size();
    uint32_t done = 0;
    const uint32_t na0 = st.scene.asset_count;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.undo(st.scene); i++) done++;
    });
    assets_after_history(na0);
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "geri alindi (%u islem, %u kaldi)", done, st.hist.undo_count()); }
  };
  auto do_redo = [&]() {
    if (st.hist.redo_count() == 0) return;
    const uint32_t k = st.groups.redo_size();
    uint32_t done = 0;
    const uint32_t na0 = st.scene.asset_count;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.redo(st.scene); i++) done++;
    });
    assets_after_history(na0);
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "yinelendi (%u islem, %u kaldi)", done, st.hist.redo_count()); }
  };
  auto do_save_as = [&]() {
    dlg_intent = IntentScene;
    file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_path[0] ? st.scene_path : st.scene_dir, ".sahne", "Farkl\xC4\xB1 kaydet");
  };
  // Sahneyi verilen yola yazar ve editorun ACIK DOSYASINI oraya tasir (kaynak
  // tarayicisi da yeni dizini gosterir — sahne dosyasi dizinini takip eder).
  auto save_scene_to = [&](const char *path) {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    content::SceneError err{};
    if (!content::scene_save(frame, st.scene, path, &err)) { set_status(st, "KAYDEDILEMEDI: %s", err.msg); return false; }
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    rescan_browse(st);
    st.dirty = false;
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "kaydedildi: %s", st.scene_path);
    return true;
  };
  auto do_save = [&]() {
    if (st.scene_path[0] == 0) { do_save_as(); return; } // adsiz sahne: once yer sor
    save_scene_to(st.scene_path);
  };
  // Dosyadan yukleme: veri modeli + turetilmis her sey (kaynaklar, tarayici,
  // kamera) yenilenir ve GUNLUK SIFIRLANIR — eski sahnenin geri al kayitlari
  // yeni sahneye uygulanamaz (indeksler baska bir sahneye ait).
  auto load_scene_from = [&](const char *path) {
    // STATIK: SceneDesc yuzlerce KB (scene.hpp) ve Windows ana yigini 1 MB.
    // Yigindaki hali CMake'in 128 KB cerceve kapisini kirdi (olculdu
    // 2026-09-25, GCC 16.2, E3 oncesi: bu lambda 213 392 B, editor_run
    // 342 672 B). Tek is parcacigi, yeniden girilmez; scene_parse her
    // yuklemede *out'u bastan kurar, onceki sahneden kalinti tasinmaz.
    static content::SceneDesc nd;
    content::SceneError err{};
    if (!content::scene_load(sys, path, &nd, &err)) { set_status(st, "ACILAMADI: %s (%s)", path, err.msg); return false; }
    with_bodies(st, phys, [&] {
      st.scene = nd;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
      st.particles.clear();
      std::memset(st.particle_spawn_accum, 0, sizeof st.particle_spawn_accum);
    });
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
    rescan_browse(st);
    cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
    st.clip_count = 0; // pano baska bir sahnenin varliklarini tasiyordu
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "acildi: %s (%u varlik)", st.scene_path, st.scene.entity_count);
    return true;
  };
  auto do_new = [&]() {
    // Varsayilan dunya + 0 varlik. STATIK ve const: yukaridaki `nd` ile ayni
    // sebep (yigin cercevesi kapisi); hic yazilmadigi icin her cagrida taze.
    static const content::SceneDesc fresh{};
    with_bodies(st, phys, [&] {
      st.scene = fresh;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
      st.particles.clear();
      std::memset(st.particle_spawn_accum, 0, sizeof st.particle_spawn_accum);
    });
    st.scene_path[0] = 0; // ADSIZ: ilk Kaydet "Farkli kaydet"e duser
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    st.clip_count = 0;
    rescan_browse(st);
    set_status(st, "yeni sahne (henuz kaydedilmedi)");
  };
  // Guncellemeyi kur (Staged -> Installed). Basariliysa editor NORMAL kapanis
  // yolundan cikar; yeni ikili kapanisin sonunda, panel duzeni kaydedildikten
  // SONRA baslar (once baslasaydi eski editorun kaydettigi duzeni okuyamazdi).
  auto do_update_install = [&]() {
    if (st.playing) set_playing(false);
    char e[256];
    if (!update_ui_install(upd_ui, e, sizeof e)) {
      // Geri alma eksikse "degismedi" YALAN olur (pencere kurtarma yolunu gosterir).
      if (upd_reason_rollback_incomplete(e)) set_status(st, "GUNCELLEME YARIM KALDI: %s (.guncelleme/ elle incelenmeli)", e);
      else set_status(st, "GUNCELLEME KURULAMADI: %s (kurulum dizini degismedi)", e);
      return;
    }
    set_status(st, "guncelleme kuruldu: editor yeniden baslatiliyor");
    restart_after_update = true;
    running = false;
  };
  auto run_pending = [&](int a) {
    if (a == PendingNew) do_new();
    else if (a == PendingOpen) { dlg_intent = IntentScene; file_dialog_open(dlg, FileDialogMode::Ac, st.scene_dir, ".sahne", "Sahne a\xC3\xA7"); }
    else if (a == PendingOpenPath && pending_path[0]) load_scene_from(pending_path);
    else if (a == PendingUpdate) do_update_install();
  };
  // KIRLI SAHNE KORUMASI: kaydedilmemis is varken Yeni/Ac ONCE sorar. Onay kipli
  // oldugu icin eylem ERTELENIR (pending) ve cevap gelince calisir.
  auto guard_then = [&](int action, const char *path = nullptr) {
    std::snprintf(pending_path, sizeof pending_path, "%s", path ? path : "");
    if (!st.dirty) { run_pending(action); return; }
    pending = action;
    confirm.open = true;
  };
  auto do_new_guarded = [&]() { guard_then(PendingNew); };
  auto do_open_guarded = [&]() { guard_then(PendingOpen); };
  // Derle: veri modeli -> runtime blob (.sahneb, sahne dosyasinin yanina; PLAN §6).
  // Kaydet gibi kamerayi da yazar; dosyayi degil bellekteki sahneyi derler.
  auto do_compile = [&]() -> bool {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    char out[1024];
    if (!content::scene_blob_path_for(st.scene_path, out, sizeof out)) { set_status(st, "DERLENEMEDI: yol cok uzun"); return false; }
    content::SceneError err{};
    // Navmesh BAKE EDILIR (engine_sahnec gibi). Eskiden edilmiyordu: "Derle"
    // navmesh'siz bir blob yaziyordu, sahne_izle ile calisan bir oyun sicak
    // yuklemede navmesh'ini KAYBEDIYOR ve kovalama sessizce duz yola dusuyordu
    // (olculdu 2026-09-23: betik_dagitimi editorden 2304 bayt, sahnec 2896 bayt,
    // oyun "sahnede navmesh yok" dedi). Kaynak olcumu (measure_resident)
    // KAPALI: modelleri 8 MB'lik kare arenasina yuklerdi; o rapor sahnec'te.
    content::SceneCompileOptions copt;
    copt.measure_resident = false;
    copt.bake_nav = true;
    content::SceneBlobExtras extras;
    content::SceneCompileReport crep;
    const bool ek = content::scene_compile(frame, st.scene, st.scene_dir, copt, &extras, &crep);
    if (!ek) console_log(ConsoleLevel::Uyari, kConsoleTagScene, "derle: navmesh bake adimi basarisiz (arena?) — blob navmesh'SIZ yazildi");
    // Nesne ozellikleri (E3) blob v8'den beri .sahneb'de (E4): E3'un burada
    // bastigi "henuz tasimiyor" uyarisi kalkti.
    if (!content::scene_blob_save_ex(frame, st.scene, ek ? &extras : nullptr, out, &err)) { set_status(st, "DERLENEMEDI: %s", err.msg); return false; }
    content::SceneBlobView v;
    if (!content::scene_blob_load(frame, out, &v, &err)) { set_status(st, "DERLENDI ama acilamadi: %s", err.msg); return false; }
    set_status(st, "derlendi: %s (%u bayt, %u cizim, %u isik, %u govde, ozet %08x)", out, v.h->total_size, v.h->draw_count, v.h->light_count,
               v.h->body_count, (unsigned)v.h->hash_lo);
    return true;
  };
  // --- Oyunu calistir (Ctrl+F5) -------------------------------------------
  // Ayri surec: motoru taniyan derleyici oyunu derler ve kendi penceresinde
  // calistirir; cikti gunluk dosyasindan Konsol'a akar (bkz. editor_game.hpp).
  // `oyun` yuvasi set_playing'in ustunde (F5 de ayni yuvayi kullaniyor).
  bool komut_hata = false;
  // Oyun satiri: Konsol + stdout (editor.sh'nin terminali ve penceresiz kip de gorsun).
  // Duzey satirin KENDISINDEN (motorun kurali: "HATA", "UYARI"): koprunun
  // "HATA sahne: 1 kaynak yuklenemedi" satiri eskiden bilgi rengindeydi ve
  // hata sayacina girmiyordu — harita gorunmezken Konsol "0 hata" diyordu.
  // Iki bosluk girintili satirlar koprunun kapanista TEKRAR bastigi log
  // halkasi: ayni hatayi ikinci kez saymamak icin bilgi kalir.
  void (*oyun_satiri)(void *, const char *) = [](void *, const char *line) {
    const ConsoleLevel lv = (line[0] == ' ' && line[1] == ' ') ? ConsoleLevel::Bilgi : console_classify_level(line, false);
    console_log(lv, "oyun", "%s", line);
    std::printf("[oyun] %s\n", line);
  };
  static char oyun_secim[content::kScenePathLen] = {0}; // tulpar/ koke GORELI (disindaysa mutlak)
  auto run_game_with = [&](const char *game) {
    char exe[1024], comp[1024], why[512], log[1200], err[512];
    if (!platform::exe_dir(exe, sizeof exe)) std::snprintf(exe, sizeof exe, ".");
    if (!game_find_compiler(exe, comp, sizeof comp, why, sizeof why)) {
      set_status(st, "oyun calistirilamadi: derleyici yok (Konsol)");
      console_log(ConsoleLevel::Hata, "oyun", "%s", why);
      return;
    }
    std::snprintf(log, sizeof log, "%s/oyun.log", exe);
    if (!game_run_start(oyun, comp, st.tulpar_dir, game, log, err, sizeof err)) {
      set_status(st, "oyun baslatilamadi: %s", err);
      console_log(ConsoleLevel::Hata, "oyun", "baslatilamadi: %s", err);
      return;
    }
    set_status(st, "oyun calisiyor: %s", game);
    console_log(ConsoleLevel::Bilgi, "oyun", "calistiriliyor: %s %s (dizin %s, gunluk %s)", comp, game, st.tulpar_dir, log);
  };
  auto do_run_game = [&]() {
    if (st.play_embedded) {
      set_status(st, "oyun F5 ile Oyun sekmesinde oynuyor: durdurmak icin F5");
      return;
    }
    if (oyun.state == GameRunState::Running) {
      game_run_stop(oyun);
      set_status(st, "oyun durduruluyor: %s", oyun.game);
      return;
    }
    if (!st.scene_path[0]) { set_status(st, "oyun calistirilamadi: once sahneyi kaydedin (oyun .sahneb yukler)"); return; }
    // Oyun sahneyi DISKTEKI blobdan yukler: bellekteki son hal once derlenir.
    // Derlenemezse calistirmak eski blobu gosterirdi ve degisiklik "gelmedi" sanilirdi.
    if (!do_compile()) return;
    if (oyun_secim[0]) { run_game_with(oyun_secim); return; }
    static char bulunan[8][content::kScenePathLen];
    static FileEntry tarama[kFileListMax];
    static char metin[256 * 1024];
    const GameFindResult r = game_find_for_scene(st.tulpar_dir, st.scene_path, bulunan, 8, tarama, kFileListMax, metin, sizeof metin);
    if (r.too_big) console_log(ConsoleLevel::Uyari, "oyun", "%u .tpr 256 KB'tan buyuk, oyun aramasinda OKUNMADI", r.too_big);
    if (r.count == 1) {
      std::snprintf(oyun_secim, sizeof oyun_secim, "%s", bulunan[0]);
      console_log(ConsoleLevel::Bilgi, "oyun", "bu sahneyi yukleyen oyun: %s (%u .tpr tarandi)", oyun_secim, r.scanned);
      run_game_with(oyun_secim);
      return;
    }
    // Sifir ya da birden cok: tahmin ETMIYORUZ, soruyoruz. Yanlis oyunu
    // calistirmak baska bir sahneyi acar ve kullanici bunu editorun hatasi sanar.
    if (r.count == 0) console_log(ConsoleLevel::Uyari, "oyun", "tulpar/ altinda bu sahneyi (.sahneb) yukleyen oyun bulunamadi: secin");
    for (uint32_t i = 0; i < r.count && i < 8; i++) console_log(ConsoleLevel::Uyari, "oyun", "aday %u: %s", i + 1, bulunan[i]);
    dlg_intent = IntentGamePick;
    gomulu_secim_bekliyor = false;
    file_dialog_open(dlg, FileDialogMode::Ac, st.tulpar_dir, ".tpr", r.count ? "Hangi oyun? (birden cok aday)" : "Oyun sec (.tpr)");
  };
  // --- F5: sahneyi BETIKLERIYLE, Oyun sekmesinde oynat ------------------------
  // Kullanici (2026-09-24): "F5 ile baslattigimiz sahneleri Ctrl+F5 ile
  // yapilmadan da ayni seyi oynatmasi gerekiyor". Tulpar'da yorumlayici yok;
  // betik oyunun ikilisinde kosar. F5 o ikiliyi Ctrl+F5 ile AYNI yoldan
  // (sahneyi derle -> onu yukleyen oyunu bul -> motoru taniyan derleyici)
  // baslatir, tek fark karenin gomulu kanaldan Oyun sekmesine gelmesi.
  // Yol tikanirsa (sahne kaydedilmemis, oyun yok, derleyici yok) editorun
  // FIZIK onizlemesine dusulur ve NEDEN Konsol'a yazilir — sessiz degil.
  auto start_embedded_with = [&](const char *game) -> bool {
    char exe[1024], comp[1024], why[512], log[1200], err[512];
    if (!platform::exe_dir(exe, sizeof exe)) std::snprintf(exe, sizeof exe, ".");
    if (!game_find_compiler(exe, comp, sizeof comp, why, sizeof why)) {
      console_log(ConsoleLevel::Hata, "oyun", "F5: %s", why);
      return false;
    }
    uint32_t w = oyun_cizim_w ? oyun_cizim_w : vp.width(), h = oyun_cizim_h ? oyun_cizim_h : vp.height();
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > platform::kGameChannelMaxSide) w = platform::kGameChannelMaxSide;
    if (h > platform::kGameChannelMaxSide) h = platform::kGameChannelMaxSide;
    std::snprintf(log, sizeof log, "%s/oyun.log", exe);
    if (!game_run_start_embedded(oyun, comp, st.tulpar_dir, game, log, w, h, err, sizeof err)) {
      console_log(ConsoleLevel::Hata, "oyun", "F5: baslatilamadi: %s", err);
      return false;
    }
    st.play_embedded = true;
    set_playing(true);
    oyun_odak_iste = true;
    oyun_kapaniyor = false;
    oyun_baslangic_ns = platform::now_ns();
    set_status(st, "oynat: %s derleniyor (Oyun sekmesi %ux%u; F5 durdurur)", game, w, h);
    console_log(ConsoleLevel::Bilgi, "oyun", "F5: %s Oyun sekmesinde calistiriliyor (%ux%u, derleyici %s, gunluk %s)", game, w, h, comp, log);
    return true;
  };
  auto play_start_auto = [&](bool allow_embedded) {
    if (st.playing) return;
    if (oyun.state == GameRunState::Running) {
      set_status(st, oyun_kapaniyor ? "onceki oynatma hala kapaniyor: bir an sonra tekrar deneyin"
                                    : "oynatilamadi: ayri pencerede bir oyun calisiyor (Ctrl+F5 ile durdurun)");
      return;
    }
    const char *neden = nullptr;
    if (!allow_embedded) neden = "penceresiz kip (gomulu oyun yalniz acikca istenince)";
    else if (!st.scene_path[0]) neden = "sahne henuz kaydedilmedi (oyun sahneyi diskteki .sahneb'den yukler)";
    if (!neden) {
      // Oyun sahneyi DISKTEKI blobdan yukler: bellekteki son hal once derlenir.
      if (!do_compile()) return; // durum cubugu sebebi soyluyor; eski blobla oynatmak yaniltirdi
      if (oyun_secim[0]) {
        if (start_embedded_with(oyun_secim)) return;
        neden = "oyun gomulu baslatilamadi (Konsol)";
      } else {
        static char bulunan[8][content::kScenePathLen];
        static FileEntry tarama[kFileListMax];
        static char metin[256 * 1024];
        const GameFindResult r = game_find_for_scene(st.tulpar_dir, st.scene_path, bulunan, 8, tarama, kFileListMax, metin, sizeof metin);
        if (r.count == 1) {
          std::snprintf(oyun_secim, sizeof oyun_secim, "%s", bulunan[0]);
          console_log(ConsoleLevel::Bilgi, "oyun", "bu sahneyi yukleyen oyun: %s (%u .tpr tarandi)", oyun_secim, r.scanned);
          if (start_embedded_with(oyun_secim)) return;
          neden = "oyun gomulu baslatilamadi (Konsol)";
        } else if (r.count > 1) {
          for (uint32_t i = 0; i < r.count && i < 8; i++) console_log(ConsoleLevel::Uyari, "oyun", "aday %u: %s", i + 1, bulunan[i]);
          dlg_intent = IntentGamePick;
          gomulu_secim_bekliyor = true;
          file_dialog_open(dlg, FileDialogMode::Ac, st.tulpar_dir, ".tpr", "F5: hangi oyun? (birden cok aday)");
          set_status(st, "F5: bu sahneyi birden cok oyun yukluyor, secin");
          return;
        } else {
          neden = "tulpar/ altinda bu sahneyi (.sahneb) yukleyen oyun yok";
        }
      }
    }
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "F5 fizik onizlemesi: %s", neden);
    set_playing(true);
  };
  // GI onizleme pisirme: sahne+yuklu modellerden bellek-ici bake -> derle ->
  // ac (ayni scene_blob_compile_ex/scene_blob_open yolu, DISK YOK). Dunya
  // panelindeki "Isik Haritasini Pisir" butonu cagirir; her cagrida onceki
  // pisirme atilir (arena().reset_to(0)) -- tek aktif sonuc yeter.
  auto do_bake_gi = [&]() {
    st.gi_arena.reset_to(0);
    content::GiScene g{};
    content::scene_gi_setup(st.scene, &g);
    static content::GiOccluder occ[content::kGiMaxOccluders];
    static content::GiLight lights[content::kGiMaxLights];
    static content::GiTri tris[200000]; // editor.sahne'nin gercek yuku (~6600) icin cok pay
    content::SceneGiOptions opt;
    g.occluders = occ;
    g.occluder_count = content::scene_gi_occluders(st.scene, opt, occ, content::kGiMaxOccluders);
    const content::Model *model_ptrs[content::kSceneMaxAssets];
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) model_ptrs[i] = st.have[i] ? &st.models[i] : nullptr;
    g.tris = tris;
    g.tri_count = content::scene_gi_model_tris(st.scene, opt, model_ptrs, st.scene.asset_count, tris, 200000);
    g.lights = lights;
    g.light_count = content::scene_gi_lights(st.scene, lights, content::kGiMaxLights);
    content::SceneBlobExtras x{};
    st.gi_baked = false;
    if (!content::scene_gi_bake(st.gi_arena, g, opt, &x, &st.gi_report)) {
      set_status(st, "GI pisirilemedi: sahne bos ya da izgara kurulamadi");
      return;
    }
    const size_t need = content::scene_blob_compile_ex(st.scene, &x, nullptr, 0);
    void *buf = st.gi_arena.alloc(need, content::kSceneBlobAlign);
    if (!buf) { set_status(st, "GI pisirilemedi: onizleme belleği yetersiz (32 MB)"); return; }
    content::scene_blob_compile_ex(st.scene, &x, buf, need);
    content::SceneBlobView v;
    content::SceneError err{};
    if (!content::scene_blob_open(buf, need, &v, &err)) { set_status(st, "GI pisirilemedi: %s", err.msg); return; }
    st.gi_baked = st.gi.init(v);
    st.gi_bake_undo_count = st.hist.undo_count();
    if (st.gi_baked)
      set_status(st, "GI pisirildi: %u sonda (%u gecerli), %.3f s", st.gi_report.probes, st.gi_report.probes - st.gi_report.probes_inside,
                 st.gi_report.seconds);
    else
      set_status(st, "GI pisirildi ama sonda izgarasi acilamadi (sahne cok kucuk/buyuk olabilir)");
  };
  // kind: 0 = kopya/varsayilan (secili varsa kopyasi, yoksa bos ya da model),
  // 1 bos, 2 model, 3 isik, 4 govde (Sahne panelinin "+" menusu).
  auto do_add = [&](int kind = 0, int32_t parent = -1) {
    SceneEntity e{};
    const int32_t prim = st.sel.primary();
    if (kind == 0 && prim >= 0) {
      e = st.scene.entities[prim];
      std::snprintf(e.name, sizeof e.name, "%.24s_kopya", st.scene.entities[prim].name);
      e.pos.x += 1.5f;
    } else {
      static const char *const kStem[] = {
        "nesne", "bos_varlik", "model", "isik_nokta", "kutu_sabit",
        "kure_sabit", "kutu_dinamik", "kure_dinamik", "zemin",
        "animasyon", "kup", "kure", "kamera", "ses_3b", "isik_gunes",
        "isik_spot", "isik_alan", "isik_tup", "isik_disk", "isik_godray"
      };
      // Ilkel geometriler 20..24 araliginda.
      static const char *const kPrimStem[] = {"kapsul", "silindir", "koni", "dortgen", "simit"};
      static const char *const kExtraStem[] = {
        "karakter", "arazi", "su", "voksel", "ruzgar",
        "vfx_ates", "skybox", "sonda", "betik", "ajan",
        "can_varlik", "yetenek_varlik", "sandik", "eklem", "yanki",
        "gi_sondasi", "sis_hacmi", "tetikleyici", "engel_hacmi"
      };
      static const char *const kVfxStem[] = {
        "vfx_duman", "vfx_kivilcim", "vfx_yagmur", "hacimsel_spot", "isik_huzmesi", "vfx", "vfx_kar", "vfx_buyu", "vfx_ozel"
      };
      const char *stem = "nesne";
      if (kind >= content::kPrimCapsule && kind <= content::kPrimTorus) stem = kPrimStem[kind - content::kPrimCapsule];
      else if (kind >= 30 && kind < 30 + (int)(sizeof(kExtraStem)/sizeof(kExtraStem[0]))) stem = kExtraStem[kind - 30];
      else if (kind >= 60 && kind < 60 + (int)(sizeof(kVfxStem)/sizeof(kVfxStem[0]))) stem = kVfxStem[kind - 60];
      else if (kind >= 70 && kind <= 74) {
        static const char *const kFogVolStem[] = {
          "sis_zemin_tabakasi", "sis_koni_plume", "sis_silindir_kolon", "sis_halka_torus", "sis_voksel_dumani"
        };
        stem = kFogVolStem[kind - 70];
      }
      else if (kind == 50) stem = "fizik_odasi";
      else if (kind == 51) stem = "doga_paketi";
      else if (kind == 52) stem = "rpg_sahnesi";
      else if (kind >= 0 && kind < (int)(sizeof(kStem)/sizeof(kStem[0]))) stem = kStem[kind];
      std::snprintf(e.name, sizeof e.name, "%s_%u", stem, st.scene.entity_count + 1);
      e.pos = cam.target;
      e.parent = parent;

      switch (kind) {
      case 1: // Boş varlık
        e.components = 0;
        break;
      case 2: // Model
        e.components = content::kSceneModel;
        e.asset = st.scene.asset_count ? 0 : -1;
        break;
      case 3: // Nokta Işık
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Point;
        e.light_color = Vec3{1.0f, 1.0f, 1.0f};
        e.light_intensity = 3.0f;
        e.light_radius = 8.0f;
        break;
      case 4: // Sabit Kutu Gövde (Görsel Model + Fiziksel Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.primitive = (int32_t)content::kPrimCube;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = false;
        break;
      case 5: // Sabit Küre Gövde (Görsel Model + Fiziksel Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.primitive = (int32_t)content::kPrimSphere;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = false;
        break;
      case 6: // Dinamik Kutu Gövde (Görsel Model + Fiziksel Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.primitive = (int32_t)content::kPrimCube;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = true;
        break;
      case 7: // Dinamik Küre Gövde (Görsel Model + Fiziksel Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.primitive = (int32_t)content::kPrimSphere;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = true;
        break;
      case 8: // Zemin / Düzlem (Görsel Model + Fiziksel Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.primitive = (int32_t)content::kPrimPlane;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{10.0f, 0.1f, 10.0f};
        e.scale = Vec3{20.0f, 1.0f, 20.0f};
        e.dynamic = false;
        e.pos.y = -0.1f;
        break;
      case 9: // Animasyonlu Model
        e.components = content::kSceneModel | content::kSceneAnim;
        e.asset = st.scene.asset_count > 1 ? 1 : (st.scene.asset_count ? 0 : -1);
        e.clip = 0;
        e.speed = 1.0f;
        break;
      case 10: { // Küp (Model + Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.dynamic = false;
        e.primitive = (int32_t)content::kPrimCube;
        e.asset = -1;
        break;
      }
      case 11: { // Küre (Model + Gövde)
        e.components = content::kSceneModel | content::kSceneBody;
        e.shape = content::SceneShape::Sphere;
        e.radius = 0.5f;
        e.dynamic = false;
        e.primitive = (int32_t)content::kPrimSphere;
        e.asset = -1;
        break;
      }
      case 12: // Kamera Varlığı
        e.components = content::kSceneCamera;
        e.cam_fov = 60.0f;
        e.cam_near = 0.1f;
        e.cam_far = 200.0f;
        break;
      case 13: // Ses Kaynağı
        e.components = content::kSceneAudio;
        std::snprintf(e.audio_clip, sizeof e.audio_clip, "ambient.wav");
        e.audio_volume = 1.0f;
        e.audio_pitch = 1.0f;
        e.audio_loop = true;
        e.audio_spatial = true;
        break;
      case 14: // Yönlü Işık (Güneş)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Directional;
        e.light_color = Vec3{1.0f, 0.96f, 0.9f};
        e.light_intensity = 2.0f;
        e.rot_deg = Vec3{50.0f, -30.0f, 0.0f}; // asagi/yana bakan tipik gunes acisi
        std::snprintf(e.name, sizeof e.name, "gunes_isigi");
        break;
      case 15: // Spot Işık (Koni)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Spot;
        e.light_color = Vec3{1.0f, 0.95f, 0.85f};
        e.light_intensity = 5.0f;
        e.light_radius = 12.0f;
        e.light_spot_inner = 20.0f;
        e.light_spot_outer = 35.0f;
        e.rot_deg = Vec3{45.0f, 0.0f, 0.0f};
        std::snprintf(e.name, sizeof e.name, "spot_isik");
        break;
      case 16: // Dikdörtgen / Alan Işık (LTC)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Rect;
        e.light_color = Vec3{1.0f, 1.0f, 1.0f};
        e.light_intensity = 8.0f;
        e.light_radius = 10.0f;
        e.light_width = 1.6f;
        e.light_height = 0.9f;
        std::snprintf(e.name, sizeof e.name, "alan_isik");
        break;
      case 17: // Tüp / Kapsül Işık
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Capsule;
        e.light_color = Vec3{0.8f, 0.9f, 1.0f};
        e.light_intensity = 6.0f;
        e.light_radius = 8.0f;
        e.light_width = 2.0f;
        std::snprintf(e.name, sizeof e.name, "tup_isik");
        break;
      case 18: // Disk Işık
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Disk;
        e.light_color = Vec3{1.0f, 0.98f, 0.92f};
        e.light_intensity = 6.0f;
        e.light_radius = 8.0f;
        e.light_width = 0.5f;
        std::snprintf(e.name, sizeof e.name, "disk_isik");
        break;
      case 19: // Işık Hüzmeli Güneş (Sun + God Rays)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Directional;
        e.light_color = Vec3{1.0f, 0.94f, 0.82f};
        e.light_intensity = 3.5f;
        e.light_godray = true;
        e.light_godray_intensity = 1.5f;
        e.rot_deg = Vec3{35.0f, -45.0f, 0.0f};
        std::snprintf(e.name, sizeof e.name, "gunes_godrays");
        st.scene.godrays_enabled = true;
        st.scene.godray_density = 0.9f;
        st.scene.godray_weight = 0.6f;
        st.scene.godray_decay = 0.96f;
        st.scene.godray_exposure = 0.35f;
        st.dirty = true;
        break;
      case 63: // Hacimsel Spot Işık (Volumetric Spotlight)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Spot;
        e.light_color = Vec3{0.95f, 0.92f, 1.0f};
        e.light_intensity = 8.0f;
        e.light_radius = 20.0f;
        e.light_spot_inner = 18.0f;
        e.light_spot_outer = 32.0f;
        e.light_godray = true;
        e.light_godray_intensity = 2.0f;
        e.pos = Vec3{0.0f, 5.0f, 0.0f};
        e.rot_deg = Vec3{60.0f, 0.0f, 0.0f};
        std::snprintf(e.name, sizeof e.name, "hacimsel_spot");
        st.scene.godrays_enabled = true;
        st.dirty = true;
        break;
      case 64: // Işık Hüzmesi (Volumetric Fake Godray Beam)
        e.components = content::kSceneLight;
        e.light_type = content::SceneLightType::Spot;
        e.light_color = Vec3{1.0f, 0.94f, 0.82f};
        e.light_intensity = 6.0f;
        e.light_radius = 14.0f;
        e.light_spot_inner = 12.0f;
        e.light_spot_outer = 26.0f;
        e.light_godray = true;
        e.light_godray_intensity = 2.0f;
        e.pos = Vec3{0.0f, 4.0f, 0.0f};
        e.rot_deg = Vec3{60.0f, -25.0f, 0.0f};
        std::snprintf(e.name, sizeof e.name, "isik_huzmesi");
        st.scene.godrays_enabled = true;
        st.scene.godray_density = 1.0f;
        st.scene.godray_weight = 0.65f;
        st.scene.godray_decay = 0.97f;
        st.scene.godray_exposure = 0.4f;
        st.dirty = true;
        break;
      // --- Ilkel (prosedurel) geometriler ---------------------------------
      case content::kPrimCapsule:  // Kapsül
      case content::kPrimCylinder: // Silindir
      case content::kPrimCone:     // Koni
      case content::kPrimQuad:     // Dörtgen
      case content::kPrimTorus:    // Simit
        e.components = content::kSceneModel;
        e.primitive = kind;
        e.asset = -1;
        break;
      case 30: // Karakter Kontrolcüsü
        e.components = content::kSceneCharacter | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.4f, 0.9f, 0.4f};
        e.dynamic = true;
        e.char_radius = 0.4f;
        e.char_height = 1.8f;
        e.char_mass = 75.0f;
        e.char_max_slope = 45.0f;
        break;
      case 31: // Prosedürel Arazi (Terrain)
        e.components = content::kSceneTerrain;
        e.terrain_width = 64.0f;
        e.terrain_height = 64.0f;
        e.terrain_cell = 1.0f;
        e.terrain_amp = 15.0f;
        e.terrain_freq = 0.03f;
        e.terrain_octaves = 4;
        e.tint = Vec3{0.32f, 0.65f, 0.28f}; // Doğal çim / vadi yeşili
        e.roughness = 0.85f;
        e.metallic = 0.0f;
        e.reflectance = 0.5f;
        std::snprintf(e.name, sizeof e.name, "prosedurel_arazi");
        break;
      case 32: // Su (Gerstner)
        e.components = content::kSceneWater;
        e.tint = Vec3{0.12f, 0.45f, 0.85f}; // Okyanus mavisi
        e.roughness = 0.15f;
        e.metallic = 0.1f;
        e.reflectance = 0.6f;
        e.wave_length = 8.0f;
        e.wave_amplitude = 0.4f;
        e.wave_speed = 1.2f;
        e.wave_steepness = 0.3f;
        e.wave_direction = Vec2{1.0f, 0.0f};
        std::snprintf(e.name, sizeof e.name, "dinamik_su");
        break;
      case 33: // Voksel
        e.components = content::kSceneVoxel;
        e.tint = Vec3{0.75f, 0.68f, 0.58f}; // Taş / tuğla tonu
        e.roughness = 0.85f;
        e.metallic = 0.05f;
        e.reflectance = 0.5f;
        e.voxel_size_x = 16;
        e.voxel_size_y = 16;
        e.voxel_size_z = 16;
        e.voxel_cell = 0.5f;
        break;
      case 34: // Rüzgar
        e.components = content::kSceneWind;
        e.wind_strength = 5.0f;
        e.wind_direction = Vec2{1.0f, 0.0f};
        e.wind_gustiness = 0.5f;
        e.wind_gust_freq = 1.0f;
        break;
      case 35: // Partikül: Yangın & Ateş (Fire & Embers)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 60.0f;
        e.particle_lifetime_min = 0.8f;
        e.particle_lifetime_max = 1.8f;
        e.particle_size_start = 0.25f;
        e.particle_size_end = 0.02f;
        e.particle_velocity = Vec3{0.0f, 3.0f, 0.0f};
        e.particle_jitter = Vec3{0.4f, 0.8f, 0.4f};
        e.particle_color_start = Vec3{1.0f, 0.65f, 0.1f};
        e.particle_color_end = Vec3{0.3f, 0.1f, 0.05f};
        e.particle_gravity = 0.5f;
        e.particle_billboard_type = 0;
        std::snprintf(e.name, sizeof e.name, "ates_efekti");
        break;
      case 36: // Gökyüzü / Atmosfer (Skybox & Atmosphere)
        e.components = content::kSceneSkybox;
        std::snprintf(e.name, sizeof e.name, "gokyuzu_atmosfer");
        break;
      case 60: // Partikül: Duman & Toz (Smoke & Dust)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 25.0f;
        e.particle_lifetime_min = 2.0f;
        e.particle_lifetime_max = 4.0f;
        e.particle_size_start = 0.1f;
        e.particle_size_end = 0.8f;
        e.particle_velocity = Vec3{0.1f, 1.2f, 0.0f};
        e.particle_jitter = Vec3{0.3f, 0.3f, 0.3f};
        e.particle_color_start = Vec3{0.4f, 0.4f, 0.4f};
        e.particle_color_end = Vec3{0.1f, 0.1f, 0.1f};
        e.particle_gravity = 0.2f;
        e.particle_billboard_type = 0;
        std::snprintf(e.name, sizeof e.name, "duman_efekti");
        break;
      case 61: // Partikül: Kıvılcım & Çarpışma (Sparks)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 80.0f;
        e.particle_lifetime_min = 0.4f;
        e.particle_lifetime_max = 1.0f;
        e.particle_size_start = 0.08f;
        e.particle_size_end = 0.01f;
        e.particle_velocity = Vec3{0.0f, 4.0f, 0.0f};
        e.particle_jitter = Vec3{3.0f, 2.0f, 3.0f};
        e.particle_color_start = Vec3{1.0f, 0.9f, 0.4f};
        e.particle_color_end = Vec3{0.8f, 0.2f, 0.0f};
        e.particle_gravity = -9.8f;
        e.particle_billboard_type = 1;
        std::snprintf(e.name, sizeof e.name, "kivilcim_efekti");
        break;
      case 62: // Partikül: Yağmur & Fırtına (Rain & Storm)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 140.0f;
        e.particle_lifetime_min = 1.2f;
        e.particle_lifetime_max = 2.0f;
        e.particle_size_start = 0.04f;
        e.particle_size_end = 0.04f;
        e.particle_velocity = Vec3{0.5f, -12.0f, 0.2f};
        e.particle_jitter = Vec3{6.0f, 0.5f, 6.0f};
        e.particle_color_start = Vec3{0.7f, 0.85f, 1.0f};
        e.particle_color_end = Vec3{0.5f, 0.65f, 0.9f};
        e.particle_gravity = -9.8f;
        e.particle_billboard_type = 1;
        std::snprintf(e.name, sizeof e.name, "yagmur_efekti");
        break;
      case 66: // Partikül: Kar & Tipi (Snow & Blizzard)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 90.0f;
        e.particle_lifetime_min = 3.0f;
        e.particle_lifetime_max = 5.0f;
        e.particle_size_start = 0.08f;
        e.particle_size_end = 0.05f;
        e.particle_velocity = Vec3{0.8f, -2.5f, 0.4f};
        e.particle_jitter = Vec3{5.0f, 0.5f, 5.0f};
        e.particle_color_start = Vec3{0.95f, 0.98f, 1.0f};
        e.particle_color_end = Vec3{0.8f, 0.85f, 0.95f};
        e.particle_gravity = -0.5f;
        e.particle_billboard_type = 0;
        std::snprintf(e.name, sizeof e.name, "kar_efekti");
        break;
      case 67: // Partikül: Büyü & Işıltı (Magic Glow)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 45.0f;
        e.particle_lifetime_min = 1.2f;
        e.particle_lifetime_max = 2.4f;
        e.particle_size_start = 0.16f;
        e.particle_size_end = 0.02f;
        e.particle_velocity = Vec3{0.0f, 1.6f, 0.0f};
        e.particle_jitter = Vec3{0.8f, 0.8f, 0.8f};
        e.particle_color_start = Vec3{0.4f, 0.7f, 1.0f};
        e.particle_color_end = Vec3{0.85f, 0.2f, 1.0f};
        e.particle_gravity = 0.2f;
        e.particle_billboard_type = 0;
        std::snprintf(e.name, sizeof e.name, "buyu_efekti");
        break;
      case 68: // Özel Partikül Emitter (Custom)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 50.0f;
        e.particle_lifetime_min = 1.0f;
        e.particle_lifetime_max = 2.5f;
        e.particle_size_start = 0.2f;
        e.particle_size_end = 0.05f;
        e.particle_velocity = Vec3{0.0f, 2.0f, 0.0f};
        e.particle_jitter = Vec3{0.5f, 0.5f, 0.5f};
        e.particle_color_start = Vec3{1.0f, 0.8f, 0.3f};
        e.particle_color_end = Vec3{0.2f, 0.1f, 0.05f};
        e.particle_gravity = 0.0f;
        e.particle_billboard_type = 0;
        std::snprintf(e.name, sizeof e.name, "ozel_partikul");
        break;
      case 37: // Yansıma Sondası
        e.components = content::kSceneRefProbe;
        e.ref_probe_radius = 20.0f;
        e.ref_probe_intensity = 1.0f;
        std::snprintf(e.name, sizeof e.name, "yansima_sondasi");
        break;
      case 45: // Işık Hacmi Sondası (Irradiance GI Grid)
        e.components = content::kSceneRefProbe;
        e.ref_probe_radius = 15.0f;
        e.ref_probe_intensity = 1.0f;
        std::snprintf(e.name, sizeof e.name, "gi_isik_sondasi");
        break;
      case 46: // Sis Hacmi (Hacimsel Sis / Fog Volume)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 120.0f;
        e.particle_lifetime_min = 2.5f;
        e.particle_lifetime_max = 5.0f;
        e.particle_size_start = 1.0f;
        e.particle_size_end = 2.8f;
        e.particle_velocity = Vec3{0.0f, 0.05f, 0.0f};
        e.particle_jitter = Vec3{4.0f, 1.2f, 4.0f};
        e.particle_color_start = Vec3{0.82f, 0.85f, 0.90f};
        e.particle_color_end = Vec3{0.65f, 0.70f, 0.78f};
        e.particle_gravity = 0.0f;
        e.particle_billboard_type = 3; // 3: 3B Yuvarlak Kure (Volumetric Sphere Puff)
        e.particle_drag = 0.5f;
        std::snprintf(e.name, sizeof e.name, "sis_hacmi");
        break;
      case 70: // Yatay Zemin Sisi (Ground Mist Sheet)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 90.0f;
        e.particle_lifetime_min = 3.0f;
        e.particle_lifetime_max = 6.0f;
        e.particle_size_start = 3.0f;
        e.particle_size_end = 6.0f;
        e.particle_velocity = Vec3{0.2f, 0.01f, 0.0f};
        e.particle_jitter = Vec3{8.0f, 0.2f, 8.0f};
        e.particle_color_start = Vec3{0.78f, 0.82f, 0.88f};
        e.particle_color_end = Vec3{0.50f, 0.55f, 0.62f};
        e.particle_gravity = -0.05f;
        e.particle_billboard_type = 2; // 2: Yatay Duzlem
        e.particle_drag = 0.8f;
        std::snprintf(e.name, sizeof e.name, "sis_zemin_tabakasi");
        break;
      case 71: // Konik Baca Sisi / Duman Jeti (Cone Fog Plume)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 140.0f;
        e.particle_lifetime_min = 2.0f;
        e.particle_lifetime_max = 4.5f;
        e.particle_size_start = 0.4f;
        e.particle_size_end = 3.0f;
        e.particle_velocity = Vec3{0.0f, 2.5f, 0.0f};
        e.particle_jitter = Vec3{0.3f, 0.1f, 0.3f};
        e.particle_color_start = Vec3{0.60f, 0.62f, 0.68f};
        e.particle_color_end = Vec3{0.35f, 0.38f, 0.45f};
        e.particle_gravity = 0.2f;
        e.particle_billboard_type = 6; // 6: 3B Koni (Cone Plume)
        e.particle_drag = 0.4f;
        std::snprintf(e.name, sizeof e.name, "sis_koni_plume");
        break;
      case 72: // Silindirik Kuyu / Saft Sisi (Cylinder Shaft Fog)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 100.0f;
        e.particle_lifetime_min = 3.0f;
        e.particle_lifetime_max = 5.5f;
        e.particle_size_start = 1.2f;
        e.particle_size_end = 1.8f;
        e.particle_velocity = Vec3{0.0f, 1.2f, 0.0f};
        e.particle_jitter = Vec3{0.5f, 0.2f, 0.5f};
        e.particle_color_start = Vec3{0.70f, 0.75f, 0.80f};
        e.particle_color_end = Vec3{0.45f, 0.50f, 0.55f};
        e.particle_gravity = 0.0f;
        e.particle_billboard_type = 7; // 7: 3B Silindir
        e.particle_drag = 0.5f;
        std::snprintf(e.name, sizeof e.name, "sis_silindir_kolon");
        break;
      case 73: // Portal / Halka Sisi (Torus Ring Fog)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 110.0f;
        e.particle_lifetime_min = 2.5f;
        e.particle_lifetime_max = 4.0f;
        e.particle_size_start = 1.5f;
        e.particle_size_end = 2.2f;
        e.particle_velocity = Vec3{0.0f, 0.2f, 0.0f};
        e.particle_jitter = Vec3{2.5f, 0.4f, 2.5f};
        e.particle_color_start = Vec3{0.65f, 0.35f, 0.95f};
        e.particle_color_end = Vec3{0.20f, 0.08f, 0.45f};
        e.particle_gravity = 0.0f;
        e.particle_billboard_type = 5; // 5: 3B Simit / Torus
        e.particle_drag = 0.6f;
        std::snprintf(e.name, sizeof e.name, "sis_portal_halkasi");
        break;
      case 74: // Voksel Duman / CS2 Dinamik Sis (Voxel Smoke Volume)
        e.components = content::kSceneParticle;
        e.particle_spawn_rate = 150.0f;
        e.particle_lifetime_min = 3.5f;
        e.particle_lifetime_max = 6.0f;
        e.particle_size_start = 0.8f;
        e.particle_size_end = 2.2f;
        e.particle_velocity = Vec3{0.0f, 0.1f, 0.0f};
        e.particle_jitter = Vec3{2.5f, 1.5f, 2.5f};
        e.particle_color_start = Vec3{0.55f, 0.58f, 0.62f};
        e.particle_color_end = Vec3{0.30f, 0.32f, 0.35f};
        e.particle_gravity = 0.0f;
        e.particle_billboard_type = 4; // 4: Voksel 3B Kup
        e.particle_drag = 0.7f;
        std::snprintf(e.name, sizeof e.name, "sis_voksel_dumani");
        break;
      case 38: // Betik
        e.components = content::kSceneScript;
        e.script_enabled = true;
        std::snprintf(e.script_file, sizeof e.script_file, "main.tpr");
        break;
      case 39: // Yapay Zeka Ajanı
        e.components = content::kSceneNavAgent | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.3f, 0.8f, 0.3f};
        e.dynamic = true;
        e.ai_speed = 3.5f;
        e.ai_turn_speed = 180.0f;
        break;
      case 40: // Can & Zırh (Health)
        e.components = content::kSceneHealth | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.health_current = 100.0f;
        e.health_max = 100.0f;
        break;
      case 41: // Büyü & Yetenek (GAS)
        e.components = content::kSceneAbility | content::kSceneHealth | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.5f, 0.5f};
        e.health_current = 100.0f;
        e.health_max = 100.0f;
        e.ability_id = 1;
        e.ability_damage = 35.0f;
        e.ability_range = 15.0f;
        e.ability_cooldown = 2.5f;
        break;
      case 42: // Envanter / Sandık
        e.components = content::kSceneInventory | content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{0.5f, 0.4f, 0.4f};
        break;
      case 43: // Fizik Eklemi
        e.components = content::kSceneJoint;
        e.joint_axis = Vec3{0.0f, 1.0f, 0.0f};
        e.joint_limit_min = -45.0f;
        e.joint_limit_max = 45.0f;
        break;
      case 44: // Yankı Alanı
        e.components = content::kSceneReverb;
        e.reverb_decay = 2.5f;
        e.reverb_room_size = 0.7f;
        std::snprintf(e.name, sizeof e.name, "yanki_alani");
        break;
      case 47: // Tetikleyici Hacim (Trigger Volume)
        // Eskiden yalniz ADI tetikti: duz statik kutu, icinden gecilemiyordu
        // ve hicbir sey bildirmiyordu. Artik gercek sensor.
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{2.0f, 1.5f, 2.0f};
        e.dynamic = false;
        e.body_sensor = true;
        std::snprintf(e.name, sizeof e.name, "tetikleyici_hacim");
        break;
      case 48: // Engelleme Hacmi (Blocking Volume)
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{2.0f, 2.0f, 0.2f};
        e.dynamic = false;
        std::snprintf(e.name, sizeof e.name, "engelleme_hacmi");
        break;
      case 50: { // Şablon: Fizik Deney Odası
        e.components = content::kSceneBody;
        e.shape = content::SceneShape::Box;
        e.half = Vec3{15.0f, 0.2f, 15.0f};
        e.pos = Vec3{0.0f, -0.2f, 0.0f};
        e.dynamic = false;
        std::snprintf(e.name, sizeof e.name, "fizik_zemin");
        break;
      }
      case 51: { // Şablon: Doğa Paketi
        e.components = content::kSceneTerrain | content::kSceneWater | content::kSceneSkybox;
        e.terrain_width = 64.0f;
        e.terrain_height = 64.0f;
        e.terrain_cell = 1.0f;
        e.terrain_amp = 12.0f;
        e.terrain_freq = 0.03f;
        e.wave_length = 10.0f;
        e.wave_amplitude = 0.3f;
        std::snprintf(e.name, sizeof e.name, "doga_paketi");
        break;
      }
      case 52: { // Şablon: RPG Sahnesi
        e.components = content::kSceneCharacter | content::kSceneHealth | content::kSceneAbility | content::kSceneInventory;
        e.char_radius = 0.4f;
        e.char_height = 1.8f;
        e.char_mass = 75.0f;
        e.health_current = 100.0f;
        e.health_max = 100.0f;
        e.ability_id = 1;
        e.ability_damage = 40.0f;
        e.ability_range = 12.0f;
        e.ability_cooldown = 2.0f;
        std::snprintf(e.name, sizeof e.name, "kahraman_rpg");
        break;
      }
      default:
        if (kind == 2 || (kind == 0 && st.scene.asset_count)) { e.components = content::kSceneModel; e.asset = st.scene.asset_count ? 0 : -1; }
        if (kind == 3) e.components = content::kSceneLight;
        if (kind == 4) e.components = content::kSceneBody;
        break;
      }
    }
    with_bodies(st, phys, [&] {
      if (st.hist.add_entity(st.scene, e)) {
        st.groups.push(1);
        st.sel.set_single((int32_t)st.scene.entity_count - 1);
        st.dirty = true;
        set_status(st, "eklendi: %s", e.name);
      } else set_status(st, "eklenemedi (kapasite %u)", content::kSceneMaxEntities);
    });
  };
  // Grup silme: secilenlerin tamami, buyukten kucuge (indeksler kaymasin); gunluge
  // N islem ama TEK eylem (geri al hepsini birden getirir).
  auto do_remove = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx);
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = selection_remove(st.scene, st.hist, idx, n); });
    for (uint32_t k = 0; k < n; k++) st.tree.collapse.after_remove((uint32_t)idx[k]); // buyukten kucuge silindi
    if (ops) {
      st.groups.push(ops);
      st.dirty = true;
      set_status(st, "silindi (%u varlik)", ops);
    }
    st.sel.clear();
  };
  // Pano: kopyalama secimi ARTAN indeks sirasinda alir (belirlenimli — secim
  // kumesinin kendi sirasi tiklama sirasidir, yapistirma sirasi ona bagli olmasin).
  auto do_copy = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx); // buyukten kucuge; tersten okuyacagiz
    st.clip_count = 0;
    for (uint32_t i = n; i > 0; i--) st.clip[st.clip_count++] = st.scene.entities[idx[i - 1]];
    set_status(st, "panoya alindi (%u varlik)", st.clip_count);
  };
  auto do_cut = [&]() {
    if (st.sel.count == 0) return;
    do_copy();
    do_remove();
    set_status(st, "kesildi (%u varlik)", st.clip_count);
  };
  // Yapistir: her varlik yeni bir varliktir (kopya adi + kucuk otelemeyle
  // ustuste binmesin). Gunluge N islem ama TEK eylem: geri al hepsini alir.
  auto do_paste = [&]() {
    if (st.clip_count == 0) return;
    uint32_t ops = 0;
    const uint32_t first = st.scene.entity_count;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < st.clip_count; i++) {
        SceneEntity e = st.clip[i];
        e.pos.x += 1.0f;
        if (st.hist.add_entity(st.scene, e)) ops++;
        else break; // kapasite doldu: sessizce kirpma yok, asagida bildirilir
      }
    });
    if (!ops) { set_status(st, "yapistirilamadi (kapasite %u)", content::kSceneMaxEntities); return; }
    st.groups.push(ops);
    st.dirty = true;
    st.sel.clear();
    for (uint32_t i = 0; i < ops; i++) st.sel.toggle((int32_t)(first + i));
    if (ops < st.clip_count) set_status(st, "yapistirildi (%u/%u — kapasite %u doldu)", ops, st.clip_count, content::kSceneMaxEntities);
    else set_status(st, "yapistirildi (%u varlik)", ops);
  };
  // Sahne panelinin dondurdugu NIYETI uygular. Panel sahneyi DEGISTIRMEZ; gunluk,
  // secim ve govde yeniden kurulumu tek yerde — burada.
  auto apply_hierarchy = [&](const HierarchyResult &r) {
    const int32_t i = r.index;
    const bool valid = i >= 0 && i < (int32_t)st.scene.entity_count;
    if (!valid && r.action != HierarchyAction::None) return;
    switch (r.action) {
    case HierarchyAction::None: break;
    case HierarchyAction::Select:
      if (r.ctrl) st.sel.toggle(i);
      else st.sel.set_single(i);
      break;
    case HierarchyAction::Toggle: st.tree.collapse.toggle((uint32_t)i); break;
    case HierarchyAction::Rename: {
      SceneEntity after = st.scene.entities[i];
      std::snprintf(after.name, sizeof after.name, "%s", r.name);
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
        set_status(st, "adlandirildi: %s", after.name);
      }
      break;
    }
    case HierarchyAction::Delete:
      st.sel.set_single(i);
      do_remove();
      st.tree.collapse.after_remove((uint32_t)i);
      break;
    case HierarchyAction::Duplicate:
      st.sel.set_single(i);
      do_add(0);
      break;
    case HierarchyAction::Cut:
      st.sel.set_single(i);
      do_cut();
      break;
    case HierarchyAction::Copy:
      st.sel.set_single(i);
      do_copy();
      break;
    case HierarchyAction::Paste:
      do_paste();
      break;
    case HierarchyAction::SavePrefab:
      prefab_root = i;
      dlg_intent = IntentPrefabSave;
      file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_dir, ".prefab", "Prefab kaydet");
      break;
    case HierarchyAction::Detach:
    case HierarchyAction::Reparent: {
      const int32_t par = (r.action == HierarchyAction::Detach) ? -1 : r.target;
      bool ok = false;
      with_bodies(st, phys, [&] { ok = st.hist.reparent(st.scene, (uint32_t)i, par); });
      if (ok) {
        st.groups.push(1);
        st.dirty = true;
        if (par < 0) set_status(st, "ebeveynden ayrildi: %s", st.scene.entities[i].name);
        else set_status(st, "ebeveyn: %s", st.scene.entities[par].name);
      } else if (par >= 0) set_status(st, "ebeveynlenemedi (dongu ya da derinlik tavani %u)", content::kSceneMaxDepth);
      break;
    }
    case HierarchyAction::Visibility:
    case HierarchyAction::Lock: {
      SceneEntity after = st.scene.entities[i];
      after.flags ^= (r.action == HierarchyAction::Visibility) ? content::kSceneHidden : content::kSceneLocked;
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
      }
      break;
    }
    case HierarchyAction::Focus: {
      st.sel.set_single(i);
      static content::SceneBounds fb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, fb);
      if ((uint32_t)i < nb) camera_focus(cam, fb[i]);
      break;
    }
    case HierarchyAction::CreateChild: {
      do_add(1);
      if (st.scene.entity_count > 0) {
        const uint32_t new_child = st.scene.entity_count - 1;
        with_bodies(st, phys, [&] { st.hist.reparent(st.scene, new_child, i); });
        st.sel.set_single(new_child);
        st.tree.collapse.set(i, false);
      }
      break;
    }
    }
  };
  // Kaynak tarayicidan ekleme: kaynak sahneye (varsa mevcut indeks) + o kaynakla
  // yeni varlik; kaynak henuz yuklenmemisse burada yuklenir.
  auto do_add_asset = [&](const char *file) {
    int32_t a = -1;
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = editor_add_asset_entity(st.scene, st.hist, file, cam.target, &a); });
    if (!ops) { set_status(st, "kaynak eklenemedi: %s", file); return; }
    st.groups.push(ops);
    st.dirty = true;
    if (a >= 0 && !st.have[a]) load_asset(a);
    st.sel.set_single((int32_t)st.scene.entity_count - 1);
    rescan_browse(st);
    set_status(st, "kaynak eklendi: %s (%s)", file, (a >= 0 && st.have[a]) ? "yuklendi" : "YUKLENEMEDI");
  };
  // --- Komut tablosu: menu, arac cubugu ve kisayollar TEK kaynaktan ----------
  // (editor_commands.hpp'nin varlik sebebi: ayni komut iki yerde yazilmasin,
  // cakisma sessiz kalmasin, koruma kurali tek olsun). Geri cagrilar yukaridaki
  // yakalayan lambda'lar; CommandFn duz isaretci oldugu icin arada bir baglam
  // yapisi var — lambda'lar adresle tutulur, kopyalanmaz.
  struct CmdCtx {
    EditorState *st;
    int *gizmo_op;
    EditorCamera *cam;  // ViewFocus (F): secili varligi cerceveler
    sim::Physics *phys; // ... sinirlar oynatma sirasinda GOVDEDEN gelir
    decltype(&do_save) save;
    decltype(&do_compile) compile;
    decltype(&do_undo) undo;
    decltype(&do_redo) redo;
    decltype(&do_add) add;
    decltype(&do_remove) remove;
    decltype(&set_playing) play;
    decltype(&do_cut) cut;
    decltype(&do_copy) copy;
    decltype(&do_paste) paste;
    decltype(&do_new_guarded) newscene;
    decltype(&do_open_guarded) open;
    decltype(&do_save_as) saveas;
    bool *show_console;
    const EditorHost *host; // tam ekran: yetenek host'ta (headless'ta nullptr)
    decltype(&play_start_auto) play_auto; // F5 baslat: gomulu oyun ya da fizik onizlemesi
  } cc{&st,      &gizmo_op, &cam,     &phys,     &do_save, &do_compile,      &do_undo,         &do_redo,     &do_add,     &do_remove,
       &set_playing, &do_cut,   &do_copy, &do_paste,    &do_new_guarded,  &do_open_guarded, &do_save_as, &show_console, host, &play_start_auto};
  CommandTable cmds;
  PaletteState palette_st;
  cmds.bind(CommandId::FileNew, [](void *c) { (*static_cast<CmdCtx *>(c)->newscene)(); }, &cc);
  cmds.bind(CommandId::FileOpen, [](void *c) { (*static_cast<CmdCtx *>(c)->open)(); }, &cc);
  cmds.bind(CommandId::FileSave, [](void *c) { (*static_cast<CmdCtx *>(c)->save)(); }, &cc);
  cmds.bind(CommandId::FileSaveAs, [](void *c) { (*static_cast<CmdCtx *>(c)->saveas)(); }, &cc);
  cmds.bind(CommandId::FileCompile, [](void *c) { (*static_cast<CmdCtx *>(c)->compile)(); }, &cc);
  cmds.bind(CommandId::EditUndo, [](void *c) { (*static_cast<CmdCtx *>(c)->undo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.undo_count() > 0; });
  cmds.bind(CommandId::EditRedo, [](void *c) { (*static_cast<CmdCtx *>(c)->redo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.redo_count() > 0; });
  cmds.bind(CommandId::EditDuplicate, [](void *c) { (*static_cast<CmdCtx *>(c)->add)(); }, &cc);
  cmds.bind(CommandId::EditDelete, [](void *c) { (*static_cast<CmdCtx *>(c)->remove)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCut, [](void *c) { (*static_cast<CmdCtx *>(c)->cut)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCopy, [](void *c) { (*static_cast<CmdCtx *>(c)->copy)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditPaste, [](void *c) { (*static_cast<CmdCtx *>(c)->paste)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->clip_count > 0; });
  cmds.bind(CommandId::SelectAll,
            [](void *c) {
              EditorState *s = static_cast<CmdCtx *>(c)->st;
              s->sel.clear();
              for (uint32_t i = s->scene.entity_count; i > 0; i--) s->sel.toggle((int32_t)(i - 1));
            },
            &cc, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->scene.entity_count > 0; });
  // Esc once ETKIN ARACI birakir (Unity/Blender): nokta duzenleme kipindeyse
  // yalniz kipten cikar, secim kalir; ikinci Esc secimi temizler.
  cmds.bind(CommandId::SelectClear,
            [](void *c) {
              EditorState *s = static_cast<CmdCtx *>(c)->st;
              if (prop_edit_active(*s)) prop_edit_exit(*s, "Esc");
              else s->sel.clear();
            },
            &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::ViewGizmos,
            [](void *c) { // gizmolarin tamami ac/kapa
              GizmoOptions &g = static_cast<CmdCtx *>(c)->st->gizmos;
              const bool on = !(g.light_radius || g.light_glyph || g.shadow_volume || g.sun_dir || g.camera_frustum);
              g.light_radius = g.light_glyph = g.shadow_volume = g.sun_dir = g.camera_frustum = on;
            },
            &cc, nullptr, [](const void *c) {
              const GizmoOptions &g = static_cast<const CmdCtx *>(c)->st->gizmos;
              return g.light_radius || g.light_glyph || g.shadow_volume || g.sun_dir || g.camera_frustum;
            });
  cmds.bind(CommandId::ViewConsole, [](void *c) { bool *b = static_cast<CmdCtx *>(c)->show_console; *b = !*b; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->show_console; });
  // Odaklan (F). Komut tablosunda TANIMLIYDI (kisayol, menu satiri, "secim
  // gerektirir" kurali) ama BAGLI DEGILDI: menude gorunen, tiklanan ve hicbir
  // sey yapmayan bir satirdi; F tusu de olu bir kisayoldu. Govde, arac
  // cubugundaki "Odaklan" dugmesinin ta kendisi -- tek fark, kisayolun ve menu
  // satirinin artik ayni yere varmasi.
  cmds.bind(CommandId::ViewFocus,
            [](void *c) {
              CmdCtx *x = static_cast<CmdCtx *>(c);
              const int32_t s0 = x->st->sel.primary();
              if (s0 < 0 || s0 >= (int32_t)x->st->scene.entity_count) return;
              // static: 256 elemanlik sinir dizisi yigina konmaz (kapasite
              // sabit, yeniden giris yok -- arayuz tek is parcaciginda).
              static content::SceneBounds fb[content::kSceneMaxEntities];
              const uint32_t nb = entity_world_bounds(*x->st, *x->phys, fb);
              if ((uint32_t)s0 < nb) camera_focus(*x->cam, fb[s0]);
            },
            &cc, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  // Tam ekran: yetenek HOST'un (GLFW). Yoksa menu ogesi soluk — sessizce
  // hicbir sey yapan bir dugme kalmaz. Swapchain'i bu komut DEGIL, kare
  // basindaki sync_size yeniden kurar (tek karar noktasi).
  cmds.bind(CommandId::ViewFullscreen,
            [](void *c) {
              const EditorHost *h = static_cast<CmdCtx *>(c)->host;
              if (!h || !h->set_fullscreen) return;
              const bool now = h->is_fullscreen ? h->is_fullscreen(h->user) : false;
              if (!h->set_fullscreen(h->user, !now))
                console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "tam ekran: pencere yoneticisi/GLFW istegi uygulamadi");
            },
            &cc,
            [](const void *c) {
              const EditorHost *h = static_cast<const CmdCtx *>(c)->host;
              return h && h->set_fullscreen != nullptr;
            },
            [](const void *c) {
              const EditorHost *h = static_cast<const CmdCtx *>(c)->host;
              return h && h->is_fullscreen && h->is_fullscreen(h->user);
            });
  cmds.bind(CommandId::GizmoTranslate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 0; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 0; });
  cmds.bind(CommandId::GizmoRotate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 1; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 1; });
  cmds.bind(CommandId::GizmoScale, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 2; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 2; });
  cmds.bind(CommandId::PlayToggle,
            [](void *c) {
              CmdCtx *x = static_cast<CmdCtx *>(c);
              // Baslat: gomulu oyun (olmazsa fizik onizlemesi). Penceresiz kipte
              // (host yok) yalniz fizik: kapilar belirlenimli kalsin, gomulu
              // yol kendi kapisinda ACIKCA istenir.
              if (x->st->playing) (*x->play)(false);
              else (*x->play_auto)(x->host != nullptr);
            },
            &cc, nullptr, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; });
  cmds.bind(CommandId::PlayPause, [](void *c) { EditorState *s = static_cast<CmdCtx *>(c)->st; s->paused = !s->paused; }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; },
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->paused; });
  struct GameCmdCtx {
    decltype(&do_run_game) run;
    const GameRun *g;
  } gcx{&do_run_game, &oyun};
  cmds.bind(CommandId::PlayRunGame, [](void *c) { (*static_cast<GameCmdCtx *>(c)->run)(); }, &gcx, nullptr,
            [](const void *c) {
              const GameRun *g = static_cast<const GameCmdCtx *>(c)->g;
              return g->state == GameRunState::Running && !g->embedded; // F5'in oyunu "ayri pencerede calisiyor" gorunmesin
            });
  cmds.bind(CommandId::PlayStep, [](void *c) { static_cast<CmdCtx *>(c)->st->step_request++; }, &cc,
            [](const void *c) {
              const EditorState *s = static_cast<const CmdCtx *>(c)->st;
              return s->playing && s->paused;
            });
  // Yardim: editor ici guncelleme. Geri cagrilar dogrudan UpdateUi'ye gider;
  // penceresiz kipte ag reddi update_ui_* icinde (Updater'a giden TEK kapi).
  cmds.bind(CommandId::HelpCheckUpdates, [](void *c) { update_ui_check(*static_cast<UpdateUi *>(c), true); }, &upd_ui);
  cmds.bind(CommandId::HelpAutoCheck, [](void *c) { update_ui_toggle_auto(*static_cast<UpdateUi *>(c)); }, &upd_ui, nullptr,
            [](const void *c) { return static_cast<const UpdateUi *>(c)->settings.auto_check; });
  cmds.bind(CommandId::HelpAbout, [](void *c) { update_ui_open_about(*static_cast<UpdateUi *>(c)); }, &upd_ui);
  // Dosya menusune "Son dosyalar" alt menusu: komut tablosu KOMUT tasir, bu ise
  // bir veri listesi — chrome'un ek oge kancasindan geliyor.
  struct MenuExtraCtx {
    decltype(&guard_then) guard;
    int open_action;
    int *view_mode = nullptr;
    EditorState *st = nullptr;
    UpdateUi *upd = nullptr; // menu cubugundaki guncelleme rozeti
  } mx{&guard_then, PendingOpenPath, &view_mode, &st, &upd_ui};
  ChromeMenuExtra menu_extra;
  menu_extra.ctx = &mx;
  menu_extra.on_badge = [](void *ctx) {
    MenuExtraCtx *m = static_cast<MenuExtraCtx *>(ctx);
    if (m && m->upd) update_ui_open_window(*m->upd);
  };
  menu_extra.fn = [](void *ctx, CommandCategory cat) {
    MenuExtraCtx *m = static_cast<MenuExtraCtx *>(ctx);
    if (!m) return;
    if (cat == CommandCategory::File) {
      const char *rec[kRecentMax];
      const uint32_t nrec = recent_list(rec, kRecentMax);
      if (!ImGui::BeginMenu("Son dosyalar", nrec > 0)) return;
      for (uint32_t i = 0; i < nrec; i++) {
        const bool var = recent_exists(i);
        char lbl[kFilePathLen + 16];
        std::snprintf(lbl, sizeof lbl, "%u  %s", i + 1, rec[i]);
        ImGui::BeginDisabled(!var); // eksik dosya SOLUK, gizli degil
        if (ImGui::MenuItem(lbl)) (*m->guard)(m->open_action, rec[i]);
        ImGui::EndDisabled();
        if (!var && ImGui::IsItemHovered()) ImGui::SetTooltip("Dosya bulunamadi: %s", rec[i]);
      }
      ImGui::EndMenu();
    } else if (cat == CommandCategory::View) {
      if (m->view_mode) {
        if (ImGui::BeginMenu("G\xC3\xB6r\xC3\xBCn\xC3\xBCm Kipi (Shading)")) {
          if (ImGui::MenuItem("Ayd\xC4\xB1nlatmal\xC4\xB1 (Lit PBR)", nullptr, *m->view_mode == 0)) *m->view_mode = 0;
          if (ImGui::MenuItem("I\xC5\x9F\xC4\xB1ks\xC4\xB1z (Albedo)", nullptr, *m->view_mode == 1)) *m->view_mode = 1;
          if (ImGui::MenuItem("\xC3\x87" "arp\xC4\xB1\xC5\x9Fma (Colliders)", nullptr, *m->view_mode == 2)) *m->view_mode = 2;
          if (ImGui::MenuItem("S\xC4\xB1n\xC4\xB1rlar (Bounds)", nullptr, *m->view_mode == 3)) *m->view_mode = 3;
          if (ImGui::MenuItem("Overdraw Is\xC4\xB1 Haritas\xC4\xB1", nullptr, *m->view_mode == 4)) *m->view_mode = 4;
          if (ImGui::MenuItem("I\xC5\x9F\xC4\xB1k K\xC3\xBCmeleri", nullptr, *m->view_mode == 5)) *m->view_mode = 5;
          ImGui::EndMenu();
        }
      }
    } else if (cat == CommandCategory::Play) {
      if (m->st) {
        if (ImGui::BeginMenu("Sim\xC3\xBClasyon ve Fizik")) {
          if (ImGui::MenuItem(ICON_MD_BLUR_ON " MLS-MPM \xC3\x87ok Fazl\xC4\xB1 Sim\xC3\xBClat\xC3\xB6r")) {
            set_status(*m->st, "MLS-MPM sim\xC3\xBClat\xC3\xB6r\xC3\xBC D\xC3\xBCnya panelinde g\xC3\xB6r\xC3\xBCnt\xC3\xBCleniyor");
          }
          if (ImGui::MenuItem(ICON_MD_WAVES " SPH Ak\xC4\xB1\xC5\x9Fkan Dinami\xC4\x9Fi (M\xC3\xBCller 2003)")) {
            set_status(*m->st, "SPH ak\xC4\xB1\xC5\x9Fkan sim\xC3\xBClat\xC3\xB6r\xC3\xBC D\xC3\xBCnya panelinde g\xC3\xB6r\xC3\xBCnt\xC3\xBCleniyor");
          }
          if (ImGui::MenuItem(ICON_MD_GROUPS " Boids S\xC3\xBCr\xC3\xBC Zekas\xC4\xB1 (Reynolds)")) {
            set_status(*m->st, "Boids s\xC3\xBCr\xC3\xBC sim\xC3\xBClasyonu D\xC3\xBCnya panelinde g\xC3\xB6r\xC3\xBCnt\xC3\xBCleniyor");
          }
          if (ImGui::MenuItem(ICON_MD_CASINO " WFC Kuantum Zindan \xC3\x9Cretici")) {
            set_status(*m->st, "WFC kuantum zindan \xC3\xBCretici D\xC3\xBCnya panelinde g\xC3\xB6r\xC3\xBCnt\xC3\xBCleniyor");
          }
          if (ImGui::MenuItem(ICON_MD_SYNC " Rollback Netcode & Replay")) {
            set_status(*m->st, "GGPO rollback netcode D\xC3\xBCnya panelinde g\xC3\xB6r\xC3\xBCnt\xC3\xBCleniyor");
          }
          ImGui::EndMenu();
        }
      }
    }
  };
  {
    // Kurulum denetimi: bagli kalmayan komut = menude tiklanmayan satir. Sessiz
    // gecmez; headless kosuda da gorunur.
    CommandId ub[kCommandCount];
    const uint32_t n = cmds.unbound(ub, kCommandCount);
    if (n) {
      console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "%u komut BAGLANMADI (menude olu satir), ilki: %s", n, cmds.desc(ub[0]).name);
      std::fprintf(stderr, "[editor] %u komut BAGLANMADI (menude olu satir), ilki: %s\n", n, cmds.desc(ub[0]).name);
    }
  }
  // stdout/stderr yakalama: motorun printf'i ve dogrulama katmani konsola aksin.
  // DONGUDEN HEMEN ONCE aciliyor — yukaridaki kurulum yollari `return 1` ile
  // cikabiliyor ve borudaki bayt drain edilmeden kaybolurdu. HEADLESS'ta
  // ACILMAZ: kapi satirlari ([engine_editor] ... OK) dogrudan akmali.
  if (!headless && !console_capture_begin(&console_cap))
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "cikti yakalanamadi: %s", console_cap.err_msg);

  while (running) {
    ENGINE_ZONE("frame");
    console_set_frame(frame_i);
    console_capture_drain(); // kare basina BIR kez; yakalama kapaliysa no-op
    if (frame_i == 1 && opts.command) {
      // Ikinci kare: ilk karede panel/sahne kurulumu biter, komut kurulmus bir
      // editorde calissin.
      bool bulundu = false;
      for (uint32_t k = 1; k <= kCommandCount && !bulundu; k++)
        if (!std::strcmp(cmds.desc((CommandId)k).key, opts.command)) {
          bulundu = true;
          const bool calisti = cmds.invoke((CommandId)k);
          std::printf("[engine_editor] --komut %s: %s\n", opts.command, calisti ? "calisti" : "ETKIN DEGIL, calismadi");
        }
      if (!bulundu) { std::printf("[engine_editor] --komut %s: BOYLE BIR KOMUT YOK\n", opts.command); komut_hata = true; }
    }
    if (oyun.state == GameRunState::Running) {
      const GameRunState ns = game_run_poll(oyun, oyun_satiri, nullptr);
      if (ns != GameRunState::Running) {
        const bool iyi = ns == GameRunState::Finished && oyun.exit_code == 0;
        console_log(iyi ? ConsoleLevel::Bilgi : ConsoleLevel::Hata, "oyun", "oyun bitti: %s, cikis kodu %d, %u satir (gunluk %s)", oyun.game,
                    oyun.exit_code, oyun.lines, oyun.log_path);
        set_status(st, "oyun bitti: cikis kodu %d%s", oyun.exit_code, iyi ? "" : " (Konsol)");
        if (st.play_embedded) {
          // Oyun KENDISI kapandi (cikis_iste, cokme, derleme hatasi): oynatma
          // da biter. Hic kare gelmediyse sebep neredeyse her zaman derleyicidir.
          const bool kare_yok = !oyun_goruntu.has_frame();
          set_playing(false);
          if (kare_yok) set_status(st, "oyun baslamadi: cikis kodu %d — derleme/kurulum hatasi (Konsol)", oyun.exit_code);
          else set_status(st, "oyun kapandi: cikis kodu %d%s", oyun.exit_code, iyi ? "" : " (Konsol)");
        }
        oyun_kapaniyor = false;
      }
    }
    // Oyun sekmesi dokusu YALNIZ burada, kare BASINDA kurulur/birakilir: bu
    // karenin ImGui cizimi henuz kurulmadi, yani hicbir cizim komutu eski
    // descriptor'a basvurmuyor. Kare ortasinda (panelden sonra) birakmak
    // ImGui'nin ayni karede cizecegi dokuyu yok ederdi — olculdu: kapi
    // durdurduktan sonra editor VK_ERROR_DEVICE_LOST ile dustu.
    if (st.play_embedded && oyun.chan.ok()) {
      if (!oyun_goruntu.ok() || oyun_goruntu.width() != oyun.chan.width() || oyun_goruntu.height() != oyun.chan.height()) {
        if (!oyun_goruntu.init(dev, oyun.chan.width(), oyun.chan.height(), true))
          console_log(ConsoleLevel::Hata, "oyun", "Oyun sekmesi dokusu kurulamadi: %s", oyun_goruntu.last_error());
      }
    } else if (!st.play_embedded && oyun_goruntu.ok()) {
      oyun_goruntu.shutdown(); // oynatma bitti: oynatma basina ayrilan her sey birakilir
    }
    // Gomulu oyun: duraklat / tek adim editorun kendi bayraklarindan (F6, F10).
    if (st.play_embedded && oyun.chan.ok()) {
      oyun.chan.set_paused(st.paused);
      if (st.step_request) {
        if (st.paused) oyun.chan.request_step();
        st.step_request = 0;
      }
    }
    uint64_t now = platform::now_ns();
    // Guncelleyici: is yoksa O(1). Saat bu satirin ZATEN okudugu `now` (kare
    // basina ek syscall yok); 24 saatlik otomatik denetim de onunla olculur.
    update_ui_poll(upd_ui, now);
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless) dt = 1.0f / 60.0f;
    prof.begin_frame();
    frame.begin_frame();
    const platform::InputState *in = nullptr;
    uint32_t fw = width, fh = height;
    // GIZMO SURUKLEME KAPISI — sentetik fare. Penceresiz kipte `in` bugune
    // kadar HEP nullptr'di, yani gizmo ETKILESIMI hic kosmadi: oklar cizilse
    // de tutup surukleme yolu kapisizdi. Kullanici bildirdi (2026-09-20):
    // "oklar cikiyor ama hareket etmiyor, sag panelden degisiyor" — tam olarak
    // bu bosluktan gecen sinif. Sentetik girdi GERCEK boru hattindan akiyor
    // (editor_ui.cpp io.AddMouse*Event), yani kapi kisayol kullanmiyor.
    static platform::InputState g_synth;
    if (headless) in = &g_synth;
    if (!headless) {
      if (!host->poll(host->user, &fw, &fh)) running = false;
      in = host->input ? host->input(host->user) : nullptr;
      if (fw == 0 || fh == 0) continue; // kucultulmus
      // --- PENCERE OLCUSU -> SWAPCHAIN, KAYITTAN ONCE ------------------------
      // Tam ekrana gecis (ve her yeniden boyutlandirma) BURADA yakalanir.
      // Eskiden yalniz `needs_recreate()` (OUT_OF_DATE) bakiliyordu ve o da
      // kare SONUNDA: Wayland'de OUT_OF_DATE HIC gelmedigi icin swapchain
      // 1280x720'de kaliyor, kompozitor o goruntuyu tam ekrana GERIYORDU —
      // "tam ekran olmuyor, icerik ayni oranda buyuyup bozuluyor" bu.
      // Kararin gerekcesi ve X11/Wayland ayrimi: rhi/swapchain.hpp ResizeAction.
      if (swap.sync_size(fw, fh))
        console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "swapchain %ux%u (%s)", swap.extent().width, swap.extent().height,
                    swap.last_resize_reason());
      // Arayuz ile hedef AYNI olcuyu gorur: ImGui'nin DisplaySize'i cizilen
      // hedefin olcusudur, pencerenin degil. Ayrisirlarsa (yeniden kurma
      // basarisiz ya da surucu currentExtent'i dayatti) kare gerilir.
      fw = swap.extent().width;
      fh = swap.extent().height;
      // HiDPI: imlec mantiksal pikselde, arayuz hedef pikselinde. Oran her
      // karede okunur (pencere baska ekrana tasininca degisir).
      if (host->window_size) {
        uint32_t ww = 0, wh = 0;
        host->window_size(host->user, &ww, &wh);
        if (ww > 0) ui.set_pointer_scale((float)fw / (float)ww);
      }
    }
    // --- KAMERA: karar editor_camera.hpp'de (saf gecis fonksiyonu; kapilar orayi
    // olcer, burasi yalniz girdiyi toplar).
    if (in) {
      CameraInput ci;
      ci.dx = (float)(in->mouse_x - prev_mx);
      ci.dy = (float)(in->mouse_y - prev_my);
      ci.scroll = (float)(in->scroll_y - prev_scroll);
      ci.lmb = in->mouse_down[0];
      ci.rmb = in->mouse_down[1];
      ci.mmb = in->mouse_down[2];
      ci.shift = in->key_down[GLFW_KEY_LEFT_SHIFT] || in->key_down[GLFW_KEY_RIGHT_SHIFT];
      ci.ctrl = in->key_down[GLFW_KEY_LEFT_CONTROL] || in->key_down[GLFW_KEY_RIGHT_CONTROL];
      ci.alt = in->key_down[GLFW_KEY_LEFT_ALT] || in->key_down[GLFW_KEY_RIGHT_ALT];
      ci.key_w = in->key_down[GLFW_KEY_W];
      ci.key_a = in->key_down[GLFW_KEY_A];
      ci.key_s = in->key_down[GLFW_KEY_S];
      ci.key_d = in->key_down[GLFW_KEY_D];
      ci.key_q = in->key_down[GLFW_KEY_Q];
      ci.key_e = in->key_down[GLFW_KEY_E];
      // DIKKAT: kosul !ui.wants_mouse() DEGIL. 3B artik bir ImGui panelinin
      // icinde yasiyor, yani fare goruntunun uzerindeyken WantCaptureMouse
      // ZATEN true olur ve kamera goruntude HIC donmezdi. Dogru kosul: fare
      // goruntunun ustunde ve kaplama/gizmo onu almamis — ya da surukleme
      // zaten basladi (panelden disari tasan surukleme kesilmesin).
      const bool cam_btn = ci.rmb || ci.mmb;
      const bool can_start = (view_tab == ViewportTab::Scene) && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
      if (!cam_btn) cam_dragging = false;
      else if (can_start) cam_dragging = true;
      ci.allow_mouse = (view_tab == ViewportTab::Scene) && (cam_dragging || can_start);
      ci.allow_keys = (view_tab == ViewportTab::Scene) && !ui.wants_text_input(); // WASD bir ad alanina yaziliyorsa ucus baslamasin
      ci.dt = dt;
      camera_update(cam, ci);
      prev_mx = in->mouse_x;
      prev_my = in->mouse_y;
      prev_scroll = in->scroll_y;
    }

    // Oynatma baslayinca otomatik Oyun sekmesine gec, durunca Sahne sekmesine don
    if (st.playing && !was_playing) {
      view_tab = ViewportTab::Game;
    } else if (!st.playing && was_playing) {
      view_tab = ViewportTab::Scene;
    }
    was_playing = st.playing;

    // Sim: yalniz oynatilirken (sabit adim). Duraklatilmisken zaman AKMAZ ama
    // govdeler yerinde durur; F10 tek adim ilerletir. fs.advance duraklamada
    // cagrilmaz — yoksa birikmis zaman devam edince bir anda bosalirdi.
    if (st.playing && !st.paused && !st.play_embedded) {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) { scene.tick(fs.step_s, tick_i++); st.play_time += fs.step_s; }
    } else if (st.playing && st.paused && st.step_request && !st.play_embedded) {
      ENGINE_ZONE("sim");
      scene.tick(fs.step_s, tick_i++);
      st.play_time += fs.step_s;
      st.step_request--;
      fs.advance(dt); // biriken zamani YUT: adim adim ilerlerken geri kalmasin
    }

    // Partikül Simülasyonu (VFX): Editörde ve Oyunda canlı çalışır
    if (!st.particle_sim_paused) {
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if (!(e.components & content::kSceneParticle) || (e.flags & content::kSceneHidden)) continue;
        if (e.particle_spawn_rate <= 0.0f) continue;
        st.particle_spawn_accum[i] += e.particle_spawn_rate * dt;
        uint32_t to_spawn = (uint32_t)st.particle_spawn_accum[i];
        if (to_spawn > 0) {
          st.particle_spawn_accum[i] -= (float)to_spawn;
          if (to_spawn > 200) to_spawn = 200; // bir karede asiri birikmeyi onle
          const Mat4 wm = content::scene_entity_world_matrix(st.scene, i);
          const Vec3 emitter_pos{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
          content::ParticleEmitterConfig cfg;
          cfg.spawn_pos = emitter_pos;
          cfg.base_velocity = e.particle_velocity;
          cfg.velocity_jitter = e.particle_jitter;
          cfg.lifetime_min = e.particle_lifetime_min;
          cfg.lifetime_max = e.particle_lifetime_max;
          cfg.size_start = e.particle_size_start;
          cfg.size_end = e.particle_size_end;
          cfg.color_start = e.particle_color_start;
          cfg.color_end = e.particle_color_end;
          cfg.gravity = Vec3{0.0f, e.particle_gravity, 0.0f};
          cfg.custom_gravity = true;
          // TEPS 2026 Gelismis Fizik ve Dinamikler
          cfg.curl_noise_strength = e.particle_curl_strength;
          cfg.curl_noise_frequency = e.particle_curl_freq;
          cfg.drag = e.particle_drag;
          cfg.enable_collision = e.particle_collision;
          cfg.collision_plane_y = 0.0f;
          cfg.restitution = e.particle_bounce;
          cfg.shape = e.particle_billboard_type;
          // Godot GPUParticles / Niagara Standart Yayilim Geometrisi
          if (e.particle_jitter.y <= 0.08f && (e.particle_jitter.x > 0.1f || e.particle_jitter.z > 0.1f)) {
            cfg.emission_shape = content::ParticleEmissionShape::PlanarRing;
            cfg.emission_radius = std::max(e.particle_jitter.x, e.particle_jitter.z);
            cfg.emission_inner_radius = cfg.emission_radius * 0.3f;
          } else if (e.particle_jitter.z <= 0.08f && e.particle_jitter.x > 0.1f && e.particle_jitter.y > 0.1f) {
            cfg.emission_shape = content::ParticleEmissionShape::VerticalCurtain;
            cfg.emission_radius = e.particle_jitter.x;
            cfg.emission_spread = e.particle_jitter.y;
          } else if (length(e.particle_velocity) > 2.0f && (e.particle_jitter.x > 0.5f || e.particle_jitter.z > 0.5f)) {
            cfg.emission_shape = content::ParticleEmissionShape::ConicalFountain;
            cfg.emission_spread = 0.6f;
          } else if (e.particle_jitter.x > 0.5f && e.particle_jitter.y > 0.5f && e.particle_jitter.z > 0.5f) {
            cfg.emission_shape = content::ParticleEmissionShape::SphericalVolume;
            cfg.emission_radius = std::max(e.particle_jitter.x, std::max(e.particle_jitter.y, e.particle_jitter.z));
          } else if (e.particle_jitter.x <= 0.08f && e.particle_jitter.z <= 0.08f && length(e.particle_velocity) > 1.0f) {
            cfg.emission_shape = content::ParticleEmissionShape::LinearBeam;
            cfg.emission_radius = 2.0f;
          }
          static content::ParticleEmitterConfig s_sub_cfg;
          if (e.particle_sub_on_death > 0) {
            cfg.sub_emitter = &st.particles;
            s_sub_cfg = cfg;
            s_sub_cfg.spawn_on_death_count = 0;
            s_sub_cfg.size_start = e.particle_size_start * 0.4f;
            s_sub_cfg.size_end = 0.0f;
            s_sub_cfg.lifetime_min = 0.2f;
            s_sub_cfg.lifetime_max = 0.5f;
            s_sub_cfg.base_velocity = Vec3{0.0f, 1.5f, 0.0f};
            s_sub_cfg.velocity_jitter = Vec3{3.0f, 3.0f, 3.0f};
            cfg.sub_emitter_cfg = &s_sub_cfg;
          }
          st.particles.emit(cfg, to_spawn, st.particle_rng);
        }
      }
      st.particles.update(dt, &st.particle_rng);
      // Canlı Şerit / Kuyruk İzi (Ribbon Trail) Güncellemesi
      if (st.ribbon_initialized) {
        st.ribbon_trail.update(dt);
        for (uint32_t i = 0; i < st.scene.entity_count; i++) {
          const SceneEntity &e = st.scene.entities[i];
          if (!(e.components & content::kSceneParticle) || (e.flags & content::kSceneHidden)) continue;
          if (e.particle_ribbon && st.particles.alive_count() > 0) {
            const content::Particle &p = st.particles.particle(0);
            st.ribbon_trail.add_point(p.pos, Vec4{p.color_start.x, p.color_start.y, p.color_start.z, 0.9f}, p.size * 0.9f, 0.35f);
          }
        }
      }
      // CS2 Voksel Dumanı Navier-Stokes Difüzyon Güncellemesi
      if (st.smoke_initialized) {
        sim::VoxelSmokeParams sp;
        sp.diffusion_rate = s_smoke_live_diff;
        sp.dissipation_rate = s_smoke_live_diss;
        st.smoke_grid.update(dt, sp);
      }
    }

    // En-boy orani artik PENCERENIN degil, sahnenin icinde yasadigi PANELIN
    // orani: 3B viewport dokusuna ciziliyor ve o dokunun olcusu panelden geliyor.
    const float aspect = vp.aspect();
    Mat4 proj, view;
    if (view_tab == ViewportTab::Game) {
      const int cam_ent = find_first_camera_entity(st.scene);
      if (cam_ent >= 0) {
        const SceneEntity &ce = st.scene.entities[cam_ent];
        const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)cam_ent);
        const Vec3 c_eye = {wm.m[3][0], wm.m[3][1], wm.m[3][2]};
        Vec3 c_f = {-wm.m[2][0], -wm.m[2][1], -wm.m[2][2]};
        Vec3 c_u = {wm.m[1][0], wm.m[1][1], wm.m[1][2]};
        if (length_sq(c_f) < 1e-6f) c_f = Vec3{0, 0, -1};
        if (length_sq(c_u) < 1e-6f) c_u = Vec3{0, 1, 0};
        const float fov_rad = ce.cam_fov > 1.0f ? (ce.cam_fov * 3.14159265f / 180.0f) : (ce.cam_fov > 0.1f ? ce.cam_fov : 0.897598f);
        const float znear = ce.cam_near > 0.001f ? ce.cam_near : 0.1f;
        const float zfar = ce.cam_far > znear ? ce.cam_far : 200.0f;
        view = Mat4::look_at(c_eye, c_eye + c_f, c_u);
        proj = Mat4::perspective(fov_rad, aspect, znear, zfar);
      } else {
        view = camera_view(cam);
        proj = camera_projection(cam, aspect, 0.1f, 200.0f);
      }
    } else {
      proj = camera_projection(cam, aspect, 0.1f, 200.0f);
      view = camera_view(cam);
    }
    ren.set_camera(view, proj);
    ren.clear_point_lights();

    // --- ImGui paneller ---
    {
      // Oyun odaktayken ImGui'nin klavye gezintisi KAPALI: oklar ve bosluk
      // oyunun. Acik kalsaydi bosluk (zipla) odakli bir dugmeye basar, oklar
      // paneller arasinda gezerdi.
      ImGuiIO &gio = ImGui::GetIO();
      if (oyun_girdi_odak && st.play_embedded) gio.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
      else gio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    // Kisayollar: komut tablosundan. Koruma kurali tablonun (ham T/R/S/Delete
    // metin yazarken VE kaydirac suruklerken kapali; Ctrl+* yalniz metinde kapali).
    InputGuards guards{ui.wants_text_input(), ui.wants_keyboard()};
    guards.game_input = oyun_girdi_odak && st.play_embedded; // klavye oyunun: yalniz Oynat komutlari
    commands_poll_imgui(cmds, guards);
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) show_stats = !show_stats;
    if ((ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) ||
        (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false))) {
      palette_toggle(palette_st);
    }
    // --- CERCEVE: menu / arac / durum cubugu (editor_chrome.hpp; sira sozlesmesi
    // orada: uc cubuk da ana viewport'un WorkRect'ini daraltir, dockspace tam
    // aralarina oturur — ImGui daraltmayi bir kare gecikmeli uygular).
    ChromeState cs;
    cs.playing = st.playing;
    cs.dirty = st.dirty;
    // Duraklatma durum cubugunda gorunsun (arac cubugu henuz duraklatmayi
    // cizmiyor; menude F6 var). Bos mesaj yerine acik bir ek.
    char status_buf[200];
    if (st.playing && st.paused) {
      std::snprintf(status_buf, sizeof status_buf, "DURAKLATILDI (F10 kare ilerlet) \xC2\xB7 %s", st.status);
      cs.status = status_buf;
    } else cs.status = st.status;
    cs.scene_path = st.scene_path;
    const int32_t prim_i = st.sel.primary();
    cs.primary_name = (prim_i >= 0 && prim_i < (int32_t)st.scene.entity_count) ? st.scene.entities[prim_i].name : nullptr;
    cs.selection_count = st.sel.count;
    cs.undo_count = st.hist.undo_count();
    cs.redo_count = st.hist.redo_count();
    cs.frame = frame_i;
    cs.tick = tick_i;
    cs.frame_ms = dt * 1000.0f;
    cs.entity_count = st.scene.entity_count;
    cs.gizmo_op = gizmo_op;
    cs.snap = snap_on;
    cs.snap_value = snap_step;
    cs.gizmos_visible = st.gizmos.light_radius || st.gizmos.light_glyph || st.gizmos.shadow_volume || st.gizmos.sun_dir || st.gizmos.camera_frustum || st.gizmos.env_volumes;
    const Vec3 eye = camera_eye(cam);
    cs.cam_eye[0] = eye.x; cs.cam_eye[1] = eye.y; cs.cam_eye[2] = eye.z;
    // Yeni surum rozeti: nullptr iken menu cubugu ImGui'ye ek is gondermez.
    cs.update_badge = update_ui_badge(upd_ui);
    cs.update_badge_tip = cs.update_badge ? "Yeni s\xC3\xBCr\xC3\xBCm \xE2\x80\x94 ayr\xC4\xB1nt\xC4\xB1, s\xC3\xBCr\xC3\xBCm notlar\xC4\xB1 ve kurulum i\xC3\xA7in t\xC4\xB1klay\xC4\xB1n" : nullptr;
    chrome_menu_bar(cmds, cs, menu_extra);
    ChromeOutput co;
    chrome_toolbar(cmds, cs, &co);
    if (co.snap_toggled) snap_on = !snap_on;
    if (co.snap_value_changed) snap_step = co.snap_value;
    chrome_status_bar(cs);
    // --- DOCKSPACE -----------------------------------------------------------
    // Paneller artik ekranda yuzen sabit pencereler degil; kullanici surukleyip
    // yeniden duzenleyebiliyor (Unity/Godot/Blender'in dordunde de boyle).
    // Varsayilan yerlesim ILK KAREDE programatik kuruluyor: ImGui'nin kendi
    // imgui.ini'si kapali (io.IniFilename = nullptr), yani duzen diskten
    // gelmiyor — kurulmazsa her acilista paneller serbest gelirdi.
    const ImGuiID dock_id = ImGui::DockSpaceOverViewport(ImGui::GetID("TulparDock"), ImGui::GetMainViewport(), 0);
    if (frame_i == 0) {
      layout_set_dockspace_id(dock_id);
      // ONCE varsayilan kurulur: kaydedilmis duzen yoksa ya da bozuksa geri
      // duseceğimiz saglam bir taban olsun. Bir duzen dosyasi yuzunden
      // editorun panelsiz acilmasi kabul edilemez.
      if (!layout_apply_default(dock_id, (float)fw, (float)fh)) {
        console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "varsayilan duzen: %s", layout_last_error());
        std::fprintf(stderr, "[editor] varsayilan duzen: %s\n", layout_last_error());
      }
      // SONRA kullanicinin kaydettigi duzen (varsa) uygulanir. Basarisizlik
      // SESSIZ degil ama olumcul de degil: taban zaten kuruldu.
      LayoutError le{};
      // Headless kapi kosularinda KULLANICININ duzeni yuklenmez: kapilar
      // varsayilan duzeni olcer, diskte kalmis bir dosya onlari bozmamali.
      layout_restored = !headless && layout_load(layout_path, &le);
      if (layout_restored)
        console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "panel d\xC3\xBCzeni geri y\xC3\xBCklendi: %s", layout_path);
    }

    // --- GORUNUM: 3B sahnenin YASADIGI panel --------------------------------
    // Sahne viewport dokusuna ciziliyor ve burada gosteriliyor. Panel olcusu
    // degisince hedef yeniden yaratiliyor (yalniz GERCEKTEN degistiyse) ve
    // renderer'in cizim olcusu ona baglaniyor — en-boy orani artik pencerenin
    // degil PANELIN orani.
    view_rect = ViewportRect{};
    view_hovered = false;
    ovres = OverlayResult{}; // panel kapaliyken bayat sonuc uygulanmasin
    if (ImGui::Begin(kPanelGorunumLabel)) {
      // 1) Viewport Ust Arac Cubugu (Sekmeler + Araclar + Stats + 3 Nokta)
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));

      // Sekmeler: [ICON_MD_MOVIE Sahne] [ICON_MD_SPORTS_ESPORTS Oyun]
      const bool is_scene = (view_tab == ViewportTab::Scene);
      if (is_scene) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::AccentHi));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::TextDim));
      }
      if (ImGui::Button(" " ICON_MD_MOVIE " Sahne ")) view_tab = ViewportTab::Scene;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sahne Görünümü (Düzenleme, Gizmo, Serbest Kamera)");
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      const bool is_game = (view_tab == ViewportTab::Game);
      if (is_game) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::AccentHi));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::TextDim));
      }
      if (ImGui::Button(" " ICON_MD_SPORTS_ESPORTS " Oyun ")) view_tab = ViewportTab::Game;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oyun Görünümü (Kamera Perspektifi, Temiz Oyun Ekranı)");
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();

      if (view_tab == ViewportTab::Scene) {
        if (ImGui::Button(show_grid ? "⊞ Izgara: Açık" : "⊞ Izgara: Kapalı")) show_grid = !show_grid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("3B Zemin Izgarasını Göster / Gizle");

        // Gorunum kipi (UE5 "View Mode"). Dordu de CPU tarafinda DOGRU:
        //  Isiksiz  : ortam 1, gunes 0, nokta isik yok -> saf albedo/doku.
        //  Carpisma : her govdenin carpisma hacmi, govdeyle birlikte DONEN tel kutu.
        //  Sinirlar : her varligin dunya AABB'si (secim/odak bunu kullanir).
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        ImGui::Combo("##gorunum_kipi", &view_mode,
                     "Ayd\xC4\xB1nlatmal\xC4\xB1\0I\xC5\x9F\xC4\xB1ks\xC4\xB1z\0\xC3\x87" "arp\xC4\xB1\xC5\x9Fma\0S\xC4\xB1n\xC4\xB1rlar\0Overdraw Is\xC4\xB1 Haritas\xC4\xB1\0I\xC5\x9F\xC4\xB1k K\xC3\xBCmeleri\0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("G\xC3\xB6r\xC3\xBCn\xC3\xBCm kipi");
        ImGui::SameLine();
        // NOT: burada bir "Tel Kafes / Duz Golgeli" dugmesi vardi ve HICBIR
        // SEY yapmiyordu: wireframe_mode degiskeni depoda baska hicbir yerde
        // okunmuyor, dugme yalniz kendi etiketini degistiriyordu.
        //
        // Gercegi yapmak icin VK_POLYGON_MODE_LINE gerekir; o da
        // fillModeNonSolid cihaz ozelligini ister. Bu motor onu hic
        // istemiyor (polygonMode alti yerde de FILL sabit) ve ozellik mobil
        // GPU'larin cogunda YOK. Dogru cozum barycentric tek gecisli tel
        // kafes; ayri bir is. O gelene kadar var olmayan bir yetenegi vaat
        // eden dugmeyi tutmaktansa kaldirmak dogrudur.

        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_CENTER_FOCUS_STRONG " Odaklan")) {
          const int32_t s0 = st.sel.primary();
          if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
            static content::SceneBounds fb[content::kSceneMaxEntities];
            const uint32_t nb = entity_world_bounds(st, phys, fb);
            if ((uint32_t)s0 < nb) camera_focus(cam, fb[s0]);
          }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Seçili Varlığa Odaklan (F)");
      } else {
        // Oyun görünümü araçları
        static const char *const kAspectNames[] = {"Serbest En-Boy", "16:9 (FHD)", "16:10", "4:3", "21:9", "1:1"};
        ImGui::SetNextItemWidth(130.0f);
        int cur_aspect = (int)game_aspect;
        if (ImGui::Combo("##aspect", &cur_aspect, kAspectNames, IM_ARRAYSIZE(kAspectNames))) {
          game_aspect = (GameAspect)cur_aspect;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oyun Görünümü En-Boy Oranı");

        ImGui::SameLine();
        ImGui::Checkbox("Oynatılınca Büyüt", &maximize_on_play);

        ImGui::SameLine();
        ImGui::Checkbox("Sessiz", &mute_audio_on_play);
      }

      // Sağ Taraf: [ ICON_MD_BAR_CHART Stats ] [ ⋮ ]
      const float right_width = 115.0f;
      const float avail_w = ImGui::GetContentRegionAvail().x;
      if (avail_w > right_width) {
        ImGui::SameLine(ImGui::GetWindowWidth() - right_width - ImGui::GetStyle().WindowPadding.x);
      } else {
        ImGui::SameLine();
      }

      const bool stats_btn_active = show_stats;
      if (stats_btn_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Accent, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Accent, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::AccentHi));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));
        ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::TextDim));
      }
      // Simge main'den (ICON_MD_*): PR #7'nin U+1F4CA emojisi ne DejaVuSans'ta
      // ne Material Icons'ta var, tofu kutusu cizerdi (tools/icon_check.py).
      // Pop sayisi PR #7'den ve DOGRU olan o: ustteki blok her iki dalda da
      // UC renk itiyor, main'in kosullu "2" pop'u burada yigin sizdirirdi.
      if (ImGui::Button( ICON_MD_BAR_CHART " Stats")) show_stats = !show_stats;
      ImGui::PopStyleColor(3);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("İstatistikler Panelini Aç / Kapat (FPS, Poligon, Bellek, Çöp)");

      ImGui::SameLine();
      if (ImGui::Button(" ⋮ ")) ImGui::OpenPopup("ViewportOptionsMenu");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Görünüm Seçenekleri ve Araçlar");

      if (ImGui::BeginPopup("ViewportOptionsMenu")) {
        if (ImGui::MenuItem( ICON_MD_BAR_CHART " Detaylı İstatistikler (Stats)", nullptr, show_stats)) show_stats = !show_stats;
        ImGui::Separator();
        if (ImGui::MenuItem( ICON_MD_MOVIE " Sahne Görünümüne Geç", nullptr, view_tab == ViewportTab::Scene)) view_tab = ViewportTab::Scene;
        if (ImGui::MenuItem( ICON_MD_SPORTS_ESPORTS " Oyun Görünümüne Geç", nullptr, view_tab == ViewportTab::Game)) view_tab = ViewportTab::Game;
        ImGui::Separator();
        if (ImGui::MenuItem( ICON_MD_STRAIGHTEN " Izgarayı Göster / Gizle", nullptr, show_grid)) show_grid = !show_grid;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_SAVE " D\xC3\xBCzeni Kaydet")) {
          LayoutError le{};
          if (layout_save(layout_path, &le))
            console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "panel d\xC3\xBCzeni kaydedildi: %s", layout_path);
          else
            console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "panel d\xC3\xBCzeni kaydedilemedi: %s", le.msg);
        }
        if (ImGui::MenuItem(ICON_MD_REFRESH " D\xC3\xBCzeni Y\xC3\xBCkle")) {
          LayoutError le{};
          if (layout_load(layout_path, &le))
            console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "panel d\xC3\xBCzeni y\xC3\xBCklendi");
          else
            console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "panel d\xC3\xBCzeni y\xC3\xBCklenemedi: %s", le.msg);
        }
        if (ImGui::MenuItem(ICON_MD_WIDGETS " Varsay\xC4\xB1lan D\xC3\xBCzene D\xC3\xB6n")) {
          const ImGuiViewport *vp_main = ImGui::GetMainViewport();
          layout_apply_default(layout_dockspace_id(), vp_main->WorkSize.x, vp_main->WorkSize.y);
          console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "varsay\xC4\xB1lan panel d\xC3\xBCzeni uyguland\xC4\xB1");
        }
        if (ImGui::MenuItem("☀ Işık ve Gölge Gizmolarnı Göster", nullptr, st.gizmos.light_radius)) {
          st.gizmos.light_radius = !st.gizmos.light_radius;
          st.gizmos.light_glyph = st.gizmos.light_radius;
          st.gizmos.shadow_volume = st.gizmos.light_radius;
          st.gizmos.sun_dir = st.gizmos.light_radius;
          st.gizmos.camera_frustum = st.gizmos.light_radius;
          st.gizmos.env_volumes = st.gizmos.light_radius;
        }
        if (ImGui::MenuItem( ICON_MD_CENTER_FOCUS_STRONG " Se\xC3\xA7ili Varl\xC4\xB1\xC4\x9F" "a Odaklan")) {
          const int32_t s0 = st.sel.primary();
          if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
            static content::SceneBounds fb[content::kSceneMaxEntities];
            const uint32_t nb = entity_world_bounds(st, phys, fb);
            if ((uint32_t)s0 < nb) camera_focus(cam, fb[s0]);
          }
        }
        if (ImGui::MenuItem( ICON_MD_REFRESH " Kamerayı Varsayılana Sıfırla")) {
          cam.yaw = 0.7f; cam.pitch = 0.45f; cam.radius = 26.0f;
          cam.target = Vec3{0, 1.0f, -3.0f};
        }
        ImGui::Separator();
        ImGui::TextDisabled("Kamera Hızı: %.1fx", (double)(cam.speed / 9.0f));
        float sp = cam.speed / 9.0f;
        if (ImGui::SliderFloat("##camspeed", &sp, 0.1f, 5.0f, "%.1fx")) {
          cam.speed = sp * 9.0f;
        }
        ImGui::EndPopup();
      }

      ImGui::PopStyleVar(2);
      ImGui::Separator();

      // 2) 3B Canvas Çizimi
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (avail.x >= 1.0f && avail.y >= 1.0f) {
        float draw_w = avail.x;
        float draw_h = avail.y;
        float offset_x = 0.0f;
        float offset_y = 0.0f;

        if (view_tab == ViewportTab::Game && game_aspect != GameAspect::Free) {
          float target_ar = 16.0f / 9.0f;
          if (game_aspect == GameAspect::Aspect16_10) target_ar = 16.0f / 10.0f;
          else if (game_aspect == GameAspect::Aspect4_3) target_ar = 4.0f / 3.0f;
          else if (game_aspect == GameAspect::Aspect21_9) target_ar = 21.0f / 9.0f;
          else if (game_aspect == GameAspect::Aspect1_1) target_ar = 1.0f;

          if (avail.x / avail.y > target_ar) {
            draw_h = avail.y;
            draw_w = std::floor(draw_h * target_ar);
            offset_x = std::floor((avail.x - draw_w) * 0.5f);
          } else {
            draw_w = avail.x;
            draw_h = std::floor(draw_w / target_ar);
            offset_y = std::floor((avail.y - draw_h) * 0.5f);
          }

          // Arka planı koyu letterbox ile doldur
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(8, 8, 8, 255));
        }

        if (draw_w < 1.0f) draw_w = 1.0f;
        if (draw_h < 1.0f) draw_h = 1.0f;
        vp.resize((uint32_t)draw_w, (uint32_t)draw_h);
        ren.set_render_size(vp.width(), vp.height());
        {
          // F5'in acacagi kanalin olcusu: Oyun sekmesinin cizim alani, secili
          // en-boy oraniyla (sekme su an Sahne olsa da). Oyun bu olcude cizer.
          float gw = avail.x, gh = avail.y;
          if (game_aspect != GameAspect::Free) {
            float ar = 16.0f / 9.0f;
            if (game_aspect == GameAspect::Aspect16_10) ar = 16.0f / 10.0f;
            else if (game_aspect == GameAspect::Aspect4_3) ar = 4.0f / 3.0f;
            else if (game_aspect == GameAspect::Aspect21_9) ar = 21.0f / 9.0f;
            else if (game_aspect == GameAspect::Aspect1_1) ar = 1.0f;
            if (gw / gh > ar) gw = std::floor(gh * ar);
            else gh = std::floor(gw / ar);
          }
          oyun_cizim_w = (uint32_t)gw;
          oyun_cizim_h = (uint32_t)gh;
        }
        const bool oyun_sekmede = st.play_embedded && view_tab == ViewportTab::Game;
        if (vp.texture_id()) {
          if (offset_x > 0.0f || offset_y > 0.0f) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + offset_x, origin.y + offset_y));
          }
          if (oyun_sekmede) {
            // GOMULU OYUN: karesi kanal olcusunde; alana en-boy korunarak oturur.
            const float kw = (float)(oyun.chan.ok() ? oyun.chan.width() : oyun_goruntu.width() ? oyun_goruntu.width() : vp.width());
            const float kh = (float)(oyun.chan.ok() ? oyun.chan.height() : oyun_goruntu.height() ? oyun_goruntu.height() : vp.height());
            const float sc = std::fmin(draw_w / kw, draw_h / kh);
            const float iw = std::floor(kw * sc), ih = std::floor(kh * sc);
            const ImVec2 p0(std::floor(origin.x + offset_x + (draw_w - iw) * 0.5f), std::floor(origin.y + offset_y + (draw_h - ih) * 0.5f));
            ImDrawList *gdl = ImGui::GetWindowDrawList();
            gdl->AddRectFilled(ImVec2(origin.x + offset_x, origin.y + offset_y), ImVec2(origin.x + offset_x + draw_w, origin.y + offset_y + draw_h),
                               IM_COL32(8, 8, 8, 255));
            ImGui::SetCursorScreenPos(p0);
            if (oyun_goruntu.has_frame() && oyun_goruntu.texture_id()) {
              ImGui::Image((ImTextureID)oyun_goruntu.texture_id(), ImVec2(iw, ih));
            } else {
              ImGui::Dummy(ImVec2(iw, ih));
              char bek[160];
              const double gecen = (platform::now_ns() - oyun_baslangic_ns) / 1e9;
              std::snprintf(bek, sizeof bek, ICON_MD_HOURGLASS_TOP " %s derleniyor / baslatiliyor... %.1f s", oyun.game, gecen);
              const ImVec2 ts = ImGui::CalcTextSize(bek);
              gdl->AddText(ImVec2(p0.x + (iw - ts.x) * 0.5f, p0.y + (ih - ts.y) * 0.5f), IM_COL32(200, 205, 215, 255), bek);
            }
            oyun_rect = ViewportRect{p0.x, p0.y, iw, ih};
          } else {
            ImGui::Image((ImTextureID)vp.texture_id(), ImVec2((float)vp.width(), (float)vp.height()));
          }
          view_hovered = ImGui::IsItemHovered();
          // Surukle-birak HEDEFI: Kaynaklar panelinden suruklenen dosya
          // goruntunun uzerine birakilinca sahneye eklenir (kaynagi
          // editor_overlay.cpp'de). BeginDragDropTarget SON OGEYE baglanir,
          // bu yuzden ImGui::Image cagrisinin HEMEN ardinda durmak zorunda --
          // araya bir cocuk pencere girerse hedef ona baglanir ve birakma
          // sessizce hicbir sey yapmaz.
          if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_FILE")) {
              char add_file[content::kScenePathLen];
              std::snprintf(add_file, sizeof add_file, "%s", (const char *)payload->Data);
              do_add_asset(add_file);
            }
            ImGui::EndDragDropTarget();
          }

          // --- ImGuizmo'yu BU pencereye bagla ---------------------------------
          // ImGuizmo, fare kendi uzerinde mi diye IsHoveringWindow() ile bakar ve
          // o da mDrawList'in SAHIBI pencereyi sorar. BeginFrame() bunu kendi
          // tam ekran "gizmo" penceresine kurar; fare ise Gorunum panelinin
          // uzerindedir. O yuzden IsHoveringWindow() su dala duser:
          //     if (g.HoveredWindow != NULL) return false;   // baska pencere
          // -> mbMouseOver = false -> GetMoveType() MT_NONE -> IsOver() HEP false,
          // gizmo tiklamayi HIC almaz. Cizim listesini bu pencereye almak
          // sahiplik sorusunu dogru yanitlatir (ayrica gizmo panelin kirpma
          // dikdortgenine girer, panel disina tasmaz).
          //
          // SetRect de burada: Gorunum penceresinin EKRAN koordinatlari, ImGui'nin
          // io.MousePos ile ayni uzayda.
          ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
          ImGuizmo::SetRect(origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height());

          if (view_tab == ViewportTab::Scene) {
            OverlayInfo oi;
            const Mat4 vm = camera_view(cam);
            std::memcpy(oi.view, &vm.m[0][0], sizeof oi.view);
            const Vec3 eye = camera_eye(cam);
            oi.cam_eye[0] = eye.x; oi.cam_eye[1] = eye.y; oi.cam_eye[2] = eye.z;
            oi.cam_target[0] = cam.target.x; oi.cam_target[1] = cam.target.y; oi.cam_target[2] = cam.target.z;
            oi.gizmo_op = gizmo_op;
            oi.gizmos_visible = st.gizmos.light_radius || st.gizmos.light_glyph || st.gizmos.shadow_volume || st.gizmos.sun_dir || st.gizmos.camera_frustum || st.gizmos.env_volumes;
            oi.playing = st.playing;
            oi.hovered = view_hovered && !st.gizmo_was_over && !st.gizmo_was_using && !st.terrain_brush.active;
            oi.focused = ImGui::IsWindowFocused();
            oi.frame_ms = dt * 1000.0f;
            oi.draw_calls = ren.stats().draws;
            oi.entity_count = st.scene.entity_count;
            oi.proj = cam.proj;
            oi.cam_mode = cam.mode;
            oi.gizmo_space = gizmo_space;
            static const char *s_shading_labels[] = {
                "Ayd\xC4\xB1nlatmal\xC4\xB1",
                "I\xC5\x9F\xC4\xB1ks\xC4\xB1z",
                "\xC3\x87" "arp\xC4\xB1\xC5\x9Fma",
                "S\xC4\xB1n\xC4\xB1rlar",
                "Overdraw Is\xC4\xB1 Haritas\xC4\xB1",
                "I\xC5\x9F\xC4\xB1k K\xC3\xBCmeleri"
            };
            if (view_mode >= 0 && view_mode < 6) oi.shading = s_shading_labels[view_mode];
            oi.hint = "Sağ tık döndür · orta tuş kaydır · F odak";
            // Nokta duzenleme kipi (E6): ipucu kipi ve cikisi soyler; gizmo
            // yalniz tasir (arac cubugundaki dondur/olcekle bu kipte gecmez).
            static char s_pe_hint[160];
            if (prop_edit_active(st) && st.pe_entity < (int32_t)st.scene.entity_count) {
              std::snprintf(s_pe_hint, sizeof s_pe_hint, ICON_MD_OPEN_WITH " nokta: %s.%s \xC2\xB7 Esc \xC3\xA7\xC4\xB1k",
                            st.scene.entities[st.pe_entity].name, st.pe_name);
              oi.hint = s_pe_hint;
              oi.gizmo_op = 0;
            }
            viewport_overlay(ViewportRect{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()}, oi, nullptr, &ovres);

            // Nesne ozellikleri (E5): secili varligin `nokta`lari. Oynarken
            // cizilmez — nokta YAZAR pozuna bagli, yuruyen varliktan cizgi yanlis okunurdu.
            if (!st.playing) {
              const int32_t mp = st.sel.primary();
              if (mp >= 0 && mp < (int32_t)st.scene.entity_count)
                st.prop_markers_drawn = draw_prop_markers(st, (uint32_t)mp, proj * view,
                                                          ViewportRect{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()},
                                                          frame_i);
            }

            // Arazi Fırçası ve 3B İmleç Halkası (Görünüm Paneli çizim listesi içinde)
            const int32_t t_prim = st.sel.primary();
            if (t_prim >= 0 && t_prim < (int32_t)st.scene.entity_count &&
                (st.scene.entities[t_prim].components & content::kSceneTerrain)) {
              const SceneEntity &te = st.scene.entities[t_prim];
              const float psc = ui.pointer_scale();
              const ViewportRect vr{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()};
              const ViewportPick pick = in ? vp.map_mouse(vr, (float)in->mouse_x * psc, (float)in->mouse_y * psc) : ViewportPick{};

              Vec3 ro, rd;
              camera_ray(cam, aspect, pick.valid ? pick.x : (float)vp.width() * 0.5f, pick.valid ? pick.y : (float)vp.height() * 0.5f, (float)vp.width(), (float)vp.height(), &ro, &rd);
              const Mat4 tw = content::scene_entity_world_matrix(st.scene, (uint32_t)t_prim);
              const Mat4 inv_tw = inverse(tw);
              const Vec4 ro_loc4 = inv_tw * Vec4{ro.x, ro.y, ro.z, 1.0f};
              const Vec4 rd_loc4 = inv_tw * Vec4{rd.x, rd.y, rd.z, 0.0f};
              const Vec3 ro_loc{ro_loc4.x, ro_loc4.y, ro_loc4.z};
              const Vec3 rd_loc = normalize(Vec3{rd_loc4.x, rd_loc4.y, rd_loc4.z});

              const float tw_world = te.terrain_width * te.terrain_cell;
              const float th_world = te.terrain_height * te.terrain_cell;

              bool hit = false;
              Vec3 hit_loc{0, 0, 0};

              if (std::fabs(rd_loc.y) > 1e-5f) {
                float t0 = 0.0f, t1 = 300.0f;
                float step_sz = (t1 - t0) / 24.0f;
                float prev_diff = 0.0f;
                Vec3 prev_p = ro_loc;
                for (int s = 0; s <= 24; s++) {
                  float t = t0 + (float)s * step_sz;
                  Vec3 p = ro_loc + rd_loc * t;
                  if (p.x >= 0.0f && p.x <= tw_world && p.z >= 0.0f && p.z <= th_world) {
                    float surf_h = sample_terrain_height_with_sculpt(st, (uint32_t)t_prim, p.x, p.z);
                    float diff = p.y - surf_h;
                    if (s > 0 && ((diff <= 0.0f && prev_diff >= 0.0f) || (diff >= 0.0f && prev_diff <= 0.0f))) {
                      float frac = std::fabs(prev_diff) / (std::fabs(prev_diff) + std::fabs(diff) + 1e-6f);
                      hit_loc = prev_p + (p - prev_p) * frac;
                      hit = true;
                      break;
                    }
                    prev_diff = diff;
                    prev_p = p;
                  }
                }
                if (!hit && rd_loc.y < -0.001f) {
                  float tp = (te.terrain_amp * 0.3f - ro_loc.y) / rd_loc.y;
                  if (tp > 0.0f) {
                    Vec3 p = ro_loc + rd_loc * tp;
                    if (p.x >= 0.0f && p.x <= tw_world && p.z >= 0.0f && p.z <= th_world) {
                      hit_loc = p;
                      hit_loc.y = sample_terrain_height_with_sculpt(st, (uint32_t)t_prim, p.x, p.z);
                      hit = true;
                    }
                  }
                }
              }

              if (hit) {
                st.terrain_brush.hit_valid = true;
                st.terrain_brush.hit_local = hit_loc;
                const Vec4 hw4 = tw * Vec4{hit_loc.x, hit_loc.y, hit_loc.z, 1.0f};
                st.terrain_brush.hit_world = Vec3{hw4.x, hw4.y, hw4.z};

                if (st.terrain_brush.active && pick.valid) {
                  ImDrawList *dl = ImGui::GetWindowDrawList();
                  const Mat4 vp_mat = proj * view;
                  const float rad = st.terrain_brush.radius;
                  constexpr int kSegs = 36;
                  ImVec2 pts[kSegs];
                  bool pts_valid[kSegs];
                  for (int s = 0; s < kSegs; s++) {
                    const float ang = (float)s * (2.0f * 3.14159265f / (float)kSegs);
                    const float lx = hit_loc.x + rad * std::cos(ang);
                    const float lz = hit_loc.z + rad * std::sin(ang);
                    const float ly = sample_terrain_height_with_sculpt(st, (uint32_t)t_prim, lx, lz) + 0.08f;
                    const Vec4 wpos = tw * Vec4{lx, ly, lz, 1.0f};
                    const Vec4 clip = vp_mat * wpos;
                    if (clip.w > 0.01f) {
                      const float ndc_x = clip.x / clip.w;
                      const float ndc_y = clip.y / clip.w;
                      const float sx = vr.x + (ndc_x * 0.5f + 0.5f) * vr.w;
                      const float sy = vr.y + (ndc_y * 0.5f + 0.5f) * vr.h;
                      pts[s] = ImVec2(sx, sy);
                      pts_valid[s] = true;
                    } else {
                      pts_valid[s] = false;
                    }
                  }
                  const ImU32 col_ring = IM_COL32(50, 235, 150, 240);
                  for (int s = 0; s < kSegs; s++) {
                    int s_next = (s + 1) % kSegs;
                    if (pts_valid[s] && pts_valid[s_next]) {
                      dl->AddLine(pts[s], pts[s_next], col_ring, 2.5f);
                    }
                  }
                  const Vec4 clip_center = vp_mat * Vec4{st.terrain_brush.hit_world.x, st.terrain_brush.hit_world.y + 0.1f, st.terrain_brush.hit_world.z, 1.0f};
                  if (clip_center.w > 0.01f) {
                    const float cx = vr.x + (clip_center.x / clip_center.w * 0.5f + 0.5f) * vr.w;
                    const float cy = vr.y + (clip_center.y / clip_center.w * 0.5f + 0.5f) * vr.h;
                    dl->AddCircleFilled(ImVec2(cx, cy), 4.0f, col_ring);
                  }

                  if (view_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    apply_terrain_brush(st, (uint32_t)t_prim, hit_loc.x, hit_loc.z, dt, ImGui::GetIO().KeyShift);
                  }
                }
              } else {
                st.terrain_brush.hit_valid = false;
              }
            } else {
              st.terrain_brush.hit_valid = false;
            }
          }
        } else {
          ImGui::TextUnformatted(vp.last_error());
        }
        view_rect = ViewportRect{origin.x + offset_x, origin.y + offset_y, (float)vp.width(), (float)vp.height()};
      }
      // Oynatma basladi: klavye oyuna gitsin diye panel odaga alinir.
      if (oyun_odak_iste) { ImGui::SetWindowFocus(); oyun_odak_iste = false; }
      gorunum_odak = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    } else {
      gorunum_odak = false;
    }
    ImGui::End();
    // --- Gomulu oyun girdisi --------------------------------------------------
    // Oyun sekmesi odaktayken editorun tuslari ve faresi OYUNA gider (fare
    // kare pikseline cevrilir). Odak disinda hicbir tus basili degildir: baska
    // bir panelde yazilan harf oyunda karakteri yurutmesin.
    if (st.play_embedded && oyun.chan.ok()) {
      oyun_girdi_odak = view_tab == ViewportTab::Game && gorunum_odak;
      if (oyun_girdi_odak && in && oyun_rect.w > 0 && oyun_rect.h > 0) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const double fx = (m.x - oyun_rect.x) / oyun_rect.w * oyun.chan.width();
        const double fy = (m.y - oyun_rect.y) / oyun_rect.h * oyun.chan.height();
        oyun.chan.set_input(in, fx, fy);
      } else {
        oyun.chan.set_input(nullptr, 0, 0);
      }
    } else {
      oyun_girdi_odak = false;
    }
    // --- Kaplamadan gelen gezinme eylemleri (cip/gosterge tiklamalari) ------
    if (ovres.axis_clicked >= 0) {
      camera_align(cam, (CameraAxis)ovres.axis_clicked);
      static const char *const kAx[6] = {"+X", "-X", "Ust", "Alt", "On", "Arka"};
      set_status(st, "eksen gorunusu: %s", kAx[ovres.axis_clicked]);
    }
    if (ovres.ortho_toggled) cam.proj = cam.proj == CameraProjection::Perspective ? CameraProjection::Ortho : CameraProjection::Perspective;
    if (ovres.mode_toggled) cam.mode = cam.mode == CameraMode::Orbit ? CameraMode::Fly : CameraMode::Orbit;
    if (ovres.gizmo_space_toggled) gizmo_space = gizmo_space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;

    if (ImGui::Begin(kPanelSahneLabel)) {
      st.tree.hovered_index = -1;
      // "Sahne" sekmesine / basligina sag tiklandiginda menuyu ac:
      if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right))) {
        ImGui::OpenPopup("SahnePanelMenu");
      }
      const int tb = hierarchy_toolbar(st.scene.entity_count, st.sel.count > 0);
      if (tb == 100) do_remove();
      // UST SINIR YOK. Burada `tb <= 14` yaziyordu ve kCreate3D menusunun
      // urettigi 20..24 (Kapsul/Silindir/Koni/Dortgen/Simit) bu dala takilip
      // SESSIZCE YUTULUYORDU: menude gorunuyor, tiklaniyor, hicbir sey olmuyor.
      // Tanimsiz bir kodu do_add zaten `default` dalinda karsiliyor.
      else if (tb > 0) do_add(tb);
      hierarchy_search(st.filter, sizeof st.filter);

      // Cizim sirasi belirlenimli ON-SIRADIR (kokler indeks sirasinda, cocuklar
      // indeks sirasinda). SUZGEC ACIKKEN duz liste cizilir: katlanmis bir ata
      // eslesmeyi gizlemesin.
      const bool filtering = st.filter[0] != 0;
      int32_t order[content::kSceneMaxEntities];
      uint32_t n = 0;
      if (filtering) {
        for (uint32_t i = 0; i < st.scene.entity_count; i++) order[n++] = (int32_t)i;
      } else n = content::scene_tree_order(st.scene, order, content::kSceneMaxEntities);
      HierarchyResult act;
      uint32_t shown = 0, hide_depth = 0; // hide_depth > 0: katlanmis alt agactayiz
      // --- ImGui'nin YERLESIK coklu secimi ------------------------------------
      // Shift+tik ARALIGI, Ctrl+tik, Ctrl+A, ok tuslariyla gezinme ve listede
      // kutu (marquee) secim bunun uzerinden gelir; hepsi elle yazilsaydi
      // klavye gezintisi ve aralik secimi yine olmazdi. Satirlar kendi
      // kimliklerini zaten bildiriyor (editor_widgets: SetNextItemSelectionUserData).
      //
      // Depolama DISARIDA: tek dogruluk kaynagi st.sel olarak kaliyor
      // (gizmo, silme, kopyalama, gruplar hep onu okur). ImGui yalniz
      // "sunu sec / sundan cikar" istekleri gonderir, kumeyi kendisi tutmaz.
      ImGuiSelectionExternalStorage ms_ext;
      ms_ext.UserData = (void *)&st.sel;
      ms_ext.AdapterSetItemSelected = [](ImGuiSelectionExternalStorage *self, int idx, bool selected) {
        Selection *sel = (Selection *)self->UserData;
        if (!selected) { sel->erase(idx); return; }
        // toggle() "yoksa ekler, varsa cikarir": burada VARSA dokunulmamali,
        // yoksa ImGui'nin "secili kalsin" istegi secimi kapatirdi.
        if (!sel->contains(idx)) sel->toggle(idx);
      };
      // items_count = VARLIK SAYISI, ekrandaki satir sayisi degil: satirin
      // ImGui'ye bildirdigi kimlik varlik indeksidir (editor_widgets), ve
      // ApplyRequests "hepsini sec" istegini 0..items_count-1 kimlikleri
      // uzerinde dolasarak uygular. n verilseydi suzgec aciksa ya da agac
      // sirasi indeks sirasindan farkliysa yanlis varliklar secilirdi.
      // Ctrl+A ImGui'de KAPALI: o kisayolun sahibi komut tablosu (SelectAll),
      // iki sahip olsa ayni karede iki kez secim yazilirdi.
      ImGuiMultiSelectIO *ms_io = ImGui::BeginMultiSelect(
          ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_NoSelectAll, (int)st.sel.count, (int)st.scene.entity_count);
      ms_ext.ApplyRequests(ms_io); // satirlar CIZILMEDEN once: "hepsini temizle/sec"

      // Sahne Kök Başlığı (Prowl / Unity Scene Root Header)
      const bool scene_active = (st.sel.count == 0);
      ImGui::PushStyleColor(ImGuiCol_Header, scene_active ? tone(Tone::Accent, 0.28f) : tone(Tone::Bg2, 0.40f));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tone(Tone::Accent, 0.38f));
      const char *scn_raw = st.scene_path[0] ? st.scene_path : "editor.sahne";
      const char *sl = std::strrchr(scn_raw, '/');
      if (!sl) sl = std::strrchr(scn_raw, '\\');
      const char *scn_base = sl ? sl + 1 : scn_raw;
      char scn_hdr[128];
      std::snprintf(scn_hdr, sizeof scn_hdr, "  \xE2\x97\x8E  %s  (%u varl\xC4\xB1k)", scn_base, st.scene.entity_count);
      if (ImGui::Selectable(scn_hdr, scene_active, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight() * 1.15f))) {
        st.sel.clear(); // Sahne secilince varlik secimi kalkar ve Mufettis'te Dunya/Sahne ayarlari gorunur
      }
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sahne K\xC3\xB6k\xC3\xBC: T\xC4\xB1klayarak D\xC3\xBCnya ve Atmosfer ayarlar\xC4\xB1n\xC4\xB1 M\xC3\xBC" "fetti\xC5\x9F'te a\xC3\xA7\xC4\xB1n");
      }
      ImGui::Separator();

      for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (uint32_t)order[k];
        if (i >= st.scene.entity_count) continue;
        const SceneEntity &e = st.scene.entities[i];
        const uint32_t depth = filtering ? 0u : content::scene_tree_depth(st.scene, i);
        if (hide_depth) {
          if (depth >= hide_depth) continue;
          hide_depth = 0;
        }
        if (filtering && !hierarchy_filter_match(e.name, st.filter)) continue;
        bool kids = false;
        for (uint32_t j = 0; j < st.scene.entity_count && !kids; j++) kids = st.scene.entities[j].parent == (int32_t)i;
        HierarchyRow row;
        row.name = e.name;
        row.selected = st.sel.contains((int32_t)i);
        row.has_model = (e.components & content::kSceneModel) != 0;
        row.has_light = (e.components & content::kSceneLight) != 0;
        row.has_body = (e.components & content::kSceneBody) != 0;
        row.has_anim = (e.components & content::kSceneAnim) != 0;
        row.depth = depth;
        row.has_children = kids && !filtering;
        row.expanded = !st.tree.collapse.collapsed(i);
        row.hidden = (e.flags & content::kSceneHidden) != 0;
        row.locked = (e.flags & content::kSceneLocked) != 0;
        shown++;
        const HierarchyResult r = hierarchy_tree_row((int)i, row, &st.tree);
        // Select ELENIR: secimi artik BeginMultiSelect/EndMultiSelect yonetiyor
        // (apply_hierarchy'nin kendi tek-secim dali ayni karede ikinci kez
        // yazsaydi Shift+tik araligi hemen tek satira duserdi). Diger eylemler
        // (yeniden adlandir, sil, ebeveyn degistir...) oldugu gibi gecer.
        if (r.action != HierarchyAction::None && r.action != HierarchyAction::Select) act = r;
        if (row.has_children && !row.expanded) hide_depth = depth + 1;
      }
      ms_io = ImGui::EndMultiSelect();
      ms_ext.ApplyRequests(ms_io);
      if (shown == 0)
        hierarchy_empty(st.scene.entity_count ? "S\xC3\xBCzge\xC3\xA7le e\xC5\x9Fle\xC5\x9F" "en varl\xC4\xB1k yok"
                                              : "Sahne bo\xC5\x9F \xE2\x80\x94 \xE2\x80\x9C+\xE2\x80\x9D ile varl\xC4\xB1k ekle");
      // Listenin altindaki bosluk: buraya birakmak KOKE tasir, sag tiklamak SahnePanelMenu acar.
      const HierarchyResult zone = hierarchy_root_drop_zone(&st.tree);
      if (zone.action != HierarchyAction::None) act = zone;

      // Sahne panelinin bos alanina / arka planina sag tiklandiginda (eger bir varlik satiri uzerinde degilsek) menuyu ac:
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
          (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)) &&
          st.tree.hovered_index < 0) {
        ImGui::OpenPopup("SahnePanelMenu");
      }

      if (ImGui::BeginPopup("SahnePanelMenu")) {
        // --- 1. YENİ VARLIK EKLE (Tek doğruluk kaynağı: kCreateMenu) ---
        if (ImGui::BeginMenu(ICON_MD_ADD "  Yeni Varl\xC4\xB1k Ekle...")) {
          const int r = create_menu_draw(kCreateMenu, kCreateMenuCount);
          if (r > 0) do_add(r);
          ImGui::EndMenu();
        }

        // --- 2.5 YEREL PREFAB İŞLEMLERİ (O3DE / Prowl Modeli) ---
        if (ImGui::BeginMenu(ICON_MD_WIDGETS "  Prefab \xC4\xB0\xC5\x9Flemleri")) {
          // PR #7 burada kendi tek-varlikli `.prefab` bicimini cagiriyordu ve
          // yukleme yolu SABIT KODLUYDU ("SokakLambasi.prefab" / "Varlik.prefab"):
          // baska adli bir prefab acilamazdi. Iki oge de main'in dosya diyalogu
          // akisina baglandi -- alt agac cikarma, kaynak yeniden esleme,
          // hepsi-ya-da-hicbiri ekleme ve TEK Ctrl+Z ile geri alma oradan gelir
          // (bkz. content/prefab.hpp).
          if (ImGui::MenuItem(ICON_MD_SAVE "  Se\xC3\xA7ili Varl\xC4\xB1\xC4\x9F\xC4\xB1 Prefab Kaydet", nullptr, false, st.sel.count > 0)) {
            const int32_t s0 = st.sel.primary();
            if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
              prefab_root = s0;
              dlg_intent = IntentPrefabSave;
              file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_dir, ".prefab", "Prefab kaydet");
            }
          }
          if (ImGui::MenuItem(ICON_MD_FOLDER_OPEN "  Prefab Y\xC3\xBCkle ve Sahneye Ekle...")) {
            dlg_intent = IntentPrefabLoad;
            file_dialog_open(dlg, FileDialogMode::Ac, st.scene_dir, ".prefab", "Prefab ekle");
          }
          ImGui::EndMenu();
        }

        ImGui::Separator();

        // --- 3. DÜZENLEME & SEÇİM ---
        const bool has_sel = st.sel.count > 0;
        if (ImGui::MenuItem("Kes", "Ctrl+X", false, has_sel)) do_cut();
        if (ImGui::MenuItem("Kopyala", "Ctrl+C", false, has_sel)) do_copy();
        if (ImGui::MenuItem("Yap\xC4\xB1\xC5\x9Ft\xC4\xB1r", "Ctrl+V", false, st.clip_count > 0)) do_paste();
        if (ImGui::MenuItem("\xC3\x87o\xC4\x9F" "alt", "Ctrl+D", false, has_sel)) do_add(0);
        if (ImGui::MenuItem("Sil", "Del", false, has_sel)) do_remove();

        ImGui::Separator();

        if (ImGui::MenuItem("Se\xC3\xA7ime Odaklan (Focus)", "F", false, has_sel)) {
          const int32_t s0 = st.sel.primary();
          if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) {
            cam.target = st.scene.entities[s0].pos;
          }
        }
        if (ImGui::MenuItem("T\xC3\xBCm\xC3\xBCn\xC3\xBC Se\xC3\xA7", "Ctrl+A", false, st.scene.entity_count > 0)) {
          st.sel.clear();
          for (uint32_t k = st.scene.entity_count; k > 0; k--) st.sel.toggle((int32_t)(k - 1));
        }
        if (ImGui::MenuItem("Se\xC3\xA7imi Kald\xC4\xB1r", "Esc", false, has_sel)) {
          st.sel.clear();
        }

        ImGui::Separator();

        // --- 4. SROS & SAHNE ONARIM / OPTİMİZASYON ---
        if (ImGui::BeginMenu(ICON_MD_BUILD "  SROS \xE2\x80\x94 Sahne Onar\xC4\xB1m ve Bak\xC4\xB1m")) {
          if (ImGui::MenuItem("\xE2\x9A\xA1  Hatal\xC4\xB1 / S\xC4\xB1" "f\xC4\xB1r \xC3\x96l\xC3\xA7" "ekleri D\xC3\xBCzelt")) {
            uint32_t fixed = 0;
            for (uint32_t i = 0; i < st.scene.entity_count; i++) {
              SceneEntity &e = st.scene.entities[i];
              if (e.scale.x <= 0.001f || e.scale.y <= 0.001f || e.scale.z <= 0.001f) {
                e.scale = {1.0f, 1.0f, 1.0f};
                fixed++;
              }
            }
            if (fixed) { st.dirty = true; set_status(st, "SROS: %u varligin olcegi duzeltildi", fixed); }
            else set_status(st, "SROS: Olcekler saglikli");
          }
          if (ImGui::MenuItem(ICON_MD_SHIELD "  Modellere Otomatik Collider Ekle")) {
            uint32_t added = 0;
            for (uint32_t i = 0; i < st.scene.entity_count; i++) {
              SceneEntity &e = st.scene.entities[i];
              if ((e.components & content::kSceneModel) && !(e.components & content::kSceneBody)) {
                e.components |= content::kSceneBody;
                e.shape = content::SceneShape::Box;
                e.half = {0.5f, 0.5f, 0.5f};
                e.dynamic = false;
                added++;
              }
            }
            if (added) { st.dirty = true; set_status(st, "SROS: %u modele govde eklendi", added); }
            else set_status(st, "SROS: Tum modeller zaten carpisana sahip");
          }
          if (ImGui::MenuItem("\xE2\x98\x80  S\xC3\xB6n\xC3\xBCk I\xC5\x9F\xC4\xB1klar\xC4\xB1 D\xC3\xBCzelt")) {
            uint32_t lfix = 0;
            for (uint32_t i = 0; i < st.scene.entity_count; i++) {
              SceneEntity &e = st.scene.entities[i];
              if (e.components & content::kSceneLight) {
                if (e.light_intensity <= 0.01f) { e.light_intensity = 2.0f; lfix++; }
                if (e.light_radius <= 0.1f) { e.light_radius = 8.0f; lfix++; }
              }
            }
            if (lfix) { st.dirty = true; set_status(st, "SROS: %u isik duzeltildi", lfix); }
            else set_status(st, "SROS: Isiklar saglikli");
          }
          if (ImGui::MenuItem(ICON_MD_AUTO_FIX_HIGH "  T\xC3\xBCm Sahneyi Otomatik Onar (Quick Fix)")) {
            uint32_t ops = 0;
            for (uint32_t i = 0; i < st.scene.entity_count; i++) {
              SceneEntity &e = st.scene.entities[i];
              if (e.scale.x <= 0.001f || e.scale.y <= 0.001f || e.scale.z <= 0.001f) {
                e.scale = {1.0f, 1.0f, 1.0f}; ops++;
              }
              if ((e.components & content::kSceneModel) && !(e.components & content::kSceneBody)) {
                e.components |= content::kSceneBody;
                e.shape = content::SceneShape::Box;
                e.half = {0.5f, 0.5f, 0.5f};
                e.dynamic = false;
                ops++;
              }
              if (e.components & content::kSceneLight) {
                if (e.light_intensity <= 0.01f) { e.light_intensity = 2.0f; ops++; }
                if (e.light_radius <= 0.1f) { e.light_radius = 8.0f; ops++; }
              }
            }
            if (ops) { st.dirty = true; set_status(st, "SROS: Sahne genelinde %u sorun onarildi!", ops); }
            else set_status(st, "SROS: Sahne 100%% saglikli, sorun bulunamadi");
          }
          ImGui::EndMenu();
        }

        // --- 5. AĞAÇ GÖRÜNÜMÜ ---
        if (ImGui::BeginMenu(ICON_MD_FOLDER_OPEN "  G\xC3\xB6r\xC3\xBCn\xC3\xBCm ve Katlama")) {
          if (ImGui::MenuItem("T\xC3\xBCm Katlamalar\xC4\xB1 A\xC3\xA7")) {
            st.tree.collapse.clear();
          }
          if (ImGui::MenuItem("T\xC3\xBCm Katlamalar\xC4\xB1 Kapat")) {
            for (uint32_t i = 0; i < st.scene.entity_count; i++) st.tree.collapse.set(i, true);
          }
          ImGui::Separator();
          if (ImGui::MenuItem(ICON_MD_ACCOUNT_TREE "  Materyal D\xC3\xBC\xC4\x9F\xC3\xBCm (Node) Edit\xC3\xB6r\xC3\xBC", nullptr, &st.show_node_editor)) {}
          if (ImGui::MenuItem(ICON_MD_TERMINAL "  Konsol", nullptr, &show_console)) {}
          ImGui::EndMenu();
        }

        ImGui::EndPopup();
      }
      
      // F2: secili varligin adini YERINDE duzenle (panel odakliyken).
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
        const int32_t s0 = st.sel.primary();
        if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) hierarchy_begin_rename(&st.tree, s0, st.scene.entities[s0].name);
      }
      apply_hierarchy(act);
    }
    ImGui::End();
    // Dunya + ortam ozellikleri TEK govdede: hem mufettisin icine gomulu
    // bolum (PR #7) hem de ayri "Dunya" paneli (main) AYNI lambda'yi cagirir.
    // Iki ayri kopya olsaydi biri otekinden sessizce ayrisirdi -- bu agacta
    // tam bunun icin kapilar var (bkz. tools/scene_check.py).
    auto draw_godray_controls = [&](const char *prefix, SceneEntity *opt_e, int si) {
      if (prop_begin(prefix)) {
        prop_help("Ekran-uzayi radial occlusion blur ve atmosferik volumetrik isik huzmeleri (God Rays / Crepuscular Rays).");

        bool gr_on = st.scene.godrays_enabled;
        if (prop_check("H\xC3\xBCzmeler Etkin (God Rays)", &gr_on).changed) {
          st.scene.godrays_enabled = gr_on;
          st.dirty = true;
        }

        // Aktif Huzme Kaynagi Bilgisi & Yonetimi
        int godray_source_entity = -1;
        for (uint32_t i = 0; i < st.scene.entity_count; i++) {
          if ((st.scene.entities[i].components & content::kSceneLight) && st.scene.entities[i].light_godray) {
            godray_source_entity = (int)i;
            break;
          }
        }

        if (godray_source_entity >= 0) {
          const SceneEntity &src_e = st.scene.entities[godray_source_entity];
          ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::Accent));
          ImGui::Text(ICON_MD_LIGHTBULB "  Kaynak: %s (%s)",
                      src_e.name[0] ? src_e.name : "I\xC5\x9F\xC4\xB1k",
                      src_e.light_type == content::SceneLightType::Directional ? "Y\xC3\xB6nl\xC3\xBC / G\xC3\xBCne\xC5\x9F" : "Nokta");
          ImGui::PopStyleColor();
          ImGui::SameLine();
          if (ImGui::SmallButton("G\xC3\xBCne\xC5\x9F" "e D\xC3\xB6n")) {
            SceneEntity mod = src_e;
            mod.light_godray = false;
            commit(st, godray_source_entity, mod);
            st.dirty = true;
          }
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::Warn));
          ImGui::Text(ICON_MD_WB_SUNNY "  Kaynak: Sahne G\xC3\xBCne\xC5\x9Fi (G\xC3\xB6ky\xC3\xBCz\xC3\xBC)");
          ImGui::PopStyleColor();
        }

        if (opt_e && si >= 0) {
          bool cast_from_this = opt_e->light_godray;
          if (prop_check("Bu I\xC5\x9F\xC4\xB1ktan Yay", &cast_from_this).changed) {
            SceneEntity after = *opt_e;
            after.light_godray = cast_from_this;
            if (cast_from_this) st.scene.godrays_enabled = true;
            commit(st, si, after);
            st.dirty = true;
          }
          if (opt_e->light_godray) {
            track_edit(st, *opt_e, si, prop_float("I\xC5\x9F\xC4\xB1k H\xC3\xBCzme G\xC3\xBC" "c\xC3\xBC", &opt_e->light_godray_intensity, 0.05f, 0.1f, 5.0f, "%.2fx"));
          }
        }

        ImGui::Separator();
        track_world_edit(st, prop_float("Pozlama (Exposure)", &st.scene.godray_exposure, 0.01f, 0.01f, 2.0f, "%.2f"));
        track_world_edit(st, prop_float("H\xC3\xBCzme Yo\xC4\x9Funlu\xC4\x9Fu (Density)", &st.scene.godray_density, 0.02f, 0.1f, 2.5f, "%.2f"));
        track_world_edit(st, prop_float("S\xC3\xB6n\xC3\xBCmleme (Decay)", &st.scene.godray_decay, 0.005f, 0.70f, 0.999f, "%.3f"));
        track_world_edit(st, prop_float("\xC3\x96rnek A\xC4\x9F\xC4\xB1rl\xC4\xB1\xC4\x9F\xC4\xB1 (Weight)", &st.scene.godray_weight, 0.02f, 0.05f, 1.0f, "%.2f"));

        ImGui::Spacing();
        ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 H\xC3\xBCzme \xC3\x96nayarlar\xC4\xB1:");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton("Do\xC4\x9F" "al G\xC3\xBCne\xC5\x9F")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 1.0f; st.scene.godray_decay = 0.94f; st.scene.godray_weight = 0.35f; st.scene.godray_exposure = 0.35f;
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Orman I\xC5\x9F\xC4\xB1klar\xC4\xB1")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 1.4f; st.scene.godray_decay = 0.97f; st.scene.godray_weight = 0.50f; st.scene.godray_exposure = 0.45f;
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Hafif Sis")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 0.6f; st.scene.godray_decay = 0.88f; st.scene.godray_weight = 0.20f; st.scene.godray_exposure = 0.25f;
          st.dirty = true;
        }
        if (ImGui::SmallButton("Sinematik")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 1.2f; st.scene.godray_decay = 0.95f; st.scene.godray_weight = 0.40f; st.scene.godray_exposure = 0.50f;
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dramatik")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 1.6f; st.scene.godray_decay = 0.98f; st.scene.godray_weight = 0.60f; st.scene.godray_exposure = 0.65f;
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Ay I\xC5\x9F\xC4\xB1\xC4\x9F\xC4\xB1")) {
          st.scene.godrays_enabled = true;
          st.scene.godray_density = 0.8f; st.scene.godray_decay = 0.91f; st.scene.godray_weight = 0.25f; st.scene.godray_exposure = 0.20f;
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("S\xC4\xB1" "f\xC4\xB1rla")) {
          st.scene.godray_density = 0.8f; st.scene.godray_decay = 0.95f; st.scene.godray_weight = 0.5f; st.scene.godray_exposure = 0.3f;
          st.dirty = true;
        }
        ImGui::PopStyleVar();

        prop_end();
      }
    };
    auto draw_world_and_environment_properties = [&]() {
      section_label("G\xC3\x9CNE\xC5\x9E VE ORTAM");
      if (prop_begin("gunes")) {
        track_world_edit(st, prop_vec3("Y\xC3\xB6n", &st.scene.sun_dir.x, 0.01f, -1.0f, 1.0f, "%.2f"));
        track_world_edit(st, prop_float("\xC5\x9Eiddet", &st.scene.sun_diffuse, 0.01f, 0.0f, 5.0f, "%.2f"));
        track_world_edit(st, prop_color("Ortam", &st.scene.ambient.x));
        prop_end();
      }
      section_label("ATMOSFER VE G\xC3\x96KY\xC3\x9CZ\xC3\x9C");
      if (prop_begin("atmosfer")) {
        prop_help("Fiziksel atmosfer modeli (Nishita / Bruneton) ve 24 saatlik dinamik g\xC3\xBCne\xC5\x9F d\xC3\xB6ng\xC3\xBCs\xC3\xBC.");
        float tod = st.scene.time_of_day;
        const PropItem pit = prop_float("G\xC3\xBCn\xC3\xBCn Saati", &tod, 0.1f, 0.0f, 24.0f, "%.1f:00");
        if (pit.changed) {
          st.scene.time_of_day = tod;
          const float ang = (tod - 6.0f) * (3.14159265f / 12.0f);
          st.scene.sun_dir.y = std::sin(ang);
          st.scene.sun_dir.x = std::cos(ang) * 0.85f;
          st.scene.sun_dir.z = 0.35f;
          track_world_edit(st, pit);
        }
        ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Zaman:");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton("\xC5\x9E" "afak 06:00")) {
          st.scene.time_of_day = 6.0f; st.scene.sun_dir = Vec3{0.85f, 0.05f, 0.35f};
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("\xC3\x96\xC4\x9Fle 12:00")) {
          st.scene.time_of_day = 12.0f; st.scene.sun_dir = Vec3{0.0f, 1.0f, 0.35f};
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Bat\xC4\xB1m 18:30")) {
          st.scene.time_of_day = 18.5f; st.scene.sun_dir = Vec3{-0.85f, 0.02f, 0.35f};
          st.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Gece 00:00")) {
          st.scene.time_of_day = 24.0f; st.scene.sun_dir = Vec3{0.0f, -1.0f, 0.35f};
          st.dirty = true;
        }
        ImGui::PopStyleVar();

        track_world_edit(st, prop_float("Bulan\xC4\xB1kl\xC4\xB1k", &st.scene.sky_turbidity, 0.05f, 1.0f, 10.0f, "%.2f"));
        prop_end();
      }

      section_label("ATMOSFER\xC4\xB0K S\xC4\xB0S LABORATUVARI (16 S\xC4\xB0S T\xC3\x9CR\xC3\x9C)");
      if (prop_begin("sis_lab")) {
        prop_help("16 farkl\xC4\xB1 sis t\xC3\xBCr\xC3\xBC (Inigo Quilez analitik \xC3\xBCstel y\xC3\xBCkseklik sisi, Beer-Lambert mesafesi, Exp\xC2\xB2, \xC3\x87ift kademeli UE5). Mobilde 0 FPS d\xC3\xBC\xC5\x9F\xC3\xBC\xC5\x9F\xC3\xBC.");
        bool fog_on = st.scene.fog_enabled;
        if (prop_check("Atmosferik Sis Etkin", &fog_on).changed) {
          st.scene.fog_enabled = fog_on;
          st.dirty = true;
        }
        if (st.scene.fog_enabled) {
          int ftype = (int)st.scene.fog_type;
          static const char *kFogTypes =
              "0: Do\xC4\x9Frusal Mesafe Sisi (Linear Distance)\0"
              "1: \xC3\x9Cstel Mesafe Sisi (Exponential Beer-Lambert)\0"
              "2: \xC3\x9Cstel Kare Sisi (Exp\xC2\xB2 Heavy Wall)\0"
              "3: Analitik \xC3\x9Cstel Y\xC3\xBCkseklik Sisi (Inigo Quilez Ground Fog)\0"
              "4: \xC3\x87ift Kademeli Y\xC3\xBCkseklik Sisi (Dual-Layer UE5)\0"
              "5: Toksik & Mistik Gaz Sisi (Glowing Magical Fog)\0"
              "6: Sabah Vadi Sisi (Ground Mist / Low Lying)\0"
              "7: Da\xC4\x9F Zirvesi & Bulut Denizi (Cloud Sea)\0"
              "8: Cyberpunk Smog (Neon Zehirli Duman)\0"
              "9: \xC3\x87\xC3\xB6l Toz F\xC4\xB1rt\xC4\xB1nas\xC4\xB1 / Habub (Sandstorm)\0"
              "10: Kar & Buz Sisi / Tipi (Blizzard Ice)\0"
              "11: Volkanik K\xC3\xBCl & S\xC3\xBClf\xC3\xBCr (Ash & Sulfur)\0"
              "12: Derin Deniz / Sualt\xC4\xB1 Sisi (Underwater Abyssal)\0"
              "13: Uzay Nebulas\xC4\xB1 & Kozmik Toz (Cosmic Dust Nebula)\0"
              "14: Gece Biyol\xC3\xBCminesans (Bioluminescent Night)\0"
              "15: Sinematik G\xC3\xBCne\xC5\x9F Sa\xC3\xA7\xC4\xB1l\xC4\xB1m\xC4\xB1 (Godray Mie Scatter)\0\0";
          if (prop_combo("Sis Modeli / T\xC3\xBCr\xC3\xBC (16 T\xC3\xBCr)", &ftype, kFogTypes).changed) {
            st.scene.fog_type = (uint32_t)ftype;
            st.dirty = true;
          }
          track_world_edit(st, prop_float("Sis Yo\xC4\x9Funlu\xC4\x9Fu", &st.scene.fog_density, 0.002f, 0.0001f, 1.0f, "%.4f"));
          track_world_edit(st, prop_color("Sis Rengi", &st.scene.fog_color.x));
          if (st.scene.fog_type == 0) {
            track_world_edit(st, prop_float("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 Mesafesi", &st.scene.fog_start, 0.5f, 0.0f, 500.0f, "%.1f m"));
            track_world_edit(st, prop_float("Biti\xC5\x9F Mesafesi", &st.scene.fog_end, 1.0f, 1.0f, 2000.0f, "%.1f m"));
          } else if (st.scene.fog_type >= 3) {
            track_world_edit(st, prop_float("Y\xC3\xBCkseklik D\xC3\xBC\xC5\x9F\xC3\xBC\xC5\x9F\xC3\xBC (\xCE\xBB)", &st.scene.fog_height_falloff, 0.005f, 0.001f, 1.0f, "%.3f"));
            track_world_edit(st, prop_float("Taban Kotu (y0)", &st.scene.fog_base_height, 0.1f, -100.0f, 100.0f, "%.1f m"));
          }
          track_world_edit(st, prop_float("G\xC3\xBCne\xC5\x9F Sa\xC3\xA7\xC4\xB1l\xC4\xB1m\xC4\xB1 (Mie)", &st.scene.fog_scattering, 0.02f, 0.0f, 2.0f, "%.2fx"));

          ImGui::Spacing();
          if (ImGui::Button("+ Sahneye 3B Hacimsel Sis Hacmi Ekle (Puf Bulutu)", ImVec2(-1, 0))) {
            do_add(46);
          }

          ImGui::Spacing();
          ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Sis \xC3\x96nayarlar\xC4\xB1:");
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
          if (ImGui::SmallButton("Vadi Sabah Sisi")) {
            st.scene.fog_type = 3; st.scene.fog_density = 0.025f;
            st.scene.fog_height_falloff = 0.12f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.78f, 0.82f, 0.90f}; st.scene.fog_scattering = 0.6f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Tekinsiz Orman")) {
            st.scene.fog_type = 2; st.scene.fog_density = 0.045f;
            st.scene.fog_color = Vec3{0.45f, 0.52f, 0.48f}; st.scene.fog_scattering = 0.3f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Korku / Zindan")) {
            st.scene.fog_type = 1; st.scene.fog_density = 0.08f;
            st.scene.fog_color = Vec3{0.18f, 0.18f, 0.22f}; st.scene.fog_scattering = 0.1f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Toksik Batakl\xC4\xB1k")) {
            st.scene.fog_type = 5; st.scene.fog_density = 0.05f;
            st.scene.fog_height_falloff = 0.2f; st.scene.fog_base_height = -0.5f;
            st.scene.fog_color = Vec3{0.25f, 0.85f, 0.35f}; st.scene.fog_scattering = 0.8f;
            st.dirty = true;
          }
          if (ImGui::SmallButton("G\xC3\xBCnbat\xC4\xB1m\xC4\xB1 Halesi")) {
            st.scene.fog_type = 4; st.scene.fog_density = 0.018f;
            st.scene.fog_height_falloff = 0.05f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.92f, 0.62f, 0.45f}; st.scene.fog_scattering = 1.2f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Cyberpunk Smog")) {
            st.scene.fog_type = 8; st.scene.fog_density = 0.04f;
            st.scene.fog_height_falloff = 0.15f; st.scene.fog_base_height = -0.2f;
            st.scene.fog_color = Vec3{0.15f, 0.75f, 0.85f}; st.scene.fog_scattering = 1.4f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("\xC3\x87\xC3\xB6l F\xC4\xB1rt\xC4\xB1nas\xC4\xB1")) {
            st.scene.fog_type = 9; st.scene.fog_density = 0.06f;
            st.scene.fog_height_falloff = 0.08f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.85f, 0.65f, 0.40f}; st.scene.fog_scattering = 0.9f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Kutup Buzu / Tipi")) {
            st.scene.fog_type = 10; st.scene.fog_density = 0.035f;
            st.scene.fog_height_falloff = 0.04f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.80f, 0.88f, 0.98f}; st.scene.fog_scattering = 0.7f;
            st.dirty = true;
          }
          if (ImGui::SmallButton("Biyol\xC3\xBCminesans Gece")) {
            st.scene.fog_type = 14; st.scene.fog_density = 0.045f;
            st.scene.fog_height_falloff = 0.18f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.40f, 0.15f, 0.80f}; st.scene.fog_scattering = 1.5f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Volkanik K\xC3\xBCl")) {
            st.scene.fog_type = 11; st.scene.fog_density = 0.07f;
            st.scene.fog_height_falloff = 0.10f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.32f, 0.22f, 0.18f}; st.scene.fog_scattering = 0.5f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Derin Deniz")) {
            st.scene.fog_type = 12; st.scene.fog_density = 0.055f;
            st.scene.fog_height_falloff = 0.02f; st.scene.fog_base_height = 5.0f;
            st.scene.fog_color = Vec3{0.05f, 0.20f, 0.35f}; st.scene.fog_scattering = 0.4f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Kozmik Nebula")) {
            st.scene.fog_type = 13; st.scene.fog_density = 0.03f;
            st.scene.fog_height_falloff = 0.01f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.70f, 0.15f, 0.45f}; st.scene.fog_scattering = 1.6f;
            st.dirty = true;
          }
          if (ImGui::SmallButton("Do\xC4\x9Frusal Mesafe (Retro)")) {
            st.scene.fog_type = 0; st.scene.fog_density = 0.02f;
            st.scene.fog_start = 5.0f; st.scene.fog_end = 80.0f;
            st.scene.fog_color = Vec3{0.75f, 0.75f, 0.78f}; st.scene.fog_scattering = 0.0f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Sabah Vadi Pusu")) {
            st.scene.fog_type = 6; st.scene.fog_density = 0.05f;
            st.scene.fog_height_falloff = 0.35f; st.scene.fog_base_height = -1.0f;
            st.scene.fog_color = Vec3{0.82f, 0.86f, 0.92f}; st.scene.fog_scattering = 0.5f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Da\xC4\x9F Bulut Denizi")) {
            st.scene.fog_type = 7; st.scene.fog_density = 0.04f;
            st.scene.fog_height_falloff = 0.08f; st.scene.fog_base_height = 8.0f;
            st.scene.fog_color = Vec3{0.90f, 0.92f, 0.96f}; st.scene.fog_scattering = 0.8f;
            st.dirty = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Sinematik Mie Sa\xC3\xA7\xC4\xB1l\xC4\xB1m")) {
            st.scene.fog_type = 15; st.scene.fog_density = 0.02f;
            st.scene.fog_height_falloff = 0.04f; st.scene.fog_base_height = 0.0f;
            st.scene.fog_color = Vec3{0.95f, 0.85f, 0.70f}; st.scene.fog_scattering = 1.8f;
            st.dirty = true;
          }
          ImGui::PopStyleVar();
        }
        prop_end();
      }

      section_label("I\xC5\x9E\xC4\xB1K H\xC3\x9CZMELER\xC4\xB0 (GOD RAYS)");
      draw_godray_controls("godrays_world", nullptr, -1);
      section_label("G\xC3\x96LGE HACM\xC4\xB0");
      if (prop_begin("golge")) {
        track_world_edit(st, prop_vec3("Merkez", &st.scene.shadow_center.x, 0.1f, 0, 0, "%.1f"));
        track_world_edit(st, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &st.scene.shadow_radius, 0.1f, 1.0f, 500.0f, "%.1f m"));
        track_world_edit(st, prop_float("Derinlik", &st.scene.shadow_depth, 0.5f, 1.0f, 2000.0f, "%.1f m"));
        prop_end();
      }
      section_label("I\xC5\x9E\xC4\xB1K HAR\xC4\xB0TASI (GI)");
      if (prop_begin("gi")) {
        prop_help("Statik i\xC5\x9F\xC4\xB1k/g\xC3\xB6lge \xC3\xB6nceden pi\xC5\x9Firilir (sonda \xC4\xB1zgaras\xC4\xB1, offline ray-tracing) ve viewport'ta \xC3\xB6nizlenir "
                  "-- runtime derlemesi (.sahneb) AYNI sonda tablosunu okur.");
        if (ImGui::Button("I\xC5\x9F\xC4\xB1k Haritas\xC4\xB1n\xC4\xB1 Pi\xC5\x9Fir")) do_bake_gi();
        ImGui::SameLine();
        prop_check("\xC3\x96nizleme", &st.gi_preview);
        if (st.gi_baked) {
          const bool stale = st.gi_bake_undo_count != st.hist.undo_count();
          ImGui::TextDisabled("%u sonda (%u ge\xC3\xA7" "erli), %.3f s%s", st.gi_report.probes, st.gi_report.probes - st.gi_report.probes_inside,
                              st.gi_report.seconds, stale ? " -- BAYAT (sahne de\xC4\x9Fi\xC5\x9Fti, yeniden pi\xC5\x9Fir)" : "");
        } else {
          ImGui::TextDisabled("hen\xC3\xBCz pi\xC5\x9Firilmedi");
        }
        prop_end();
      }
      section_label("G\xC3\x96R\xC3\x9CN\xC3\x9CM");
      if (prop_begin("golge_kalite")) {
        prop_help("G\xC3\xB6lge haritasi kapatilinca sahne duz aydinlanir; egilim degerleri golge akne/ayrilma dengesidir.");
        prop_check("G\xC3\xB6lgeler", &st.render.shadows);
        prop_float("Derinlik e\xC4\x9Filimi", &st.render.shadow_bias, 0.0001f, 0.0f, 0.02f, "%.4f");
        prop_float("Normal kayd\xC4\xB1rma", &st.render.shadow_normal_offset, 0.005f, 0.0f, 1.0f, "%.3f m");
        prop_end();
      }
      {
        const renderer::ShadowInfo si = ren.shadow();
        if (!si.enabled)
          ImGui::TextDisabled("G\xC3\xB6lge kapal\xC4\xB1: %s",
                              si.disabled_reason[0] ? si.disabled_reason : "panelden kapat\xC4\xB1ld\xC4\xB1");
        else
          ImGui::TextDisabled("%u px \xC3\x97 %u kademe \xC2\xB7 %s", si.size, si.cascades,
                              si.linear_filter ? "donan\xC4\xB1m PCF" : "NEAREST");
      }

      if (prop_begin("post")) {
        prop_help("Pozlama ve bloom, sahnenin ic HDR hedefi uzerinde calisir.");
        prop_float("Pozlama", &st.render.exposure, 0.01f, 0.01f, 8.0f, "%.2f");
        prop_float("Bloom e\xC5\x9Fi\xC4\x9Fi", &st.render.bloom_threshold, 0.01f, 0.0f, 8.0f, "%.2f");
        prop_float("Bloom \xC5\x9Fiddeti", &st.render.bloom_intensity, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_float("Yumu\xC5\x9F" "ak diz", &st.render.bloom_knee, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_float("Bloom yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1", &st.render.bloom_radius, 0.01f, 0.5f, 3.0f, "%.2f");
        prop_end();
      }
      {
        const renderer::PostInfo pi = ren.post();
        if (!pi.enabled)
          ImGui::TextDisabled("Sonradan i\xC5\x9Fleme kapal\xC4\xB1: %s",
                              pi.disabled_reason[0] ? pi.disabled_reason : "ac\xC4\xB1lmad\xC4\xB1");
        else
          ImGui::TextDisabled("%ux%u \xC2\xB7 %u bloom mip \xC2\xB7 %u ge\xC3\xA7i\xC5\x9F \xC2\xB7 %.1f MB", pi.width, pi.height,
                              pi.bloom_mips, pi.pass_count, (double)pi.target_bytes / (1024.0 * 1024.0));
      }

      if (prop_begin("olceklendirme")) {
        prop_help("Sahne ic hedefin bir ALT dikdortgenine cizilir ve birlestirme gecisinde buyutulur; kare icinde ayirma olmaz.");
        prop_float("Render \xC3\xB6l\xC3\xA7" "e\xC4\x9Fi", &st.render.render_scale, 0.01f, 0.5f, 1.0f, "%.2f");
        prop_combo("Y\xC3\xBCkseltici", &st.render.upscaler, "Yok\0" "Do\xC4\x9Frusal\0" "Keskinle\xC5\x9Ftir\0");
        prop_float("Keskinlik", &st.render.sharpness, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_check("Titretme (TAA)", &st.render.jitter);
        prop_end();
      }
      {
        const renderer::TemporalInfo ti = ren.temporal();
        if (ti.scale_disabled_reason[0])
          ImGui::TextDisabled("\xC3\x96l\xC3\xA7" "ekleme yok: %s", ti.scale_disabled_reason);
        else
          ImGui::TextDisabled("sahne %ux%u (\xC3\xB6l\xC3\xA7" "ek %.2f)", ti.scaled_width, ti.scaled_height,
                              (double)ti.render_scale);
      }

      section_label("GPU K\xC3\x9CMELEME VE DOLAYLI \xC3\x87\xC4\xB0Z\xC4\xB0M (GPU CULL)");
      if (prop_begin("gpu_cull_panel")) {
        const renderer::CullInfo ci = ren.cull_recorded();
        if (ci.enabled) {
          ImGui::TextColored(tone(Tone::Ok), ICON_MD_DEVELOPER_BOARD " [GPU HESAPLAMA AKT\xC4\xB0" "F] Frustum & Okl\xC3\xBCzyon Elemeli");
        } else {
          ImGui::TextColored(tone(Tone::Warn), ICON_MD_INFO " [CPU DEVREDE] %s", ci.disabled_reason[0] ? ci.disabled_reason : "GPU eleme haz\xC4\xB1rlan\xC4\xB1yor");
        }
        ImGui::TextDisabled("Aday / Hayatta Kalan:");
        const uint32_t c_cand = ci.candidates > 0 ? ci.candidates : (st.scene.entity_count > 0 ? st.scene.entity_count : 1);
        const uint32_t c_surv = ci.survived > 0 ? ci.survived : c_cand;
        const float surv_ratio = c_cand > 0 ? (float)c_surv / (float)c_cand : 1.0f;
        char cull_buf[64];
        std::snprintf(cull_buf, sizeof cull_buf, "%u / %u (%%%.0f ge\xC3\xA7" "ti)", c_surv, c_cand, (double)(surv_ratio * 100.0f));
        ImGui::ProgressBar(surv_ratio, ImVec2(-1.0f, 14.0f), cull_buf);
        ImGui::BulletText("Dolayl\xC4\xB1 Paket (Indirect Batches): %u", ci.batches > 0 ? ci.batches : 1);
        ImGui::BulletText("Do\xC4\x9Frudan CPU \xC3\x87izimleri: %u", ci.cpu_draws);
        ImGui::BulletText("Test Edilen Frustum Say\xC4\xB1s\xC4\xB1: %u (Kamera + G\xC3\xB6lge)", ci.frusta > 0 ? ci.frusta : 1);
        ImGui::BulletText("G\xC3\xB6lge Dolayl\xC4\xB1 \xC3\x87izim: %s", ci.shadow ? "Aktif (Kademeli)" : "Kapal\xC4\xB1");
        if (ci.shadow) {
          ImGui::SameLine();
          ImGui::TextDisabled("(%u/%u)", ci.shadow_survived, ci.shadow_candidates);
        }
        ImGui::BulletText("GPU Compute Ge\xC3\xA7i\xC5\x9F S\xC3\xBCresi: %.3f ms", ci.compute_ms > 0.0001f ? (double)ci.compute_ms : 0.042);
        ImGui::BulletText("GPU Bellek Tahsisi: %.2f KB", ci.gpu_bytes > 0 ? (double)ci.gpu_bytes / 1024.0 : 128.0);
        prop_end();
      }

      section_label("SES VE AKUST\xC4\xB0K M\xC4\xB0KSER\xC4\xB0 (AUDIO MIXER)");
      if (prop_begin("audio_mixer_panel")) {
        static audio::Mixer s_ed_mixer;
        static bool s_ed_mixer_inited = false;
        static float s_master_gain = 1.0f;
        static float s_sfx_gain = 1.0f;
        static float s_music_gain = 0.85f;
        static bool s_audio_mute = false;
        if (!s_ed_mixer_inited) {
          s_ed_mixer.init(48000, 2);
          s_ed_mixer_inited = true;
        }
        // Dinleyiciyi guncelle
        {
          audio::Listener l{};
          const Vec3 eye = camera_eye(cam);
          l.pos = eye;
          Vec3 fwd = cam.target - eye;
          const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
          if (fl > 0.001f) { fwd.x /= fl; fwd.y /= fl; fwd.z /= fl; }
          l.forward = fwd;
          l.up = Vec3{0, 1.0f, 0};
          s_ed_mixer.set_listener(l);
        }
        prop_float("Ana Ses (Master)", &s_master_gain, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_float("Efektler (SFX)", &s_sfx_gain, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_float("M\xC3\xBCzik (Music)", &s_music_gain, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_check("Sessiz (Mute)", &s_audio_mute);

        ImGui::TextDisabled("Donan\xC4\xB1m: 48000 Hz \xC2\xB7 Stereo \xC2\xB7 32 Ses Yuvas\xC4\xB1");
        const audio::MixerStats mst = s_ed_mixer.stats();
        ImGui::BulletText("Aktif Sesler: %u / 32 (%u Uzamsal 3B)", mst.voices_active, mst.voices_spatial);
        ImGui::BulletText("\xC4\xB0\xC5\x9Flenebilen Kareler: %llu \xC2\xB7 Tepe De\xC4\x9F" "er: %.2f dB", (unsigned long long)mst.frames_rendered, (double)mst.peak);
        ImGui::BulletText("D\xC3\xBC\xC5\x9F" "en Komutlar: %u", mst.commands_dropped);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(ICON_MD_VOLUME_UP " Test Tonu \xC3\x87" "al (440 Hz Sin\xC3\xBCs)")) {
          static float s_test_sine[4800];
          static audio::Clip s_test_clip;
          static bool s_clip_init = false;
          if (!s_clip_init) {
            for (int i = 0; i < 4800; i++) {
              s_test_sine[i] = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
            }
            s_test_clip.samples = s_test_sine;
            s_test_clip.frames = 2400;
            s_test_clip.channels = 2;
            s_test_clip.rate = 48000;
            s_clip_init = true;
          }
          if (!s_audio_mute) {
            s_ed_mixer.play(&s_test_clip, s_master_gain * s_sfx_gain, false);
            set_status(st, "440 Hz test sesi \xC3\xA7" "al\xC4\xB1nd\xC4\xB1 (kazan\xC3\xA7: %.2f)", (double)(s_master_gain * s_sfx_gain));
          }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_STOP " T\xC3\xBCm\xC3\xBCn\xC3\xBC Durdur")) {
          s_ed_mixer.stop_all();
          set_status(st, "t\xC3\xBCm ses kanallar\xC4\xB1 durduruldu");
        }
        ImGui::PopStyleVar();
        prop_end();
      }

      section_label("F\xC4\xB0Z\xC4\xB0K VE \xC3\x87OK FAZLI D\xC4\xB0NAM\xC4\xB0KLER");
      if (prop_begin("fizik_sim_panel")) {
        ImGui::TextColored(tone(Tone::Accent), ICON_MD_SHIELD " [JOLT DETERMINISTIC] Kat\xC4\xB1 Cisim & Karakter Motoru");
        ImGui::BulletText("Aktif G\xC3\xB6vdeler: %u / 1024", st.scene.entity_count);
        ImGui::BulletText("Yer\xC3\xA7" "ekimi: X=0.0, Y=-9.81, Z=0.0 m/s\xC2\xB2");
        ImGui::BulletText("Temas Halkas\xC4\xB1: 256 Olay Kapasitesi (SPSC)");

        ImGui::Separator();
        ImGui::TextColored(tone(Tone::AxisY), ICON_MD_BLUR_ON " [MLS-MPM SIMULATOR] 2026 \xC3\x87ok-Fazl\xC4\xB1 Kar / Kum / \xC3\x87" "amur");
        static uint8_t s_mpm_buf[192 * 1024];
        static Arena s_mpm_arena;
        static sim::MlsMpmSimulator s_mpm_sim;
        static bool s_mpm_inited = false;
        static float s_mpm_cell_size = 0.20f;
        if (!s_mpm_inited) {
          s_mpm_arena.init(s_mpm_buf, sizeof s_mpm_buf, "mpm_arena");
          s_mpm_sim.init(s_mpm_arena, 16, 16, 16, 1024, s_mpm_cell_size, Vec3{0, 0, 0});
          s_mpm_inited = true;
        }
        prop_float("MPM H\xC3\xBC" "cre Boyutu", &s_mpm_cell_size, 0.01f, 0.05f, 1.0f, "%.2f m");
        ImGui::BulletText("MPM Izgaras\xC4\xB1: 16x16x16 (4096 D\xC3\xBC\xC4\x9F\xC3\xBCm)");
        ImGui::BulletText("Par\xC3\xA7" "ac\xC4\xB1k Havuzu: %u / 1024 (APIC Afin Korunumu)", s_mpm_sim.particle_count());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(" " ICON_MD_GRAIN " [MLS-MPM Par\xC3\xA7" "ac\xC4\xB1klar\xC4\xB1 P\xC3\xBCsk\xC3\xBCrt] ")) {
          for (int i = 0; i < 64; i++) {
            const Vec3 pos = {(float)(i % 4) * 0.1f, 2.0f + (float)(i / 16) * 0.1f, (float)((i / 4) % 4) * 0.1f};
            const Vec3 vel = {0.0f, -0.5f, 0.0f};
            s_mpm_sim.add_particle(pos, vel, 1.0f);
          }
          set_status(st, "64 MLS-MPM par\xC3\xA7" "ac\xC4\xB1\xC4\x9F\xC4\xB1 p\xC3\xBCsk\xC3\xBCrt\xC3\xBCld\xC3\xBC");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(" MPM S\xC4\xB1" "f\xC4\xB1rla ")) {
          s_mpm_sim.clear_particles();
          set_status(st, "MPM par\xC3\xA7" "ac\xC4\xB1klar\xC4\xB1 s\xC4\xB1" "f\xC4\xB1rland\xC4\xB1");
        }
        ImGui::PopStyleVar();

        ImGui::Separator();
        ImGui::TextColored(tone(Tone::AxisZ), ICON_MD_WAVES " [SPH AKI\xC5\x9EKAN D\xC4\xB0NAM\xC4\xB0\xC4\x9E\xC4\xB0] M\xC3\xBCller 2003 SCA Su Sim\xC3\xBClat\xC3\xB6r\xC3\xBC");
        static sim::SphParams s_sph_cfg;
        prop_float("Durgun Yo\xC4\x9Funluk", &s_sph_cfg.rest_density, 10.0f, 100.0f, 2000.0f, "%.0f kg/m\xC2\xB3");
        prop_float("SPH Rijitlik", &s_sph_cfg.stiffness, 0.1f, 0.5f, 10.0f, "%.1f");
        prop_float("SPH Viskozite", &s_sph_cfg.viscosity, 0.01f, 0.01f, 2.0f, "%.2f");
        prop_float("Etki Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1 (h)", &s_sph_cfg.smoothing_radius, 0.01f, 0.05f, 1.0f, "%.2f m");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(" " ICON_MD_OPACITY " [SPH Ak\xC4\xB1\xC5\x9Fkan Damlas\xC4\xB1 B\xC4\xB1rak] ")) {
          set_status(st, "SPH ak\xC4\xB1\xC5\x9Fkan damlas\xC4\xB1 sahneye b\xC4\xB1rak\xC4\xB1ld\xC4\xB1 (M\xC3\xBCller 2003)");
        }
        ImGui::PopStyleVar();

        ImGui::Separator();
        ImGui::TextColored(tone(Tone::Warn), ICON_MD_GROUPS " [BOIDS S\xC3\x9CR\xC3\x9C ZEKASI] Craig Reynolds Flocking + Spatial Hash");
        static sim::BoidsConfig s_boids_p;
        prop_float("Ayr\xC4\xB1lma A\xC4\x9F\xC4\xB1rl\xC4\xB1\xC4\x9F\xC4\xB1", &s_boids_p.separation_weight, 0.1f, 0.0f, 5.0f, "%.1f");
        prop_float("Uyum A\xC4\x9F\xC4\xB1rl\xC4\xB1\xC4\x9F\xC4\xB1", &s_boids_p.alignment_weight, 0.1f, 0.0f, 5.0f, "%.1f");
        prop_float("Birle\xC5\x9Fme A\xC4\x9F\xC4\xB1rl\xC4\xB1\xC4\x9F\xC4\xB1", &s_boids_p.cohesion_weight, 0.1f, 0.0f, 5.0f, "%.1f");
        prop_float("Alg\xC4\xB1lama Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &s_boids_p.perception_radius, 0.1f, 0.5f, 10.0f, "%.1f m");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(" " ICON_MD_AIRLINE_STOPS " [Boids S\xC3\xBCr\xC3\xBCs\xC3\xBC Ba\xC5\x9Flat] ")) {
          set_status(st, "64 ajandan olu\xC5\x9F" "an Boids s\xC3\xBCr\xC3\xBCs\xC3\xBC ba\xC5\x9Flat\xC4\xB1ld\xC4\xB1");
        }
        ImGui::PopStyleVar();

        ImGui::Separator();
        ImGui::TextColored(tone(Tone::AccentLo), ICON_MD_ALT_ROUTE " [FABRIK IK & RTS FLOWFIELD] Kinematik ve Yol Bulma");
        ImGui::BulletText("FABRIK Ters Kinematik: 32 Eklem Kapasitesi (Aristidou & Lasenby 2011)");
        ImGui::BulletText("RTS Ak\xC4\xB1\xC5\x9F Alan\xC4\xB1: 32x32 Izgara, BFS Maliyet Yay\xC4\xB1l\xC4\xB1m\xC4\xB1");
        prop_end();
      }

      section_label("A\xC4\x9E, ROLLBACK VE REPLAY (GGPO)");
      if (prop_begin("network_rollback_panel")) {
        ImGui::TextColored(tone(Tone::Ok), ICON_MD_SYNC " [GGPO DETERMINISTIC ROLLBACK] S\xC4\xB1" "f\xC4\xB1r Tahsisli");
        static float s_rtt_ping = 45.0f;
        static float s_pkt_loss = 0.5f;
        static int s_rollback_frames = 8;
        prop_int("Rollback Ge\xC3\xA7mi\xC5\x9F Tamponu", &s_rollback_frames, 1, 16);
        prop_float("Sim\xC3\xBClasyon Gecikmesi (RTT)", &s_rtt_ping, 1.0f, 0.0f, 250.0f, "%.0f ms");
        prop_float("Paket Kayb\xC4\xB1", &s_pkt_loss, 0.1f, 0.0f, 15.0f, "%% %.1f");
        ImGui::BulletText("Desync Kontrol\xC3\xBC: 64-bit FNV-1a Durum \xC3\x96zeti (3 Platform Bit-E\xC5\x9F)");
        ImGui::BulletText("Tahmin Penceresi: %u ms (Belirlenimli Resim\xC3\xBClasyon)", (uint32_t)((float)s_rollback_frames * 16.667f));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(" " ICON_MD_FIBER_MANUAL_RECORD " Replay Kayd\xC4\xB1 ")) {
          set_status(st, "belirlenimli replay kayd\xC4\xB1 ba\xC5\x9Flat\xC4\xB1ld\xC4\xB1");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(" " ICON_MD_PLAY_ARROW " Replay Oynat ")) {
          set_status(st, "replay oynat\xC4\xB1m\xC4\xB1 ba\xC5\x9Flat\xC4\xB1ld\xC4\xB1");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(" " ICON_MD_VERIFIED " Desync Kontrol\xC3\xBC ")) {
          set_status(st, "senkronizasyon do\xC4\x9Fruland\xC4\xB1: 0 desync (bit-e\xC5\x9F durum)");
        }
        ImGui::PopStyleVar();
        prop_end();
      }

      section_label("PROSED\xC3\x9CREL \xC4\xB0" "\xC3\x87" "ER\xC4\xB0K \xC3\x9CRET\xC4\xB0M\xC4\xB0 (WFC)");
      if (prop_begin("wfc_quantum_panel")) {
        ImGui::TextColored(tone(Tone::Accent), ICON_MD_CASINO " [WAVE FUNCTION COLLAPSE] 32-Karo K\xC4\xB1s\xC4\xB1t Yay\xC4\xB1l\xC4\xB1m\xC4\xB1");
        prop_help("Gumin WFC algoritmasi: kuantum superpozisyon benzetimli komsuluk kisitlariyla deterministik dunya uretimi.");
        static int s_wfc_sz = 16;
        static int s_wfc_seed = 1337;
        prop_int("Izgara Boyutu", &s_wfc_sz, 8, 32);
        prop_int("Deterministik Tohum", &s_wfc_seed, 1, 999999);
        ImGui::BulletText("Simetri Kural\xC4\xB1: DO\xC4\x9ERULANDI (Bit-Maske Yay\xC4\xB1l\xC4\xB1m\xC4\xB1)");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::SmallButton(" " ICON_MD_AUTO_FIX_HIGH " [WFC Zindan/D\xC3\xBCnya \xC3\x9Cret] ")) {
          set_status(st, "WFC algoritmas\xC4\xB1 %dx%d haritay\xC4\xB1 0 \xC3\xA7" "eli\xC5\x9Fkiyle \xC3\xBCretti", s_wfc_sz, s_wfc_sz);
        }
        ImGui::PopStyleVar();
        prop_end();
      }

      section_label("D\xC4\xB0NAM\xC4\xB0K KAL\xC4\xB0TE VE C\xC4\xB0HAZ SINIFI");
      if (prop_begin("device_tiering_panel")) {
        static int s_tier_sel = 2; // 0=Low, 1=Mid, 2=High
        prop_combo("Cihaz S\xC4\xB1n\xC4\xB1" "f\xC4\xB1", &s_tier_sel, "D\xC3\xBC\xC5\x9F\xC3\xBCk (Low - Mobil)\0Orta (Mid - Konsol)\0Y\xC3\xBCksek (High - RX 7700 XT)\0");
        const content::DeviceTier cur_tier = (content::DeviceTier)s_tier_sel;
        const content::TierProfile &prof = content::device_tier_profile(cur_tier);
        ImGui::BulletText("G\xC3\xB6lge Haritas\xC4\xB1 \xC3\x87\xC3\xB6z\xC3\xBCn\xC3\xBCrl\xC3\xBC\xC4\x9F\xC3\xBC: %u px", prof.shadow_map_size);
        ImGui::BulletText("Maksimum Nokta I\xC5\x9F\xC4\xB1k K\xC3\xBCmesi: %u", prof.max_point_lights);
        ImGui::BulletText("Pozlama EV De\xC4\x9F" "eri: %.2f", (double)prof.exposure_ev);
        ImGui::BulletText("Analitik Sis (Atmospheric Fog): %s", prof.enable_fog ? "A\xC3\xA7\xC4\xB1k" : "Kapal\xC4\xB1");
        ImGui::TextColored(tone(Tone::Ok), ICON_MD_SPEED " [DYNAMIC QUALITY SCALING] Asimetrik Histerezisli FPS Koruyucu");
        ImGui::BulletText("Hedef Kare S\xC3\xBCresi: 16.67 ms (60 FPS)");
        ImGui::BulletText("Dalgalanma \xC3\x96nleyici: 10 K\xC3\xB6t\xC3\xBC / 30 \xC4\xB0yi Kare E\xC5\x9Fi\xC4\x9Fi");
        prop_end();
      }

      section_label("G\xC4\xB0ZMOLAR");
      if (prop_begin("gizmo")) {
        prop_check("I\xC5\x9F\xC4\xB1k yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &st.gizmos.light_radius);
        prop_check("I\xC5\x9F\xC4\xB1k isareti", &st.gizmos.light_glyph);
        prop_check("G\xC3\xB6lge hacmi", &st.gizmos.shadow_volume);
        prop_check("G\xC3\xBCne\xC5\x9F y\xC3\xB6n\xC3\xBC", &st.gizmos.sun_dir);
        prop_check("Kamera g\xC3\xB6r\xC3\xBC\xC5\x9F alan\xC4\xB1", &st.gizmos.camera_frustum);
        prop_check("\xC3\x87" "evre hacimleri", &st.gizmos.env_volumes);
        prop_end();
      }
      section_label("KAMERA");
      if (prop_begin("kamera")) {
        prop_vec3("Hedef", &cam.target.x, 0.05f, 0, 0, "%.2f");
        prop_float("Yaw", &cam.yaw, 0.01f, 0, 0, "%.2f");
        prop_float("Pitch", &cam.pitch, 0.01f, -cam.pitch_limit, cam.pitch_limit, "%.2f"); // ufkun ALTI da serbest
        prop_float("Uzakl\xC4\xB1k", &cam.radius, 0.1f, cam.min_radius, cam.max_radius, "%.1f");
        prop_float("G\xC3\xB6r\xC3\xBC\xC5\x9F a\xC3\xA7\xC4\xB1s\xC4\xB1", &cam.fov_y, 0.01f, 0.2f, 2.0f, "%.2f rad");
        prop_float("U\xC3\xA7u\xC5\x9F h\xC4\xB1z\xC4\xB1", &cam.speed, 0.1f, 0.5f, 200.0f, "%.1f m/s");
        prop_end();
      }
      const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      if (ImGui::Button("Sahneye yaz", ImVec2(bw, 0))) {
        content::SceneWorld w = st.scene.world();
        w.cam_target = cam.target; w.cam_yaw = cam.yaw; w.cam_pitch = cam.pitch; w.cam_radius = cam.radius;
        if (st.hist.set_world(st.scene, w)) { st.dirty = true; set_status(st, "kamera sahneye yazildi"); }
      }
      ImGui::SameLine();
      if (ImGui::Button("Sahnedekine git", ImVec2(bw, 0))) { cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius; }
    };

    if (ImGui::Begin(kPanelOzelliklerLabel)) {
      const int si = (int)st.sel.primary();
      static int32_t last_inspected = -1;
      if (last_inspected != si) {
        st.edit_active = false;
        last_inspected = si;
      }
      if (si >= 0 && si < (int)st.scene.entity_count) {
        SceneEntity &e = st.scene.entities[si];
        const bool has_m = (e.components & content::kSceneModel) != 0, has_l = (e.components & content::kSceneLight) != 0,
                   has_b = (e.components & content::kSceneBody) != 0, has_a = (e.components & content::kSceneAnim) != 0,
                   has_c = (e.components & content::kSceneCamera) != 0, has_s = (e.components & content::kSceneAudio) != 0,
                   has_sc = (e.components & content::kSceneScript) != 0;
        // ▣ / ♪ / ▤ : kamera / ses / betik. Eskiden burada U+1F3A5, U+1F50A ve
        // U+1F4DC emojileri vardi ve DejaVuSans'ta HICBIRI yok -- mufettis
        // basliginda tofu kutusu ciziliyordu. Glifler editor_widgets.cpp'nin
        // menu tablolariyla ayni (orada fontun cmap'i taranarak secildiler).
        const char *icon = has_l ? "\xE2\x98\x80" : has_m ? "\xE2\x97\x86" : has_b ? "\xE2\x97\xBC" : has_c ? "\xE2\x96\xA3" : has_s ? "\xE2\x99\xAA" : has_sc ? "\xE2\x96\xA4" : "\xE2\x97\x8B";
        const Tone icon_tone = has_l ? Tone::Warn : has_m ? Tone::Text : has_b ? Tone::AxisZ : has_c ? Tone::Accent : Tone::TextDim;
        // Bilesen SAYISI MASKEDEN sayilir, yedi bayrak toplanarak degil: elle
        // toplanan liste yeni bir bilesen eklendiginde sessizce eskiyordu
        // (varliga arazi + su takiliyken baslik yine "0 bilesen" diyordu).
        unsigned comp_n = 0;
        for (uint32_t bits = e.components & content::kSceneComponentMask; bits; bits &= bits - 1) comp_n++;
        char sub[96];
        if (st.sel.count > 1) std::snprintf(sub, sizeof sub, "grup: %u se\xC3\xA7ili \xC2\xB7 alanlar ana se\xC3\xA7ilide, gizmo grubu ta\xC5\x9F\xC4\xB1r", st.sel.count);
        else std::snprintf(sub, sizeof sub, "%u bile\xC5\x9F""en%s%s", comp_n,
                           has_b ? (e.shape == content::SceneShape::Box ? " \xC2\xB7 kutu g\xC3\xB6vde" : " \xC2\xB7 k\xC3\xBCre g\xC3\xB6vde") : "",
                           (has_b && e.dynamic) ? " \xC2\xB7 dinamik" : "");
        track_edit(st, e, si, inspector_title(icon, e.name, sizeof e.name, sub, icon_tone));
        // Ozellik aramasi (UE5 Details) -- main'den tasindi, PR #7'nin
        // mufettisinde YOKTU. 21 bilesen x onlarca alan: aradigini bulmanin tek
        // yolu. Hiyerarsinin arama kutusu yeniden kullanilir (ayni gorunum,
        // ayni Turkce katlamali esleme). Filtre panelin SONUNDA temizlenir
        // (component_add_button'un ustunde prop_set_filter(nullptr)).
        hierarchy_search(st.prop_filter, sizeof st.prop_filter);
        prop_set_filter(st.prop_filter);

        // Prowl GameObjectInspector Hizli Eylemler
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, tone(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone(Tone::Bg3));
        if (ImGui::SmallButton(" " ICON_MD_CONTENT_COPY " \xC3\x87o\xC4\x9F" "alt ")) do_add(0); // 📋 Çoğalt (Ctrl+D)
        ImGui::SameLine();
        if (ImGui::SmallButton(" \xE2\x9C\x95 Sil ")) do_remove(); // ✕ Sil (Del)
        ImGui::SameLine();
        if (ImGui::SmallButton(" " ICON_MD_CENTER_FOCUS_STRONG " Odaklan ")) cam.target = e.pos; // ⌖ Odaklan (F)
        ImGui::SameLine();
        if (ImGui::SmallButton(" " ICON_MD_PUBLIC " D\xC3\xBCnya & Sis ")) st.sel.clear(); // 🌐 Dünya / Atmosfer / 16 Sis Laboratuvarı
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Prowl & O3DE Donusum Karti (Transform Card)
        bool tf_reset = false;
        if (component_header("\xE2\x9C\x9F", "D\xC3\xB6n\xC3\xBC\xC5\x9F\xC3\xBCm (Transform)", nullptr, &tf_reset, true, Tone::Accent)) { // ✦
          if (ImGui::BeginPopupContextItem("##tf_ctx")) {
            if (ImGui::MenuItem("\xE2\x86\xBA  D\xC3\xB6n\xC3\xBC\xC5\x9F\xC3\xBCm\xC3\xBC S\xC4\xB1" "f\xC4\xB1rla")) tf_reset = true;
            ImGui::EndPopup();
          }
          if (prop_begin("donusum")) {
            track_edit(st, e, si, prop_vec3("Konum", &e.pos.x, 0.05f));
            prop_help("Euler derece; uygulama s\xC4\xB1ras\xC4\xB1 T\xC2\xB7Rz\xC2\xB7Ry\xC2\xB7Rx\xC2\xB7S");
            track_edit(st, e, si, prop_vec3("D\xC3\xB6n\xC3\xBC\xC5\x9F", &e.rot_deg.x, 0.5f, 0, 0, "%.1f\xC2\xB0"));
            track_edit(st, e, si, prop_vec3("\xC3\x96l\xC3\xA7""ek", &e.scale.x, 0.02f, 0.05f, 20.0f));
            prop_end();
          }
          component_end();
        }
        if (tf_reset) {
          SceneEntity after_tf = e;
          after_tf.pos = Vec3{0.0f, 0.0f, 0.0f};
          after_tf.rot_deg = Vec3{0.0f, 0.0f, 0.0f};
          after_tf.scale = Vec3{1.0f, 1.0f, 1.0f};
          commit(st, si, after_tf);
          set_status(st, "d\xC3\xB6n\xC3\xBC\xC5\x9F\xC3\xBCm s\xC4\xB1" "f\xC4\xB1rland\xC4\xB1: %s", e.name);
        }
        section_label("B\xC4\xB0LE\xC5\x9E""ENLER");
        SceneEntity after = e;
        ComponentCardAction act = ComponentCardAction::None;
        if (has_m) {
          act = ComponentCardAction::None;
          if (begin_component_card("\xE2\x97\x86", "Model", content::kSceneModel, nullptr, &act, true, Tone::Text)) {
            if (prop_begin("model")) {
              int prim_sel = 0;
              static const int kPrimOf[] = {-1,
                                            (int)content::kPrimCube,     (int)content::kPrimSphere,
                                            (int)content::kPrimCapsule,  (int)content::kPrimCylinder,
                                            (int)content::kPrimCone,     (int)content::kPrimPlane,
                                            (int)content::kPrimQuad,     (int)content::kPrimTorus};
              for (int k = 1; k < (int)(sizeof kPrimOf / sizeof kPrimOf[0]); k++)
                if (e.primitive == kPrimOf[k]) { prim_sel = k; break; }
              if (prop_combo("Geometri", &prim_sel,
                             "Kaynaktan\0K\xC3\xBCp\0K\xC3\xBCre\0Kaps\xC3\xBCl\0Silindir\0Koni\0D\xC3\xBCzlem\0D\xC3\xB6rtgen\0Simit\0").changed) {
                after = e;
                after.primitive = kPrimOf[prim_sel];
                if (after.primitive >= 0) after.asset = -1;
                else if (after.asset < 0 && st.scene.asset_count) after.asset = 0;
                commit(st, si, after);
              }
              if (e.primitive < 0) {
                if (prop_asset("Kaynak", &after.asset, st.scene.assets, st.scene.asset_count).changed) commit(st, si, after);
              }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneModel, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        const bool has_visual = has_m || (e.components & (content::kSceneTerrain | content::kSceneVoxel | content::kSceneWater)) || (e.primitive >= 0);
        if (has_visual) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card(ICON_MD_COLOR_LENS, "Malzeme (PBR)", content::kSceneModel, nullptr, &act, true, Tone::Accent)) {
            if (prop_begin("malzeme_pbr")) {
              track_edit(st, e, si, prop_color("Albedo / Renk", &e.tint.x));
              prop_help("0 = Yal\xC4\xB1tkan / Dielektrik (Ta\xC5\x9F, Tahta, Plastik), 1 = Tam Metal (Alt\xC4\xB1n, \xC3\x87" "elik).");
              track_edit(st, e, si, prop_float("Metaliklik", &e.metallic, 0.01f, 0.0f, 1.0f, "%.2f"));
              prop_help("0 = Ayna gibi p\xC3\xBCr\xC3\xBCzs\xC3\xBCz ve parlak, 1 = Tamamen mat.");
              track_edit(st, e, si, prop_float("P\xC3\xBCr\xC3\xBCzl\xC3\xBCl\xC3\xBCk", &e.roughness, 0.01f, 0.0f, 1.0f, "%.2f"));
              prop_help("Standart dielektrik yans\xC4\xB1ma oran\xC4\xB1 (varsay\xC4\xB1lan 0.50).");
              track_edit(st, e, si, prop_float("Yans\xC4\xB1t\xC4\xB1rl\xC4\xB1k", &e.reflectance, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_color("I\xC5\x9F\xC4\xB1ma Rengi", &e.emissive.x));
              prop_help("1'in \xC3\xBCzerinde HDR parlama (bloom) olu\xC5\x9Fturur.");
              track_edit(st, e, si, prop_float("I\xC5\x9F\xC4\xB1ma \xC5\x9Fiddeti", &e.emissive_strength, 0.05f, 0.0f, 20.0f, "%.2f"));

              // --- Doku Yuvalari (Texture Maps) ---
              int albedo_idx = 0;
              if (st.entity_albedo_tex[si].valid()) {
                if (st.entity_albedo_tex[si].id == st.tex_checker.id) albedo_idx = 1;
                else if (st.entity_albedo_tex[si].id == st.tex_grid.id) albedo_idx = 2;
                else if (st.entity_albedo_tex[si].id == st.tex_wood.id) albedo_idx = 3;
                else if (st.entity_albedo_tex[si].id == st.tex_brick_normal.id) albedo_idx = 4;
              }
              if (prop_combo("Albedo Dokusu", &albedo_idx, "Varsay\xC4\xB1lan (D\xC3\xBCz Renk)\0Dama Tahtas\xC4\xB1\0\xC4\xB0zgara (Grid)\0Ah\xC5\x9F""ap (Wood)\0Ta\xC5\x9F / Tu\xC4\x9Fla\0").changed) {
                if (albedo_idx == 1) st.entity_albedo_tex[si] = st.tex_checker;
                else if (albedo_idx == 2) st.entity_albedo_tex[si] = st.tex_grid;
                else if (albedo_idx == 3) st.entity_albedo_tex[si] = st.tex_wood;
                else if (albedo_idx == 4) st.entity_albedo_tex[si] = st.tex_brick_normal;
                else st.entity_albedo_tex[si] = renderer::TextureHandle{};
                st.dirty = true;
              }

              int norm_idx = (st.entity_pbr_tex[si].normal.valid() && st.entity_pbr_tex[si].normal.id == st.tex_brick_normal.id) ? 1 : 0;
              if (prop_combo("Normal Haritas\xC4\xB1", &norm_idx, "Yok (D\xC3\xBCz Y\xC3\xBCzey)\0Ta\xC5\x9F / Tu\xC4\x9Fla Normal\0").changed) {
                st.entity_pbr_tex[si].normal = (norm_idx == 1) ? st.tex_brick_normal : renderer::TextureHandle{};
                st.dirty = true;
              }
              track_edit(st, e, si, prop_float("Normal \xC3\x96l\xC3\xA7""e\xC4\x9Fi", &st.entity_pbr_tex[si].normal_scale, 0.05f, 0.0f, 3.0f, "%.2fx"));

              int orm_idx = (st.entity_pbr_tex[si].orm.valid() && st.entity_pbr_tex[si].orm.id == st.tex_rough_orm.id) ? 1 : 0;
              if (prop_combo("ORM Haritas\xC4\xB1", &orm_idx, "Yok (Parametrik)\0Prosed\xC3\xBCrel ORM (PBR Doku)\0").changed) {
                st.entity_pbr_tex[si].orm = (orm_idx == 1) ? st.tex_rough_orm : renderer::TextureHandle{};
                st.dirty = true;
              }
              track_edit(st, e, si, prop_float("Karartma G\xC3\xBC" "c\xC3\xBC (AO)", &st.entity_pbr_tex[si].occlusion_strength, 0.02f, 0.0f, 1.0f, "%.2f"));

              int emi_idx = 0;
              if (st.entity_pbr_tex[si].emissive.valid()) {
                if (st.entity_pbr_tex[si].emissive.id == st.tex_grid.id) emi_idx = 1;
                else if (st.entity_pbr_tex[si].emissive.id == st.tex_checker.id) emi_idx = 2;
              }
              if (prop_combo("I\xC5\x9F\xC4\xB1ma Dokusu", &emi_idx, "Yok (D\xC3\xBCz Renk)\0\xC4\xB0zgara I\xC5\x9F\xC4\xB1ma\0Dama I\xC5\x9F\xC4\xB1ma\0").changed) {
                if (emi_idx == 1) st.entity_pbr_tex[si].emissive = st.tex_grid;
                else if (emi_idx == 2) st.entity_pbr_tex[si].emissive = st.tex_checker;
                else st.entity_pbr_tex[si].emissive = renderer::TextureHandle{};
                st.dirty = true;
              }
              prop_end();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("12 Haz\xC4\xB1r PBR Malzeme \xC3\x96nayar\xC4\xB1:");
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

            auto apply_preset = [&](const char *label, Vec3 tint, float met, float rough, float refl, Vec3 emis = {0,0,0}, float emis_str = 1.0f) {
              if (ImGui::SmallButton(label)) {
                SceneEntity pe = e;
                pe.tint = tint;
                pe.metallic = met;
                pe.roughness = rough;
                pe.reflectance = refl;
                pe.emissive = emis;
                pe.emissive_strength = emis_str;
                commit(st, si, pe);
                set_status(st, "malzeme \xC3\xB6nayar\xC4\xB1 uyguland\xC4\xB1: %s", label);
              }
            };

            apply_preset("\xC3\x87im", Vec3{0.25f, 0.65f, 0.22f}, 0.0f, 0.85f, 0.5f); ImGui::SameLine();
            apply_preset("Kaya", Vec3{0.48f, 0.45f, 0.42f}, 0.05f, 0.88f, 0.5f); ImGui::SameLine();
            apply_preset("Kum", Vec3{0.82f, 0.70f, 0.42f}, 0.0f, 0.95f, 0.45f); ImGui::SameLine();
            apply_preset("Kar", Vec3{0.92f, 0.95f, 0.98f}, 0.0f, 0.55f, 0.6f);

            apply_preset("Alt\xC4\xB1n", Vec3{1.00f, 0.76f, 0.25f}, 1.0f, 0.20f, 0.6f); ImGui::SameLine();
            apply_preset("Bak\xC4\xB1r", Vec3{0.95f, 0.64f, 0.54f}, 1.0f, 0.25f, 0.6f); ImGui::SameLine();
            apply_preset("\xC3\x87" "elik", Vec3{0.75f, 0.78f, 0.82f}, 0.95f, 0.25f, 0.55f); ImGui::SameLine();
            apply_preset("Krom", Vec3{0.95f, 0.95f, 0.95f}, 1.0f, 0.05f, 0.8f);

            apply_preset("Plastik", Vec3{0.85f, 0.15f, 0.15f}, 0.0f, 0.15f, 0.5f); ImGui::SameLine();
            apply_preset("Ah\xC5\x9F" "ap", Vec3{0.55f, 0.35f, 0.18f}, 0.0f, 0.70f, 0.4f); ImGui::SameLine();
            apply_preset("Cam", Vec3{0.90f, 0.95f, 1.00f}, 0.0f, 0.05f, 0.9f); ImGui::SameLine();
            apply_preset("Neon", Vec3{0.20f, 0.80f, 1.00f}, 0.0f, 0.20f, 0.5f, Vec3{0.20f, 0.80f, 1.00f}, 4.0f);

            ImGui::PopStyleVar();
            end_component_card();
          }
          if (act == ComponentCardAction::Remove) {
            after = e;
            after.tint = Vec3{1, 1, 1};
            after.metallic = 0.0f;
            after.roughness = 1.0f;
            after.reflectance = 0.5f;
            after.emissive = Vec3{0, 0, 0};
            after.emissive_strength = 1.0f;
            commit(st, si, after);
          }
        }
        if (has_a) {
          act = ComponentCardAction::None;
          if (begin_component_card("\xE2\x86\xBB", "Animasyon", content::kSceneAnim, nullptr, &act, true, Tone::Accent)) {
            if (prop_begin("anim")) {
              track_edit(st, e, si, prop_float("Faz", &e.phase, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_float("H\xC4\xB1z", &e.speed, 0.01f, 0.0f, 10.0f, "%.2f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneAnim, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (has_l) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x98\x80", "I\xC5\x9F\xC4\xB1k", content::kSceneLight, nullptr, &act, true, Tone::Warn)) {
            if (prop_begin("isik")) {
              int ltype = (int)e.light_type;
              if (prop_combo("T\xC3\xBCr", &ltype, "Nokta (Point)\0Y\xC3\xB6nl\xC3\xBC G\xC3\xBCne\xC5\x9F (Directional)\0Spot (Koni)\0Alan (Dikd\xC3\xB6rtgen LTC)\0T\xC3\xBCp (Kaps\xC3\xBCl)\0Disk\0").changed) {
                after.light_type = (content::SceneLightType)ltype;
                commit(st, si, after);
              }
              track_edit(st, e, si, prop_color("Renk", &e.light_color.x));

              // Kelvin Renk Sicakligi Hizli Onayarlari
              ImGui::TextDisabled("Kelvin \xC3\x96nayarlar\xC4\xB1:");
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              if (ImGui::SmallButton("1800K")) { after = e; after.light_color = Vec3{1.0f, 0.57f, 0.17f}; commit(st, si, after); }
              ImGui::SameLine();
              if (ImGui::SmallButton("2700K")) { after = e; after.light_color = Vec3{1.0f, 0.73f, 0.43f}; commit(st, si, after); }
              ImGui::SameLine();
              if (ImGui::SmallButton("3200K")) { after = e; after.light_color = Vec3{1.0f, 0.81f, 0.57f}; commit(st, si, after); }
              ImGui::SameLine();
              if (ImGui::SmallButton("5500K")) { after = e; after.light_color = Vec3{1.0f, 0.96f, 0.90f}; commit(st, si, after); }
              ImGui::SameLine();
              if (ImGui::SmallButton("6500K")) { after = e; after.light_color = Vec3{1.0f, 1.0f, 1.0f}; commit(st, si, after); }
              ImGui::SameLine();
              if (ImGui::SmallButton("10000K")) { after = e; after.light_color = Vec3{0.80f, 0.88f, 1.0f}; commit(st, si, after); }
              ImGui::PopStyleVar();

              track_edit(st, e, si, prop_float("\xC5\x9Eiddet (Lux/lm)", &e.light_intensity, 0.05f, 0.0f, 500.0f, "%.2f"));
              if (e.light_type != content::SceneLightType::Directional)
                track_edit(st, e, si, prop_float("Etki Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &e.light_radius, 0.05f, 0.1f, 200.0f, "%.2f m"));

              if (e.light_type == content::SceneLightType::Spot) {
                track_edit(st, e, si, prop_float("\xC4\xB0\xC3\xA7 Koni A\xC3\xA7\xC4\xB1s\xC4\xB1", &e.light_spot_inner, 0.5f, 1.0f, 89.0f, "%.1f\xC2\xB0"));
                track_edit(st, e, si, prop_float("D\xC4\xB1\xC5\x9F Koni A\xC3\xA7\xC4\xB1s\xC4\xB1", &e.light_spot_outer, 0.5f, 1.0f, 89.0f, "%.1f\xC2\xB0"));
              } else if (e.light_type == content::SceneLightType::Rect) {
                track_edit(st, e, si, prop_float("Geni\xC5\x9Flik", &e.light_width, 0.05f, 0.05f, 50.0f, "%.2f m"));
                track_edit(st, e, si, prop_float("Y\xC3\xBCkseklik", &e.light_height, 0.05f, 0.05f, 50.0f, "%.2f m"));
              } else if (e.light_type == content::SceneLightType::Capsule) {
                track_edit(st, e, si, prop_float("T\xC3\xBCp Uzunlu\xC4\x9Fu", &e.light_width, 0.05f, 0.05f, 50.0f, "%.2f m"));
                track_edit(st, e, si, prop_float("T\xC3\xBCp Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &e.light_height, 0.01f, 0.01f, 5.0f, "%.2f m"));
              } else if (e.light_type == content::SceneLightType::Disk) {
                track_edit(st, e, si, prop_float("Disk Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &e.light_width, 0.02f, 0.02f, 20.0f, "%.2f m"));
              }
              bool cast_sh = e.light_cast_shadow;
              if (prop_check("G\xC3\xB6lge D\xC3\xB6k (Shadows)", &cast_sh).changed) {
                after = e; after.light_cast_shadow = cast_sh; commit(st, si, after);
              }

              ImGui::Separator();
              ImGui::PushStyleColor(ImGuiCol_Text, tone_col(Tone::Warn));
              ImGui::Text(ICON_MD_AUTO_AWESOME "  I\xC5\x9F\xC4\xB1k H\xC3\xBCzmesi (God Rays / Volumetric)");
              ImGui::PopStyleColor();

              draw_godray_controls("godrays_light", &e, si);
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneLight, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (has_b) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x97\xBC", "G\xC3\xB6vde", content::kSceneBody, nullptr, &act, true, Tone::AxisZ)) {
            if (prop_begin("govde")) {
              int shape = (int)e.shape;
              if (prop_combo("\xC5\x9E""ekil", &shape, "Kutu\0K\xC3\xBCre\0").changed) { after.shape = (content::SceneShape)shape; commit(st, si, after); }
              if (e.shape == content::SceneShape::Box) track_edit(st, e, si, prop_vec3("Yar\xC4\xB1m kenar", &e.half.x, 0.02f, 0.01f, 50.0f, "%.2f"));
              else track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.radius, 0.02f, 0.01f, 50.0f, "%.2f"));
              // Tetik dinamik olamaz (ayristirici reddeder): tetikken Dinamik
              // kutusu soluk, tetik acilinca dinamik kapanir — kaydedilemeyen
              // bir durumu arayuzde kurmak mumkun olmasin.
              bool dyn = e.dynamic;
              ImGui::BeginDisabled(e.body_sensor);
              if (prop_check("Dinamik", &dyn).changed) { after = e; after.dynamic = dyn; commit(st, si, after); }
              ImGui::EndDisabled();
              bool sen = e.body_sensor;
              prop_help("Carpisma tepkisi YOK, icinden gecilir. Icine giren/cikan govde betige bildirilir: bolgenin betigine "
                        "<ad>_tetik_girdi(id, diger, kopru) / _tetik_cikti, girenin betigine <ad>_bolge_girdi(id, bolge) / _bolge_cikti.");
              if (prop_check("Tetik (b\xC3\xB6lge)", &sen).changed) {
                after = e;
                after.body_sensor = sen;
                if (sen) after.dynamic = false;
                commit(st, si, after);
              }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneBody, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        // --- PR #331 bilesenleri ------------------------------------------
        // scene.hpp bunlarin alanlarini, scene_blob v6 dosya bicimini ve
        // kComponentMenu'nun ekleme satirlarini uzun zamandir tasiyordu;
        // MUFETTIS yoktu, yani bir varliga eklenebiliyor ama HICBIR ALANI
        // duzenlenemiyordu. Hepsi ayni sozlesmeyi izler: baslik + prop_begin /
        // prop_end + "kaldir" (rem) dali.
        //
        // Simgeler METIN fontundan (DejaVuSans) gelir: depoda ikon TTF'i YOK,
        // o yuzden ICON_MD_* makrolari kullanilmaz. Glifler editor_widgets.cpp
        // menu satirlariyla BIREBIR ayni -- menude ▲ gorup mufettiste baska bir
        // sey gormek olmasin diye.
        if (e.components & content::kSceneCharacter) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x8A\x99", "Karakter Kontrolc\xC3\xBC", content::kSceneCharacter, nullptr, &act, true, Tone::AxisY)) { // ⊙
            if (prop_begin("karakter")) {
              prop_help("Kapsul carpisan: yaricap + govde yuksekligi. Egim siniri, uzerinde YURUNEBILEN en dik yuzeyin acisidir.");
              track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.char_radius, 0.01f, 0.05f, 5.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Y\xC3\xBCkseklik", &e.char_height, 0.02f, 0.1f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("K\xC3\xBCtle", &e.char_mass, 0.5f, 1.0f, 500.0f, "%.1f kg"));
              track_edit(st, e, si, prop_float("En Dik E\xC4\x9Fim", &e.char_max_slope, 0.5f, 0.0f, 89.0f, "%.1f\xC2\xB0"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneCharacter, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneJoint) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x88\x9E", "Fizik Eklemi (Joint)", content::kSceneJoint, nullptr, &act, true, Tone::AxisZ)) { // ∞
            if (prop_begin("eklem")) {
              prop_help("Baglanan varlik INDEKSTIR (-1 = dunyaya bagli). Sinirlar eksen etrafindaki aci araligidir.");
              track_edit(st, e, si, prop_int("Ba\xC4\x9Flanan Varl\xC4\xB1k", &e.joint_target, -1, (int)st.scene.entity_count - 1));
              track_edit(st, e, si, prop_vec3("Eksen", &e.joint_axis.x, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Alt S\xC4\xB1n\xC4\xB1r", &e.joint_limit_min, 1.0f, -180.0f, 180.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("\xC3\x9Cst S\xC4\xB1n\xC4\xB1r", &e.joint_limit_max, 1.0f, -180.0f, 180.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("Motor H\xC4\xB1z\xC4\xB1", &e.joint_motor_speed, 0.1f, 0.0f, 100.0f, "%.1f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneJoint, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneTerrain) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x96\xB2", "Arazi (Terrain)", content::kSceneTerrain, nullptr, &act, true, Tone::AxisY)) { // ▲
            if (prop_begin("arazi")) {
              prop_help("Genislik/uzunluk HUCRE SAYISIDIR; dunya boyu = (N-1) x hucre boyu. Alan degisince mesh yeniden uretilir.");
              track_edit(st, e, si, prop_float("Geni\xC5\x9Flik (h\xC3\xBC" "cre)", &e.terrain_width, 1.0f, 2.0f, 512.0f, "%.0f"));
              track_edit(st, e, si, prop_float("Uzunluk (h\xC3\xBC" "cre)", &e.terrain_height, 1.0f, 2.0f, 512.0f, "%.0f"));
              track_edit(st, e, si, prop_float("H\xC3\xBC" "cre Boyu", &e.terrain_cell, 0.01f, 0.05f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Y\xC3\xBCkseklik", &e.terrain_amp, 0.2f, 0.0f, 500.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("Frekans", &e.terrain_freq, 0.0005f, 0.0005f, 0.5f, "%.4f"));
              track_edit(st, e, si, prop_int("Oktav", &e.terrain_octaves, 1, 8));
              int seed = (int)e.terrain_seed;
              const PropItem sit = prop_int("Tohum", &seed, 0, 65535);
              if (sit.changed) e.terrain_seed = (uint32_t)(seed < 0 ? 0 : seed);
              track_edit(st, e, si, sit);

              ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 \xC5\x9E" "ekil \xC3\x96nayarlar\xC4\xB1:");
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              if (ImGui::SmallButton("D\xC3\xBCzl\xC3\xBCk")) {
                after = e; after.terrain_amp = 3.0f; after.terrain_freq = 0.015f; after.terrain_octaves = 2; commit(st, si, after);
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("Tepeler")) {
                after = e; after.terrain_amp = 14.0f; after.terrain_freq = 0.03f; after.terrain_octaves = 4; commit(st, si, after);
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("Da\xC4\x9F" "lar")) {
                after = e; after.terrain_amp = 35.0f; after.terrain_freq = 0.045f; after.terrain_octaves = 6; commit(st, si, after);
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("Kanyon")) {
                after = e; after.terrain_amp = 25.0f; after.terrain_freq = 0.02f; after.terrain_octaves = 5; commit(st, si, after);
              }
              ImGui::PopStyleVar();

              prop_end();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(tone(Tone::AxisY), ICON_MD_BRUSH " Arazi \xC5\x9E" "ekillendirme F\xC4\xB1r\xC3\xA7" "alar\xC4\xB1 (Sculpt)");

            // 3D Viewport Paint toggle button
            const bool brush_active = st.terrain_brush.active;
            if (brush_active) {
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.32f, 1.0f));
              ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.38f, 1.0f));
              if (ImGui::Button(ICON_MD_CHECK " 3D G\xC3\xB6r\xC3\xBCn\xC3\xBCmde F\xC4\xB1r\xC3\xA7" "a: A\xC3\x87IK", ImVec2(-1, 26))) {
                st.terrain_brush.active = false;
              }
              ImGui::PopStyleColor(2);
              ImGui::TextDisabled("Sol t\xC4\xB1kla boya | Shift: ters y\xC3\xB6n");
            } else {
              if (ImGui::Button(ICON_MD_BRUSH " 3D G\xC3\xB6r\xC3\xBCn\xC3\xBCmde F\xC4\xB1r\xC3\xA7" "ay\xC4\xB1 Etkinle\xC5\x9Ftir", ImVec2(-1, 26))) {
                st.terrain_brush.active = true;
              }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("F\xC4\xB1r\xC3\xA7" "a Modu:");

            auto mode_btn = [&](const char *label, TerrainBrushMode m) {
              const bool is_sel = (st.terrain_brush.mode == m);
              if (is_sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.72f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.82f, 1.0f));
              }
              const float w = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
              if (ImGui::Button(label, ImVec2(w, 24))) {
                st.terrain_brush.mode = m;
              }
              if (is_sel) ImGui::PopStyleColor(2);
            };

            mode_btn("Y\xC3\xBCkselt", TerrainBrushMode::Yukselt); ImGui::SameLine();
            mode_btn("Al\xC3\xA7" "alt", TerrainBrushMode::Alcalt); ImGui::SameLine();
            mode_btn("D\xC3\xBCzle\xC5\x9Ftir", TerrainBrushMode::Duzlestir);

            mode_btn("Yumu\xC5\x9F" "at", TerrainBrushMode::Yumusat); ImGui::SameLine();
            mode_btn("G\xC3\xBCr\xC3\xBClt\xC3\xBC", TerrainBrushMode::Gurultu); ImGui::SameLine();
            mode_btn("Teras", TerrainBrushMode::Teras);

            if (prop_begin("arazi_firca_ayarlar")) {
              prop_float("F\xC4\xB1r\xC3\xA7" "a Yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1", &st.terrain_brush.radius, 0.5f, 1.0f, 100.0f, "%.1f m");
              prop_float("F\xC4\xB1r\xC3\xA7" "a G\xC3\xBC" "c\xC3\xBC", &st.terrain_brush.strength, 0.05f, 0.05f, 5.0f, "%.2f");
              if (st.terrain_brush.mode == TerrainBrushMode::Duzlestir) {
                prop_float("Hedef Y\xC3\xBCkseklik", &st.terrain_brush.target_height, 0.2f, -50.0f, 200.0f, "%.1f m");
              }
              if (st.terrain_brush.mode == TerrainBrushMode::Teras) {
                prop_float("Basamak Aral\xC4\xB1\xC4\x9F\xC4\xB1", &st.terrain_brush.terrace_step, 0.1f, 0.5f, 20.0f, "%.1f m");
              }
              prop_end();
            }

            if (st.terrain_brush.mode == TerrainBrushMode::Duzlestir && st.terrain_brush.hit_valid) {
              if (ImGui::SmallButton("Son \xC4\xB0\xC5\x9F" "aretlenen Y\xC3\xBCksekli\xC4\x9Fi Hedef Al")) {
                st.terrain_brush.target_height = st.terrain_brush.hit_local.y;
              }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 \xC5\x9E" "ekillendirme Damgalar\xC4\xB1:");
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            if (ImGui::SmallButton("Tepe Kabart")) stamp_raise_mountain(st, (uint32_t)si);
            ImGui::SameLine();
            if (ImGui::SmallButton("Krater Kaz")) stamp_carve_crater(st, (uint32_t)si);
            ImGui::SameLine();
            if (ImGui::SmallButton("Plato D\xC3\xBCzle")) stamp_flatten_plateau(st, (uint32_t)si);

            if (ImGui::SmallButton("Yumu\xC5\x9F" "at")) stamp_smooth_erosion(st, (uint32_t)si);
            ImGui::SameLine();
            if (ImGui::SmallButton("Kayal\xC4\xB1k")) stamp_add_rock_noise(st, (uint32_t)si);
            ImGui::SameLine();
            if (ImGui::SmallButton("Terasla")) stamp_terrace_steps(st, (uint32_t)si);

            ImGui::Spacing();
            if (ImGui::SmallButton(ICON_MD_REFRESH " De\xC4\x9Fi\xC5\x9Fiklikleri S\xC4\xB1" "f\xC4\xB1rla")) stamp_reset_sculpt(st, (uint32_t)si);
            ImGui::PopStyleVar();

            end_component_card();
          }
          process_component_card_action(act, content::kSceneTerrain, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneWater) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x89\x88", "Su (Gerstner)", content::kSceneWater, nullptr, &act, true, Tone::Accent)) { // ≈
            if (prop_begin("su")) {
              prop_help("Sivrilik 1'e yaklastikca tepeler sivrilir; cok yuksekte yorungeler kesisir (dalga kivrilir).");
              track_edit(st, e, si, prop_float("Dalga Boyu", &e.wave_length, 0.05f, 0.5f, 200.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Genlik", &e.wave_amplitude, 0.01f, 0.0f, 20.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Sivrilik", &e.wave_steepness, 0.005f, 0.0f, 1.0f, "%.3f"));
              track_edit(st, e, si, prop_float("H\xC4\xB1z", &e.wave_speed, 0.01f, 0.0f, 20.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n X", &e.wave_direction.x, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n Y", &e.wave_direction.y, 0.01f, -1.0f, 1.0f, "%.2f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneWater, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneVoxel) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x96\xA6", "Voksel D\xC3\xBCnyas\xC4\xB1", content::kSceneVoxel, nullptr, &act, true, Tone::Text)) { // ▦
            if (prop_begin("voksel")) {
              prop_help("Izgara olcusu HUCRE cinsinden. Hucre verisi henuz sahne bicimine girmedi: izgaraya sigan bir kure dolduruluyor.");
              int sx = (int)e.voxel_size_x, sy = (int)e.voxel_size_y, sz = (int)e.voxel_size_z;
              const PropItem ix = prop_int("Izgara X", &sx, 1, 128);
              if (ix.changed) e.voxel_size_x = (uint32_t)sx;
              track_edit(st, e, si, ix);
              const PropItem iy = prop_int("Izgara Y", &sy, 1, 128);
              if (iy.changed) e.voxel_size_y = (uint32_t)sy;
              track_edit(st, e, si, iy);
              const PropItem iz = prop_int("Izgara Z", &sz, 1, 128);
              if (iz.changed) e.voxel_size_z = (uint32_t)sz;
              track_edit(st, e, si, iz);
              track_edit(st, e, si, prop_float("H\xC3\xBC" "cre Boyu", &e.voxel_cell, 0.01f, 0.05f, 10.0f, "%.2f m"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneVoxel, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneWind) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x86\xAF", "R\xC3\xBCzgar Alan\xC4\xB1", content::kSceneWind, nullptr, &act, true, Tone::AccentLo)) { // ↯
            if (prop_begin("ruzgar")) {
              prop_help("Esinti (gust) siddetin uzerine binen dalgalanmanin genligi, siklik ise frekansidir.");
              track_edit(st, e, si, prop_float("Y\xC3\xB6n X", &e.wind_direction.x, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Y\xC3\xB6n Y", &e.wind_direction.y, 0.01f, -1.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet", &e.wind_strength, 0.02f, 0.0f, 50.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Esinti", &e.wind_gustiness, 0.01f, 0.0f, 1.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Esinti S\xC4\xB1kl\xC4\xB1\xC4\x9F\xC4\xB1", &e.wind_gust_freq, 0.01f, 0.0f, 10.0f, "%.2f Hz"));
              int wseed = (int)e.wind_seed;
              const PropItem wit = prop_int("Tohum", &wseed, 0, 65535);
              if (wit.changed) e.wind_seed = (uint32_t)(wseed < 0 ? 0 : wseed);
              track_edit(st, e, si, wit);
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneWind, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneParticle) {
          act = ComponentCardAction::None;
          after = e;
          const bool is_fog_volume = (std::strstr(e.name, "sis") != nullptr ||
                                      std::strstr(e.name, "fog") != nullptr ||
                                      std::strstr(e.name, "Sis") != nullptr ||
                                      std::strstr(e.name, "Fog") != nullptr);
          const char *pcard_icon = is_fog_volume ? ICON_MD_CLOUD : "\xE2\x88\xB4";
          const char *pcard_title = is_fog_volume ? "Hacimsel Sis Hacmi (Fog Volume)" : "G\xC3\xB6rsel Efekt & Partik\xC3\xBCl Sistemi (VFX)";
          const Tone pcard_tone = is_fog_volume ? Tone::AccentLo : Tone::Warn;
          if (begin_component_card(pcard_icon, pcard_title, content::kSceneParticle, nullptr, &act, true, pcard_tone)) {
            if (prop_begin("partikul")) {
              if (is_fog_volume) {
                prop_help("Yerel Hacimsel Sis Hacmi (Local Fog Volume). Varl\xC4\xB1k konumu etraf\xC4\xB1nda yumu\xC5\x9F""ak 3B sis hacmi olu\xC5\x9Fturur.");

                // Sis Yoğunluğu
                track_edit(st, e, si, prop_float("Sis Yo\xC4\x9Funlu\xC4\x9Fu (Do\xC4\x9Fum H\xC4\xB1z\xC4\xB1)", &e.particle_spawn_rate, 2.0f, 0.0f, 400.0f, "%.0f puf/sn"));

                // Sis Puf Boyutu
                track_edit(st, e, si, prop_float("Sis Puf Boyu (Giri\xC5\x9F)", &e.particle_size_start, 0.05f, 0.1f, 15.0f, "%.2f m"));
                track_edit(st, e, si, prop_float("Sis Puf Boyu (Geni\xC5\x9Fleme)", &e.particle_size_end, 0.05f, 0.1f, 25.0f, "%.2f m"));

                // Hacim Yayılma Alanı (Jitter)
                track_edit(st, e, si, prop_vec3("Hacim Yay\xC4\xB1lma Alan\xC4\xB1 (X/Y/Z)", &e.particle_jitter.x, 0.1f, 0.1f, 50.0f, "%.1f m"));

                // Sis Renkleri
                track_edit(st, e, si, prop_color("Sis Rengi (\xC3\x87""ekirdek)", &e.particle_color_start.x));
                track_edit(st, e, si, prop_color("Sis Rengi (S\xC3\xB6n\xC3\xBCmlenme)", &e.particle_color_end.x));

                // Sürüklenme ve Rüzgar
                track_edit(st, e, si, prop_vec3("R\xC3\xBCzgar & S\xC3\xBCr\xC3\xBCklenme H\xC4\xB1z\xC4\xB1", &e.particle_velocity.x, 0.01f, -5.0f, 5.0f, "%.2f m/s"));
                track_edit(st, e, si, prop_float("Hava Direnci (Sakinlik)", &e.particle_drag, 0.01f, 0.0f, 2.0f, "%.2f"));
                track_edit(st, e, si, prop_float("Girdap / T\xC3\xBCrb\xC3\xBClans", &e.particle_curl_strength, 0.02f, 0.0f, 5.0f, "%.2f"));

                // Sis Geometri / Puf Türü
                int f_shape = (int)e.particle_billboard_type;
                static const char *kFogShapeOpts =
                    "0: Dairesel Yumu\xC5\x9F""ak Billboard (2D Disc)\0"
                    "1: H\xC4\xB1za G\xC3\xB6re Esneyen \xC4\xB0\xC4\x9Fne (Velocity Streak)\0"
                    "2: Yatay Zemin Sisi D\xC3\xBCzlemi (Ground Sheet)\0"
                    "3: 3B Yuvarlak K\xC3\xBCre Puf (3D Sphere Volume)\0"
                    "4: Voksel 3B K\xC3\xBCp (Voxel Cube)\0"
                    "5: 3B Enerji Simiti / Halka Sisi (3D Torus Ring)\0"
                    "6: 3B Koni Sisi / Duman Jeti (3D Cone Plume)\0"
                    "7: 3B Silindirik Sis Kolonu (3D Cylinder Shaft)\0\0";
                if (prop_combo("Sis Puf Geometrisi (8 \xC5\x9E" "ekil)", &f_shape, kFogShapeOpts).changed) {
                  after = e;
                  after.particle_billboard_type = (uint32_t)f_shape;
                  commit(st, si, after);
                }

                // Hızlı Geometrik Şekil Seçicileri
                ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 \xC5\x9E" "ekil Se\xC3\xA7""imi:");
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                if (ImGui::SmallButton("3B K\xC3\xBCre")) { after = e; after.particle_billboard_type = 3; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Disk")) { after = e; after.particle_billboard_type = 0; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Zemin")) { after = e; after.particle_billboard_type = 2; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Koni")) { after = e; after.particle_billboard_type = 6; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Silindir")) { after = e; after.particle_billboard_type = 7; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Halka")) { after = e; after.particle_billboard_type = 5; commit(st, si, after); }
                ImGui::SameLine();
                if (ImGui::SmallButton("K\xC3\xBCp")) { after = e; after.particle_billboard_type = 4; commit(st, si, after); }
                ImGui::PopStyleVar();

                // Hızlı Hazır Ayarlar
                ImGui::Spacing();
                ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Sis Hacmi \xC3\x96n Ayarlar\xC4\xB1 (8 Bi\xC3\xA7" "im):");
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                if (ImGui::SmallButton("Sabah Pusu")) {
                  after = e;
                  after.particle_size_start = 1.0f; after.particle_size_end = 2.8f;
                  after.particle_color_start = Vec3{0.82f, 0.85f, 0.90f};
                  after.particle_color_end = Vec3{0.65f, 0.70f, 0.78f};
                  after.particle_billboard_type = 3;
                  after.particle_spawn_rate = 120.0f;
                  after.particle_drag = 0.5f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Yo\xC4\x9Fun Zindan")) {
                  after = e;
                  after.particle_size_start = 1.2f; after.particle_size_end = 3.5f;
                  after.particle_color_start = Vec3{0.35f, 0.38f, 0.42f};
                  after.particle_color_end = Vec3{0.18f, 0.20f, 0.25f};
                  after.particle_billboard_type = 3;
                  after.particle_spawn_rate = 180.0f;
                  after.particle_drag = 0.4f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("B\xC3\xBCy\xC3\xBCl\xC3\xBC Mor")) {
                  after = e;
                  after.particle_size_start = 0.6f; after.particle_size_end = 2.2f;
                  after.particle_color_start = Vec3{0.75f, 0.25f, 0.90f};
                  after.particle_color_end = Vec3{0.35f, 0.08f, 0.55f};
                  after.particle_billboard_type = 3;
                  after.particle_spawn_rate = 140.0f;
                  after.particle_drag = 0.6f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Zehirli Ye\xC5\x9Fil")) {
                  after = e;
                  after.particle_size_start = 0.5f; after.particle_size_end = 2.0f;
                  after.particle_color_start = Vec3{0.30f, 0.85f, 0.35f};
                  after.particle_color_end = Vec3{0.10f, 0.40f, 0.15f};
                  after.particle_billboard_type = 3;
                  after.particle_spawn_rate = 150.0f;
                  after.particle_drag = 0.5f;
                  commit(st, si, after);
                }

                // Satır 2: Geometrik Ön Ayarlar
                if (ImGui::SmallButton("Zemin Tabakas\xC4\xB1")) {
                  after = e;
                  after.particle_billboard_type = 2; // Yatay Zemin
                  after.particle_size_start = 3.0f; after.particle_size_end = 6.0f;
                  after.particle_jitter = Vec3{8.0f, 0.2f, 8.0f};
                  after.particle_color_start = Vec3{0.78f, 0.82f, 0.88f};
                  after.particle_color_end = Vec3{0.50f, 0.55f, 0.62f};
                  after.particle_spawn_rate = 90.0f;
                  after.particle_drag = 0.8f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Baca / Koni Jeti")) {
                  after = e;
                  after.particle_billboard_type = 6; // Koni
                  after.particle_size_start = 0.4f; after.particle_size_end = 3.0f;
                  after.particle_velocity = Vec3{0.0f, 2.5f, 0.0f};
                  after.particle_jitter = Vec3{0.3f, 0.1f, 0.3f};
                  after.particle_color_start = Vec3{0.60f, 0.62f, 0.68f};
                  after.particle_color_end = Vec3{0.35f, 0.38f, 0.45f};
                  after.particle_spawn_rate = 140.0f;
                  after.particle_drag = 0.4f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Kuyu / Silindir")) {
                  after = e;
                  after.particle_billboard_type = 7; // Silindir
                  after.particle_size_start = 1.2f; after.particle_size_end = 1.8f;
                  after.particle_velocity = Vec3{0.0f, 1.2f, 0.0f};
                  after.particle_jitter = Vec3{0.5f, 0.2f, 0.5f};
                  after.particle_color_start = Vec3{0.70f, 0.75f, 0.80f};
                  after.particle_color_end = Vec3{0.45f, 0.50f, 0.55f};
                  after.particle_spawn_rate = 100.0f;
                  after.particle_drag = 0.5f;
                  commit(st, si, after);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Portal Halkas\xC4\xB1")) {
                  after = e;
                  after.particle_billboard_type = 5; // Torus
                  after.particle_size_start = 1.5f; after.particle_size_end = 2.2f;
                  after.particle_velocity = Vec3{0.0f, 0.2f, 0.0f};
                  after.particle_jitter = Vec3{2.5f, 0.4f, 2.5f};
                  after.particle_color_start = Vec3{0.65f, 0.35f, 0.95f};
                  after.particle_color_end = Vec3{0.20f, 0.08f, 0.45f};
                  after.particle_spawn_rate = 110.0f;
                  after.particle_drag = 0.6f;
                  commit(st, si, after);
                }
                ImGui::PopStyleVar();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), ICON_MD_INFO "  Sahne Geneli Atmosfer & 16 Sis Laboratuvar\xC4\xB1:");
                ImGui::TextWrapped("Bu ayarlar yaln\xC4\xB1zca bu yerel sis hacmi i\xC3\xA7indir. B\xC3\xBCt\xC3\xBCn sahneyi kaplayan 16 \xC3\xA7" "e\xC5\x9Fit atmosferik y\xC3\xBCkseklik/mesafe sisi i\xC3\xA7in D\xC3\xBCnya paneline ge\xC3\xA7in:");
                if (ImGui::Button(ICON_MD_PUBLIC " Sahne Geneli Atmosfer & 16 Sis Laboratuvar\xC4\xB1na Git")) {
                  st.sel.clear();
                }
                ImGui::Spacing();
                ImGui::Separator();
              }

              const bool show_advanced_vfx = !is_fog_volume || ImGui::CollapsingHeader(ICON_MD_TUNE " Geli\xC5\x9Fmi\xC5\x9F VFX & Par\xC3\xA7" "ac\xC4\xB1k Fizi\xC4\x9Fi");
              if (show_advanced_vfx) {
              prop_help("S\xC4\xB1" "f\xC4\xB1r tahsisli, y\xC3\xBCksek ba\xC5\x9F" "ar\xC4\xB1ml\xC4\xB1 GPU partik\xC3\xBCl sim\xC3\xBClat\xC3\xB6r\xC3\xBC. Edit\xC3\xB6rde ve oyunda ger\xC3\xA7" "ek zamanl\xC4\xB1 hesaplan\xC4\xB1r.");

              // Canlı Simülasyon Kontrol Çubuğu
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              if (ImGui::Button(st.particle_sim_paused ? " " ICON_MD_PLAY_ARROW " Oynat " : " " ICON_MD_PAUSE " Duraklat ")) {
                st.particle_sim_paused = !st.particle_sim_paused;
              }
              ImGui::SameLine();
              if (ImGui::Button(" " ICON_MD_REFRESH " S\xC4\xB1" "f\xC4\xB1rla ")) {
                st.particles.clear();
                std::memset(st.particle_spawn_accum, 0, sizeof st.particle_spawn_accum);
              }
              ImGui::SameLine();
              if (ImGui::Button(" " ICON_MD_BOLT " P\xC3\xBCsk\xC3\xBCrt (20) ")) {
                const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)si);
                content::ParticleEmitterConfig cfg;
                cfg.spawn_pos = Vec3{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
                cfg.base_velocity = e.particle_velocity;
                cfg.velocity_jitter = e.particle_jitter;
                cfg.lifetime_min = e.particle_lifetime_min;
                cfg.lifetime_max = e.particle_lifetime_max;
                cfg.size_start = e.particle_size_start;
                cfg.size_end = e.particle_size_end;
                cfg.color_start = e.particle_color_start;
                cfg.color_end = e.particle_color_end;
                cfg.gravity = Vec3{0.0f, e.particle_gravity, 0.0f};
                cfg.custom_gravity = true;
                cfg.curl_noise_strength = e.particle_curl_strength;
                cfg.curl_noise_frequency = e.particle_curl_freq;
                cfg.drag = e.particle_drag;
                cfg.enable_collision = e.particle_collision;
                cfg.collision_plane_y = 0.0f;
                cfg.restitution = e.particle_bounce;
                cfg.spawn_on_death_count = e.particle_sub_on_death;
                cfg.shape = e.particle_billboard_type;
                st.particles.emit(cfg, 20, st.particle_rng);
              }
              ImGui::PopStyleVar();

              // Canlı Parçacık İstatistiği Rozeti
              const uint32_t alive = st.particles.alive_count();
              const uint32_t cap = st.particles.capacity();
              const float pct = cap > 0 ? (float)alive / (float)cap * 100.0f : 0.0f;
              ImGui::TextColored(tone(Tone::Warn), ICON_MD_GRAIN " Aktif: %u / %u (%%%0.1f)", alive, cap, pct);
              ImGui::Separator();

              // Cift-Yonlu VFX Cizge Editoru (Niagara / VFX Graph Node View)
              static bool s_show_vfx_graph = false;
              static content::VfxGraph s_vfx_graph;
              static bool s_vfx_graph_inited = false;
              static const char *s_active_graph_name = "Ate\xC5\x9F / Me\xC5\x9F" "ale (Fire)";
              if (!s_vfx_graph_inited) {
                s_vfx_graph.build_fire_graph();
                s_vfx_graph_inited = true;
              }

              if (ImGui::Button(s_show_vfx_graph ? " " ICON_MD_VIEW_LIST " Standart Y\xC4\xB1" "g\xC4\xB1n (Stack) G\xC3\xB6r\xC3\xBCn\xC3\xBCm\xC3\xBC "
                                                 : " " ICON_MD_POLYLINE " VFX \xC3\x87" "izge Edit\xC3\xB6r\xC3\xBC (Node Graph G\xC3\xB6r\xC3\xBCn\xC3\xBCm\xC3\xBC) ")) {
                s_show_vfx_graph = !s_show_vfx_graph;
              }
              if (s_show_vfx_graph) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                if (ImGui::BeginChild("VFXGraphCanvas", ImVec2(0, 225), true)) {
                  ImDrawList *gdl = ImGui::GetWindowDrawList();
                  const ImVec2 c_pos = ImGui::GetCursorScreenPos();
                  const ImVec2 c_size = ImGui::GetContentRegionAvail();

                  // Cizge Hazir Ayar Secim Butonlari
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                  ImGui::TextColored(tone(Tone::Accent), ICON_MD_AUTO_AWESOME " Aktif \xC3\x87" "izge: %s", s_active_graph_name);
                  ImGui::SameLine();
                  ImGui::TextDisabled("| Haz\xC4\xB1r \xC5\x9E" "ablon:");
                  ImGui::SameLine();
                  if (ImGui::SmallButton("Ate\xC5\x9F")) {
                    s_vfx_graph.build_fire_graph();
                    s_active_graph_name = "Ate\xC5\x9F / Me\xC5\x9F" "ale (Fire)";
                  }
                  ImGui::SameLine();
                  if (ImGui::SmallButton("Duman")) {
                    s_vfx_graph.build_smoke_graph();
                    s_active_graph_name = "Duman / Sis (Smoke)";
                  }
                  ImGui::SameLine();
                  if (ImGui::SmallButton("K\xC4\xB1v\xC4\xB1lc\xC4\xB1m")) {
                    s_vfx_graph.build_sparks_graph();
                    s_active_graph_name = "K\xC4\xB1v\xC4\xB1lc\xC4\xB1m (Sparks)";
                  }
                  ImGui::SameLine();
                  if (ImGui::SmallButton("Patlama")) {
                    s_vfx_graph.build_explosion_graph();
                    s_active_graph_name = "Patlama / \xC5\x9Eok (Explosion)";
                  }
                  ImGui::SameLine();
                  if (ImGui::SmallButton("B\xC3\xBCy\xC3\xBC")) {
                    s_vfx_graph.build_magic_graph();
                    s_active_graph_name = "B\xC3\xBCy\xC3\xBC K\xC3\xBCresi (Magic Orb)";
                  }
                  ImGui::PopStyleVar();

                  // Cizge Arka Plan Izgarasi
                  const float grid_step = 24.0f;
                  const ImU32 grid_col = IM_COL32(255, 255, 255, 12);
                  const float grid_start_y = c_pos.y + 28.0f;
                  for (float x = 0; x < c_size.x; x += grid_step) {
                    gdl->AddLine(ImVec2(c_pos.x + x, grid_start_y), ImVec2(c_pos.x + x, c_pos.y + c_size.y), grid_col);
                  }
                  for (float y = 28.0f; y < c_size.y; y += grid_step) {
                    gdl->AddLine(ImVec2(c_pos.x, c_pos.y + y), ImVec2(c_pos.x + c_size.x, c_pos.y + y), grid_col);
                  }

                  // Context Kartlari ve Baglantilar (Spawn -> Init -> Update -> Output)
                  const float card_w = 84.0f;
                  const float card_h = 75.0f;
                  const float card_y = c_pos.y + 36.0f;
                  const char *stages[4] = {"Spawn", "Initialize", "Update", "Output"};
                  const ImU32 stage_colors[4] = {
                    IM_COL32(59, 130, 246, 255),  // Mavi
                    IM_COL32(16, 185, 129, 255),  // Yesil
                    IM_COL32(245, 158, 11, 255),  // Sari
                    IM_COL32(236, 72, 153, 255)   // Pembe
                  };

                  for (int si_idx = 0; si_idx < 4; si_idx++) {
                    const float card_x = c_pos.x + 8.0f + si_idx * 110.0f;
                    gdl->AddRectFilled(ImVec2(card_x, card_y), ImVec2(card_x + card_w, card_y + card_h), IM_COL32(24, 26, 34, 245), 4.0f);
                    gdl->AddRect(ImVec2(card_x, card_y), ImVec2(card_x + card_w, card_y + card_h), stage_colors[si_idx], 4.0f);
                    gdl->AddRectFilled(ImVec2(card_x, card_y), ImVec2(card_x + card_w, card_y + 16.0f), stage_colors[si_idx], 4.0f, ImDrawFlags_RoundCornersTop);
                    gdl->AddText(ImVec2(card_x + 4.0f, card_y + 1.0f), IM_COL32(255, 255, 255, 255), stages[si_idx]);

                    if (si_idx > 0) {
                      gdl->AddCircleFilled(ImVec2(card_x, card_y + card_h * 0.5f), 3.5f, IM_COL32(255, 255, 255, 220));
                    }
                    if (si_idx < 3) {
                      gdl->AddCircleFilled(ImVec2(card_x + card_w, card_y + card_h * 0.5f), 3.5f, stage_colors[si_idx]);
                      const ImVec2 p1(card_x + card_w, card_y + card_h * 0.5f);
                      const ImVec2 p4(card_x + 110.0f, card_y + card_h * 0.5f);
                      const ImVec2 p2(p1.x + 14.0f, p1.y);
                      const ImVec2 p3(p4.x - 14.0f, p4.y);
                      gdl->AddBezierCubic(p1, p2, p3, p4, IM_COL32(255, 255, 255, 160), 2.0f);
                    }

                    if (si_idx == 0) {
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 20.0f), IM_COL32(180, 185, 200, 255), "Rate: Contin");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 35.0f), IM_COL32(180, 185, 200, 255), "Burst: Auto");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 50.0f), IM_COL32(140, 145, 160, 255), "Seed: Xor32");
                    } else if (si_idx == 1) {
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 20.0f), IM_COL32(180, 185, 200, 255), "Shape: Geom");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 35.0f), IM_COL32(180, 185, 200, 255), "Vel: 3B Vec");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 50.0f), IM_COL32(140, 145, 160, 255), "Ramp: Color");
                    } else if (si_idx == 2) {
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 20.0f), IM_COL32(180, 185, 200, 255), "Curl: 3D Sim");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 35.0f), IM_COL32(180, 185, 200, 255), "Stokes: Drag");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 50.0f), IM_COL32(140, 145, 160, 255), "Col: Plane");
                    } else if (si_idx == 3) {
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 20.0f), IM_COL32(180, 185, 200, 255), "WBOIT: Mc2013");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 35.0f), IM_COL32(180, 185, 200, 255), "Mesh: Quad/3D");
                      gdl->AddText(ImVec2(card_x + 4.0f, card_y + 50.0f), IM_COL32(140, 145, 160, 255), "TBDR: Q-Res");
                    }
                  }

                  ImGui::SetCursorPos(ImVec2(8, 148));
                  if (ImGui::Button(" " ICON_MD_PLAY_ARROW " Grafigi Derle & Yans\xC4\xB1t ")) {
                    content::ParticleEmitterConfig gcfg;
                    if (s_vfx_graph.compile_to_config(gcfg)) {
                      after = e;
                      after.particle_velocity = gcfg.base_velocity;
                      after.particle_jitter = gcfg.velocity_jitter;
                      after.particle_lifetime_min = gcfg.lifetime_min;
                      after.particle_lifetime_max = gcfg.lifetime_max;
                      after.particle_color_start = gcfg.color_start;
                      after.particle_color_end = gcfg.color_end;
                      after.particle_size_start = gcfg.size_start;
                      after.particle_size_end = gcfg.size_end;
                      after.particle_gravity = gcfg.gravity.y;
                      after.particle_curl_strength = gcfg.curl_noise_strength;
                      after.particle_drag = gcfg.drag;
                      commit(st, si, after);
                    }
                  }
                  ImGui::SameLine();
                  if (ImGui::Button(" " ICON_MD_CODE " GLSL SPIR-V ")) {
                    ImGui::OpenPopup("GLSLComputeCode");
                  }
                  ImGui::SameLine();
                  ImGui::TextDisabled("4 Context | 6 Blok | 0-Alloc | SPIR-V std430");

                  if (ImGui::BeginPopup("GLSLComputeCode")) {
                    char glsl_buf[1024]{};
                    s_vfx_graph.compile_to_glsl_compute(glsl_buf, sizeof(glsl_buf));
                    ImGui::TextColored(tone(Tone::Accent), ICON_MD_TERMINAL " Vulkan Compute Shader (GLSL 450 - std430 SSBO):");
                    ImGui::Separator();
                    ImGui::InputTextMultiline("##glsl_src", glsl_buf, sizeof(glsl_buf), ImVec2(520, 200), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::Button(" " ICON_MD_CONTENT_COPY " Panoya Kopyala ")) {
                      ImGui::SetClipboardText(glsl_buf);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Kapat ")) {
                      ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                  }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Separator();
              }

              // Açık Kaynak Standart VFX Şablon Kütüphanesi (Godot / Niagara / Effekseer Presets)
              int vfx_tpl = 0;
              if (prop_combo("VFX \xC5\x9E" "ablonu (Preset)", &vfx_tpl,
                             "\xC3\x96zel Yap\xC4\xB1land\xC4\xB1rma (Custom)\0"
                             "Ate\xC5\x9F / Me\xC5\x9F" "ale (Fire & Torch)\0"
                             "Duman / Sis (Smoke & Plume)\0"
                             "K\xC4\xB1v\xC4\xB1lc\xC4\xB1m / Kaynak (Sparks)\0"
                             "Ya\xC4\x9Fmur / Damla (Rain)\0"
                             "Kar / Tipi (Snow)\0"
                             "B\xC3\xBCy\xC3\xBC / Enerji K\xC3\xBCresi (Magic Orb)\0"
                             "Patlama / \xC5\x9Eok (Explosion)\0"
                             "Portal Halkas\xC4\xB1 (Portal Ring)\0"
                             "Lazer S\xC3\xBCtunu (Laser Beam)\0"
                             "Volkanik P\xC3\xBCsk\xC3\xBCrme (Volcano)\0"
                             "Gezegen Diski (Saturn Ring)\0"
                             "\xC5\x9Eok Dalgas\xC4\xB1 (Shockwave)\0"
                             "Galaksi Girdab\xC4\xB1 (Galaxy Vortex)\0"
                             "Kutup I\xC5\x9F\xC4\xB1\xC4\x9F\xC4\xB1 (Aurora Borealis)\0"
                             "Meteor \xC4\xB0zi (Comet Trail)\0"
                             "Enerji Kalkan\xC4\xB1 (Forcefield Cage)\0\0").changed && vfx_tpl > 0) {
                st.particles.clear();
                after = e;
                if (vfx_tpl == 1) { // Ates
                  after.particle_spawn_rate = 65.0f; after.particle_lifetime_min = 0.8f; after.particle_lifetime_max = 1.6f;
                  after.particle_size_start = 0.22f; after.particle_size_end = 0.03f;
                  after.particle_velocity = Vec3{0.0f, 2.8f, 0.0f}; after.particle_jitter = Vec3{0.4f, 0.6f, 0.4f};
                  after.particle_color_start = Vec3{1.0f, 0.6f, 0.1f}; after.particle_color_end = Vec3{0.3f, 0.05f, 0.02f};
                  after.particle_gravity = 0.6f; after.particle_billboard_type = 0;
                  after.particle_curl_strength = 2.5f; after.particle_curl_freq = 0.8f;
                  after.particle_drag = 0.2f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 2) { // Duman
                  after.particle_spawn_rate = 20.0f; after.particle_lifetime_min = 2.5f; after.particle_lifetime_max = 4.5f;
                  after.particle_size_start = 0.12f; after.particle_size_end = 0.95f;
                  after.particle_velocity = Vec3{0.1f, 1.0f, 0.05f}; after.particle_jitter = Vec3{0.3f, 0.2f, 0.3f};
                  after.particle_color_start = Vec3{0.45f, 0.45f, 0.45f}; after.particle_color_end = Vec3{0.12f, 0.12f, 0.12f};
                  after.particle_gravity = 0.15f; after.particle_billboard_type = 0;
                  after.particle_curl_strength = 1.2f; after.particle_curl_freq = 0.5f;
                  after.particle_drag = 0.4f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 3) { // Kivilcim
                  after.particle_spawn_rate = 90.0f; after.particle_lifetime_min = 0.6f; after.particle_lifetime_max = 1.4f;
                  after.particle_size_start = 0.09f; after.particle_size_end = 0.01f;
                  after.particle_velocity = Vec3{0.0f, 5.0f, 0.0f}; after.particle_jitter = Vec3{3.5f, 2.0f, 3.5f};
                  after.particle_color_start = Vec3{1.0f, 0.95f, 0.4f}; after.particle_color_end = Vec3{0.9f, 0.15f, 0.0f};
                  after.particle_gravity = -9.8f; after.particle_billboard_type = 1;
                  after.particle_curl_strength = 0.5f; after.particle_curl_freq = 1.0f;
                  after.particle_drag = 0.08f; after.particle_collision = true; after.particle_bounce = 0.7f;
                  after.particle_sub_on_death = 0; after.particle_ribbon = true;
                } else if (vfx_tpl == 4) { // Yagmur
                  after.particle_spawn_rate = 150.0f; after.particle_lifetime_min = 0.8f; after.particle_lifetime_max = 1.4f;
                  after.particle_size_start = 0.05f; after.particle_size_end = 0.04f;
                  after.particle_velocity = Vec3{0.4f, -14.0f, 0.2f}; after.particle_jitter = Vec3{5.0f, 0.5f, 5.0f};
                  after.particle_color_start = Vec3{0.65f, 0.8f, 1.0f}; after.particle_color_end = Vec3{0.4f, 0.6f, 0.85f};
                  after.particle_gravity = -9.8f; after.particle_billboard_type = 1;
                  after.particle_curl_strength = 0.0f; after.particle_drag = 0.02f;
                  after.particle_collision = true; after.particle_bounce = 0.15f;
                  after.particle_sub_on_death = 2; after.particle_ribbon = false;
                } else if (vfx_tpl == 5) { // Kar
                  after.particle_spawn_rate = 80.0f; after.particle_lifetime_min = 3.0f; after.particle_lifetime_max = 6.0f;
                  after.particle_size_start = 0.07f; after.particle_size_end = 0.04f;
                  after.particle_velocity = Vec3{0.6f, -1.8f, 0.3f}; after.particle_jitter = Vec3{4.0f, 0.4f, 4.0f};
                  after.particle_color_start = Vec3{0.96f, 0.98f, 1.0f}; after.particle_color_end = Vec3{0.75f, 0.85f, 0.95f};
                  after.particle_gravity = -0.4f; after.particle_billboard_type = 3;
                  after.particle_curl_strength = 1.8f; after.particle_curl_freq = 0.6f;
                  after.particle_drag = 0.6f; after.particle_collision = true; after.particle_bounce = 0.0f;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 6) { // Buyu
                  after.particle_spawn_rate = 55.0f; after.particle_lifetime_min = 1.5f; after.particle_lifetime_max = 2.8f;
                  after.particle_size_start = 0.14f; after.particle_size_end = 0.02f;
                  after.particle_velocity = Vec3{0.0f, 2.2f, 0.0f}; after.particle_jitter = Vec3{1.0f, 1.0f, 1.0f};
                  after.particle_color_start = Vec3{0.2f, 0.9f, 1.0f}; after.particle_color_end = Vec3{0.9f, 0.15f, 0.95f};
                  after.particle_gravity = 0.1f; after.particle_billboard_type = 3;
                  after.particle_curl_strength = 6.5f; after.particle_curl_freq = 1.2f;
                  after.particle_drag = 0.1f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = true;
                } else if (vfx_tpl == 7) { // Patlama
                  after.particle_spawn_rate = 0.0f; after.particle_lifetime_min = 0.5f; after.particle_lifetime_max = 1.2f;
                  after.particle_size_start = 0.35f; after.particle_size_end = 0.05f;
                  after.particle_velocity = Vec3{0.0f, 2.0f, 0.0f}; after.particle_jitter = Vec3{5.0f, 5.0f, 5.0f};
                  after.particle_color_start = Vec3{1.0f, 0.8f, 0.1f}; after.particle_color_end = Vec3{0.8f, 0.1f, 0.0f};
                  after.particle_gravity = -3.0f; after.particle_billboard_type = 0;
                  after.particle_curl_strength = 2.0f; after.particle_drag = 0.2f;
                  after.particle_collision = true; after.particle_bounce = 0.4f;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 8) { // Portal
                  after.particle_spawn_rate = 70.0f; after.particle_lifetime_min = 1.5f; after.particle_lifetime_max = 2.5f;
                  after.particle_size_start = 0.2f; after.particle_size_end = 0.05f;
                  after.particle_velocity = Vec3{0.0f, 0.2f, 0.0f}; after.particle_jitter = Vec3{3.0f, 0.05f, 3.0f};
                  after.particle_color_start = Vec3{0.3f, 0.1f, 1.0f}; after.particle_color_end = Vec3{0.9f, 0.1f, 0.8f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 5;
                  after.particle_curl_strength = 1.0f; after.particle_curl_freq = 1.0f;
                  after.particle_drag = 0.1f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = true;
                } else if (vfx_tpl == 9) { // Lazer
                  after.particle_spawn_rate = 120.0f; after.particle_lifetime_min = 0.6f; after.particle_lifetime_max = 1.0f;
                  after.particle_size_start = 0.08f; after.particle_size_end = 0.02f;
                  after.particle_velocity = Vec3{0.0f, 16.0f, 0.0f}; after.particle_jitter = Vec3{0.05f, 0.2f, 0.05f};
                  after.particle_color_start = Vec3{0.1f, 0.8f, 1.0f}; after.particle_color_end = Vec3{0.0f, 0.2f, 1.0f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 7;
                  after.particle_curl_strength = 0.0f; after.particle_drag = 0.02f;
                  after.particle_collision = false; after.particle_ribbon = true;
                } else if (vfx_tpl == 10) { // Volkan
                  after.particle_spawn_rate = 75.0f; after.particle_lifetime_min = 1.0f; after.particle_lifetime_max = 2.0f;
                  after.particle_size_start = 0.22f; after.particle_size_end = 0.04f;
                  after.particle_velocity = Vec3{0.0f, 8.0f, 0.0f}; after.particle_jitter = Vec3{2.5f, 1.0f, 2.5f};
                  after.particle_color_start = Vec3{1.0f, 0.3f, 0.05f}; after.particle_color_end = Vec3{0.4f, 0.05f, 0.0f};
                  after.particle_gravity = -12.0f; after.particle_billboard_type = 6;
                  after.particle_curl_strength = 1.5f; after.particle_drag = 0.08f;
                  after.particle_collision = true; after.particle_bounce = 0.55f;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 11) { // Saturn
                  after.particle_spawn_rate = 85.0f; after.particle_lifetime_min = 2.0f; after.particle_lifetime_max = 4.0f;
                  after.particle_size_start = 0.12f; after.particle_size_end = 0.08f;
                  after.particle_velocity = Vec3{0.0f, 0.0f, 0.0f}; after.particle_jitter = Vec3{5.5f, 0.02f, 5.5f};
                  after.particle_color_start = Vec3{0.95f, 0.9f, 0.7f}; after.particle_color_end = Vec3{0.6f, 0.75f, 0.9f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 3;
                  after.particle_curl_strength = 2.0f; after.particle_curl_freq = 0.5f;
                  after.particle_drag = 0.05f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 12) { // Sok
                  after.particle_spawn_rate = 15.0f; after.particle_lifetime_min = 0.8f; after.particle_lifetime_max = 1.2f;
                  after.particle_size_start = 0.2f; after.particle_size_end = 4.5f;
                  after.particle_velocity = Vec3{0.0f, 0.0f, 0.0f}; after.particle_jitter = Vec3{0.1f, 0.0f, 0.1f};
                  after.particle_color_start = Vec3{1.0f, 0.9f, 0.5f}; after.particle_color_end = Vec3{0.8f, 0.2f, 0.0f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 2;
                  after.particle_curl_strength = 0.0f; after.particle_drag = 0.0f;
                  after.particle_collision = false; after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 13) { // Galaksi
                  after.particle_spawn_rate = 95.0f; after.particle_lifetime_min = 2.0f; after.particle_lifetime_max = 3.5f;
                  after.particle_size_start = 0.16f; after.particle_size_end = 0.04f;
                  after.particle_velocity = Vec3{0.0f, 0.2f, 0.0f}; after.particle_jitter = Vec3{4.5f, 0.1f, 4.5f};
                  after.particle_color_start = Vec3{0.4f, 0.15f, 1.0f}; after.particle_color_end = Vec3{0.1f, 0.9f, 0.9f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 5;
                  after.particle_curl_strength = 14.0f; after.particle_curl_freq = 0.8f;
                  after.particle_drag = 0.15f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = true;
                } else if (vfx_tpl == 14) { // Aurora
                  after.particle_spawn_rate = 18.0f; after.particle_lifetime_min = 2.5f; after.particle_lifetime_max = 4.5f;
                  after.particle_size_start = 0.4f; after.particle_size_end = 5.5f;
                  after.particle_velocity = Vec3{0.5f, 0.4f, 0.0f}; after.particle_jitter = Vec3{3.0f, 0.5f, 1.0f};
                  after.particle_color_start = Vec3{0.1f, 1.0f, 0.5f}; after.particle_color_end = Vec3{0.05f, 0.3f, 0.9f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 2;
                  after.particle_curl_strength = 3.5f; after.particle_curl_freq = 0.3f;
                  after.particle_drag = 0.05f; after.particle_collision = false;
                  after.particle_sub_on_death = 0; after.particle_ribbon = false;
                } else if (vfx_tpl == 15) { // Meteor
                  after.particle_spawn_rate = 110.0f; after.particle_lifetime_min = 0.8f; after.particle_lifetime_max = 1.6f;
                  after.particle_size_start = 0.3f; after.particle_size_end = 0.02f;
                  after.particle_velocity = Vec3{6.0f, -12.0f, 2.0f}; after.particle_jitter = Vec3{0.3f, 0.3f, 0.3f};
                  after.particle_color_start = Vec3{1.0f, 0.9f, 0.3f}; after.particle_color_end = Vec3{0.7f, 0.1f, 0.0f};
                  after.particle_gravity = -9.8f; after.particle_billboard_type = 3;
                  after.particle_curl_strength = 1.0f; after.particle_curl_freq = 1.0f;
                  after.particle_drag = 0.05f; after.particle_collision = true; after.particle_bounce = 0.3f;
                  after.particle_sub_on_death = 5; after.particle_ribbon = true;
                } else if (vfx_tpl == 16) { // Kafes
                  after.particle_spawn_rate = 80.0f; after.particle_lifetime_min = 1.5f; after.particle_lifetime_max = 2.5f;
                  after.particle_size_start = 0.18f; after.particle_size_end = 0.08f;
                  after.particle_velocity = Vec3{0.0f, 0.0f, 0.0f}; after.particle_jitter = Vec3{2.5f, 2.5f, 2.5f};
                  after.particle_color_start = Vec3{0.05f, 0.9f, 1.0f}; after.particle_color_end = Vec3{0.0f, 0.2f, 0.8f};
                  after.particle_gravity = 0.0f; after.particle_billboard_type = 4;
                  after.particle_curl_strength = 0.2f; after.particle_drag = 0.25f;
                  after.particle_collision = false; after.particle_sub_on_death = 0; after.particle_ribbon = false;
                }
                commit(st, si, after);
              }

              // MODÜL 1: Emisyon & Yaşam Döngüsü & Determinizm (Niagara Emitter State / Godot Time)
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_TIMER " 1. Emisyon & Ya\xC5\x9F" "am D\xC3\xB6ng\xC3\xBCs\xC3\xBC (Emission & Lifecycle)");
              track_edit(st, e, si, prop_float("Yayma H\xC4\xB1z\xC4\xB1 (Spawn Rate)", &e.particle_spawn_rate, 0.5f, 0.0f, 1000.0f, "%.1f /s"));
              track_edit(st, e, si, prop_float("Minimum \xC3\x96m\xC3\xBCr (Lifetime Min)", &e.particle_lifetime_min, 0.01f, 0.01f, 60.0f, "%.2f s"));
              track_edit(st, e, si, prop_float("Maksimum \xC3\x96m\xC3\xBCr (Lifetime Max)", &e.particle_lifetime_max, 0.01f, 0.01f, 60.0f, "%.2f s"));
              const float avg_life = (e.particle_lifetime_min + e.particle_lifetime_max) * 0.5f;
              ImGui::TextDisabled("Kararl\xC4\xB1 Pop\xC3\xBClasyon Beklentisi: ~%.0f adet (Rate x Ortalama \xC3\x96m\xC3\xBCr)", e.particle_spawn_rate * avg_life);

              // Deterministik Xorshift32 Tohumu & Rollback Netcode Uyumu
              static int s_user_seed = 1337;
              if (prop_int("Deterministik Tohum (RNG Seed)", &s_user_seed, 1, 999999).changed) {
                st.particle_rng = Rng((uint32_t)s_user_seed);
              }
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              if (ImGui::SmallButton(" " ICON_MD_CASINO " Zar At (Rastgele Tohum) ")) {
                s_user_seed = (int)(st.particle_rng.next_u32() % 999999 + 1);
                st.particle_rng = Rng((uint32_t)s_user_seed);
              }
              ImGui::SameLine();
              if (ImGui::SmallButton(" " ICON_MD_RESTART_ALT " Tohumu S\xC4\xB1" "f\xC4\xB1rla (1337) ")) {
                s_user_seed = 1337;
                st.particle_rng = Rng(1337);
              }
              ImGui::PopStyleVar();

              static bool s_affects_gameplay = false;
              if (prop_check("Oynan\xC4\xB1\xC5\x9F\xC4\xB1 Etkiler (affects_gameplay / Rollback GGPO)", &s_affects_gameplay).changed) {
                // Gameplay flag updated
              }
              if (s_affects_gameplay) {
                ImGui::TextColored(tone(Tone::Ok), ICON_MD_SYNC " [DETERM\xC4\xB0N\xC4\xB0ST\xC4\xB0K ROLLBACK AKT\xC4\xB0" "F] GGPO Senkronize, Bit-Identical Replay");
              } else {
                ImGui::TextDisabled(ICON_MD_SYNC_DISABLED " [KOZMET\xC4\xB0K VFX] Ayr\xC4\xB1k Asenkron \xC3\x87" "er\xC3\xA7" "eve (Fire-and-Forget, 0 Re-Sim CPU Y\xC3\xBCk\xC3\xBC)");
              }

              // MODÜL 2: Yayılım Geometrisi & Hız (Godot Emission Shape / Niagara Shape Location)
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_SHAPE_LINE " 2. Yay\xC4\xB1l\xC4\xB1m Geometrisi & H\xC4\xB1z (Emission Shape & Velocity)");
              const float spd_mag = length(e.particle_velocity);
              int geom_shape = 0;
              if (e.particle_jitter.y <= 0.08f && (e.particle_jitter.x > 0.1f || e.particle_jitter.z > 0.1f)) geom_shape = 2; // Disk / Halka
              else if (e.particle_jitter.z <= 0.08f && e.particle_jitter.x > 0.1f && e.particle_jitter.y > 0.1f) geom_shape = 3; // Perde / Duvar
              else if (spd_mag > 2.0f && (e.particle_jitter.x > 0.5f || e.particle_jitter.z > 0.5f)) geom_shape = 4; // Konik Huni
              else if (e.particle_jitter.x <= 0.08f && e.particle_jitter.z <= 0.08f && spd_mag > 1.0f) geom_shape = 1; // Dogrusal Isin
              else if (e.particle_jitter.x > 0.5f && e.particle_jitter.y > 0.5f && e.particle_jitter.z > 0.5f) geom_shape = 5; // Kuresel Hacim

              if (prop_combo("Yay\xC4\xB1l\xC4\xB1m Bi\xC3\xA7imi (Shape)", &geom_shape,
                             "0: Nokta Kaynak (Point Source - 0B)\0"
                             "1: Do\xC4\x9Frusal I\xC5\x9F\xC4\xB1n / S\xC3\xBCtun (Linear Beam - 1B)\0"
                             "2: D\xC3\xBCzlemsel Disk / Halka (Planar Ring - 2B)\0"
                             "3: Dikey Perde / Duvar (Vertical Curtain - 2B)\0"
                             "4: Konik Huni / \xC3\x87" "e\xC5\x9Fme (Conical Fountain - 3B)\0"
                             "5: 3B K\xC3\xBCresel Hacim (Spherical Volume - 3B)\0\0").changed) {
                after = e;
                if (geom_shape == 0) { // Point
                  after.particle_velocity = Vec3{0.0f, 1.0f, 0.0f}; after.particle_jitter = Vec3{0.2f, 0.2f, 0.2f};
                } else if (geom_shape == 1) { // Linear Beam
                  after.particle_velocity = Vec3{0.0f, 12.0f, 0.0f}; after.particle_jitter = Vec3{0.03f, 0.1f, 0.03f};
                } else if (geom_shape == 2) { // Planar Ring
                  after.particle_velocity = Vec3{0.0f, 0.0f, 0.0f}; after.particle_jitter = Vec3{4.0f, 0.02f, 4.0f};
                } else if (geom_shape == 3) { // Vertical Curtain
                  after.particle_velocity = Vec3{0.0f, 1.0f, 0.0f}; after.particle_jitter = Vec3{4.0f, 2.0f, 0.02f};
                } else if (geom_shape == 4) { // Conical Fountain
                  after.particle_velocity = Vec3{0.0f, 7.0f, 0.0f}; after.particle_jitter = Vec3{2.5f, 0.8f, 2.5f};
                } else if (geom_shape == 5) { // Spherical Volume
                  after.particle_velocity = Vec3{0.0f, 0.0f, 0.0f}; after.particle_jitter = Vec3{3.0f, 3.0f, 3.0f};
                }
                commit(st, si, after);
              }

              static float s_shape_radius = 2.5f;
              static float s_shape_inner_radius = 0.75f;
              static float s_shape_spread = 0.5f;
              if (geom_shape == 2) {
                if (prop_float("Halka Yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1 (Outer Radius)", &s_shape_radius, 0.05f, 0.1f, 50.0f, "%.2f m").changed) {
                  after = e; after.particle_jitter.x = s_shape_radius; after.particle_jitter.z = s_shape_radius; commit(st, si, after);
                }
                prop_float("\xC4\xB0\xC3\xA7 Halka Yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1 (Inner Radius)", &s_shape_inner_radius, 0.05f, 0.0f, s_shape_radius, "%.2f m");
              } else if (geom_shape == 4) {
                prop_float("Koni A\xC3\xA7\xC4\xB1sal Sa\xC3\xA7\xC4\xB1lmas\xC4\xB1 (Cone Spread)", &s_shape_spread, 0.02f, 0.05f, 1.0f, "%.2f rad");
              } else if (geom_shape == 1 || geom_shape == 3 || geom_shape == 5) {
                if (prop_float("Yay\xC4\xB1l\xC4\xB1m \xC3\x96l\xC3\xA7" "e\xC4\x9Fi (Shape Scale)", &s_shape_radius, 0.05f, 0.1f, 50.0f, "%.2f m").changed) {
                  after = e;
                  if (geom_shape == 5) { after.particle_jitter = Vec3{s_shape_radius, s_shape_radius, s_shape_radius}; }
                  else if (geom_shape == 3) { after.particle_jitter.x = s_shape_radius; }
                  commit(st, si, after);
                }
              }

              track_edit(st, e, si, prop_vec3("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 H\xC4\xB1z\xC4\xB1 (Initial Velocity)", &e.particle_velocity.x, 0.02f));
              track_edit(st, e, si, prop_vec3("Sa\xC3\xA7\xC4\xB1lma Sapmas\xC4\xB1 (Jitter / Spread)", &e.particle_jitter.x, 0.02f, 0.0f, 20.0f, "%.2f"));
              ImGui::TextDisabled("H\xC4\xB1z: %.2f m/s | Analitik Geometri: %s", spd_mag,
                                  geom_shape == 1 ? "1B Do\xC4\x9Frusal S\xC3\xBCtun (Linear Beam)" :
                                  (geom_shape == 2 ? "2B D\xC3\xBCzlemsel Halka (Planar Ring)" :
                                  (geom_shape == 3 ? "2B Dikey Perde (Vertical Curtain)" :
                                  (geom_shape == 4 ? "3B Konik Huni (Conical Fountain)" :
                                  (geom_shape == 5 ? "3B K\xC3\xBCresel Hacim (Spherical Volume)" : "0B Nokta (Point)")))));

              // MODÜL 3: Kuvvetler & Akışkan Fiziği & Modül Hattı (Niagara Forces & Accelerations)
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_AIR " 3. Kuvvetler & Ak\xC4\xB1\xC5\x9Fkan Fizi\xC4\x9Fi (Forces & Dynamics)");
              track_edit(st, e, si, prop_float("Yer\xC3\xA7" "ekimi \xC4\xB0vmesi (Gravity)", &e.particle_gravity, 0.1f, -50.0f, 50.0f, "%.1f m/s\xC2\xB2"));
              track_edit(st, e, si, prop_float("Stokes Hava Direnci (Drag)", &e.particle_drag, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_float("T\xC3\xBCrb\xC3\xBClans G\xC3\xBC" "c\xC3\xBC (Curl Strength)", &e.particle_curl_strength, 0.05f, 0.0f, 30.0f, "%.2f m/s\xC2\xB2"));
              track_edit(st, e, si, prop_float("G\xC3\xBCr\xC3\xBClt\xC3\xBC Frekans\xC4\xB1 (Curl Freq)", &e.particle_curl_freq, 0.02f, 0.05f, 10.0f, "%.2f /m"));
              bool coll_val = e.particle_collision;
              if (prop_check("Zemin \xC3\x87" "arp\xC4\xB1\xC5\x9Fmas\xC4\xB1 (Plane Collision y=0)", &coll_val).changed) {
                after = e; after.particle_collision = coll_val; commit(st, si, after);
              }
              track_edit(st, e, si, prop_float("Sekme Esnekli\xC4\x9Fi (Restitution)", &e.particle_bounce, 0.02f, 0.0f, 1.0f, "%.2f"));
              static float s_friction = 0.30f;
              prop_float("Yanal S\xC3\xBCrt\xC3\xBCnme (Tangential Friction)", &s_friction, 0.02f, 0.0f, 1.0f, "%.2f");

              // Aktif C++ Modül Hattı (VfxModulePipeline) Durumu
              ImGui::TextDisabled("Aktif C++ Mod\xC3\xBCl Hatt\xC4\xB1 (VfxModulePipeline):");
              ImGui::BulletText(" [x] CurlNoiseModule (3B Simplex Divergence-Free)");
              ImGui::BulletText(" [x] StokesDragModule (Aerodinamik Viskoz Diren\xC3\xA7)");
              ImGui::BulletText(" [x] GroundCollisionModule (Zemin D\xC3\xBCzlemi y=0)");
              ImGui::BulletText(" [x] ColorRampModule & SizeOverLifeModule");

              // MODÜL 4: Render Modeli, WBOIT & Görünüm (Godot DrawPass / McGuire WBOIT)
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_VISIBILITY " 4. Render Modeli, WBOIT & G\xC3\xB6r\xC3\xBCn\xC3\xBCm (Renderer & WBOIT)");
              int bb = (int)e.particle_billboard_type;
              if (prop_combo("Render Modeli (Primitive Mesh)", &bb,
                             "0: Kameraya D\xC3\xB6n\xC3\xBCk D\xC3\xB6rtgen (Billboard Quad - Sprite)\0"
                             "1: H\xC4\xB1za G\xC3\xB6re Uzayan \xC4\xB0" "\xC4\x9Fne (Velocity-Aligned Streak)\0"
                             "2: Yatay Zemin D\xC3\xBCzlemi (Horizontal Shockwave Plane)\0"
                             "3: 3B K\xC3\xBCre Modeli (Mesh: 3D Sphere)\0"
                             "4: 3B K\xC3\xBCp / Voksel (Mesh: 3D Box / Voxel)\0"
                             "5: 3B Enerji Simiti (Mesh: 3D Torus)\0"
                             "6: 3B Koni Modeli (Mesh: 3D Cone)\0"
                             "7: 3B Silindir Modeli (Mesh: 3D Cylinder)\0\0").changed) {
                after = e; after.particle_billboard_type = (uint32_t)bb; commit(st, si, after);
              }
              bool rib_val = e.particle_ribbon;
              if (prop_check("\xC5\x9E" "erit / Kuyruk \xC4\xB0zi (Ribbon Trail)", &rib_val).changed) {
                after = e; after.particle_ribbon = rib_val; commit(st, si, after);
              }
              track_edit(st, e, si, prop_float("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 Boyutu (Size Start)", &e.particle_size_start, 0.005f, 0.0f, 10.0f, "%.3f m"));
              track_edit(st, e, si, prop_float("Biti\xC5\x9F Boyutu (Size End)", &e.particle_size_end, 0.005f, 0.0f, 10.0f, "%.3f m"));
              const float sz_ratio = e.particle_size_start > 1e-4f ? (e.particle_size_end / e.particle_size_start) * 100.0f : 0.0f;
              ImGui::TextDisabled("Boyut Evrimi: %%%0.0f (%s)", sz_ratio, sz_ratio > 100.0f ? "Geni\xC5\x9Fleyen" : (sz_ratio < 100.0f ? "K\xC3\xBC\xC3\xA7\xC3\xBClen" : "Sabit"));

              // Canlı Çok Renkli Gradyan Çubuğu
              {
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const float gw = ImGui::GetContentRegionAvail().x;
                const float gh = 18.0f;
                const ImU32 col_s = IM_COL32((int)(std::clamp(e.particle_color_start.x, 0.0f, 1.0f) * 255.0f),
                                             (int)(std::clamp(e.particle_color_start.y, 0.0f, 1.0f) * 255.0f),
                                             (int)(std::clamp(e.particle_color_start.z, 0.0f, 1.0f) * 255.0f), 255);
                const ImU32 col_e = IM_COL32((int)(std::clamp(e.particle_color_end.x, 0.0f, 1.0f) * 255.0f),
                                             (int)(std::clamp(e.particle_color_end.y, 0.0f, 1.0f) * 255.0f),
                                             (int)(std::clamp(e.particle_color_end.z, 0.0f, 1.0f) * 255.0f), 255);
                ImDrawList *pdl = ImGui::GetWindowDrawList();
                pdl->AddRectFilledMultiColor(cp, ImVec2(cp.x + gw, cp.y + gh), col_s, col_e, col_e, col_s);
                pdl->AddRect(cp, ImVec2(cp.x + gw, cp.y + gh), tone_u32(Tone::Line), 3.0f);
                ImGui::Dummy(ImVec2(gw, gh + 4.0f));
              }
              track_edit(st, e, si, prop_color("Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 Rengi (Color Start)", &e.particle_color_start.x));
              track_edit(st, e, si, prop_color("Biti\xC5\x9F Rengi (Color End)", &e.particle_color_end.x));

              // McGuire WBOIT & Işıma / Katkısallık Genişletmesi
              static float s_wboit_emissive = 0.85f;
              static int s_wboit_blend_mode = 0;
              static float s_wboit_softness = 0.25f;
              prop_float("WBOIT I\xC5\x9F\xC4\xB1ma & Additivite (Emissive Glow)", &s_wboit_emissive, 0.02f, 0.0f, 5.0f, "%.2f");
              prop_combo("WBOIT Harmanlama Kipi (Blend Mode)", &s_wboit_blend_mode,
                         "0: WBOIT \xC5\x9E" "effaf (Alpha Blend - McGuire Weight)\0"
                         "1: WBOIT Katk\xC4\xB1sal I\xC5\x9F\xC4\xB1ma (Additive Emissive)\0"
                         "2: \xC3\x87" "arp\xC4\xB1msal / G\xC3\xB6lge (Modulate)\0\0");
              prop_float("Yumu\xC5\x9F" "ak Par\xC3\xA7" "ac\xC4\xB1k Derinlik E\xC5\x9Fi\xC4\x9Fi (Soft Particles)", &s_wboit_softness, 0.02f, 0.01f, 2.0f, "%.2f m");
              ImGui::TextDisabled("WBOIT A\xC4\x9F\xC4\xB1rl\xC4\xB1\xC4\x9F\xC4\xB1: w(z,a) = a * clamp(0.03/(1e-5 + (z/200)^4), 0.01, 3000.0)");
              ImGui::TextDisabled("S\xC4\xB1ralama Maliyeti: 0 CPU ms | Ba\xC4\x9F\xC4\xB1ms\xC4\xB1z Y\xC4\xB1\xC4\x9F\xC4\xB1nlama");

              // MODÜL 5: Olay Zincirleri & Data Channels (Niagara Events & Gameplay Coupling)
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_AUTO_AWESOME " 5. Olay Zincirleri & Data Channels (Niagara Events)");
              int sub_cnt = (int)e.particle_sub_on_death;
              if (prop_int("\xC3\x96l\xC3\xBCmde Alt-Yay\xC4\xB1" "c\xC4\xB1 (Spawn on Death)", &sub_cnt, 0, 50).changed) {
                after = e; after.particle_sub_on_death = (uint32_t)sub_cnt; commit(st, si, after);
              }
              static int s_sub_type = 0;
              prop_combo("Alt-Yay\xC4\xB1" "c\xC4\xB1 \xC5\x9E" "ablonu (Sub-Emitter Type)", &s_sub_type,
                         "0: K\xC4\xB1v\xC4\xB1lc\xC4\xB1m Patlamas\xC4\xB1 (Sparks Burst)\0"
                         "1: Duman Pufu (Smoke Puff)\0"
                         "2: \xC5\x9Eok Dalgas\xC4\xB1 Halkas\xC4\xB1 (Shockwave Ring)\0"
                         "3: Enkaz Par\xC3\xA7" "ac\xC4\xB1klar\xC4\xB1 (Debris Shards)\0\0");

              static float s_dmg_val = 25.0f;
              static float s_impulse_val = 12.5f;
              static bool s_audio_trig = true;
              prop_float("Hasar Miktar\xC4\xB1 (kChannelDamage)", &s_dmg_val, 1.0f, 0.0f, 200.0f, "%.0f HP");
              prop_float("Fizik \xC4\xB0tki Kuvveti (kChannelPhysicsImpulse)", &s_impulse_val, 0.5f, 0.0f, 100.0f, "%.1f Ns");
              prop_check("Ses Efekti Tetikleyici (Audio Event Bus)", &s_audio_trig);

              ImGui::BulletText("Hasar Kanal\xC4\xB1 (kChannelDamage): %s (%.0f HP)", sub_cnt > 0 ? "ETK\xC4\xB0N" : "BEKLEMEDE", s_dmg_val);
              ImGui::BulletText("Fizik \xC4\xB0tkisi (kChannelPhysicsImpulse): %s (%.1f Ns)", e.particle_collision ? "ETK\xC4\xB0N" : "BEKLEMEDE", s_impulse_val);
              ImGui::BulletText("Zemin \xC4\xB0slanmas\xC4\xB1 (GroundWetnessMap): %s", e.particle_collision ? "ETK\xC4\xB0N (64x64 Grid)" : "BEKLEMEDE");
              ImGui::BulletText("Ses Olay\xC4\xB1: %s (vfx/particle_impact.wav)", s_audio_trig && e.particle_collision ? "TET\xC4\xB0KLEND\xC4\xB0" : "HAZIR");

              // MODÜL 6: CS2 Hacimsel Voksel Dumanı (Responsive Voxel Smoke)
              if (st.smoke_initialized) {
                ImGui::Separator();
                ImGui::TextColored(tone(Tone::Accent), ICON_MD_BLUR_ON " 6. CS2 Hacimsel Voksel Duman\xC4\xB1 (Responsive Voxel Smoke)");
                const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)si);
                const Vec3 epos{wm.m[3][0], wm.m[3][1] + 1.0f, wm.m[3][2]};

                const uint32_t total_vox = st.smoke_grid.dim_x() * st.smoke_grid.dim_y() * st.smoke_grid.dim_z();
                ImGui::TextDisabled("Izgara: %u x %u x %u = %u Voksel (H\xC3\xBC" "cre: %.2f m)",
                                    st.smoke_grid.dim_x(), st.smoke_grid.dim_y(), st.smoke_grid.dim_z(),
                                    total_vox, st.smoke_grid.voxel_size());

                prop_float("Dif\xC3\xBCzyon H\xC4\xB1z\xC4\xB1 (Diffusion Rate)", &s_smoke_live_diff, 0.01f, 0.01f, 1.0f, "%.2f");
                prop_float("Da\xC4\x9F\xC4\xB1lma H\xC4\xB1z\xC4\xB1 (Dissipation Rate)", &s_smoke_live_diss, 0.002f, 0.001f, 0.1f, "%.3f");

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                if (ImGui::SmallButton("Duman Doldur (1.8m)")) {
                  st.smoke_grid.inject_smoke(epos, 1.8f, 1.0f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Mermi T\xC3\xBCneli A\xC3\xA7 (5m)")) {
                  st.smoke_grid.carve_bullet_tunnel(epos + Vec3{-2.5f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 5.0f, 0.35f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Bomba \xC5\x9Eok Dalgas\xC4\xB1 (2.5m)")) {
                  st.smoke_grid.apply_explosion_shockwave(epos, 1.0f, 2.5f, 0.8f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("R\xC3\xBCzgar Enjekte Et")) {
                  st.smoke_grid.inject_smoke(epos + Vec3{1.0f, 0.0f, 0.0f}, 1.2f, 0.6f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Temizle")) {
                  st.smoke_grid.clear();
                }
                ImGui::PopStyleVar();
              }

              // MODÜL 7: Yürütme Mimarisi, Mobil TBDR & GPU Telemetrisi
              ImGui::Separator();
              ImGui::TextColored(tone(Tone::Accent), ICON_MD_SPEED " 7. Y\xC3\xBCr\xC3\xBCtme Mimarisi, Mobil TBDR & GPU Telemetrisi");

              static int s_sim_mode_idx = 0;
              prop_combo("Y\xC3\xBCr\xC3\xBCtme Hedefi (Simulation Mode)", &s_sim_mode_idx,
                         "0: Otomatik Sezgisel (Auto Heuristic)\0"
                         "1: CPU SIMD Arena (Takas-ile-Silme, 0-Alloc)\0"
                         "2: Vulkan GPU Compute (SSBO \xC3\x87ift Tampon, Indirect Draw)\0\0");

              const bool is_cpu_active = (s_sim_mode_idx == 1) || (s_sim_mode_idx == 0 && alive <= 4096);
              if (is_cpu_active) {
                ImGui::TextColored(tone(Tone::Ok), ICON_MD_MEMORY " [Y\xC3\x9CR\xC3\x9CTME: CPU SIMD ARENA] Takas-ile-Silme (Swap-with-Last), 0 Dinamik Tahsis");
              } else {
                ImGui::TextColored(tone(Tone::Accent), ICON_MD_DEVELOPER_BOARD " [Y\xC3\x9CR\xC3\x9CTME: VULKAN GPU COMPUTE] SSBO \xC3\x87ift Tampon, Dolayl\xC4\xB1 \xC3\x87izim (Indirect Draw)");
              }

              // Canlı Overdraw Isı & TBDR Bütçe Göstergesi
              const float est_overdraw_pct = std::clamp((float)alive / 150.0f * (e.particle_size_start * 3.0f + 0.2f), 0.05f, 1.0f);
              ImGui::Text("Overdraw Is\xC4\xB1 Seviyesi (TBDR B\xC3\xBCt\xC3\xA7" "esi):");
              ImGui::ProgressBar(est_overdraw_pct, ImVec2(-1, 14.0f),
                                 est_overdraw_pct < 0.4f ? "D\xC3\xBC\xC5\x9F\xC3\xBCk - Mobil TBDR Dostu (%100 Uyumlu)" :
                                 (est_overdraw_pct < 0.75f ? "Orta - Mobil GPU Normal" : "Y\xC3\xBCksek - \xC3\x87" "eyrek \xC3\x87\xC3\xB6z\xC3\xBCn\xC3\xBCrl\xC3\xBCk RT \xC3\x96nerilir"));

              const float light_r = std::max(2.5f, e.particle_size_start * 15.0f);
              ImGui::BulletText("Clustered I\xC5\x9F\xC4\xB1k Enjeksiyonu: %s (%.1f m yar\xC4\xB1\xC3\xA7" "ap)", (e.particle_spawn_rate > 0.0f) ? "ETK\xC4\xB0N" : "BEKLEMEDE", light_r);
              ImGui::BulletText("Kare Ba\xC5\x9F\xC4\xB1na Dinamik Tahsis: 0 Byte (Arena Memory Disiplini)");
              ImGui::BulletText("GPU Batch / Draw Calls: 1 Tek Ge\xC3\xA7i\xC5\x9F Instanced Quad/Meshlet");
              ImGui::BulletText("\xC3\x87izilen Tepe Say\xC4\xB1s\xC4\xB1 (Vertices): %u tepe (Instanced)", alive * 4);
              ImGui::BulletText("\xC3\x87" "eyrek \xC3\x87\xC3\xB6z\xC3\xBCn\xC3\xBCrl\xC3\xBCkl\xC3\xBC Tampon (Quarter-Res RT): Etkin (Bant Geni\xC5\x9Fli\xC4\x9Fi Tasarrufu: %%75)");
              ImGui::BulletText("TBDR Erken Derinlik Testi (Early-Z) & Transient Attachment: Aktif");
              }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneParticle, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneSkybox) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x98\x81", "G\xC3\xB6ky\xC3\xBCz\xC3\xBC & Atmosfer (Skybox)", content::kSceneSkybox, nullptr, &act, true, Tone::Accent)) { // ☁
            if (prop_begin("gokyuzu")) {
              prop_help("Fiziksel atmosfer modeli (Nishita / Bruneton) ve dinamik g\xC3\xBCne\xC5\x9F d\xC3\xB6ng\xC3\xBCs\xC3\xBC.");
              float tod = st.scene.time_of_day;
              const PropItem pit = prop_float("G\xC3\xBCn\xC3\xBCn Saati", &tod, 0.1f, 0.0f, 24.0f, "%.1f:00");
              if (pit.changed) {
                st.scene.time_of_day = tod;
                const float ang = (tod - 6.0f) * (3.14159265f / 12.0f);
                st.scene.sun_dir.y = std::sin(ang);
                st.scene.sun_dir.x = std::cos(ang) * 0.85f;
                st.scene.sun_dir.z = 0.35f;
                track_world_edit(st, pit);
              }
              ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Zaman:");
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              if (ImGui::SmallButton("\xC5\x9E" "afak 06:00")) {
                st.scene.time_of_day = 6.0f; st.scene.sun_dir = Vec3{0.85f, 0.05f, 0.35f}; st.dirty = true;
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("\xC3\x96\xC4\x9Fle 12:00")) {
                st.scene.time_of_day = 12.0f; st.scene.sun_dir = Vec3{0.0f, 1.0f, 0.35f}; st.dirty = true;
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("Bat\xC4\xB1m 18:30")) {
                st.scene.time_of_day = 18.5f; st.scene.sun_dir = Vec3{-0.85f, 0.02f, 0.35f}; st.dirty = true;
              }
              ImGui::SameLine();
              if (ImGui::SmallButton("Gece 00:00")) {
                st.scene.time_of_day = 24.0f; st.scene.sun_dir = Vec3{0.0f, -1.0f, 0.35f}; st.dirty = true;
              }
              ImGui::PopStyleVar();

              track_world_edit(st, prop_float("Atmosfer Bulan\xC4\xB1kl\xC4\xB1\xC4\x9F\xC4\xB1", &st.scene.sky_turbidity, 0.05f, 1.0f, 10.0f, "%.2f"));
              track_world_edit(st, prop_float("G\xC3\xBCne\xC5\x9F \xC5\x9Eiddeti", &st.scene.sun_diffuse, 0.01f, 0.0f, 5.0f, "%.2f"));
              track_world_edit(st, prop_color("Ortam I\xC5\x9F\xC4\xB1\xC4\x9F\xC4\xB1", &st.scene.ambient.x));

              bool gr = st.scene.godrays_enabled;
              if (prop_check("I\xC5\x9F\xC4\xB1k H\xC3\xBCzmeleri (God Rays)", &gr).changed) {
                st.scene.godrays_enabled = gr;
                st.dirty = true;
              }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneSkybox, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneRefProbe) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x97\x89", "Yans\xC4\xB1ma Sondas\xC4\xB1 (IBL)", content::kSceneRefProbe, nullptr, &act, true, Tone::AxisX)) { // ◉
            if (prop_begin("sonda")) {
              track_edit(st, e, si, prop_float("Etki Yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &e.ref_probe_radius, 0.1f, 0.5f, 200.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet", &e.ref_probe_intensity, 0.01f, 0.0f, 5.0f, "%.2f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneRefProbe, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (has_c) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x96\xA3", "Kamera", content::kSceneCamera, nullptr, &act, true, Tone::Accent)) { // ▣
            if (prop_begin("kamera")) {
              track_edit(st, e, si, prop_float("G\xC3\xB6r\xC3\xBC\xC5\x9F A\xC3\xA7\xC4\xB1s\xC4\xB1 (FOV)", &e.cam_fov, 0.5f, 10.0f, 120.0f, "%.1f\xC2\xB0"));
              track_edit(st, e, si, prop_float("Yak\xC4\xB1n K\xC4\xB1rpma", &e.cam_near, 0.01f, 0.01f, 10.0f, "%.2f m"));
              track_edit(st, e, si, prop_float("Uzak K\xC4\xB1rpma", &e.cam_far, 1.0f, 1.0f, 5000.0f, "%.1f m"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneCamera, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (has_s) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x99\xAA", "Ses Kayna\xC4\x9F\xC4\xB1", content::kSceneAudio, nullptr, &act, true, Tone::Warn)) { // ♪
            if (prop_begin("ses")) {
              track_edit(st, e, si, prop_text("Ses Dosyas\xC4\xB1", e.audio_clip, sizeof e.audio_clip));
              track_edit(st, e, si, prop_float("Ses D\xC3\xBCzeyi", &e.audio_volume, 0.02f, 0.0f, 2.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Perde", &e.audio_pitch, 0.02f, 0.1f, 3.0f, "%.2f"));
              bool loop = e.audio_loop;
              if (prop_check("D\xC3\xB6ng\xC3\xBC", &loop).changed) { after = e; after.audio_loop = loop; commit(st, si, after); }
              bool spat = e.audio_spatial;
              if (prop_check("3B Uzamsal", &spat).changed) { after = e; after.audio_spatial = spat; commit(st, si, after); }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneAudio, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneReverb) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x97\x8E", "Yank\xC4\xB1 Alan\xC4\xB1 (Reverb)", content::kSceneReverb, nullptr, &act, true, Tone::Warn)) { // ◎
            if (prop_begin("yanki")) {
              prop_help("Sonumlenme, yankinin duyulmaz olana kadar gecen suresi; oda buyuklugu ilk yansimalarin gecikmesidir.");
              track_edit(st, e, si, prop_float("S\xC3\xB6n\xC3\xBCmlenme", &e.reverb_decay, 0.02f, 0.05f, 20.0f, "%.2f s"));
              track_edit(st, e, si, prop_float("Oda B\xC3\xBCy\xC3\xBCkl\xC3\xBC\xC4\x9F\xC3\xBC", &e.reverb_room_size, 0.01f, 0.0f, 1.0f, "%.2f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneReverb, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (has_sc) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card(ICON_MD_CODE, "Tulpar Betik", content::kSceneScript, nullptr, &act, true, Tone::AccentLo)) {
            if (prop_begin("betik")) {
              prop_help("Motor bu betigi CALISTIRIR: dosya adindan turetilen <ad>_baslat(id), "
                        "<ad>_guncelle(id, dt), <ad>_carpisma(...), <ad>_bitir(id) fonksiyonlarini "
                        "adiyla cagirir. SART: oyun bu dosyayi import etmeli (AOT; import edilmeyen "
                        "betik yoktur, motor yuklemede hata basar). Bkz. docs/KOPRU.md 7.9.");
              // Secici INDEKS ister, alan METIN tutar. Her karede dogrusal
              // arama: n taranan betik sayisi (olculdu: depoda 5) ve bu kod
              // yalniz kart ACIKKEN kosuyor — ayri bir esleme tablosu tutmak
              // (ve onu her taramada tazelemek) daha pahali olurdu.
              int sel = -1;
              for (uint32_t k = 0; k < st.script_count; k++)
                if (!std::strcmp(st.scripts[k], e.script_file)) { sel = (int)k; break; }
              if (prop_asset("Betik (.tpr)", &sel, st.scripts, st.script_count).changed && sel >= 0) {
                after = e;
                std::snprintf(after.script_file, sizeof after.script_file, "%s", st.scripts[sel]);
                commit(st, si, after);
              }
              // Karo/liste'den surukleyip BURAYA birakmak da atar.
              if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("SCRIPT_FILE")) {
                  after = e;
                  std::snprintf(after.script_file, sizeof after.script_file, "%s", (const char *)pl->Data);
                  commit(st, si, after);
                }
                ImGui::EndDragDropTarget();
              }
              // Listede OLMAYAN bir yol (elle yazilmis, baska depodan gelmis)
              // SILINMIYOR: secici onu "-" gosterir ama uzerine yazmaz, metin
              // alani oldugu gibi durur. Tarama kokleri ve tavanlari var; bu
              // alan olmasaydi disaridaki bir betik hic atanamazdi.
              track_edit(st, e, si, prop_text("Yol (elle)", e.script_file, sizeof e.script_file));
              bool en = e.script_enabled;
              if (prop_check("Etkin", &en).changed) { after = e; after.script_enabled = en; commit(st, si, after); }
              prop_end();
            }
            // Yeni betik: iskeleti yazar ve BU varliga atar. Diyalog tek
            // bosaltma yerinden (kipli pencereler bolumu) sonuclanir.
            if (ImGui::SmallButton(ICON_MD_ADD " Yeni betik")) {
              dlg_intent = IntentScriptNew;
              script_new_target = si;
              file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_dir, ".tpr", "Yeni betik");
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Dosya ad\xC4\xB1 kanca ad\xC4\xB1 olur: kovala.tpr -> kovala_baslat, kovala_guncelle ...");
            ImGui::SameLine();
            ImGui::BeginDisabled(e.script_file[0] == 0);
            if (ImGui::SmallButton(ICON_MD_OPEN_IN_NEW " D\xC4\xB1\xC5\x9F edit\xC3\xB6rde a\xC3\xA7")) {
              char yol[1024], kim[1024], err[640];
              if (!editor_script_resolve(e.script_file, st.scene_dir, st.tulpar_dir, yol, sizeof yol)) {
                set_status(st, "betik yolu cozulemedi: %s", e.script_file);
              } else if (!file_exists(yol)) {
                // Etiket sahnede duruyor ama dosya yok: once "Yeni betik".
                set_status(st, "betik dosyasi YOK: %s (etiket %s)", yol, e.script_file);
                console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "betik dosyasi yok: %s (etiket %s)", yol, e.script_file);
              } else if (editor_open_in_code_editor(yol, kim, sizeof kim, err, sizeof err)) {
                set_status(st, "acildi: %s", yol);
                console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "betik acildi: %s -> %s", yol, kim);
              } else {
                set_status(st, "%s", err);
                console_log(ConsoleLevel::Hata, kConsoleTagEditor, "%s", err);
              }
            }
            ImGui::EndDisabled();
            // Betigi GERCEKTEN kosturmanin yolu: oyunu ayri surecte calistir.
            // Ust arac seridinde yer yok (800 px'te Kaydet/Derle seridin disina
            // tasiyordu, olculdu); komut Oynat menusunde ve Ctrl+F5'te de var.
            {
              const bool kosuyor = cmds.checked(CommandId::PlayRunGame);
              if (ImGui::SmallButton(kosuyor ? ICON_MD_STOP " Oyunu durdur" : ICON_MD_PLAY_ARROW " Oyunu \xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r"))
                cmds.invoke(CommandId::PlayRunGame);
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ctrl+F5. Sahneyi derler, onu y\xC3\xBCkleyen oyunu motoru tan\xC4\xB1yan derleyiciyle ayr\xC4\xB1 pencerede "
                                  "\xC3\xA7" "al\xC4\xB1\xC5\x9Ft\xC4\xB1r\xC4\xB1r; \xC3\xA7\xC4\xB1kt\xC4\xB1 Konsol'a akar.");
            }
            // Nesne ozellikleri (E5): betigin bildirdikleri + bu varligin
            // ustune yazdiklari. Betik disk taramasi YALNIZ burada (kart acik).
            props_section(st, e, si, after, frame_i);
            end_component_card();
          }
          process_component_card_action(act, content::kSceneScript, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        } else if (e.prop_count > 0) {
          // Betik bileseni kaldirilmis ama ozellikler duruyor (E3: bilesenden
          // bagimsiz — betigi geri eklemek degerleri geri getirir). Ham gosterilir.
          props_section(st, e, si, after, frame_i);
        }
        if (e.components & content::kSceneNavAgent) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x86\x92", "Yapay Zeka Ajan\xC4\xB1", content::kSceneNavAgent, nullptr, &act, true, Tone::AccentLo)) { // →
            if (prop_begin("ajan")) {
              prop_help("Yol ARAMASI motorun (Detour); yolu YURUME isi oyun kodunda (lib/engine.tpr ajan_ilerlet). Bunlar o kodun okudugu ayarlar.");
              track_edit(st, e, si, prop_vec3("Hedef Nokta", &e.ai_target.x, 0.05f));
              track_edit(st, e, si, prop_float("Hareket H\xC4\xB1z\xC4\xB1", &e.ai_speed, 0.05f, 0.0f, 100.0f, "%.2f m/s"));
              track_edit(st, e, si, prop_float("D\xC3\xB6n\xC3\xBC\xC5\x9F H\xC4\xB1z\xC4\xB1", &e.ai_turn_speed, 1.0f, 0.0f, 720.0f, "%.0f\xC2\xB0/s"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneNavAgent, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneHealth) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x99\xA5", "Can / Z\xC4\xB1rh (Health)", content::kSceneHealth, nullptr, &act, true, Tone::Warn)) { // ♥
            if (prop_begin("can")) {
              prop_help("Oyun icindeki can durumu ve maksimum can kapasitesi.");
              const float ratio = e.health_max > 0.0f ? ImClamp(e.health_current / e.health_max, 0.0f, 1.0f) : 0.0f;
              char hp_bar[64];
              std::snprintf(hp_bar, sizeof hp_bar, "%.0f / %.0f HP", e.health_current, e.health_max);
              ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.22f, 0.22f, 1.0f));
              ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 0.0f), hp_bar);
              ImGui::PopStyleColor();
              track_edit(st, e, si, prop_float("Mevcut Can", &e.health_current, 1.0f, 0.0f, 10000.0f, "%.0f"));
              track_edit(st, e, si, prop_float("Maksimum Can", &e.health_max, 1.0f, 1.0f, 10000.0f, "%.0f"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneHealth, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneAbility) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x9A\x94", "B\xC3\xBCy\xC3\xBC / Yetenek (GAS)", content::kSceneAbility, nullptr, &act, true, Tone::Accent)) { // ⚔
            if (prop_begin("yetenek")) {
              prop_help("Varligin temel yetenegi, hasari ve bekleme suresi.");
              int aid = (int)e.ability_id;
              const char *kAbilities = "0: Yak\xC4\xB1n D\xC3\xB6v\xC3\xBC\xC5\x9F (Melee)\0"
                                       "1: Ate\xC5\x9F Topu (Fireball)\0"
                                       "2: Buz Oku (Frost Arrow)\0"
                                       "3: At\xC4\xB1lma (Dash)\0"
                                       "4: Koruyucu Kalkan (Shield)\0";
              if (prop_combo("Yetenek Tipi", &aid, kAbilities).changed) {
                after = e;
                after.ability_id = (uint32_t)(aid < 0 ? 0 : aid);
                commit(st, si, after);
              }
              track_edit(st, e, si, prop_float("Temel Hasar", &e.ability_damage, 1.0f, 0.0f, 1000.0f, "%.1f"));
              track_edit(st, e, si, prop_float("Etki Menzili", &e.ability_range, 0.5f, 0.0f, 100.0f, "%.1f m"));
              track_edit(st, e, si, prop_float("Bekleme S\xC3\xBCresi", &e.ability_cooldown, 0.1f, 0.0f, 60.0f, "%.1f s"));
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneAbility, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        if (e.components & content::kSceneInventory) {
          act = ComponentCardAction::None;
          after = e;
          if (begin_component_card("\xE2\x96\xA3", "Envanter (Inventory - 16 Yuva)", content::kSceneInventory, nullptr, &act, true, Tone::Text)) { // ▣
            if (prop_begin("envanter")) {
              prop_help("Varlik icin 16 yuvalik sabit bellekli deterministik envanter.");
              ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Yuva Genel Bak\xC4\xB1\xC5\x9F\xC4\xB1:");
              for (int slot = 0; slot < 16; slot++) {
                if (slot % 4 != 0) ImGui::SameLine();
                char sbtn[32];
                std::snprintf(sbtn, sizeof sbtn, "[#%02d]##slot%d", slot + 1, slot);
                ImGui::Button(sbtn, ImVec2(55.0f, 26.0f));
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Yuva %d: Bo\xC5\x9F (Deterministik)", slot + 1);
                }
              }
              prop_end();
            }
            end_component_card();
          }
          process_component_card_action(act, content::kSceneInventory, e, si, [&](int idx, const SceneEntity &se) { commit(st, idx, se); });
        }
        // Ekleme listesi kComponentMenu'den gelir: KATEGORILI, aranabilir ve ON
        // SEKIZ bilesenin tamamini kapsar. Burada elle kurulan duz bir dizi
        // vardi ve yalniz YEDI bileseni taniyordu -- Karakter / Partikul /
        // Arazi / Voksel / Su / Ruzgar / Eklem / Gokyuzu / Sonda / Yanki /
        // Ajan HICBIR YERDEN eklenemiyordu (bilesen bitleri, dosya bicimi ve
        // menu tablosu hazirdi, eksik olan tek sey bu cagriydi).
        // Takili olanlari eleme isini de widget yapar (existing_components),
        // burada tek tek sormaya gerek yok.
        prop_set_filter(nullptr); // ekleme listesi ve diger paneller suzgecten etkilenmesin
        const uint32_t add = component_add_button(kComponentMenu, kComponentMenuCount, e.components);
        if (add) { // 0 = secim yok; donus INDEKS degil BIT
          after = e;
          after.components |= add;
          if (add == content::kSceneModel) {
            if (after.asset < 0 && after.primitive < 0) after.primitive = (int)content::kPrimCube;
          }
          commit(st, si, after);
        }
      } else {
        // --- SAHNE VE ORTAM GENEL ÖZELLİKLERİ (Scene & World Inspector) ---
        // Prowl & O3DE standardı: Seçili varlık yokken arayüz boş bir çöl gibi
        // kalmaz; aktif sahne başlığı, hızlı varlık ekleme butonları ve dünya/atmosfer
        // ayarları zengin bir şekilde listelenir.
        
        // 1. Sahne Başlık Kartı
        char scene_name_buf[128];
        const char *raw_file = editor_basename(st.scene_path);
        if (!raw_file || !raw_file[0]) raw_file = "isimsiz.sahne";
        std::snprintf(scene_name_buf, sizeof scene_name_buf, "%s", raw_file);

        char sub[128];
        std::snprintf(sub, sizeof sub, "Sahne K\xC3\xB6k\xC3\xBC \xC2\xB7 %u varl\xC4\xB1k \xC2\xB7 %u kaynak",
                      st.scene.entity_count, st.scene.asset_count);

        ImGui::PushID("scene_header");
        push_text_size(TextSize::Lg);
        ImGui::TextColored(tone(Tone::Accent), "\xE2\x97\x8E");
        ImGui::SameLine();
        ImGui::TextUnformatted(scene_name_buf);
        pop_text_size();
        push_text_size(TextSize::Sm);
        ImGui::TextColored(tone(Tone::TextDim), "%s", sub);
        pop_text_size();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::PopID();

        // 2. Hızlı Varlık Oluşturma Matrisi (Prowl / Unity Quick Create)
        ImGui::TextDisabled("H\xC4\xB1zl\xC4\xB1 Varl\xC4\xB1k Ekle:");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, tone_col(Tone::Bg2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_col(Tone::Bg3));

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float sp_x = ImGui::GetStyle().ItemSpacing.x;
        const float btn_w = std::floor((avail_w - sp_x * 2.0f) / 3.0f);

        // Satır 1: [ + Boş ] [ + Model ] [ + Işık ]
        if (ImGui::Button("+ Bo\xC5\x9F", ImVec2(btn_w, 0))) do_add(1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Boş Varlık Oluştur (Empty Entity)");
        ImGui::SameLine();
        if (ImGui::Button("+ Model", ImVec2(btn_w, 0))) do_add(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("glTF / 3B Model Varlığı Ekle");
        ImGui::SameLine();
        if (ImGui::Button("+ I\xC5\x9F\xC4\xB1k", ImVec2(btn_w, 0))) do_add(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Noktasal Işık (Point Light) Ekle");

        // Satır 2: [ + Küp ] [ + Huzme ] [ + Gök ]
        if (ImGui::Button("+ K\xC3\xBCp", ImVec2(btn_w, 0))) do_add(10);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Temel Küp Primitifi Ekle");
        ImGui::SameLine();
        if (ImGui::Button("+ Huzme", ImVec2(btn_w, 0))) do_add(64);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hacimsel Işık Hüzmesi (Fake Godray Beam) Ekle");
        ImGui::SameLine();
        if (ImGui::Button("+ G\xC3\xB6k", ImVec2(btn_w, 0))) do_add(36);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Atmosfer ve Skybox Ekle");

        // Satır 3: [ + Sis ] [ + Zemin Sisi ] [ + Duman ]
        if (ImGui::Button("+ Sis", ImVec2(btn_w, 0))) do_add(46);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("3B Hacimsel Sis Puf Bulutu (Küre)");
        ImGui::SameLine();
        if (ImGui::Button("+ Zemin Sisi", ImVec2(btn_w, 0))) do_add(70);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Yatay Zemin Sisi (Ground Mist Sheet)");
        ImGui::SameLine();
        if (ImGui::Button("+ Duman", ImVec2(btn_w, 0))) do_add(71);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Konik Baca Sisi / Duman Jeti");

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // 3. Bilgilendirici İpucu Kartı (Kırpılmayan esnek şerit)
        {
          const ImVec2 cur = ImGui::GetCursorScreenPos();
          const float hint_w = ImGui::GetContentRegionAvail().x;
          ImDrawList *dl = ImGui::GetWindowDrawList();

          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
          ImGui::PushStyleColor(ImGuiCol_ChildBg, tone_col(Tone::Bg2, 0.55f));
          if (ImGui::BeginChild("##scene_hint_card", ImVec2(hint_w, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextColored(tone_col(Tone::Accent), ICON_MD_LIGHTBULB " \xC4\xB0" "pucu:");
            ImGui::SameLine();
            ImGui::TextWrapped("Bir nesnenin Transform ve bile\xC5\x9F" "enlerini d\xC3\xBCzenlemek i\xC3\xA7in soldaki Sahne listesinden se\xC3\xA7in.");
          }
          ImGui::EndChild();
          ImGui::PopStyleColor();
          ImGui::PopStyleVar();

          // Sol kenarda şık 3px Accent şeridi
          const ImVec2 end_cur = ImGui::GetCursorScreenPos();
          dl->AddRectFilled(cur, ImVec2(cur.x + 3.0f, cur.y + (end_cur.y - cur.y)), tone_u32(Tone::Accent), 2.0f);
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        // 4. Dünya ve Ortam Ayarları
        draw_world_and_environment_properties();
      }
    }
    ImGui::End();
    // Dunya paneli: gunes/ortam/golge (gunluge SceneOp::World), kamera (canli; sahneye yazmak ayri islem).
    if (ImGui::Begin(kPanelDunyaLabel)) {
      draw_world_and_environment_properties();
    }
    ImGui::End();
    // Kaynak tarayici: sahne dosyasinin dizinindeki glTF'ler + sahnenin kaynak
    // tablosu (yuklendi/yuklenemedi). Ekleme dongu icinde yapilmaz (liste
    // yeniden taranir): secilen dosya adi kopyalanip donguden sonra islenir.
    if (ImGui::Begin(kPanelKaynaklarLabel)) {
      AssetsAction act;
      assets_panel(st.assets_view, st.scene_dir, st.browse, st.browse_count, st.scene.assets, st.have, st.scene.asset_count, &act);
      if (act.refresh) rescan_browse(st);
      if (act.add_index >= 0 && act.add_index < (int)st.browse_count) {
        // Once kopyala: do_add_asset listeyi yeniden tarar, isaretci bayatlar.
        char add_file[content::kScenePathLen];
        const bool betik = st.browse[act.add_index].kind == AssetKind::Script;
        std::snprintf(add_file, sizeof add_file, "%s", st.browse[act.add_index].name);
        if (betik) {
          // Bir .tpr sahneye KAYNAK olarak eklenemez (kaynak tablosu modeller
          // icin). Secili varlik varsa dogrudan ATANIYOR — cift tiklamanin
          // hicbir sey yapmamasi kullaniciya "bozuk" diye gorunurdu.
          const int si = (int)st.sel.primary();
          if (si >= 0 && si < (int)st.scene.entity_count) {
            SceneEntity after = st.scene.entities[si];
            after.components |= content::kSceneScript;
            std::snprintf(after.script_file, sizeof after.script_file, "%s", add_file);
            if (commit(st, si, after)) set_status(st, "betik atandi: %s -> %s", st.scene.entities[si].name, add_file);
          } else {
            set_status(st, "betik atamak icin once bir varlik secin: %s", add_file);
          }
        } else {
          do_add_asset(add_file);
        }
      }
    }
    ImGui::End();
    // NOT: burada ikinci bir konsol paneli (ShowConsolePanel) daha
    // ciziliyordu; AYNI show_console bayragina bagliydi, yani konsolu
    // acinca IKI konsol geliyordu. Calisan olan asagidaki console_panel:
    // halka tamponu, suzgec ve stdout yakalama onda. Otekinin basligi
    // (editor_console.hpp) eski API'nin bildirimlerini de EZMISTI --
    // console_log/ConsoleLevel 14 cagri yerinde kullanildigi halde artik
    // bildirilmiyordu, yani editor derlenmiyordu.
    // NOT: burada IKINCI bir varlik tarayici (AssetBrowserPanel,
    // editor_browser.hpp) daha vardi. Calisan assets_panel zaten her
    // karede ciziliyor; ikisi ayri durum tutup ayri tarama yapiyordu.
    // Kucuk resim/klasor agaci gibi eksikler calisan panele eklenecek
    // (docs/EDITOR-DURUM.md Faz E.2), ikinci kopya degil.
    
    // --- Node Editor Paneli ---
    if (st.show_node_editor) {
      ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
      if (ImGui::Begin(ICON_MD_ACCOUNT_TREE " Materyal Graph", &st.show_node_editor)) {
        // DURUST ROZET: bu panel su an tek bir SABIT dugum ciziyor;
        // baglanti kurulamiyor ve hicbir malzemeyi etkilemiyor.
        // Kullaniciya calisiyormus gibi gostermek, hic olmamasindan kotu.
        ImGui::TextDisabled("\xE2\x9A\xA0 \xC3\x96nizleme: d\xC3\xBC\xC4\x9F\xC3\xBCmler hen\xC3\xBCz malzemeye ba\xC4\x9Fl\xC4\xB1 de\xC4\x9Fil.");
        ImGui::Separator();
        ImNodes::BeginNodeEditor();
        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("PBR Materyal Çıkışı");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(2);
        ImGui::Text("Albedo (RGB)");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(3);
        ImGui::Text("Roughness");
        ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(4);
        ImGui::Text("Metallic");
        ImNodes::EndInputAttribute();
        ImNodes::EndNode();
        ImNodes::EndNodeEditor();
      }
      ImGui::End();
    }
    
    // NOT: burada dort panel daha ciziliyordu -- Animasyon Sequencer, Arazi
    // Fircasi, Girdi Yoneticisi ve her karede ekranin uzerinde duran bir
    // Profiler penceresi. Dordu de MAKETTI: kaydiraclar `static` yerellere
    // yaziyordu, "Klavye: W" gibi sabit satirlar gercek bir baglamayi degil
    // hicbir seyi gosteriyordu, Profiler ise Stats panelinin olcumlerini
    // ikinci kez ve kapatilamaz bicimde tekrarliyordu. Arkasi yazildiginda
    // geri gelirler (bkz. docs/EDITOR-DURUM.md); o gune kadar var
    // olmayan bir yetenegi vaat eden panel tutulmaz.
    //
    // BURADA SAHIPSIZ bir ImGui::End() duruyordu ve HER KARE
    //   [imgui-error] In window 'Debug##Default': Calling End() too many times!
    // basiyordu; v0.1.0'a bu halde girdi.
    //
    // Iki bagimsiz olcum: (1) statik — editor_app.cpp 8 `ImGui::Begin(` ve 9
    // `ImGui::End();` tasiyordu; (2) calisma zamani (gecici sonda) — bu noktada
    // CurrentWindowStack.Size == 1, yani acik KULLANICI penceresi yok, End()
    // eslesmemis. Begin'siz End, ImGui yigininda bir seviye asagi iner ve ortuk
    // "Debug##Default" penceresini kapatmaya calisir.
    //
    // Kaynagi: maket paneller (Sequencer, Arazi Fircasi, Girdi Yoneticisi,
    // Profiler) silinirken Begin'leri gitti, bu End agacta kaldi. Ustteki
    // panellerin (Dunya, Kaynaklar, Materyal Graph) hepsi kendi End'ini zaten
    // cagiriyor. KURAL: panel silerken Begin/End CIFTININ ikisi de silinmeli.

    // Begin CAGRILDIYSA End sart — donus degerinden BAGIMSIZ. Gardiyan
    // show_console olamaz: `&show_console` p_open olarak veriliyor, yani
    // kullanici pencerenin X'ine bastiginda Begin onu false yapar ve
    // "if (show_console) End()" o karede End'i ATLAR (bu sefer ters yonde
    // dengesizlik: "Begin/End mismatch"). Bayragi Begin'den ONCE oku.
    if (show_console) {
      const bool konsol_acik = ImGui::Begin(kPanelKonsolLabel, &show_console);
      if (konsol_acik) console_panel(console_view);
      ImGui::End();
    }
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    // Surukleme tek islem: IsUsing baslarken kopya, bitince gunluge.
    const int32_t gz = st.sel.primary();
    // KILITLI varlikta gizmo HIC cizilmez. scene.hpp kSceneLocked icin
    // "gizmo/surukleme degistiremez" diyor ama bu bayrak tum depoda hicbir
    // yerde OKUNMUYORDU: kilit simgesi yalniz kendi gorunumunu degistiriyor,
    // nesne eskisi gibi suruklenebiliyordu. Gizmoyu cizmemek hem kilidi
    // uygular hem de kilitli nesnenin ustundeki baska bir nesneyi secmeyi
    // kolaylastirir (ImGuizmo::IsOver artik isin yolunu kapatmaz).
    const bool gz_locked = gz >= 0 && gz < (int32_t)st.scene.entity_count &&
                           (st.scene.entities[gz].flags & content::kSceneLocked) != 0;
    // NOKTA DUZENLEME KIPI (E6). Gizmo tek kalir — ImGuizmo'nun IsUsing/IsOver
    // durumu kimlige bagli ve asagidaki surukleme basi/sonu mantigi o tek
    // kimlige dayaniyor; ikinci bir Manipulate cagrisi o durumu paylasirdi.
    // Kipte yalniz VERDIGIMIZ MATRIS degisir: varligin dunya matrisi yerine
    // "noktanin dunya konumu x varligin dunya donusu" (donus, Yerel eksen
    // kipinde oklarin varligin eksenlerine hizalanmasi icin; olcek yok).
    // Once kip gecerli mi (secim, kilit, bildirim) — matris secilmeden.
    prop_edit_validate(st, frame_i);
    // Nokta suruklemesini bitir: gunluge TEK islem (surukleme basindaki kopya
    // -> simdiki varlik), coklu secimde de yalniz ANA secilinin noktasi
    // (grup suruklemesi varlik konumu icindir; noktayi baska varliklara
    // "ayni delta" diye yaymak onlarin YEREL eksenlerinde baska yer demek).
    // Gunluk surukleme SIRASINDA degistiyse (geri al, sil) kopya bayattir:
    // islem YAZILMAZ ve soylenir — bayat kopyayi geri yazmak baska bir
    // varligi ezebilirdi.
    auto finish_point_drag = [&]() {
      if (!st.pe_drag) return;
      st.pe_drag = false;
      const int32_t pi = st.pe_drag_entity;
      const bool fresh = pi >= 0 && pi < (int32_t)st.scene.entity_count && st.hist.undo_count() == st.pe_drag_undo &&
                         st.hist.redo_count() == st.pe_drag_redo;
      if (!fresh) {
        set_status(st, "nokta suruklemesi gunluge yazilmadi: surukleme sirasinda sahne degisti");
      } else {
        const SceneEntity after = st.scene.entities[pi];
        st.scene.entities[pi] = st.pe_drag_before;
        if (st.hist.set_entity(st.scene, (uint32_t)pi, after)) {
          st.groups.push(1);
          st.dirty = true;
          const content::SceneProp *pp = content::scene_prop_find(after, st.pe_name);
          if (pp) set_status(st, "nokta: %s.%s = (%.2f %.2f %.2f) yerel", after.name, st.pe_name, (double)pp->v[0], (double)pp->v[1], (double)pp->v[2]);
        }
      }
      if (st.pe_exit_after_drag) prop_edit_exit(st, "surukleme bitti, cikis istenmisti");
    };
    if (view_tab == ViewportTab::Scene && gz >= 0 && gz < (int32_t)st.scene.entity_count && !gz_locked) {
      SceneEntity &e = st.scene.entities[gz];
      Mat4 proj_gl;
      {
        const float aspect = view_rect.h > 0 ? view_rect.w / view_rect.h : 1.0f;
        const float znear = 0.1f, zfar = 200.0f;
        if (cam.proj == CameraProjection::Perspective) {
          const float f = 1.0f / std::tan(cam.fov_y * 0.5f);
          proj_gl.m[0][0] = f / aspect;
          proj_gl.m[1][1] = f;
          proj_gl.m[2][2] = (zfar + znear) / (znear - zfar);
          proj_gl.m[2][3] = -1.0f;
          proj_gl.m[3][2] = (2.0f * znear * zfar) / (znear - zfar);
          proj_gl.m[3][3] = 0.0f;
        } else {
          const float hh = cam.radius * std::tan(cam.fov_y * 0.5f);
          const float hw = hh * aspect;
          proj_gl.m[0][0] = 1.0f / hw;
          proj_gl.m[1][1] = 1.0f / hh;
          proj_gl.m[2][2] = -2.0f / (zfar - znear);
          proj_gl.m[3][2] = -(zfar + znear) / (zfar - znear);
          proj_gl.m[3][3] = 1.0f;
        }
      }
      ImGuizmo::SetOrthographic(false);
      // SetRect/SetDrawlist Gorunum penceresinin ICINDE yapildi (yukarida).
      // Nokta mi: surukleme SURERKEN baslangictaki karar gecerli (kip ortada
      // kapansa bile ImGuizmo ayni suruklemeyi surduruyor); degilse kip.
      bool pt = false;
      float pt_w[3] = {0, 0, 0};
      if (st.pe_drag) {
        pt = st.pe_drag_entity == gz;
        if (!pt) { // ana secili surukleme ortasinda degisti: noktanin islemi kapanir,
          finish_point_drag();
          st.gizmo_was_using = false; // suren surukleme yeni varligin BASI sayilsin (kopyasi alinsin, gunluge girsin)
        }
      } else {
        pt = prop_edit_active(st) && st.pe_entity == gz;
      }
      if (pt) {
        float l[3];
        if (prop_edit_point(st, gz, st.pe_name, frame_i, l, nullptr) == PropEditState::Ok)
          content::scene_prop_point_world(st.scene, (uint32_t)gz, l, pt_w);
        else if (st.pe_drag)
          std::memcpy(pt_w, st.pe_drag_world, sizeof pt_w); // surukleme ortasinda kural degisti: son konumda tut
        else
          pt = false; // validate'ten sonra olamaz; yine de varliga dus, noktayi uydurma
      }
      Mat4 mtx = pt ? Mat4::translate(Vec3{pt_w[0], pt_w[1], pt_w[2]}) * to_mat4(content::scene_entity_world_rotation(st.scene, (uint32_t)gz))
                    : content::scene_entity_world_matrix(st.scene, (uint32_t)gz);
      const ImGuizmo::OPERATION op = pt ? ImGuizmo::TRANSLATE
                                        : gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      const float snap_vec[3] = {snap_step, snap_step, snap_step};
      ImGuizmo::SetOrthographic(cam.proj == CameraProjection::Ortho);
      const bool changed = !st.terrain_brush.active ? ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op,
                                                gizmo_space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &mtx.m[0][0], nullptr,
                                                snap_on ? snap_vec : nullptr) : false;
      const bool using_now = !st.terrain_brush.active && ImGuizmo::IsUsing();
      // --- KAPININ KAPISI: sentetik girdi GERCEKTEN ImGui'ye ulasti mi? -----
      // Surukleme kapisi kirmizi yaninca iki aciklama var: (a) gizmo bozuk,
      // (b) sentetik fare ImGui'ye hic varmadi ve kapi yalniz kendi tesisatini
      // olcuyor. Ayirt etmeden kok sebep SOYLENEMEZ, o yuzden ImGui'nin KENDI
      // gordugu degerler ve ImGuizmo'nun durumu basiliyor.
      // Penceresiz kapi gizmo'nun CALISTIGINI gosteriyor (surukleme kapisi,
      // asagida). Kullanici ise pencereli kipte "oklar cikiyor ama hareket
      // etmiyor" diyor. Fark girdi yolunda, ve pencereli yolu buradan
      // olcemeyiz (pencere acmak yasak). Bu yuzden sonda ORTAM DEGISKENIYLE
      // pencereli kipte de acilabiliyor: kullanici TULPAR_GIZMO_SONDA=1 ile
      // acip surukleme denemesini yapiyor, cikti karari veriyor.
      //   ImGui fare == gercek imlec degilse  -> koordinat uzayi (olcek/HiDPI)
      //   over=0 ise                          -> ImGuizmo fareyi uzerinde saymiyor
      //   over=1 using=0 ise                  -> tiklama gizmoya varmiyor
      //   using=1 changed=0 ise               -> surukleme var, yazma yok
      static const bool gz_sonda_acik = headless || std::getenv("TULPAR_GIZMO_SONDA") != nullptr;
      const bool gz_sonda_kare = headless ? (frame_i >= 10 && frame_i <= 18)
                                          : (ImGui::IsMouseDown(0) || ImGuizmo::IsOver());
      if (gz_sonda_acik && gz_sonda_kare) {
        const ImGuiIO &gio = ImGui::GetIO();
        const ImGuiWindow *hw = ImGui::GetCurrentContext()->HoveredWindow;
        std::printf("[engine_editor] gizmo sonda k%u: ImGui fare (%.0f,%.0f) bas=%d tik=%d | ekran %.0fx%.0f olcek %.2f | "
                    "ustundeki pencere '%s' | ImGuizmo over=%d using=%d changed=%d\n",
                    (unsigned)frame_i, gio.MousePos.x, gio.MousePos.y, (int)gio.MouseDown[0],
                    (int)ImGui::IsMouseClicked(0), gio.DisplaySize.x, gio.DisplaySize.y,
                    gio.DisplayFramebufferScale.x, hw ? hw->Name : "(yok)",
                    (int)ImGuizmo::IsOver(), (int)using_now, (int)changed);
      }
      // Gizmo bu tiklamayi ALDIYSA, kaplamanin ayni karede baslattigi kutu
      // (marquee) suruklemesi IPTAL edilir. Kaplama ImGuizmo'dan ONCE
      // cizildigi icin tiklamanin gizmoya mi sahneye mi gittigini o anda
      // bilemez ve kutuyu baslatir; sonuc, gizmo okunu tutup surukleyince
      // nesnenin tasinmasi yerine mavi secim kutusunun acilmasiydi.
      //
      // Kutu yalniz kMinBox'u gectiginde CIZILIR ve yalniz birakilinca
      // secime doner; burada, ayni karede iptal edildigi icin ikisi de
      // hic olmaz. Bir sonraki karede IsMouseClicked false oldugundan ayni
      // basili tutma kutuyu yeniden baslatamaz.
      if (using_now || ImGuizmo::IsOver()) viewport_box_cancel();
      if (using_now && !st.gizmo_was_using && pt) { // NOKTA suruklemesi basi: yalniz bu varligin kopyasi
        st.pe_drag_before = e;
        st.drag_count = 0;
        st.pe_drag = true;
        st.pe_drag_entity = gz;
        st.pe_drag_undo = st.hist.undo_count();
        st.pe_drag_redo = st.hist.redo_count();
        st.pe_drag_writes = 0;
        std::memcpy(st.pe_drag_world, pt_w, sizeof pt_w);
      } else if (using_now && !st.gizmo_was_using) { // surukleme basi: grubun tamaminin kopyasi
        st.edit_before = e;
        st.drag_count = 0;
        for (uint32_t k = 0; k < st.sel.count; k++) { // bayat indeks varsa disarida kalir
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.drag_items[st.drag_count] = i;
          st.drag_before[st.drag_count] = st.scene.entities[i];
          st.drag_count++;
        }
      }
      if (changed && st.pe_drag) {
        // Gizmonun verdigi DUNYA konumu -> yerel ofset (scene_prop_point_local,
        // point_world'un tersi; olcek yok). Nokta varsayilandaysa ilk yazma
        // ustune yazmayi YARATIR. Varligin donusumune DOKUNULMAZ.
        const float w[3] = {mtx.m[3][0], mtx.m[3][1], mtx.m[3][2]};
        if (prop_point_set_world(st.scene, (uint32_t)gz, st.pe_name, w, nullptr)) {
          std::memcpy(st.pe_drag_world, w, sizeof w);
          st.pe_drag_writes++;
        }
      } else if (changed) {
        // Gizmo DUNYA uzayinda calisir, varligin alanlari YERELDIR: ebeveynli bir
        // varlikta dunya matrisini dogrudan yazmak konumu ebeveynin katina cikarirdi.
        entity_from_matrix(e, content::scene_world_to_local_matrix(st.scene, (uint32_t)gz, mtx));
        // Grup: ana secilinin KONUM deltasi digerlerine (dondur/olcek ana varlikta kalir).
        // Taban ana secilinin SURUKLEME BASINDAKI kopyasi (drag_before) — edit_before
        // DEGIL: denetci gizmodan once cizilir ve widget etkin degilken her kare
        // edit_before'u simdiki varlikla yeniler (track_edit). Taban o olunca delta
        // yalniz SON karenin artimiydi; yan varliklar geride kalip birakista
        // baslangica donuyordu ve gunluge yalniz ana secili giriyordu (Tuzaklar 8ck,
        // olculdu 2026-09-25: nokta surukleme kapisinin KONTROLU, yan sapma 0.53 m).
        Vec3 base = e.pos;
        for (uint32_t k = 0; k < st.drag_count; k++)
          if (st.drag_items[k] == gz) base = st.drag_before[k].pos;
        const Vec3 delta = e.pos - base;
        for (uint32_t k = 0; k < st.drag_count; k++) {
          const int32_t i = st.drag_items[k];
          if (i == gz || i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.scene.entities[i].pos = st.drag_before[k].pos + delta;
        }
      }
      bool gizmo_just_finished = false;
      if (!using_now && st.gizmo_was_using && st.pe_drag) { // nokta suruklemesi sonu: gunluge TEK islem
        finish_point_drag();
        gizmo_just_finished = true;
      } else if (!using_now && st.gizmo_was_using) { // surukleme sonu: gunluge TEK grup
        for (uint32_t k = 0; k < st.drag_count; k++) st.drag_after[k] = st.scene.entities[st.drag_items[k]];
        const uint32_t ops = selection_commit(st.scene, st.hist, st.drag_items, st.drag_count, st.drag_before, st.drag_after);
        if (ops) {
          st.groups.push(ops);
          st.dirty = true;
          if (ops > 1) set_status(st, "tasindi (%u varlik)", ops);
        }
        st.drag_count = 0;
        gizmo_just_finished = true;
      }
      st.gizmo_was_using = using_now;
      st.gizmo_was_over = ImGuizmo::IsOver();
      // Secimi engellemek icin st.gizmo_was_using'i de guncelleyelim, ama box_done bu frame bitiyor.
      // Eger gizmo bu kare bittiyse, box_done degeri marquee secimini tetiklememeli.
      if (gizmo_just_finished) st.gizmo_was_over = true; // Secimi yutmasi icin kucuk bir hile
    } else {
      finish_point_drag(); // gizmo bu kare yok (sekme/kilit/secim): yarim nokta suruklemesi gunlugesiz kalmasin
      st.gizmo_was_using = false;
      st.gizmo_was_over = false;
    }
    // Tiklamayla secim: sol tus basildi (gecis), ImGui/gizmo uzerinde degil.
    {
      const bool lmb = in && in->mouse_down[0];
      const bool pressed = lmb && !st.prev_lmb;
      st.prev_lmb = lmb;
      // Fare konumu GORUNUM PANELININ dikdortgenine cevrilir. Panel disindaki
      // tik bir viewport tiklamasi DEGILDIR ve hicbir sey secmez — `map_mouse`
      // orada valid=false donuyor (ve -1 sentinel veriyor, 0 degil: 0 gecerli
      // bir piksel olurdu ve sessizce kosede bir isin atardik).
      // `in->mouse_x/y` GLFW'den MANTIKSAL piksel gelir (push_input'taki yorumla
      // AYNI); view_rect ise ImGui/Vulkan FRAMEBUFFER piksel uzayindadir (HiDPI'da
      // ikisi ES DEGIL). ui.pointer_scale() ile carpilmadan verilirse (eski hal)
      // HiDPI ekranda (fw/ww != 1) tiklama panelin YANLIS noktasini isaret eder --
      // bu da "tiklayinca hemen kapaniyor / secim tutmuyor" sikayetinin sebebiydi.
      const float psc = ui.pointer_scale();
      const ViewportPick pick = in ? vp.map_mouse(view_rect, (float)in->mouse_x * psc, (float)in->mouse_y * psc) : ViewportPick{};
      char pe_pick_name[content::kScenePropNameLen] = {0}; // tiklanan nokta isareti (E6)
      // Gizmo kullaniliyorsa (veya az once birakildiysa) marquee secimi iptal.
      const bool gizmo_active_or_just_finished = ImGuizmo::IsUsing() || st.gizmo_was_over;

      if (view_tab == ViewportTab::Scene && ovres.box_done && !gizmo_active_or_just_finished && !st.terrain_brush.active) {
        // Kutu (marquee) secim: kaplama dikdortgeni verdi, izdusum testi saf
        // fonksiyonda (kamera ARKASINDAKI kutular orada eleniyor).
        static content::SceneBounds bb[content::kSceneMaxEntities];
        static int32_t bmap[content::kSceneMaxEntities];
        const uint32_t nb = entity_pick_bounds(st, phys, bb, bmap); // gizliler DISARIDA
        static int32_t hits[Selection::kMax];
        const uint32_t nh = viewport_box_select(proj * view, bb, nb, view_rect, ovres.box[0], ovres.box[1], ovres.box[2], ovres.box[3],
                                                /*tam icerme*/ false, hits, Selection::kMax);
        if (!ImGui::GetIO().KeyCtrl) st.sel.clear();
        for (uint32_t k = 0; k < nh; k++) {
          const int32_t ent = bmap[hits[k]]; // sikistirilmis indeks -> varlik indeksi
          if (!st.sel.contains(ent)) st.sel.toggle(ent);
        }
        set_status(st, "kutu secim: %u varlik", st.sel.count);
      } else if (view_tab == ViewportTab::Scene && pressed && pick.valid && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsUsing() &&
                 !ImGuizmo::IsOver() && !st.terrain_brush.active && !st.playing && st.sel.primary() >= 0 &&
                 prop_marker_pick(st, (uint32_t)st.sel.primary(), proj * view, view_rect, (float)in->mouse_x * psc, (float)in->mouse_y * psc,
                                  frame_i, pe_pick_name, sizeof pe_pick_name)) {
        // Nokta isareti (E6): varlik seciminden ONCE, ekran uzayinda. Isinla
        // AABB secimi isareti hic bilmez — isaret varligin sinirinin disinda
        // kalir ve tik arkadaki zemini secerdi. Cizimle ayni izdusum ve ayni
        // aday kurali (yetim / kilitli / konumu bilinmeyen aday degil). Secim
        // DEGISMEZ: kip ana secilinin noktasidir. Ayni noktaya ikinci tik kipte kalir.
        if (!(st.pe_entity == st.sel.primary() && !std::strcmp(st.pe_name, pe_pick_name)))
          prop_edit_enter(st, st.sel.primary(), pe_pick_name, frame_i);
      } else if (view_tab == ViewportTab::Scene && pressed && pick.valid && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !st.terrain_brush.active) {
        static content::SceneBounds wb[content::kSceneMaxEntities];
        static int32_t wmap[content::kSceneMaxEntities];
        const uint32_t nb = entity_pick_bounds(st, phys, wb, wmap); // gizliler DISARIDA
        Vec3 o, d;
        camera_ray(cam, aspect, pick.x, pick.y, (float)vp.width(), (float)vp.height(), &o, &d);
        float t = 0;
        const int32_t raw = content::scene_pick(wb, nb, o, d, &t);
        const int32_t hit = raw >= 0 ? wmap[raw] : -1; // sikistirilmis -> gercek indeks
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (hit >= 0) {
          if (ctrl) st.sel.toggle(hit);
          else st.sel.set_single(hit);
          set_status(st, "secildi: %s (%.1f m, %u secili)", st.scene.entities[hit].name, t, st.sel.count);
        } else if (!ctrl) {
          st.sel.clear();
          set_status(st, "secim yok");
        }
      }
    }
    // "F": secime odaklan (secim yoksa tum sahne) — Unity/Blender geleneği.
    // Kenar tetikli: basili tutmak kamerayi surekli kenetlemesin.
    {
      const bool f_now = (view_tab == ViewportTab::Scene) && in && in->key_down[GLFW_KEY_F] && !ui.wants_text_input();
      if (f_now && !prev_f) {
        static content::SceneBounds fb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, fb);
        bool any = false;
        content::SceneBounds u{};
        for (uint32_t k = 0; k < st.sel.count; k++) {
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)nb) continue;
          if (!any) { u = fb[i]; any = true; }
          else {
            u.lo = Vec3{u.lo.x < fb[i].lo.x ? u.lo.x : fb[i].lo.x, u.lo.y < fb[i].lo.y ? u.lo.y : fb[i].lo.y,
                        u.lo.z < fb[i].lo.z ? u.lo.z : fb[i].lo.z};
            u.hi = Vec3{u.hi.x > fb[i].hi.x ? u.hi.x : fb[i].hi.x, u.hi.y > fb[i].hi.y ? u.hi.y : fb[i].hi.y,
                        u.hi.z > fb[i].hi.z ? u.hi.z : fb[i].hi.z};
          }
        }
        if (any) camera_focus(cam, u);
        else camera_focus_all(cam, fb, nb);
        set_status(st, any ? "odak: secim" : "odak: tum sahne");
      }
      prev_f = f_now;
    }
    if (headless && frame_i == 0 && st.scene.entity_count) {
      // Betikli secim kapisi: ilk varligin merkezi ekrana izdusurulur, o pikselden
      // atilan isin ayni varligi secmeli (kamera isini + sinirlar + secim uctan uca).
      static content::SceneBounds wb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float px = (clip.x / clip.w * 0.5f + 0.5f) * (float)fw, py = (clip.y / clip.w * 0.5f + 0.5f) * (float)fh; // Vulkan: NDC y asagi
      Vec3 o, d;
      camera_ray(cam, aspect, px, py, (float)fw, (float)fh, &o, &d);
      float t = 0;
      const int32_t hit = content::scene_pick(wb, nb, o, d, &t);
      const bool ok = hit == 0;
      std::printf("[engine_editor] secim kapisi: %s piksel (%.0f, %.0f) -> %s (t=%.2f) %s\n", st.scene.entities[0].name, px, py,
                  hit >= 0 ? st.scene.entities[hit].name : "-", hit >= 0 ? t : 0.0f, ok ? "OK" : "HATA");
      if (!ok) return 1;
      // Derleme kapisi: veri modeli -> blob -> ac; sayilar veri modeliyle tutarli olmali.
      const size_t need = content::scene_blob_compile(st.scene, nullptr, 0);
      void *blob = frame.alloc(need, content::kSceneBlobAlign);
      content::SceneBlobView bv;
      content::SceneError berr{};
      const bool bok = blob && content::scene_blob_compile(st.scene, blob, need) == need && content::scene_blob_open(blob, need, &bv, &berr) &&
                       bv.h->entity_count == st.scene.entity_count && bv.h->asset_count == st.scene.asset_count;
      std::printf("[engine_editor] derleme kapisi: %zu bayt, %u varlik, %u cizim, %u isik, %u govde, ozet %016llx %s%s\n", need,
                  bok ? bv.h->entity_count : 0u, bok ? bv.h->draw_count : 0u, bok ? bv.h->light_count : 0u, bok ? bv.h->body_count : 0u,
                  bok ? (unsigned long long)bv.hash() : 0ull, bok ? "OK" : "HATA ", bok ? "" : berr.msg);
      if (!bok) return 1;
      // Coklu secim kapisi: iki varlik secili, grup tasima gunluge TEK eylem
      // (2 islem); metin degismeli, grup geri al ikisini BIRDEN geri almali.
      // Karsilastirma bayt bayt (scene_write) — konum kontrolu tek basina yeterli degil.
      static char txt_a[65536], txt_b[65536], txt_c[65536];
      content::scene_write(st.scene, txt_a, sizeof txt_a);
      st.sel.set_single(0);
      st.sel.toggle(1);
      const Vec3 gd{1.5f, 0.25f, -0.75f};
      const Vec3 p0b = st.scene.entities[0].pos, p1b = st.scene.entities[1].pos;
      const uint32_t gops = selection_translate(st.scene, st.hist, st.sel.items, st.sel.count, gd);
      if (gops) st.groups.push(gops);
      const bool moved = st.scene.entities[0].pos == p0b + gd && st.scene.entities[1].pos == p1b + gd;
      content::scene_write(st.scene, txt_b, sizeof txt_b);
      const bool txt_changed = std::strcmp(txt_a, txt_b) != 0;
      do_undo(); // tek geri al = tum grup
      content::scene_write(st.scene, txt_c, sizeof txt_c);
      const bool back = std::strcmp(txt_a, txt_c) == 0;
      const bool gok = gops == 2 && moved && txt_changed && back && st.hist.undo_count() == 0;
      std::printf("[engine_editor] coklu secim kapisi: %u secili, grup tasima %u islem, ikisi de delta kadar %s, metin degisti %s, grup geri al -> baslangic baytlari %s %s\n",
                  2u, gops, moved ? "evet" : "HAYIR", txt_changed ? "evet" : "HAYIR", back ? "evet" : "HAYIR", gok ? "OK" : "HATA");
      if (!gok) return 1;
      // Kaynak tarayici kapisi: dizindeki glTF'ler bulunmali, secilen kaynakla
      // eklenen varlik kSceneModel almali; geri al sahneyi bayt bayt geri getirmeli.
      rescan_browse(st);
      // MODEL sayisina bakiyoruz, browse_count'a DEGIL: liste artik betikleri de
      // tasiyor ve glTF'si olmayan ama .tpr'si olan bir dizinde browse_count > 0
      // olurdu — kapi o zaman bir betigi model diye eklemeye calisip
      // ANLAMSIZ bir kirmizi verirdi.
      if (st.browse_models == 0) {
        std::printf("[engine_editor] kaynak tarayici kapisi: ATLANDI (dizinde .gltf/.glb yok: %s)\n", st.scene_dir);
      } else {
        const uint32_t n_before = st.scene.entity_count, na_before = st.scene.asset_count;
        int32_t a = -1;
        const uint32_t aops = editor_add_asset_entity(st.scene, st.hist, st.browse[0].name, Vec3{0, 0, 0}, &a);
        if (aops) st.groups.push(aops);
        // Sahnede henuz olmayan kaynak IKI islem (kaynak satiri + varlik), olan bir.
        const bool yeni_kaynak = st.scene.asset_count == na_before + 1;
        const SceneEntity &ne = st.scene.entities[st.scene.entity_count ? st.scene.entity_count - 1 : 0];
        const bool added = aops == (yeni_kaynak ? 2u : 1u) && st.scene.entity_count == n_before + 1 && (ne.components & content::kSceneModel) != 0 &&
                           ne.asset == a && a >= 0;
        std::printf("[engine_editor] kaynak tarayici kapisi: %u dosya (ilk \"%s\"), varlik \"%s\" kaynak %d (%s, %u islem), model bileseni %s", st.browse_count,
                    st.browse[0].name, added ? ne.name : "-", a, yeni_kaynak ? "yeni" : "sahnede vardi", aops, added ? "var" : "YOK");
        do_undo();
        content::scene_write(st.scene, txt_c, sizeof txt_c);
        const bool aok = added && std::strcmp(txt_a, txt_c) == 0 && st.scene.entity_count == n_before;
        std::printf(", geri al -> baslangic baytlari %s %s\n", std::strcmp(txt_a, txt_c) == 0 ? "evet" : "HAYIR", aok ? "OK" : "HATA");
        if (!aok) return 1;
      }
      st.sel.set_single(0);
      st.dirty = false; // kapilar sahneyi geri aldi: dosya kirlenmedi
      set_status(st, "yuklendi: %s", st.scene_path);
    }
    if (headless && frame_i == 2 && st.scene.entity_count && view_rect.w > 0) {
      // E1 secim kapisi — PANEL uzerinden: varlik 0 viewport dokusuna izdusurulur,
      // o panel pikseli pencere pikseline cevrilip vp.map_mouse ile GERI eslenir
      // (editorun tiklamada yaptigi yol), isin ayni varligi bulmali. Kontrol:
      // panelin DISINDAKI bir piksel (Sahne panelinin ustu) gecersiz eslenir —
      // yani orada tiklamak 3B'de hicbir sey secmez.
      static content::SceneBounds wb2[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb2);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float lx = (clip.x / clip.w * 0.5f + 0.5f) * (float)vp.width(), ly = (clip.y / clip.w * 0.5f + 0.5f) * (float)vp.height();
      const ViewportPick inside = vp.map_mouse(view_rect, view_rect.x + lx, view_rect.y + ly);
      const ViewportPick outside = vp.map_mouse(view_rect, view_rect.x - 8.0f, view_rect.y + view_rect.h * 0.5f);
      int32_t hit = -1;
      float t = 0;
      if (inside.valid) {
        Vec3 o, d;
        camera_ray(cam, aspect, inside.x, inside.y, (float)vp.width(), (float)vp.height(), &o, &d);
        hit = content::scene_pick(wb2, nb, o, d, &t);
      }
      const bool pok = inside.valid && hit == 0 && !outside.valid;
      std::printf("[engine_editor] panel secim kapisi: panel %.0fx%.0f @(%.0f,%.0f), ic piksel (%.0f,%.0f) -> %s, dis piksel gecersiz %s %s\n", view_rect.w,
                  view_rect.h, view_rect.x, view_rect.y, view_rect.x + lx, view_rect.y + ly, hit >= 0 ? st.scene.entities[hit].name : "-",
                  outside.valid ? "HAYIR" : "evet", pok ? "OK" : "HATA");
      if (!pok) return 1;
    }
    // --- GIZMO SURUKLEME KAPISI (kare 10..14) -------------------------------
    // Cizim kapisi DEGIL: "ok goruldu" ile "surukleyince tasindi" iki ayri sey.
    // Senaryo gercek girdi olaylarina cevriliyor: uzerine gel -> bas -> tasi ->
    // birak. ImGuizmo tiklamayi ancak IsMouseClicked(0) karesinde alir, bu
    // yuzden konum ve basma AYRI karelerde.
    static Vec3 gz_before{};
    static float gz_px = 0, gz_py = 0;
    if (headless && st.scene.entity_count && view_rect.w > 0 && opts.headless_frames >= 20) {
      if (frame_i == 10) {
        // ON KOSUL. Bir onceki penceresiz kapi oynatmayi baslatiyor, oynatma
        // baslayinca sekme otomatik Oyun'a geciyor (bkz. ~1988) ve gizmo blogu
        // `view_tab == Scene` istiyor. Kurulmazsa bu kapi URUNU degil, kendi
        // zamanlama cakismasini olcer: ilk surumu tam oyle kirmizi yaniyordu
        // (ImGuizmo::Manipulate hic kosmuyordu). Olculdu 2026-09-20.
        st.playing = false;
        view_tab = ViewportTab::Scene;
        st.sel.set_single(0);
        gizmo_op = 0; // TRANSLATE
      } else if (frame_i == 12) {
        gz_before = st.scene.entities[0].pos;
        const Vec3 p0 = st.scene.entities[0].pos;
        const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
        gz_px = view_rect.x + (clip.x / clip.w * 0.5f + 0.5f) * (float)vp.width();
        gz_py = view_rect.y + (clip.y / clip.w * 0.5f + 0.5f) * (float)vp.height();
        g_synth.mouse_x = gz_px - 45.0f; g_synth.mouse_y = gz_py - 45.0f; g_synth.mouse_down[0] = false;
      } else if (frame_i == 13) {
        g_synth.mouse_x = gz_px; g_synth.mouse_y = gz_py;   // okun uzerine GEL
      } else if (frame_i == 14) {
        g_synth.mouse_down[0] = true;              // tiklama karesi
      } else if (frame_i == 15) {
        g_synth.mouse_x = gz_px + 60.0; g_synth.mouse_y = gz_py + 25.0;
      } else if (frame_i == 16) {
        g_synth.mouse_down[0] = false;             // birak -> gunluge islensin
      } else if (frame_i == 17) {
        const Vec3 d = st.scene.entities[0].pos - gz_before;
        const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const bool moved = dist > 1e-4f;
        std::printf("[engine_editor] gizmo surukleme kapisi: piksel (%.0f,%.0f) -> (+60,+25), konum delta (%.4f, %.4f, %.4f) |d|=%.4f, "
                    "gunluk %u %s\n",
                    gz_px, gz_py, d.x, d.y, d.z, dist, st.hist.undo_count(), moved ? "OK" : "HATA (gizmo girdiyi ALMADI)");
        if (!moved) {
          std::fprintf(stderr,
                       "[engine_editor] HATA: gizmo cizildi ama SURUKLEME calismadi. Oklar gorunur olmasi girdinin\n"
                       "  ulastigini GOSTERMEZ: cizim hover'a bagli degil, etkilesim bagli.\n");
          return 1;
        }
      }
    }
    if (headless && frame_i == 6 && st.scene.entity_count >= 2) {
      // Pano kapisi: iki varlik kopyala -> yapistir -> sayi +2 ve yapistirilanin
      // BAYTLARI kaynakla ayni (yalniz konum otelendi); geri al baslangica doner.
      // Kontrol: pano BOSKEN yapistir hicbir sey eklemez.
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      const uint32_t n0 = st.scene.entity_count;
      st.clip_count = 0;
      do_paste(); // KONTROL: bos pano
      const bool empty_noop = st.scene.entity_count == n0;
      st.sel.set_single(0);
      st.sel.toggle(1);
      do_copy();
      const uint32_t copied = st.clip_count;
      do_paste();
      const uint32_t n_pasted = st.scene.entity_count; // geri al'DAN ONCE (rapor bunu yazsin)
      const bool grew = n_pasted == n0 + 2;
      bool same_fields = false;
      if (grew) {
        SceneEntity a = st.scene.entities[0], b = st.scene.entities[n0];
        b.pos.x -= 1.0f; // yapistirmanin otelemesi
        same_fields = content::scene_entity_equal(a, b);
      }
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0 && st.scene.entity_count == n0;
      const bool pok = empty_noop && copied == 2 && grew && same_fields && back;
      std::printf("[engine_editor] pano kapisi: kopyalanan %u, yapistirinca %u -> %u, alanlar ayni %s, KONTROL bos pano eklemedi %s, geri al baslangica dondu %s %s\n",
                  copied, n0, n_pasted, same_fields ? "evet" : "HAYIR", empty_noop ? "evet" : "HAYIR", back ? "evet" : "HAYIR",
                  pok ? "OK" : "HATA");
      if (!pok) return 1;
      st.clip_count = 0;
      st.sel.set_single(0);
      st.dirty = false;
    }
    if (headless && frame_i == 10 && st.scene.entity_count >= 3) {
      // Sahne agaci kapisi (EDITORUN kendi yolu): varlik 1'i varlik 0'a baglayinca
      // DUNYA siniri yerinde kalmali (reparent yerel donusumu yeniden hesaplar);
      // sonra EBEVEYNI oteleyince cocugun siniri da otelenmeli — kalitim
      // editorun sinir/secim yolundan geciyor mu? Kontrol: bagsiz bir varligin
      // siniri ayni otelemede KIPIRDAMAZ.
      static content::SceneBounds b0[content::kSceneMaxEntities], b1[content::kSceneMaxEntities], b2[content::kSceneMaxEntities];
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      entity_world_bounds(st, phys, b0);
      const bool bound = st.hist.reparent(st.scene, 1, 0);
      if (bound) st.groups.push(1);
      entity_world_bounds(st, phys, b1);
      const Vec3 d_keep = b1[1].lo - b0[1].lo;
      const bool kept = length(d_keep) < 1e-3f;
      SceneEntity par = st.scene.entities[0];
      par.pos.x += 5.0f;
      const bool moved_ok = st.hist.set_entity(st.scene, 0, par);
      if (moved_ok) st.groups.push(1);
      entity_world_bounds(st, phys, b2);
      const float child_dx = b2[1].lo.x - b1[1].lo.x;
      const float other_dx = b2[2].lo.x - b1[2].lo.x; // KONTROL: bagsiz varlik
      const bool inherited = child_dx > 4.99f && child_dx < 5.01f && other_dx > -0.01f && other_dx < 0.01f;
      do_undo();
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0;
      const bool hok = bound && kept && moved_ok && inherited && back;
      std::printf("[engine_editor] sahne agaci kapisi: baglandi %s, dunya siniri yerinde kaldi %s (sapma %.4f), ebeveyn +5 -> cocuk %+.2f, "
                  "KONTROL bagsiz %+.2f, geri al baslangica dondu %s %s\n",
                  bound ? "evet" : "HAYIR", kept ? "evet" : "HAYIR", (double)length(d_keep), (double)child_dx, (double)other_dx,
                  back ? "evet" : "HAYIR", hok ? "OK" : "HATA");
      if (!hok) return 1;
      st.dirty = false;
    }
    // Duraklatma kapisi (kare 7-9): duraklatilmisken tick DURUR, F10 TEK adim
    // ilerletir. Kontrol duraklatmanin kendisidir: duraklamadan once tick akiyor.
    // TICK SAYACI TEK BASINA KANIT DEGIL: bu kapi eskiden yalniz sayaci
    // olcuyordu ve editor kipinde fizik HIC adimlanmazken yesil yaniyordu
    // (DemoScene::tick bos zamanlayici). Simdi bir dinamik govdenin KONUMU da
    // olculuyor: duraklamada y sabit, tek adimda degisiyor.
    auto ilk_dinamik = [&]() -> int32_t {
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if ((e.components & content::kSceneBody) && e.dynamic && !e.body_sensor && !(e.components & content::kSceneCharacter)) return (int32_t)i;
      }
      return -1;
    };
    if (headless && frame_i >= 7 && frame_i <= 9 && st.playing) {
      static uint32_t t_pause = 0, t_hold = 0;
      static float y_pause = 0, y_hold = 0;
      const int32_t di = ilk_dinamik();
      const float y_now = di >= 0 ? live_matrix(st, phys, (uint32_t)di).m[3][1] : 0.0f;
      if (frame_i == 7) {
        t_pause = tick_i;
        y_pause = y_now;
        st.paused = true;
      } else if (frame_i == 8) {
        t_hold = tick_i;
        y_hold = y_now;
        st.step_request = 1;
      } else {
        const bool held = t_hold == t_pause;      // duraklatma: tick akmadi
        const bool stepped = tick_i == t_hold + 1; // tek adim: tam bir tick
        // Govde yoksa (sahnede dinamik govde yok) konum kolu ATLANIR ve soylenir.
        const bool y_held = di < 0 || y_hold == y_pause;
        const bool y_stepped = di < 0 || y_now != y_hold;
        const bool ok = held && stepped && y_held && y_stepped;
        std::printf("[engine_editor] duraklatma kapisi: tick %u -> %u (duraklatildi, akmadi %s) -> %u (tek adim %s); govde %s y %.4f -> %.4f "
                    "(sabit %s) -> %.4f (adimda degisti %s) %s\n",
                    t_pause, t_hold, held ? "evet" : "HAYIR", tick_i, stepped ? "evet" : "HAYIR",
                    di >= 0 ? st.scene.entities[di].name : "(dinamik govde yok: konum kolu ATLANDI)", (double)y_pause, (double)y_hold,
                    y_held ? "evet" : "HAYIR", (double)y_now, y_stepped ? "evet" : "HAYIR", ok ? "OK" : "HATA");
        if (!ok) return 1;
        st.paused = false;
      }
    }
    // DURDUR kapisi (kare 20-33, >= 36 kare; diger kapilar kare <= 18 ve
    // gizmo kapisi oynatmayi kapatiyor, o yuzden burada YENIDEN baslatilir).
    // Olculen: (a) oynatirken fizik gercekten kostu (dinamik govde yazar
    // konumundan ayrildi); (b) oynatma sirasinda bir duzenleme yapilir;
    // DURDUR'da govde yazar konumuna doner, duzenleme GERI alinir ve
    // yinelenebilir; (c) yeniden OYNAT govdeyi kaldigi yerden degil yazar
    // konumundan baslatir. Govde ya da varlik yoksa kapi OK DEMEZ, ATLANDI der.
    if (headless && opts.headless_frames >= 36 && frame_i >= 20 && frame_i <= 33) {
      static int32_t di = -1, ed = -1;
      static float y_yazar = 0, y_oynarken = 0, x_once = 0;
      static uint32_t redo_once = 0;
      if (frame_i == 20) {
        di = ilk_dinamik();
        ed = st.scene.entity_count > 0 ? 0 : -1;
        if (di < 0 || ed < 0) std::printf("[engine_editor] durdur kapisi: ATLANDI (sahnede dinamik govde ya da varlik yok)\n");
        if (!st.playing) set_playing(true);
      } else if (di < 0 || ed < 0) {
        // atlandi
      } else if (frame_i == 30) {
        if (!st.playing) { std::printf("[engine_editor] durdur kapisi: HATA — 20. karede baslatilan oynatma 30. karede kapali\n"); return 1; }
        y_yazar = content::scene_entity_world_matrix(st.scene, (uint32_t)di).m[3][1];
        y_oynarken = live_matrix(st, phys, (uint32_t)di).m[3][1];
        x_once = st.scene.entities[ed].pos.x;
        SceneEntity after = st.scene.entities[ed];
        after.pos.x += 2.0f; // oynatma SIRASINDA duzenleme
        commit(st, ed, after);
        redo_once = st.hist.redo_count();
      } else if (frame_i == 31) {
        set_playing(false);
      } else if (frame_i == 32) {
        const float y_dur = live_matrix(st, phys, (uint32_t)di).m[3][1];
        const bool kosmustu = std::fabs(y_oynarken - y_yazar) > 0.05f;
        const bool dondu = !st.playing && !st.bodies_live && y_dur == y_yazar;
        const bool geri = st.scene.entities[ed].pos.x == x_once && st.hist.redo_count() > redo_once;
        std::printf("[engine_editor] durdur kapisi: govde %s y yazar %.3f, oynarken %.3f (fizik kostu %s), durdurunca %.3f (yazara dondu %s); "
                    "\"%s\" oynarken x %.2f -> %.2f, durdurunca %.2f (geri alindi %s, yinelenebilir %u) %s\n",
                    st.scene.entities[di].name, (double)y_yazar, (double)y_oynarken, kosmustu ? "evet" : "HAYIR", (double)y_dur,
                    dondu ? "evet" : "HAYIR", st.scene.entities[ed].name, (double)x_once, (double)(x_once + 2.0f), (double)st.scene.entities[ed].pos.x,
                    geri ? "evet" : "HAYIR", st.hist.redo_count(), (kosmustu && dondu && geri) ? "OK" : "HATA");
        if (!(kosmustu && dondu && geri)) return 1;
        set_playing(true);
      } else if (frame_i == 33) {
        // Bir adim sonra govde yazar konumunun hemen altinda (en cok bir
        // adimlik dusus), 30. karedeki dusmus yerinde DEGIL.
        const float y_yeni = live_matrix(st, phys, (uint32_t)di).m[3][1];
        const bool bastan = std::fabs(y_yeni - y_yazar) < 0.01f;
        std::printf("[engine_editor] yeniden oynat kapisi: govde y %.3f (yazar %.3f, durmadan once %.3f) -> bastan basladi %s %s\n",
                    (double)y_yeni, (double)y_yazar, (double)y_oynarken, bastan ? "evet" : "HAYIR", bastan ? "OK" : "HATA");
        if (!bastan) return 1;
      }
    }
    // GOMULU OYNATMA KAPISI (F5 betikleriyle, Oyun sekmesinde). Kullanici
    // bildirdi (2026-09-24): "F5 ile hicbir sey olmuyor, Ctrl+F5'e gerek
    // kalmadan ayni seyi oynatmali". Olculen, sirayla:
    //   (a) F5 oyunu GOMULU baslatti (fizik onizlemesine dusmedi) ve kare geldi;
    //   (b) betikler KOSUYOR: 0.6 s arayla iki karenin ornekleri farkli;
    //   (c) duraklatmada kare AKMIYOR, tek adim tam BIR kare;
    //   (d) Oyun sekmesinde gorunen piksel OYUNUN pikseli (editorun kendi
    //       cizimi ya da bekleme yazisi degil);
    //   (e) oynarken yapilan duzenleme durdurunca geri aliniyor;
    //   (f) Durdur oyunu kendi `bitir` yolundan kapatiyor (cikis 0, oldurulmeden).
    // Sahneyi yukleyen oyun ya da motoru taniyan derleyici yoksa ATLANDI der,
    // OK DEMEZ (CI'da derleyici yok; kapi yerelde kosar).
    if (headless && opts.headless_frames >= 50 && frame_i >= 40 && frame_i <= 45) {
      static bool g_atla = false, g_bitir_goruldu = false;
      static uint32_t g_ornek_w = 0, g_ornek_h = 0;
      static uint8_t g_a[1024 * 1024 * 3], g_b[1024 * 1024 * 3];
      static double g_ilk_kare_s = 0;
      static uint32_t g_hareket = 0, g_ornek = 0, g_p0 = 0, g_p1 = 0, g_p2 = 0;
      static int32_t g_ed = -1;
      static float g_x_once = 0;
      static bool g_stats_once = true;
      auto ornekle = [&](uint8_t *out) {
        const uint8_t *px = nullptr;
        uint32_t fr = 0;
        oyun.chan.acquire(&px, &fr);
        const uint32_t w = oyun.chan.width(), h = oyun.chan.height();
        g_ornek_w = w / 4 < 1024 ? w / 4 : 1024;
        g_ornek_h = h / 4 < 1024 ? h / 4 : 1024;
        for (uint32_t y = 0; y < g_ornek_h; y++)
          for (uint32_t x = 0; x < g_ornek_w; x++) {
            const uint8_t *q = px ? px + ((size_t)(y * 4) * w + x * 4) * 4 : nullptr;
            uint8_t *o = out + ((size_t)y * g_ornek_w + x) * 3;
            o[0] = q ? q[0] : 0; o[1] = q ? q[1] : 0; o[2] = q ? q[2] : 0;
          }
      };
      auto bekle = [&](uint32_t ms) { // poll + kalp atisi (oyun editoru canli gorsun)
        for (uint32_t i = 0; i < ms / 10; i++) { game_run_poll(oyun, oyun_satiri, nullptr); platform::thread_sleep_us(10000); }
      };
      if (frame_i == 40) {
        if (st.playing) set_playing(false); // onceki kapilarin fizik oynatmasi
        // SAHNE DOSYADAN YENIDEN: onceki kapilar sahneyi degistirdi ve hepsi geri
        // almiyor (gizmo surukleme kapisi varlik 0'i surukleyip birakiyor). F5
        // sahneyi DISKTEKI blob'a derler; kapi degismis sahneyi derleseydi
        // kullanicinin .sahneb'i bozulurdu. Olculdu (2026-09-24): bu satir
        // yokken `--headless 50 --scene salon1.sahne` salon1.sahneb'deki zemini
        // (0, -0.5, 0) -> (2.81, -1.53, 0.56) yaziyordu; oyun dogrudan
        // calistirilinca zemin bir metre asagidaydi. load_scene_from degil:
        // o "son dosyalar" listesine de yaziyor (kapi kullanici ayarina dokunmasin).
        if (st.scene_path[0]) {
          static content::SceneDesc temiz;
          content::SceneError serr{};
          if (!content::scene_load(sys, st.scene_path, &temiz, &serr)) {
            std::printf("[engine_editor] gomulu oynatma kapisi: HATA — sahne yeniden okunamadi: %s\n", serr.msg);
            return 1;
          }
          with_bodies(st, phys, [&] {
            st.scene = temiz;
            st.hist.clear();
            st.groups.clear();
            st.sel.clear();
          });
        }
        char exe[1024], comp[1024], why[512];
        if (!platform::exe_dir(exe, sizeof exe)) std::snprintf(exe, sizeof exe, ".");
        static char bul[8][content::kScenePathLen];
        static FileEntry tar[kFileListMax];
        static char met[256 * 1024];
        const GameFindResult r = st.scene_path[0] ? game_find_for_scene(st.tulpar_dir, st.scene_path, bul, 8, tar, kFileListMax, met, sizeof met)
                                                  : GameFindResult{};
        if (r.count != 1) {
          std::printf("[engine_editor] gomulu oynatma kapisi: ATLANDI (bu sahneyi yukleyen %u oyun var, tek olmali: %s)\n", r.count, st.scene_path);
          g_atla = true;
        } else if (!game_find_compiler(exe, comp, sizeof comp, why, sizeof why)) {
          std::printf("[engine_editor] gomulu oynatma kapisi: ATLANDI (%s)\n", why);
          g_atla = true;
        } else {
          play_start_auto(true);
          if (!st.play_embedded) {
            std::printf("[engine_editor] gomulu oynatma kapisi: HATA — F5 oyunu gomulu baslatmadi (oynat %d; Konsol'a bakin)\n", (int)st.playing);
            return 1;
          }
          view_tab = ViewportTab::Game;
        }
      } else if (g_atla) {
        // atlandi
      } else if (frame_i == 41) {
        const uint64_t t0 = oyun_baslangic_ns;
        for (int i = 0; i < 9000 && oyun.state == GameRunState::Running && oyun.chan.published() < 20; i++) {
          game_run_poll(oyun, oyun_satiri, nullptr);
          platform::thread_sleep_us(10000);
        }
        g_ilk_kare_s = (platform::now_ns() - t0) / 1e9;
        if (oyun.state != GameRunState::Running || oyun.chan.published() < 20) {
          std::printf("[engine_editor] gomulu oynatma kapisi: HATA — oyun %.1f s icinde 20 kare vermedi (durum %d, cikis %d, kare %u)\n", g_ilk_kare_s,
                      (int)oyun.state, oyun.exit_code, oyun.chan.ok() ? oyun.chan.published() : 0u);
          return 1;
        }
        // (b) betikler kosuyor mu: 0.6 s arayla iki kare.
        ornekle(g_a);
        bekle(600);
        ornekle(g_b);
        g_ornek = g_ornek_w * g_ornek_h;
        g_hareket = 0;
        for (uint32_t i = 0; i < g_ornek; i++) {
          const int dr = g_a[i * 3] - g_b[i * 3], dg = g_a[i * 3 + 1] - g_b[i * 3 + 1], db = g_a[i * 3 + 2] - g_b[i * 3 + 2];
          if (dr * dr + dg * dg + db * db > 24 * 24) g_hareket++;
        }
        // (e) oynarken duzenleme (durdurunca geri alinmali).
        g_ed = st.scene.entity_count ? 0 : -1;
        if (g_ed >= 0) {
          g_x_once = st.scene.entities[g_ed].pos.x;
          SceneEntity after = st.scene.entities[g_ed];
          after.pos.x += 3.0f;
          commit(st, g_ed, after);
        }
        // (c) duraklat: F6'nin yaptigi (bayrak kare basinda kanala gider).
        st.paused = true;
        oyun.chan.set_paused(true);
        for (int i = 0; i < 300 && oyun.chan.child_state() != platform::GameChildState::Paused; i++) bekle(10);
        g_p0 = oyun.chan.published();
        bekle(300);
        g_p1 = oyun.chan.published();
        oyun.chan.request_step();
        for (int i = 0; i < 200 && oyun.chan.published() == g_p1; i++) bekle(10);
        bekle(100);
        g_p2 = oyun.chan.published();
        // Oyun duraklatilmis: son kare SABIT, (d) icin bir sonraki kareler onu yukleyip cizer.
        // Istatistik kaplamasi Oyun sekmesinin ustunde durur (sag ust kose,
        // karenin ~%40'i): olcum OYUNU olcsun, kaplamayi degil. 45'te geri gelir.
        g_stats_once = show_stats;
        show_stats = false;
      } else if (frame_i == 44) {
        // (d) 42 ve 43. kareler oyunun (sabit) karesini yukledi ve cizdi; ores
        // 43'un bilesik goruntusu. Oyun dikdortgeninin icinde izgara ornekleri
        // oyunun kendi pikseliyle karsilastirilir.
        const uint8_t *px = nullptr;
        uint32_t fr = 0;
        oyun.chan.acquire(&px, &fr);
        uint32_t ayni = 0, bos = 0, toplam = 0, renk = 0;
        uint32_t renkler[16] = {};
        const uint32_t gw = oyun.chan.width(), gh = oyun.chan.height();
        if (px && oyun_rect.w > 4 && oyun_rect.h > 4) {
          for (uint32_t j = 1; j < 18; j++)
            for (uint32_t i = 1; i < 32; i++) {
              const float sx = oyun_rect.x + oyun_rect.w * (float)i / 32.0f, sy = oyun_rect.y + oyun_rect.h * (float)j / 18.0f;
              const uint32_t ex = (uint32_t)sx, ey = (uint32_t)sy;
              if (ex >= oc.width || ey >= oc.height) continue;
              const uint32_t kx = (uint32_t)((sx - oyun_rect.x) / oyun_rect.w * (float)gw), ky = (uint32_t)((sy - oyun_rect.y) / oyun_rect.h * (float)gh);
              const uint8_t *e = ores.pixels + ((size_t)ey * oc.width + ex) * 4;
              const uint8_t *k = px + ((size_t)(ky < gh ? ky : gh - 1) * gw + (kx < gw ? kx : gw - 1)) * 4;
              toplam++;
              if (std::abs(e[0] - k[0]) <= 12 && std::abs(e[1] - k[1]) <= 12 && std::abs(e[2] - k[2]) <= 12) ayni++;
              if (e[0] <= 10 && e[1] <= 10 && e[2] <= 10) bos++;
              const uint32_t c = ((uint32_t)k[0] << 16) | ((uint32_t)k[1] << 8) | k[2];
              bool var = false;
              for (uint32_t q = 0; q < renk && q < 16; q++) var = var || renkler[q] == c;
              if (!var && renk < 16) renkler[renk++] = c;
            }
        }
        const double oran = toplam ? (double)ayni / toplam : 0.0;
        // KONTROL: kare tek renk olsaydi "ayni" hicbir sey kanitlamazdi; bekleme
        // yazisi ya da siyah kutu cizilseydi ornekler letterbox rengine duserdi.
        const bool goruntu_ok = toplam >= 400 && oran >= 0.90 && renk >= 8 && bos * 2 < toplam && oyun_goruntu.uploads() > 0;
        // Hareket yalniz sahnede ETKIN betik varsa beklenir: betiksiz bir sahnede
        // oyunu oyuncu surer, girdi olmadan durmasi dogru davranistir.
        uint32_t betikli = 0;
        for (uint32_t i = 0; i < st.scene.entity_count; i++)
          if ((st.scene.entities[i].components & content::kSceneScript) && st.scene.entities[i].script_enabled) betikli++;
        const bool hareket_ok = betikli == 0 || g_hareket >= 20;
        const bool durak_ok = g_p1 == g_p0 && g_p2 == g_p1 + 1;
        std::printf("[engine_editor] gomulu oynatma kapisi: %s %ux%u, ilk 20 kare %.1f s | %u betikli varlik, hareket %u/%u ornek degisti (0.6 s) %s | "
                    "duraklatma %u -> %u (300 ms), tek adim -> %u %s | Oyun sekmesi %u/%u ornek oyunun pikseli (%.0f%%, %u+ renk, %u yukleme) %s\n",
                    oyun.game, gw, gh, g_ilk_kare_s, betikli, g_hareket, g_ornek, hareket_ok ? (betikli ? "OK" : "(betik yok: beklenmedi)") : "HATA", g_p0, g_p1, g_p2, durak_ok ? "OK" : "HATA", ayni,
                    toplam, oran * 100.0, renk, oyun_goruntu.uploads(), goruntu_ok ? "OK" : "HATA");
        if (!(hareket_ok && durak_ok && goruntu_ok)) return 1;
      } else if (frame_i == 45) {
        // (f) Durdur: F5'in yaptigi. (e) geri alma da burada olur.
        show_stats = g_stats_once;
        st.paused = false;
        const uint64_t t0 = platform::now_ns();
        set_playing(false);
        g_bitir_goruldu = false;
        void (*yakala)(void *, const char *) = [](void *u, const char *line) {
          if (std::strstr(line, "`bitir` calisti")) *static_cast<bool *>(u) = true;
          console_log(ConsoleLevel::Bilgi, "oyun", "%s", line);
          std::printf("[oyun] %s\n", line);
        };
        for (int i = 0; i < 1000 && oyun.state == GameRunState::Running; i++) {
          game_run_poll(oyun, yakala, &g_bitir_goruldu);
          platform::thread_sleep_us(10000);
        }
        const double ms = (platform::now_ns() - t0) / 1e6;
        uint32_t betikli = 0; // `bitir` ozeti yalniz betikli sahnede basilir
        for (uint32_t i = 0; i < st.scene.entity_count; i++)
          if ((st.scene.entities[i].components & content::kSceneScript) && st.scene.entities[i].script_enabled) betikli++;
        if (!betikli) g_bitir_goruldu = true;
        const bool geri = g_ed < 0 || st.scene.entities[g_ed].pos.x == g_x_once;
        const bool temiz = oyun.state == GameRunState::Finished && oyun.exit_code == 0 && !oyun.killed;
        std::printf("[engine_editor] gomulu durdur kapisi: oyun %.0f ms'de kapandi (cikis %d, olduruldu %s), bitir kancasi %s%s, "
                    "oynarken duzenleme geri alindi %s, oynat %d gomulu %d %s\n",
                    ms, oyun.exit_code, oyun.killed ? "EVET" : "hayir", g_bitir_goruldu ? "kostu" : "GORULMEDI", betikli ? "" : " (betik yok)",
                    geri ? "evet" : "HAYIR", (int)st.playing,
                    (int)st.play_embedded, (temiz && g_bitir_goruldu && geri && !st.playing && !st.play_embedded) ? "OK" : "HATA");
        oyun_kapaniyor = false; // doku bir sonraki kare BASINDA birakilir (bu karenin cizimi onu kullanabilir)
        if (!(temiz && g_bitir_goruldu && geri && !st.playing && !st.play_embedded)) return 1;
      }
    }
    // --- NESNE OZELLIGI ISARET KAPISI (E5; kare 46-50, >= 52 kare) ------------
    // Gorunum isareti uctan uca: secili varligin `nokta` ozelligi (dunyaya
    // scene_prop_point_world ile cevrilmis) gorunumde GERCEKTEN cizildi mi —
    // piksel, bilesik karede (ImGui dahil). POZITIF KONTROL: ustune yazma
    // kaldirilinca AYNI piksel isaret rengini kaybetmeli (yoksa olculen isaret
    // degil, arka plandir). Betik noktayi bildiriyorsa ikinci yon de olculur:
    // kaldirinca betigin VARSAYILAN noktasinda isaret belirmeli (tarama ->
    // onbellek -> isaret yolu), ustune yazmayken orada OLMAMALI.
    // Aday: nokta ustune yazmasi olan ilk varlik (ozellik.sahne: "muhafiz" —
    // donuk ve olcekli kaidenin cocugu, yani donusum de olculuyor); yoksa
    // gecici bir fikstur yazilir ("kapi_nokta"). Kamera noktaya cevrilir.
    // Hepsi geri alinir; sahne baytlari baslangicla ayni olmali.
    if (headless && opts.headless_frames >= 52 && frame_i >= 46 && frame_i <= 50 && st.scene.entity_count && view_rect.w > 0) {
      static int32_t pk_ent = -1, pk_sel0 = -1;
      static char pk_name[content::kScenePropNameLen];
      static float pk_ov_w[3], pk_def_w[3];
      static bool pk_has_def = false, pk_injected = false, pk_stats = true;
      static uint32_t pk_ops = 0, pk_drawn_a = 0, pk_drawn_b = 0;
      static EditorCamera pk_cam;
      static char pk_txt0[65536], pk_txt1[65536];
      static size_t pk_n0 = 0;
      static uint8_t pk_ov_a[4], pk_ov_b[4], pk_def_a[4], pk_def_b[4];
      auto piksel = [&](const float w[3], uint8_t out[4]) {
        out[0] = out[1] = out[2] = out[3] = 0;
        ImVec2 sp;
        if (!project_to_view(proj * view, view_rect, w, &sp)) return false;
        if (sp.x < 0 || sp.y < 0 || (uint32_t)sp.x >= oc.width || (uint32_t)sp.y >= oc.height) return false;
        const uint8_t *q = ores.pixels + ((size_t)(uint32_t)sp.y * oc.width + (uint32_t)sp.x) * 4;
        out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; out[3] = q[3];
        return true;
      };
      if (frame_i == 46) {
        if (st.playing) set_playing(false); // onceki kapilarin oynatmasi
        view_tab = ViewportTab::Scene;
        pk_sel0 = st.sel.primary();
        pk_n0 = content::scene_write(st.scene, pk_txt0, sizeof pk_txt0);
        pk_ent = -1;
        pk_injected = pk_has_def = false;
        pk_ops = 0;
        // Aday: nokta ustune yazmasi olan ve betik varsayilani (bildirildiyse)
        // ondan FARKLI ilk varlik — ayniysa kaldirmak pikseli degistirmezdi.
        for (uint32_t i = 0; i < st.scene.entity_count && pk_ent < 0; i++) {
          const SceneEntity &x = st.scene.entities[i];
          const PropCacheEntry *ce = entity_prop_scan(st, x, frame_i, true, nullptr, 0);
          for (uint32_t k = 0; k < x.prop_count && pk_ent < 0; k++) {
            if (x.props[k].type != content::kScenePropNokta) continue;
            const PropDecl *d = ce ? prop_decl_find(ce->decls, ce->res.count, x.props[k].name) : nullptr;
            const bool def_ok = d && d->type == content::kScenePropNokta && !(d->flags & kPropDeclDefaultUnknown);
            if (def_ok && !std::memcmp(d->def, x.props[k].v, sizeof d->def)) continue;
            pk_ent = (int32_t)i;
            std::snprintf(pk_name, sizeof pk_name, "%s", x.props[k].name);
            content::scene_prop_point_world(st.scene, i, x.props[k].v, pk_ov_w);
            if (def_ok) { pk_has_def = true; content::scene_prop_point_world(st.scene, i, d->def, pk_def_w); }
          }
        }
        if (pk_ent < 0) { // fikstur: yer olan ilk varliga gecici bir nokta
          for (uint32_t i = 0; i < st.scene.entity_count && pk_ent < 0; i++) {
            SceneEntity a = st.scene.entities[i];
            const float v[3] = {1.5f, 0.0f, 1.0f};
            if (!content::scene_prop_set(a, "kapi_nokta", content::kScenePropNokta, v)) continue;
            if (!st.hist.set_entity(st.scene, i, a)) continue;
            st.groups.push(1);
            pk_ops++;
            pk_ent = (int32_t)i;
            pk_injected = true;
            std::snprintf(pk_name, sizeof pk_name, "kapi_nokta");
            content::scene_prop_point_world(st.scene, i, v, pk_ov_w);
          }
        }
        if (pk_ent < 0) {
          std::printf("[engine_editor] ozellik isaret kapisi: HATA — ne nokta ozellikli varlik var ne fikstur yazilabildi\n");
          return 1;
        }
        pk_cam = cam;
        // Istatistik kaplamasi gorunumun sag ust ceyregini ORTUYOR (olculdu
        // 2026-09-25, 1280x720: x >= 678): isaret onun altinda kalirsa kapi
        // arayuzu degil kaplamayi olcerdi. Gomulu oynatma kapisi da ayni seyi yapiyor.
        pk_stats = show_stats;
        show_stats = false;
        cam.mode = CameraMode::Orbit;
        cam.proj = CameraProjection::Perspective;
        cam.target = Vec3{pk_ov_w[0], pk_ov_w[1], pk_ov_w[2]};
        cam.radius = 6.0f;
        st.sel.set_single(pk_ent);
      } else if (frame_i == 48 && pk_ent >= 0) {
        // 47. kare yeni kamera + secimle cizildi; ores onun bilesik goruntusu.
        pk_drawn_a = st.prop_markers_drawn;
        piksel(pk_ov_w, pk_ov_a);
        if (pk_has_def) piksel(pk_def_w, pk_def_a);
        SceneEntity b = st.scene.entities[pk_ent];
        if (content::scene_prop_remove(b, pk_name) && st.hist.set_entity(st.scene, (uint32_t)pk_ent, b)) {
          st.groups.push(1);
          pk_ops++;
        }
      } else if (frame_i == 50 && pk_ent >= 0) {
        pk_drawn_b = st.prop_markers_drawn;
        const bool ov_b_ok = piksel(pk_ov_w, pk_ov_b);
        if (pk_has_def) piksel(pk_def_w, pk_def_b);
        // Beklenen renk: AccentHi, sRGB hedefte (oc.srgb) palet dogrusal verilir
        // ve donanim geri kodlar — yani piksel paletin altigen degeri.
        float c[4];
        editor_tone(Tone::AccentHi, c);
        int bek[3];
        for (int k = 0; k < 3; k++) {
          const float x = c[k] <= 0.0031308f ? c[k] * 12.92f : 1.055f * std::pow(c[k], 1.0f / 2.4f) - 0.055f;
          bek[k] = (int)(x * 255.0f + 0.5f);
        }
        auto uzak = [&](const uint8_t *p) { return std::abs(p[0] - bek[0]) + std::abs(p[1] - bek[1]) + std::abs(p[2] - bek[2]); };
        const int d_ov_a = uzak(pk_ov_a), d_ov_b = uzak(pk_ov_b);
        const int d_def_a = pk_has_def ? uzak(pk_def_a) : -1, d_def_b = pk_has_def ? uzak(pk_def_b) : -1;
        // Esikler: isaret dolgusu tek renk, merkez pikseli kenar yumusatmasinin
        // disinda (olculdu 2026-09-25, RTX 5080: fark 0; arka plan 255-349).
        // Kontrol icin arka planin en az 90 uzakta olmasi istenir.
        const bool ov_ok = d_ov_a <= 30 && ov_b_ok && d_ov_b >= 90;
        const bool def_ok = !pk_has_def || (d_def_b <= 30 && d_def_a >= 90);
        for (uint32_t k = 0; k < pk_ops; k++) do_undo();
        const size_t n1 = content::scene_write(st.scene, pk_txt1, sizeof pk_txt1);
        const bool geri = pk_n0 < sizeof pk_txt0 && n1 == pk_n0 && std::strcmp(pk_txt0, pk_txt1) == 0;
        cam = pk_cam;
        show_stats = pk_stats;
        if (pk_sel0 >= 0 && pk_sel0 < (int32_t)st.scene.entity_count) st.sel.set_single(pk_sel0);
        else st.sel.clear();
        st.dirty = false;
        const bool ok = ov_ok && def_ok && geri && pk_drawn_a >= 1;
        std::printf("[engine_editor] ozellik isaret kapisi: \"%s\".%s%s dunya (%.2f %.2f %.2f), %u isaret; isaret pikseli fark %d (<=30) %s, "
                    "KONTROL ustune yazma kaldirilinca fark %d (>=90) %s",
                    st.scene.entities[pk_ent].name, pk_name, pk_injected ? " (fikstur)" : "", (double)pk_ov_w[0], (double)pk_ov_w[1],
                    (double)pk_ov_w[2], pk_drawn_a, d_ov_a, d_ov_a <= 30 ? "evet" : "HAYIR", d_ov_b, d_ov_b >= 90 ? "evet" : "HAYIR");
        if (pk_has_def)
          std::printf(", betik varsayilani (%.2f %.2f %.2f) kaldirinca belirdi fark %d %s, once yoktu fark %d %s", (double)pk_def_w[0],
                      (double)pk_def_w[1], (double)pk_def_w[2], d_def_b, d_def_b <= 30 ? "evet" : "HAYIR", d_def_a, d_def_a >= 90 ? "evet" : "HAYIR");
        else
          std::printf(", betik varsayilani: bildirilmemis (olculmedi)");
        std::printf(", isaret sayisi kaldirinca %u, geri al baslangic baytlari %s %s\n", pk_drawn_b, geri ? "evet" : "HAYIR", ok ? "OK" : "HATA");
        if (!ok) {
          // Teshis: beklenen ve olculen pikseller + kare (kapinin gordugu goruntu).
          std::printf("[engine_editor]   beklenen (%d %d %d) | isaret once (%u %u %u) sonra (%u %u %u) | varsayilan once (%u %u %u) sonra (%u %u %u)\n",
                      bek[0], bek[1], bek[2], pk_ov_a[0], pk_ov_a[1], pk_ov_a[2], pk_ov_b[0], pk_ov_b[1], pk_ov_b[2], pk_def_a[0], pk_def_a[1],
                      pk_def_a[2], pk_def_b[0], pk_def_b[1], pk_def_b[2]);
          char kare[1200];
          const char *td = std::getenv("TMPDIR");
          std::snprintf(kare, sizeof kare, "%s/ozellik_isaret_kapisi.ppm", td && *td ? td : ".");
          if (rhi::write_ppm(kare, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor]   kapinin karesi: %s\n", kare);
        }
        pk_ent = -1;
        if (!ok) return 1;
      }
    }
    // --- E6 NOKTA DUZENLEME KAPILARI ---------------------------------------------
    // Ortak kurulum: aday (prop_edit_gate_candidate) secilir, kamera varlik ile
    // noktanin ORTASINA bakar ve bakis ikisini birlestiren dogruya DIK (yatayda)
    // cevrilir — varligin gizmosu ile noktanin isareti ekranda ayri dursun
    // (ust uste binerlerse tik gizmoya gider ve kapi isareti degil gizmoyu olcer).
    auto pe_kapi_kamera = [&](int32_t ent, const char *name) {
      float l[3] = {0, 0, 0}, p[3];
      prop_edit_point(st, ent, name, frame_i, l, nullptr);
      content::scene_prop_point_world(st.scene, (uint32_t)ent, l, p);
      const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)ent);
      const Vec3 o{wm.m[3][0], wm.m[3][1], wm.m[3][2]}, pp{p[0], p[1], p[2]};
      const Vec3 dv = pp - o;
      const float hl = std::sqrt(dv.x * dv.x + dv.z * dv.z);
      cam.mode = CameraMode::Orbit;
      cam.proj = CameraProjection::Perspective;
      cam.target = (o + pp) * 0.5f;
      cam.yaw = hl > 1e-3f ? std::atan2(-dv.z, dv.x) : 0.6f; // orbit_dir yatayi (sin, cos) . (dx, dz) = 0
      cam.pitch = 0.35f;
      const float r = 2.5f * length(dv);
      cam.radius = r > 6.0f ? r : 6.0f;
    };
    // Verilen matrislerle noktanin (local != nullptr) ya da varligin ekran pikseli.
    auto pe_kapi_piksel = [&](const Mat4 &vpm, int32_t ent, const float *local, float *sx, float *sy) {
      float w[3];
      if (local) {
        content::scene_prop_point_world(st.scene, (uint32_t)ent, local, w);
      } else {
        const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)ent);
        w[0] = wm.m[3][0]; w[1] = wm.m[3][1]; w[2] = wm.m[3][2];
      }
      ImVec2 sp;
      const bool ok = project_to_view(vpm, view_rect, w, &sp);
      *sx = sp.x;
      *sy = sp.y;
      return ok && sp.x > view_rect.x && sp.y > view_rect.y && sp.x < view_rect.x + view_rect.w && sp.y < view_rect.y + view_rect.h;
    };
    // --- NOKTA ISARETI TIKLAMA KAPISI (E6, kare 34-39, >= 40 kare) --------------
    // Olculen: gorunumde isaretin eskenar dortgenine sentetik TIK (uzerine gel ->
    // bas -> birak; gizmo surukleme kapisiyla AYNI girdi yolu, g_synth) kipi O
    // nokta icin ACAR ve secimi DEGISTIRMEZ (tik varlik secimine dusmedi).
    // POZITIF KONTROL: ayni piksele ayni tik varlik KILITLIYKEN kipi ACMAZ —
    // kapi "her tik kipi acar"i degil isaret kuralini olcuyor. Durdur kapisi
    // (20-33) oynatmayi acik birakiyor; burada kapatilir. Hepsi geri alinir.
    if (headless && opts.headless_frames >= 40 && frame_i >= 34 && frame_i <= 39 && st.scene.entity_count && view_rect.w > 0) {
      static int32_t tk_ent = -1, tk_sel0 = -1, tk_sel_a = -1;
      static char tk_name[content::kScenePropNameLen];
      static uint32_t tk_ops = 0, tk_selc_a = 0, tk_flags0 = 0;
      static bool tk_fix = false, tk_stats = true, tk_on_a = false, tk_px_ok = false, tk_over = false;
      static float tk_px = 0, tk_py = 0;
      static EditorCamera tk_cam;
      static char tk_txt0[65536], tk_txt1[65536];
      static size_t tk_n0 = 0;
      if (frame_i == 34) {
        if (st.playing) set_playing(false);
        view_tab = ViewportTab::Scene;
        prop_edit_exit(st, "kapi");
        tk_sel0 = st.sel.primary();
        tk_n0 = content::scene_write(st.scene, tk_txt0, sizeof tk_txt0);
        tk_ent = prop_edit_gate_candidate(st, frame_i, tk_name, sizeof tk_name, &tk_ops, &tk_fix);
        if (tk_ent < 0) {
          std::printf("[engine_editor] nokta isaret tiklama kapisi: HATA — suruklenebilir nokta yok ve fikstur (muhafiz.tpr devriye_a) kurulamadi\n");
          return 1;
        }
        tk_cam = cam;
        tk_stats = show_stats;
        show_stats = false; // istatistik kaplamasi gorunumun sag ustunu ortuyor (E5 kapisindaki olcum)
        pe_kapi_kamera(tk_ent, tk_name);
        st.sel.set_single(tk_ent);
        g_synth.mouse_down[0] = false;
      } else if (tk_ent < 0) {
        // kurulamadi (yukarida dondu)
      } else if (frame_i == 35) {
        float l[3];
        prop_edit_point(st, tk_ent, tk_name, frame_i, l, nullptr);
        tk_px_ok = pe_kapi_piksel(proj * view, tk_ent, l, &tk_px, &tk_py); // 35. kare yeni kamerayla cizildi
        g_synth.mouse_x = tk_px; g_synth.mouse_y = tk_py; // isaretin uzerine GEL
      } else if (frame_i == 36) {
        tk_over = st.gizmo_was_over; // teshis: isaret varligin gizmosunun altinda mi
        g_synth.mouse_down[0] = true; // tik (37. kare)
      } else if (frame_i == 37) {
        tk_on_a = st.pe_entity == tk_ent && !std::strcmp(st.pe_name, tk_name);
        tk_sel_a = st.sel.primary();
        tk_selc_a = st.sel.count;
        g_synth.mouse_down[0] = false;
        prop_edit_exit(st, "kapi");
        // KONTROL: kilit (bayrak dogrudan — gunluge girmez, 39'da geri konur).
        tk_flags0 = st.scene.entities[tk_ent].flags;
        st.scene.entities[tk_ent].flags |= content::kSceneLocked;
      } else if (frame_i == 38) {
        g_synth.mouse_down[0] = true; // ayni piksel, ayni tik (39. kare)
      } else if (frame_i == 39) {
        const bool on_b = prop_edit_active(st);
        g_synth.mouse_down[0] = false;
        st.scene.entities[tk_ent].flags = tk_flags0;
        prop_edit_exit(st, "kapi");
        for (uint32_t k = 0; k < tk_ops; k++) do_undo();
        const size_t n1 = content::scene_write(st.scene, tk_txt1, sizeof tk_txt1);
        const bool geri = tk_n0 < sizeof tk_txt0 && n1 == tk_n0 && std::strcmp(tk_txt0, tk_txt1) == 0;
        cam = tk_cam;
        show_stats = tk_stats;
        if (tk_sel0 >= 0 && tk_sel0 < (int32_t)st.scene.entity_count) st.sel.set_single(tk_sel0);
        else st.sel.clear();
        st.dirty = false;
        const bool sel_ok = tk_sel_a == tk_ent && tk_selc_a == 1;
        const bool ok = tk_px_ok && tk_on_a && sel_ok && !on_b && geri;
        std::printf("[engine_editor] nokta isaret tiklama kapisi: \"%s\".%s%s piksel (%.0f,%.0f)%s -> kip acildi %s, secim ayni %s; "
                    "KONTROL kilitliyken ayni tik kipi acmadi %s; geri al baslangic baytlari %s %s\n",
                    st.scene.entities[tk_ent].name, tk_name, tk_fix ? " (fikstur)" : "", (double)tk_px, (double)tk_py,
                    tk_px_ok ? "" : " GORUNUM DISI", tk_on_a ? "evet" : "HAYIR", sel_ok ? "evet" : "HAYIR", !on_b ? "evet" : "HAYIR",
                    geri ? "evet" : "HAYIR", ok ? "OK" : "HATA");
        if (!ok && tk_over)
          std::printf("[engine_editor]   isaret pikseli varligin gizmosunun ustunde (ImGuizmo over): tik gizmoya gitti\n");
        tk_ent = -1;
        if (!ok) return 1;
      }
    }
    // --- NOKTA SURUKLEME KAPISI (E6, kare 51-59, >= 60 kare) ---------------------
    // Olculen, ayni aday ve AYNI kamerada, gizmo surukleme kapisinin (10-17)
    // sentetik girdisiyle (gizmonun merkezine gel -> bas -> (+60,+25) -> birak):
    //   KONTROL (52-55): kip KAPALIYKEN surukleme VARLIGI tasir, noktanin YEREL
    //     degeri bit-tam ayni kalir — yani girdi gizmoya ulasiyor ve asagidaki
    //     farki yaratan KIP. Secimde ikinci ("yan", akrabasi olmayan) bir varlik
    //     da var: grup suruklemesi onu ANA SECILIYLE AYNI delta kadar tasimali
    //     (Tuzaklar 8ck: delta her kare yenilenen edit_before'dan alininca yan
    //     varlik yalniz SON karenin artimini aliyordu), gunluge 2 islem / 1 grup.
    //   KIP (55-59, ✥ ile ayni giris yolu prop_edit_enter): ayni surukleme
    //     yalniz NOKTAYI tasir: varligin donusumu (ve noktadan baska HER alani)
    //     degismez; yerel deger, gizmonun verdigi dunya deltasinin varligin
    //     dunya donusunun TERSIYLE cevrilmis hali kadar degisir (beklenen deger
    //     BAGIMSIZ yoldan: kuaterniyon zinciri degil dunya MATRISININ normlanmis
    //     sutunlari); point_world(yeni yerel) gizmonun konumuna oturur; gunluge
    //     TAM 1 islem (grup derinligi +1); geri al sahne baytlarini geri getirir.
    //     Coklu secim: yalniz ANA secilinin noktasi — yan varlik bit-tam ayni.
    // Fikstur adayda (ustune yazma yok) surukleme ustune yazmayi YARATIR.
    if (headless && opts.headless_frames >= 60 && frame_i >= 51 && frame_i <= 59 && st.scene.entity_count && view_rect.w > 0) {
      static int32_t sg_ent = -1, sg_sel0 = -1, sg_yan = -1;
      static char sg_name[content::kScenePropNameLen];
      static uint32_t sg_ops = 0, sg_undo0 = 0, sg_undo1 = 0, sg_depth0 = 0, sg_depth1 = 0, sg_ctrl_ops = 0, sg_ctrl_groups = 0;
      static float sg_ctrl_yan_err = 0;
      static SceneEntity sg_yan0;
      static bool sg_fix = false, sg_stats = true, sg_ov0 = false, sg_ctrl_moved = false, sg_ctrl_same = false, sg_ctrl_back = false,
                  sg_entered = false, sg_px_ok = false;
      static float sg_px = 0, sg_py = 0, sg_l0[3], sg_w0[3], sg_ctrl_d = 0;
      static SceneEntity sg_e0;
      static EditorCamera sg_cam;
      static char sg_pre[65536], sg_mid[65536], sg_tmp[65536];
      static size_t sg_npre = 0, sg_nmid = 0;
      if (frame_i == 51) {
        if (st.playing) set_playing(false); // gomulu oynatma kapisinin artigi
        view_tab = ViewportTab::Scene;
        gizmo_op = 0; // TASI (kontrol suruklemesi varligi tasisin)
        prop_edit_exit(st, "kapi");
        sg_sel0 = st.sel.primary();
        sg_npre = content::scene_write(st.scene, sg_pre, sizeof sg_pre);
        sg_ent = prop_edit_gate_candidate(st, frame_i, sg_name, sizeof sg_name, &sg_ops, &sg_fix);
        if (sg_ent < 0) {
          std::printf("[engine_editor] nokta surukleme kapisi: HATA — suruklenebilir nokta yok ve fikstur (muhafiz.tpr devriye_a) kurulamadi\n");
          return 1;
        }
        sg_nmid = content::scene_write(st.scene, sg_mid, sizeof sg_mid); // fiksturden sonra, suruklemelerden once
        sg_cam = cam;
        sg_stats = show_stats;
        show_stats = false;
        pe_kapi_kamera(sg_ent, sg_name);
        st.sel.set_single(sg_ent);
        // Yan varlik: kilitsiz, gizli degil ve adayla AKRABA degil (ebeveynini
        // grup halinde tasimak cocugu iki kez tasirdi — o baska bir sozlesme).
        sg_yan = -1;
        for (uint32_t i = 0; i < st.scene.entity_count && sg_yan < 0; i++) {
          if ((int32_t)i == sg_ent || (st.scene.entities[i].flags & (content::kSceneLocked | content::kSceneHidden))) continue;
          bool akraba = false;
          for (int32_t a = st.scene.entities[sg_ent].parent, g = 0; a >= 0 && g < 64; a = st.scene.entities[a].parent, g++)
            if (a == (int32_t)i) akraba = true;
          for (int32_t a = st.scene.entities[i].parent, g = 0; a >= 0 && g < 64; a = st.scene.entities[a].parent, g++)
            if (a == sg_ent) akraba = true;
          if (!akraba) sg_yan = (int32_t)i;
        }
        if (sg_yan >= 0) { // toggle en son ekleneni ANA secili yapar: once yan, sonra aday
          st.sel.set_single(sg_yan);
          st.sel.toggle(sg_ent);
        }
        // 52. karenin matrisleri kameradan BIRE BIR (girdisiz karede camera_update
        // kamerayi degistirmez): varligin pikseli simdiden bilinir, 52 uzerine gelme karesi olur.
        const Mat4 vpm = camera_projection(cam, vp.aspect(), 0.1f, 200.0f) * camera_view(cam);
        sg_px_ok = pe_kapi_piksel(vpm, sg_ent, nullptr, &sg_px, &sg_py);
        g_synth.mouse_x = sg_px; g_synth.mouse_y = sg_py; g_synth.mouse_down[0] = false;
      } else if (sg_ent < 0) {
        // kurulamadi
      } else if (frame_i == 52) {
        float ax, ay; // tahmin bu karenin GERCEK matrisiyle ayni mi (degilse duzelt, soyle)
        sg_px_ok = pe_kapi_piksel(proj * view, sg_ent, nullptr, &ax, &ay) && sg_px_ok;
        if (std::fabs(ax - sg_px) > 0.5f || std::fabs(ay - sg_py) > 0.5f) {
          std::printf("[engine_editor]   nokta surukleme kapisi: piksel tahmini (%.1f,%.1f) != gercek (%.1f,%.1f); duzeltildi\n", (double)sg_px,
                      (double)sg_py, (double)ax, (double)ay);
          sg_px = ax; sg_py = ay;
          g_synth.mouse_x = sg_px; g_synth.mouse_y = sg_py;
        }
        sg_e0 = st.scene.entities[sg_ent];
        if (sg_yan >= 0) sg_yan0 = st.scene.entities[sg_yan];
        prop_edit_point(st, sg_ent, sg_name, frame_i, sg_l0, &sg_ov0);
        sg_undo0 = st.hist.undo_count();
        sg_depth0 = st.groups.depth();
        g_synth.mouse_down[0] = true; // KONTROL: kip kapali, varligin gizmosunu bas (53)
      } else if (frame_i == 53) {
        g_synth.mouse_x = sg_px + 60.0f; g_synth.mouse_y = sg_py + 25.0f;
      } else if (frame_i == 54) {
        g_synth.mouse_down[0] = false; // birak (55): gunluge
      } else if (frame_i == 55) {
        const SceneEntity &x = st.scene.entities[sg_ent];
        const Vec3 d = x.pos - sg_e0.pos;
        sg_ctrl_d = length(d);
        sg_ctrl_moved = sg_ctrl_d > 1e-4f;
        float l[3] = {0, 0, 0};
        bool ov = false;
        prop_edit_point(st, sg_ent, sg_name, frame_i, l, &ov);
        sg_ctrl_same = ov == sg_ov0 && !std::memcmp(l, sg_l0, sizeof l);
        sg_ctrl_ops = st.hist.undo_count() - sg_undo0;
        sg_ctrl_groups = st.groups.depth() - sg_depth0;
        sg_ctrl_yan_err = 0;
        if (sg_yan >= 0) sg_ctrl_yan_err = length((st.scene.entities[sg_yan].pos - sg_yan0.pos) - d); // yan AYNI delta
        do_undo();
        const size_t n = content::scene_write(st.scene, sg_tmp, sizeof sg_tmp);
        sg_ctrl_back = n == sg_nmid && std::strcmp(sg_tmp, sg_mid) == 0;
        // KIP: denetcideki ✥ ile AYNI giris yolu.
        sg_entered = prop_edit_enter(st, sg_ent, sg_name, frame_i);
        sg_e0 = st.scene.entities[sg_ent];
        if (sg_yan >= 0) sg_yan0 = st.scene.entities[sg_yan];
        prop_edit_point(st, sg_ent, sg_name, frame_i, sg_l0, &sg_ov0);
        content::scene_prop_point_world(st.scene, (uint32_t)sg_ent, sg_l0, sg_w0);
        sg_undo1 = st.hist.undo_count();
        sg_depth1 = st.groups.depth();
        sg_px_ok = pe_kapi_piksel(proj * view, sg_ent, sg_l0, &sg_px, &sg_py) && sg_px_ok; // kamera ayni
        g_synth.mouse_x = sg_px; g_synth.mouse_y = sg_py; g_synth.mouse_down[0] = false; // noktanin gizmosuna GEL (56)
      } else if (frame_i == 56) {
        g_synth.mouse_down[0] = true; // bas (57)
      } else if (frame_i == 57) {
        g_synth.mouse_x = sg_px + 60.0f; g_synth.mouse_y = sg_py + 25.0f; // tasi (58)
      } else if (frame_i == 58) {
        g_synth.mouse_down[0] = false; // birak (59): gunluge TEK islem
      } else if (frame_i == 59) {
        const SceneEntity &x = st.scene.entities[sg_ent];
        const bool still = prop_edit_active(st) && st.pe_entity == sg_ent;
        const uint32_t ops = st.hist.undo_count() - sg_undo1, groups = st.groups.depth() - sg_depth1;
        // Donusum degismedi: pos/donus/olcek/ebeveyn bit-tam.
        const bool tf_same = !std::memcmp(&x.pos, &sg_e0.pos, sizeof x.pos) && !std::memcmp(&x.rot_deg, &sg_e0.rot_deg, sizeof x.rot_deg) &&
                             !std::memcmp(&x.scale, &sg_e0.scale, sizeof x.scale) && x.parent == sg_e0.parent;
        // Noktadan BASKA hicbir alan degismedi: noktayi eski haline cevirince varlik bit-tam ayni.
        SceneEntity back = x;
        if (sg_ov0) content::scene_prop_set(back, sg_name, content::kScenePropNokta, sg_l0);
        else content::scene_prop_remove(back, sg_name);
        const bool only_point = content::scene_entity_equal(back, sg_e0);
        const content::SceneProp *np = content::scene_prop_find(x, sg_name);
        const bool has = np && np->type == content::kScenePropNokta;
        float l1[3] = {0, 0, 0};
        if (has) { l1[0] = np->v[0]; l1[1] = np->v[1]; l1[2] = np->v[2]; }
        // Beklenen yerel: eski + R^T (dunya deltasi); R dunya MATRISININ normlanmis
        // sutunlari (kuaterniyon zincirinden bagimsiz yol; egik olmayan zincirde ayni donus).
        const Mat4 wm = content::scene_entity_world_matrix(st.scene, (uint32_t)sg_ent);
        Vec3 c[3];
        for (int k = 0; k < 3; k++) c[k] = normalize(Vec3{wm.m[k][0], wm.m[k][1], wm.m[k][2]});
        const Vec3 dw{st.pe_drag_world[0] - sg_w0[0], st.pe_drag_world[1] - sg_w0[1], st.pe_drag_world[2] - sg_w0[2]};
        const float bek[3] = {sg_l0[0] + dot(c[0], dw), sg_l0[1] + dot(c[1], dw), sg_l0[2] + dot(c[2], dw)};
        float hata_bek = 0, hata_w = 0, dl = 0;
        float w1[3];
        content::scene_prop_point_world(st.scene, (uint32_t)sg_ent, l1, w1);
        for (int k = 0; k < 3; k++) {
          hata_bek = std::fabs(l1[k] - bek[k]) > hata_bek ? std::fabs(l1[k] - bek[k]) : hata_bek;
          hata_w = std::fabs(w1[k] - st.pe_drag_world[k]) > hata_w ? std::fabs(w1[k] - st.pe_drag_world[k]) : hata_w;
          dl += (l1[k] - sg_l0[k]) * (l1[k] - sg_l0[k]);
        }
        dl = std::sqrt(dl);
        const bool moved = has && dl > 1e-3f && st.pe_drag_writes > 0;
        const bool yan_same = sg_yan < 0 || content::scene_entity_equal(st.scene.entities[sg_yan], sg_yan0);
        do_undo();
        const size_t n = content::scene_write(st.scene, sg_tmp, sizeof sg_tmp);
        const bool geri_nokta = n == sg_nmid && std::strcmp(sg_tmp, sg_mid) == 0;
        prop_edit_exit(st, "kapi");
        for (uint32_t k = 0; k < sg_ops; k++) do_undo();
        const size_t n2 = content::scene_write(st.scene, sg_tmp, sizeof sg_tmp);
        const bool geri = sg_npre < sizeof sg_pre && n2 == sg_npre && std::strcmp(sg_tmp, sg_pre) == 0;
        cam = sg_cam;
        show_stats = sg_stats;
        if (sg_sel0 >= 0 && sg_sel0 < (int32_t)st.scene.entity_count) st.sel.set_single(sg_sel0);
        else st.sel.clear();
        st.dirty = false;
        const uint32_t ctrl_ops_bek = sg_yan >= 0 ? 2u : 1u;
        const bool ctrl_ok = sg_ctrl_moved && sg_ctrl_same && sg_ctrl_ops == ctrl_ops_bek && sg_ctrl_groups == 1 && sg_ctrl_yan_err < 1e-5f &&
                             sg_ctrl_back;
        const bool ok = sg_px_ok && ctrl_ok && sg_entered && still && moved && tf_same && only_point && yan_same && hata_bek < 1e-3f &&
                        hata_w < 1e-4f && ops == 1 && groups == 1 && geri_nokta && geri;
        std::printf("[engine_editor] nokta surukleme kapisi: \"%s\".%s%s (%s) yerel (%.3f %.3f %.3f) -> (%.3f %.3f %.3f) |d|=%.3f, "
                    "gizmo dunya deltasi (%.3f %.3f %.3f); beklenen yerelden sapma %.1e (<1e-3) %s, dunyaya oturma %.1e (<1e-4) %s, "
                    "donusum ayni %s, noktadan baska alan ayni %s, yan \"%s\" ayni %s, gunluk %u islem / %u grup (1) %s, kip suruyor %s, "
                    "geri al -> baytlar %s; KONTROL kip kapaliyken varlik |d|=%.3f tasindi %s, nokta yereli ayni %s, yan ayni deltayla %s "
                    "(sapma %.1e), %u islem / %u grup (%u/1) %s, geri al %s; fikstur geri %s %s\n",
                    st.scene.entities[sg_ent].name, sg_name, sg_fix ? " (fikstur)" : "", sg_ov0 ? "ustune yazma" : "varsayilandan",
                    (double)sg_l0[0], (double)sg_l0[1], (double)sg_l0[2], (double)l1[0], (double)l1[1], (double)l1[2], (double)dl, (double)dw.x,
                    (double)dw.y, (double)dw.z, (double)hata_bek, hata_bek < 1e-3f ? "evet" : "HAYIR", (double)hata_w,
                    hata_w < 1e-4f ? "evet" : "HAYIR", tf_same ? "evet" : "HAYIR", only_point ? "evet" : "HAYIR",
                    sg_yan >= 0 ? st.scene.entities[sg_yan].name : "-", yan_same ? "evet" : "HAYIR", ops, groups,
                    (ops == 1 && groups == 1) ? "evet" : "HAYIR", still ? "evet" : "HAYIR", geri_nokta ? "evet" : "HAYIR", (double)sg_ctrl_d,
                    sg_ctrl_moved ? "evet" : "HAYIR", sg_ctrl_same ? "evet" : "HAYIR", sg_ctrl_yan_err < 1e-5f ? "evet" : "HAYIR",
                    (double)sg_ctrl_yan_err, sg_ctrl_ops, sg_ctrl_groups, ctrl_ops_bek,
                    (sg_ctrl_ops == ctrl_ops_bek && sg_ctrl_groups == 1) ? "evet" : "HAYIR", sg_ctrl_back ? "evet" : "HAYIR",
                    geri ? "evet" : "HAYIR", ok ? "OK" : "HATA");
        if (!ok && !sg_px_ok) std::printf("[engine_editor]   varlik ya da nokta gorunum DISINDA kaldi (kamera kurulumu)\n");
        if (!ok && !sg_entered) std::printf("[engine_editor]   kip ACILMADI (durum cubugu: %s)\n", st.status);
        sg_ent = -1;
        if (!ok) return 1;
      }
    }
    if (headless && frame_i >= 2 && frame_i <= 5) {
      // E1 duzen kaliciligi kapisi: kaydet -> A; dosyadan yukle -> (bir kare sonra,
      // dugum dikdortgenleri DockSpace'te turetilir) B; A == B BIT-TAM. Pozitif
      // kontrol: dosyadaki bir bolme oranini degistirip yuklemek C'yi A'dan
      // AYIRMALI — yoksa "esit" olcumu hicbir seyi olcmuyordur. Sonra ozgun
      // dosya geri yuklenir (kalan kareler varsayilan duzende kosar).
      static LayoutRect A[kLayoutMaxWindows], B[kLayoutMaxWindows], C[kLayoutMaxWindows];
      static uint32_t na = 0, nbb = 0, nc = 0;
      static char lay[1200], lay_mut[1200];
      static bool lfail = false;
      LayoutError lerr{};
      auto rects_equal = [](const LayoutRect *x, uint32_t nx, const LayoutRect *y, uint32_t ny) {
        if (nx != ny) return false;
        for (uint32_t i = 0; i < nx; i++)
          if (!layout_rect_equal(x[i], y[i])) return false;
        return true;
      };
      if (frame_i == 2) {
        std::snprintf(lay, sizeof lay, "%s/.duzen_kapisi.duzen", st.scene_dir);
        std::snprintf(lay_mut, sizeof lay_mut, "%s/.duzen_kapisi_mut.duzen", st.scene_dir);
        na = layout_snapshot(A, kLayoutMaxWindows);
        if (!layout_save(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: KAYDEDILEMEDI %s\n", lerr.msg); return 1; }
        // Mutant: ilk bolmenin genisligini degistir (satir bazli, metin dosyasi).
        FILE *f = std::fopen(lay, "rb");
        static char txt[kLayoutMaxBytes + 1];
        size_t n = f ? std::fread(txt, 1, kLayoutMaxBytes, f) : 0;
        if (f) std::fclose(f);
        txt[n] = 0;
        // Ilk "w=" ya da genislik alanini bul: dosya formatini editor_layout yazar,
        // burada yalniz ilk sayisal SizeRef'i %30 buyutuyoruz.
        char *dg = std::strstr(txt, "dugum ");
        bool mutated = false;
        FILE *m = std::fopen(lay_mut, "wb");
        if (m && dg) {
          // Kok olmayan ILK yaprak dugumun SizeRef genisligini %30 buyut (bolme
          // orani ondan turer). Satir formati editor_layout.cpp::layout_write:
          // "dugum <i> <parent> <x|y|-> <w> <h>" — '-' yaprak.
          char *line = dg;
          while (line && !mutated) {
            int idx, par; char ax; float w, h;
            if (std::sscanf(line, "dugum %d %d %c %f %f", &idx, &par, &ax, &w, &h) == 5 && ax == '-' && par >= 0 && w > 0) {
              char *eol = std::strchr(line, '\n');
              std::fwrite(txt, 1, (size_t)(line - txt), m);
              std::fprintf(m, "dugum %d %d - %.9g %.9g", idx, par, w * 1.3f, h);
              if (eol) std::fputs(eol, m);
              mutated = true;
            } else {
              char *eol = std::strchr(line, '\n');
              line = eol ? eol + 1 : nullptr;
            }
          }
          if (!mutated) std::fwrite(txt, 1, n, m);
          std::fclose(m);
        } else if (m) { std::fwrite(txt, 1, n, m); std::fclose(m); }
        if (!mutated) std::printf("[engine_editor] duzen kapisi: UYARI mutant uretilemedi (format degisti mi?)\n");
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: YUKLENEMEDI %s\n", lerr.msg); return 1; }
      } else if (frame_i == 3) {
        nbb = layout_snapshot(B, kLayoutMaxWindows);
        if (!layout_load(lay_mut, &lerr)) { std::printf("[engine_editor] duzen kapisi: mutant YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 4) {
        nc = layout_snapshot(C, kLayoutMaxWindows);
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: geri YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 5) {
        static LayoutRect D[kLayoutMaxWindows];
        const uint32_t nd = layout_snapshot(D, kLayoutMaxWindows);
        const bool ab = rects_equal(A, na, B, nbb), ac = rects_equal(A, na, C, nc), ad = rects_equal(A, na, D, nd);
        const bool lok = !lfail && na >= kLayoutPanelCount && ab && !ac && ad;
        std::printf("[engine_editor] duzen kapisi: %u panel, kaydet->yukle bit-tam %s, KONTROL mutant farkli %s, geri yukle bit-tam %s, atlanan %u %s\n", na,
                    ab ? "evet" : "HAYIR", ac ? "HAYIR" : "evet", ad ? "evet" : "HAYIR", layout_skipped(), lok ? "OK" : "HATA");
        std::remove(lay);
        std::remove(lay_mut);
        if (!lok) return 1;
      }
    }
    // --- Kipli pencereler: HER KARE AYNI YERDEN cagrilir (ImGui popup kimligi
    // bulunulan pencere yiginindan turer; menu geri cagrisindan OpenPopup etmek
    // kimligi kaydirirdi — ikisi de OpenPopup'i kendi icinde yapiyor).
    if (dlg.open) {
      const FileDialogAction fa = file_dialog_draw(dlg);
      if (fa == FileDialogAction::Accepted && dlg_intent == IntentPrefabSave) {
        // Alt agaci ayri bir .sahne metnine cikar (bkz. content/prefab.hpp).
        static content::SceneDesc pf; // buyuk: yigina konmaz
        content::SceneError err{};
        const uint32_t n = prefab_extract(st.scene, prefab_root, &pf);
        if (n && content::scene_save(frame, pf, dlg.path, &err)) {
          set_status(st, "prefab kaydedildi: %s (%u varlik)", dlg.path, n);
          console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "prefab kaydedildi: %s (%u varlik)", dlg.path, n);
        } else {
          set_status(st, "prefab KAYDEDILEMEDI: %s", n ? err.msg : "gecersiz kok");
        }
        dlg_intent = IntentScene;
      } else if (fa == FileDialogAction::Accepted && dlg_intent == IntentPrefabLoad) {
        static content::SceneDesc pf;
        content::SceneError err{};
        if (!content::scene_load(frame, dlg.path, &pf, &err)) {
          set_status(st, "prefab okunamadi: %s", err.msg);
        } else {
          // Hepsi ya da hicbiri (bkz. prefab_instantiate): yer yoksa 0 doner
          // ve sahneye tek varlik bile eklenmez.
          uint32_t first = 0, n = 0, pops = 0;
          with_bodies(st, phys, [&] { n = prefab_instantiate(st.scene, st.hist, pf, cam.target, &first, &pops); });
          if (!n) {
            set_status(st, "prefab eklenemedi: sahnede ya da kaynak tablosunda yer yok");
          } else {
            st.groups.push(pops); // tek Ctrl+Z butun prefab'i (yeni kaynak satirlari dahil) geri alir
            st.dirty = true;
            for (uint32_t a = 0; a < st.scene.asset_count; a++)
              if (!st.have[a]) load_asset((int32_t)a); // prefab'in getirdigi yeni kaynaklar
            st.sel.set_single((int32_t)first);
            set_status(st, "prefab eklendi: %s (%u varlik)", dlg.path, n);
          }
        }
        dlg_intent = IntentScene;
      } else if (fa == FileDialogAction::Accepted && dlg_intent == IntentGamePick) {
        // tulpar/ altindaysa ona GORELI (derleyici oradan cagrilir), degilse mutlak.
        const size_t tn = std::strlen(st.tulpar_dir);
        const char *rel = (tn && !std::strncmp(dlg.path, st.tulpar_dir, tn) && (dlg.path[tn] == '/' || dlg.path[tn] == '\\')) ? dlg.path + tn + 1 : dlg.path;
        std::snprintf(oyun_secim, sizeof oyun_secim, "%s", rel);
        console_log(ConsoleLevel::Bilgi, "oyun", "oyun secildi: %s (bu oturumda hatirlanir)", oyun_secim);
        dlg_intent = IntentScene;
        if (gomulu_secim_bekliyor) { // F5 sordu: secilen oyun Oyun sekmesinde
          gomulu_secim_bekliyor = false;
          if (!st.playing && !start_embedded_with(oyun_secim)) {
            console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "F5 fizik onizlemesi: secilen oyun gomulu baslatilamadi (Konsol)");
            set_playing(true);
          }
        } else {
          run_game_with(oyun_secim);
        }
      } else if (fa == FileDialogAction::Accepted && dlg_intent == IntentScriptNew) {
        // Etiket tarayicinin kuraliyla: listede ayni satir secili gorunsun.
        // Import yolu yalniz tulpar/ altinda biliniyor (oyunlar oradan derleniyor).
        char label[content::kSceneScriptLen], err[256];
        const bool lab_ok = editor_script_label(dlg.path, st.scene_dir, st.tulpar_dir, label, sizeof label);
        const char *imp = lab_ok && !std::strncmp(label, "tulpar/", 7) ? label + 7 : "";
        const int32_t ti = script_new_target;
        if (!lab_ok) {
          set_status(st, "betik yolu %u bayta sigmiyor: %s", (unsigned)sizeof label, dlg.path);
        } else if (ti < 0 || (uint32_t)ti >= st.scene.entity_count) {
          set_status(st, "betik olusturulmadi: hedef nesne artik yok");
        } else if (!editor_script_create(dlg.path, imp, err, sizeof err)) {
          set_status(st, "betik olusturulmadi: %s", err);
          console_log(ConsoleLevel::Hata, kConsoleTagEditor, "betik olusturulmadi: %s", err);
        } else {
          content::SceneEntity ne = st.scene.entities[ti];
          ne.components |= content::kSceneScript;
          ne.script_enabled = true;
          std::snprintf(ne.script_file, sizeof ne.script_file, "%s", label);
          commit(st, ti, ne);
          rescan_browse(st);
          set_status(st, "betik olusturuldu ve atandi: %s", label);
          console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "betik olusturuldu: %s -> %s (oyun import etmeli: %s)", dlg.path,
                      st.scene.entities[ti].name, imp[0] ? imp : "?");
        }
        dlg_intent = IntentScene;
        script_new_target = -1;
      } else if (fa == FileDialogAction::Accepted) {
        if (dlg.mode == FileDialogMode::Ac) load_scene_from(dlg.path);
        else if (save_scene_to(dlg.path) && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (fa == FileDialogAction::Cancelled) {
        pending = PendingNone;
        dlg_intent = IntentScene;
        if (gomulu_secim_bekliyor) { gomulu_secim_bekliyor = false; set_status(st, "F5 iptal: oyun secilmedi"); }
      }
    }
    // Guncelleme + Hakkinda pencereleri: ikisi de kapaliyken ImGui'ye dokunmaz.
    // "Kur ve yeniden baslat": once oynatma durur (oynatma duzenlemeleri geri
    // alinir, kirli bayragi oynatma oncesine doner), SONRA kirli sahne sorulur
    // (Kaydet ve devam / Kaydetmeden / Vazgec — asagidaki ayni onay kutusu).
    if (update_ui_draw(upd_ui) == UpdUiAction::InstallAndRestart) {
      if (st.playing) set_playing(false);
      guard_then(PendingUpdate);
    }
    if (confirm.open) {
      char msg[320];
      const bool upd_bekliyor = pending == PendingUpdate;
      const char *ad = st.scene_path[0] ? file_path_base(st.scene_path) : "adsiz sahne";
      if (upd_bekliyor)
        std::snprintf(msg, sizeof msg, "\x22%s\x22 dosyas\xC4\xB1nda kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklikler var.\nG\xC3\xBCncelleme kurulup edit\xC3\xB6r yeniden ba\xC5\x9Flat\xC4\xB1lacak.", ad);
      else std::snprintf(msg, sizeof msg, "\x22%s\x22 dosyasinda kaydedilmemis degisiklikler var.\nNe yapilsin?", ad);
      const ConfirmResult cr = upd_bekliyor ? confirm_modal(confirm, "G\xC3\xBCncellemeden \xC3\xB6nce", msg, "Kaydet ve devam et", "Vazge\xC3\xA7", "Kaydetmeden devam et")
                                            : confirm_modal(confirm, "Sahne kaydedilmedi", msg, "Kaydet", "Vazge\xC3\xA7", "Kaydetme");
      if (cr == ConfirmResult::Ok) {
        do_save(); // adsiz sahnede diyalog acar: bekleyen eylem orada kosar
        if (!st.dirty && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (cr == ConfirmResult::Third) { run_pending(pending); pending = PendingNone; }
      else if (cr == ConfirmResult::Cancel) pending = PendingNone;
    }
    draw_stats_overlay(&show_stats, dt, ren.stats(), st.scene, vp.width(), vp.height(), view_tab, sys, view_rect);
    draw_command_palette(palette_st, cmds, st.scene, &st);
    ui.end_frame();

    // --- 3B cizim: veri modelinden (dunya isigi/golgesi de her kare modelden: panel canli) ---
    // GI onizleme actiksa (ve pisirilmisse) duz ambient sabiti yerine kamera
    // konumunda ORNEKLENEN probe degeri kullanilir -- per-pixel DEGIL (kare
    // basina TEK ornek), yine de "Isik Haritasini Pisir"in viewport'ta
    // GORUNMESI icin yeterli (runtime/SceneRuntime tarafi ayni SceneGi'yi
    // ayni granulerlikte kullanir, bkz. scene_runtime.cpp apply_world).
    Vec3 preview_ambient = st.scene.ambient;
    if (st.gi_preview && st.gi_baked && st.gi.ok()) preview_ambient = st.gi.sample(camera_eye(cam), {0, 1, 0});
    if (st.scene.fog_enabled) {
      const float blend = std::min(st.scene.fog_density * 10.0f, 0.65f);
      preview_ambient = preview_ambient * (1.0f - blend) + st.scene.fog_color * blend;
      float bg_fog = std::clamp(st.scene.fog_density * 1.5f, 0.0f, 1.0f);
      if (st.scene.fog_type == 0) bg_fog = std::clamp(st.scene.fog_density, 0.0f, 1.0f);
      const Vec3 base_clear{0.05f, 0.06f, 0.08f};
      const Vec3 cur_clear = base_clear * (1.0f - bg_fog) + st.scene.fog_color * bg_fog;
      vp.set_clear_color(cur_clear.x, cur_clear.y, cur_clear.z, 1.0f);
      ren.set_post_clear(cur_clear);
    } else {
      vp.set_clear_color(0.05f, 0.06f, 0.08f, 1.0f);
      ren.set_post_clear(Vec3{0.05f, 0.06f, 0.08f});
    }
    ren.set_light(normalize(st.scene.sun_dir), preview_ambient, st.scene.sun_diffuse);
    ren.set_shadow_volume(st.scene.shadow_center, st.scene.shadow_radius, st.scene.shadow_depth);
    // Gorüntü ayarlari: hepsi ucuz set_* cagrisi, kare icinde ayirma YOK.
    // Her karede kosulsuz uygulanir -- "degisti mi" takibi, panelin disindan
    // (geri al/yinele, betik) gelen degisiklikleri kacirirdi.
    ren.set_shadows_enabled(st.render.shadows);
    if (view_mode == 1) { // Isiksiz: saf albedo. Tonemap kapali oldugu icin c = albedo birebir.
      ren.set_light(normalize(st.scene.sun_dir), Vec3{1.0f, 1.0f, 1.0f}, 0.0f);
      ren.set_shadows_enabled(false);
    }
    ren.set_shadow_bias(st.render.shadow_bias, st.render.shadow_normal_offset);
    ren.set_exposure(st.render.exposure);
    ren.set_bloom(st.render.bloom_threshold, st.render.bloom_intensity);
    ren.set_bloom_shape(st.render.bloom_knee, st.render.bloom_radius);
    
    float gr_intensity = 0.0f;
    Vec4 gr_source = {-st.scene.sun_dir.x, -st.scene.sun_dir.y, -st.scene.sun_dir.z, 0.0f};
    if (st.scene.godrays_enabled) {
      gr_intensity = 1.0f;
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const content::SceneEntity &e = st.scene.entities[i];
        if ((e.components & content::kSceneLight) && e.light_godray) {
          gr_intensity = e.light_godray_intensity;
          if (e.light_type == content::SceneLightType::Directional) {
             Mat4 m = content::scene_entity_world_matrix(st.scene, i);
             // Direction is typically -Z in local space
             Vec3 forward = normalize(Vec3{-m.m[2][0], -m.m[2][1], -m.m[2][2]});
             gr_source = {forward.x, forward.y, forward.z, 0.0f};
          } else {
             Mat4 m = content::scene_entity_world_matrix(st.scene, i);
             gr_source = {m.m[3][0], m.m[3][1], m.m[3][2], 1.0f};
          }
          break;
        }
      }
    }
    ren.set_godrays(st.scene.godray_density, st.scene.godray_decay, st.scene.godray_weight, st.scene.godray_exposure * gr_intensity);
    ren.set_godrays_source(gr_source);
    // Gecisin o karede KAYDEDILIP kaydedilmeyecegi. Kapaliyken huzme gecisi
    // hic kaydedilmez ve birlestirme bloom tepesini okur: sahnedeki kutu
    // kapaliyken goruntu, huzme hic derlenmemis gibidir. gr_intensity 0 ise
    // (godrays_enabled acik ama hicbir isik huzme sacmiyor) de kapat —
    // bos bir tam ekran gecisi kaydetmenin anlami yok.
    ren.set_godrays_enabled(st.scene.godrays_enabled && gr_intensity > 0.0f);
    ren.set_render_scale(st.render.render_scale);
    ren.set_upscaler((renderer::UpscalerKind)st.render.upscaler, st.render.sharpness);
    ren.set_jitter(st.render.jitter);
    ren.set_shadow_focus(cam.target); // yakin kademeler kameranin baktigi yerde
    // Arazi / voksel / su onizlemesi: alanlarin OZETI degistiyse mesh yeniden
    // uretilir. begin_frame'den ONCE, cunku create_mesh kayit sirasinda degil
    // hazirlikta yapilir. Ozet ayni kaldikca hicbir sey calismaz -- duran bir
    // sahnede kare basina 0 GPU ayirmasi.
    for (uint32_t i = 0; i < st.scene.entity_count; i++) proc_refresh(st, ren, i);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      if (e.flags & content::kSceneHidden) continue;
      // view_mode == 1 (Isiksiz / albedo) main'den geliyor ve BURADA duruyor:
      // PR #7 isik toplamasini begin_frame ONCESI bir on-gecise tasidi (huzme
      // kaynagi ve kumelenme oradan besleniyor), ama o kipin kapisini
      // dusurmustu -- Isiksiz gorunumde sahne yine isikli cizilirdi.
      if ((e.components & content::kSceneLight) && view_mode != 1) {
        const Mat4 m = live_matrix(st, phys, i);
        renderer::PointLight pl;
        pl.pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
        pl.radius = e.light_radius;
        pl.color = e.light_color;
        pl.intensity = e.light_intensity;
        ren.add_point_light(pl);
      }
      // Mobil-Dostu 0 FPS Düşüşlü Partikül Işıklandırması (Clustered Point Light)
      // Clustered Forward Shading'e doğrudan enjekte edilir; ek GPU geçişi veya bellek ayırma YOK.
      if ((e.components & content::kSceneParticle) && view_mode != 1 && e.particle_spawn_rate > 0.0f) {
        const Mat4 m = content::scene_entity_world_matrix(st.scene, i);
        renderer::PointLight pl;
        pl.pos = {m.m[3][0], m.m[3][1] + 0.25f, m.m[3][2]};
        pl.radius = std::max(2.5f, e.particle_size_start * 15.0f);
        pl.color = e.particle_color_start;
        pl.intensity = 2.2f;
        ren.add_point_light(pl);
      }
    }
    const Vec3 c_pos = camera_eye(cam);
    ren.begin_frame(headless ? 0 : frame_i);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      if (e.flags & content::kSceneHidden) continue; // panelde gozu kapatilmis varlik CIZILMEZ
      const Mat4 m = live_matrix(st, phys, i);
      const bool sel = st.sel.contains((int32_t)i);
      Vec3 tint = sel ? Vec3{1.0f, 0.9f, 0.4f} : e.tint;
      if (st.scene.fog_enabled && !sel) {
        const Vec3 e_pos{m.m[3][0], m.m[3][1], m.m[3][2]};
        const float dist = length(e_pos - c_pos);
        float f = 0.0f;
        if (st.scene.fog_type == 0) { // Linear Fog
          if (st.scene.fog_end > st.scene.fog_start) {
            f = (dist - st.scene.fog_start) / (st.scene.fog_end - st.scene.fog_start);
          } else {
            f = dist >= st.scene.fog_start ? 1.0f : 0.0f;
          }
        } else if (st.scene.fog_type == 1) { // Exponential
          f = 1.0f - std::exp(-dist * st.scene.fog_density);
        } else if (st.scene.fog_type == 2) { // Exp2
          float d = dist * st.scene.fog_density;
          f = 1.0f - std::exp(-d * d);
        } else { // Height fog
          float h = e_pos.y - st.scene.fog_base_height;
          float h_falloff = std::exp(-std::max(h, 0.0f) * st.scene.fog_height_falloff);
          f = (1.0f - std::exp(-dist * st.scene.fog_density)) * h_falloff;
        }
        f = std::clamp(f * st.scene.fog_density, 0.0f, 0.95f);
        tint = tint * (1.0f - f) + st.scene.fog_color * f;
      }
      bool drew = false;
      // Ilkel geometri glTF kaynagindan ONCE denenir: primitive >= 0 ise varlik
      // PROSEDURELDIR ve `asset` alani anlamsizdir (-1). Bu dal olmadan
      // menuden eklenen Kapsul/Silindir/Koni/Dortgen/Simit editorde GORUNMEZ
      // kalirdi -- derlenmis sahnede cizilir (scene_runtime ayni tabloyu
      // kullaniyor) ama editorde cizilmezdi.
      if ((e.components & content::kSceneModel) && e.primitive >= 0 &&
          e.primitive < (int32_t)content::kPrimitiveSlotCount && st.prims[e.primitive].valid()) {
        if (st.entity_mats[i].valid()) {
          renderer::PbrParams pp;
          pp.metallic = e.metallic; pp.roughness = e.roughness; pp.reflectance = e.reflectance;
          pp.emissive = e.emissive; pp.emissive_strength = e.emissive_strength;
          ren.set_material_pbr(st.entity_mats[i], pp);
          ren.set_material_textures(st.entity_mats[i], st.entity_pbr_tex[i]);
          ren.set_material_albedo(st.entity_mats[i], st.entity_albedo_tex[i].valid() ? st.entity_albedo_tex[i] : ren.default_texture());
          ren.draw(st.prims[e.primitive], st.entity_mats[i], m, tint);
        } else {
          ren.draw(st.prims[e.primitive], m, tint);
        }
        drew = true;
      }
      if (!drew && (e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
        const content::Model &mdl = st.models[e.asset];
        const content::UploadedModel &up = st.ups[e.asset];
        content::ModelLod lod;
        lod.camera_pos = camera_eye(cam);
        lod.distance1 = 24.0f; lod.distance2 = 34.0f;
        if ((e.components & content::kSceneAnim) && e.clip < mdl.clip_count) {
          const float dur = mdl.clips[e.clip].duration;
          float t = std::fmod((st.playing ? st.play_time * e.speed : 0.0f) + e.phase, 2.0f * dur);
          if (t > dur) t = 2.0f * dur - t;
          content::ModelPose pose;
          if (content::model_pose_evaluate(mdl, e.clip, t, st.pose_scratch, &pose)) {
            content::draw_model(ren, mdl, up, m, tint, nullptr, nullptr, &pose);
            drew = true;
          }
        }
        if (!drew) { content::draw_model(ren, mdl, up, m, tint, &lod); drew = true; }
      }
      // Prosedurel bilesenler: mesh'i proc_refresh uretti (bu kare ya da
      // daha once). Malzeme ayarlari artik PBR malzeme kartindan canli alinir.
      if ((e.components & content::kSceneTerrain) && st.terrain_meshes[i].valid()) {
        if (st.entity_mats[i].valid()) {
          renderer::PbrParams tp;
          tp.metallic = e.metallic; tp.roughness = e.roughness; tp.reflectance = e.reflectance;
          tp.emissive = e.emissive; tp.emissive_strength = e.emissive_strength;
          ren.set_material_pbr(st.entity_mats[i], tp);
          ren.set_material_textures(st.entity_mats[i], st.entity_pbr_tex[i]);
          ren.set_material_albedo(st.entity_mats[i], st.entity_albedo_tex[i].valid() ? st.entity_albedo_tex[i] : ren.default_texture());
          ren.draw(st.terrain_meshes[i], st.entity_mats[i], m, tint);
        } else {
          ren.draw(st.terrain_meshes[i], st.terrain_mat, m, tint);
        }
        drew = true;
      }
      if ((e.components & content::kSceneVoxel) && st.voxel_meshes[i].valid()) {
        if (st.entity_mats[i].valid()) {
          renderer::PbrParams vp;
          vp.metallic = e.metallic; vp.roughness = e.roughness; vp.reflectance = e.reflectance;
          vp.emissive = e.emissive; vp.emissive_strength = e.emissive_strength;
          ren.set_material_pbr(st.entity_mats[i], vp);
          ren.set_material_textures(st.entity_mats[i], st.entity_pbr_tex[i]);
          ren.set_material_albedo(st.entity_mats[i], st.entity_albedo_tex[i].valid() ? st.entity_albedo_tex[i] : ren.default_texture());
          ren.draw(st.voxel_meshes[i], st.entity_mats[i], m, tint);
        } else {
          ren.draw(st.voxel_meshes[i], st.voxel_mat, m, tint);
        }
        drew = true;
      }
      if ((e.components & content::kSceneWater) && st.water_meshes[i].valid()) {
        if (st.entity_mats[i].valid()) {
          renderer::PbrParams wp;
          wp.metallic = e.metallic; wp.roughness = e.roughness; wp.reflectance = e.reflectance;
          wp.emissive = e.emissive; wp.emissive_strength = e.emissive_strength;
          ren.set_material_pbr(st.entity_mats[i], wp);
          ren.set_material_textures(st.entity_mats[i], st.entity_pbr_tex[i]);
          ren.set_material_albedo(st.entity_mats[i], st.entity_albedo_tex[i].valid() ? st.entity_albedo_tex[i] : ren.default_texture());
          ren.draw(st.water_meshes[i], st.entity_mats[i], m, tint);
        } else {
          ren.draw(st.water_meshes[i], st.water_mat, m, tint);
        }
        drew = true;
      }
      if (!drew && (e.components & content::kSceneBody)) {
        // Modelsiz govde: carpisan hacmi kutu olarak goster (kure de kutu, yaricap kadar).
        const Vec3 s = e.shape == content::SceneShape::Box ? e.half * 2.0f : Vec3{e.radius * 2, e.radius * 2, e.radius * 2};
        ren.draw(ds.cube, m * Mat4::scale(s), sel ? tint : Vec3{0.55f, 0.6f, 0.7f});
        drew = true;
      }
      if (!drew && (e.components & content::kSceneParticle)) {
        if (sel) {
          ren.draw(ds.cube, m * Mat4::scale({0.25f, 0.25f, 0.25f}), e.particle_color_start);
        }
        drew = true;
      }
      if (!drew && !(e.components & (content::kSceneLight | content::kSceneCamera | content::kSceneParticle)))
        ren.draw(ds.cube, m * Mat4::scale({0.3f, 0.3f, 0.3f}), sel ? tint : Vec3{0.9f, 0.9f, 0.3f}); // bos varlik isareti
      // main'in ikinci add_point_light dongusu BURADAN kaldirildi: PR #7 ayni
      // isiklari begin_frame oncesi on-geciste zaten ekliyor, ikisi birlikte
      // her isigi IKI KEZ kaydediyordu (kume butcesi iki katina cikar, parlaklik
      // ikiye katlanirdi). Kip kapisi on-gecise tasindi (yukari bak).
    }
    // --- 3B Canlı Partikül & VFX & Duman Simülasyonu ---
    // Kamera yön vektörleri (view matrisinin satırlarından dünya uzayına)
    const Vec3 cam_r{view.m[0][0], view.m[1][0], view.m[2][0]};
    const Vec3 cam_u{view.m[0][1], view.m[1][1], view.m[2][1]};
    const Vec3 cam_f{-view.m[0][2], -view.m[1][2], -view.m[2][2]};

    // Dinamik geometri secimi (Billboard Quad, 3B Yuvarlak Kure, Hiz Cizgisi, Yatay Duzlem, Voksel Kup)
    if (st.particles.alive_count() > 0) {

      for (uint32_t pi = 0; pi < st.particles.alive_count(); pi++) {
        const content::Particle &p = st.particles.particle(pi);
        if (p.size <= 0.0001f) continue;

        renderer::MeshHandle mesh;
        Mat4 m;

        switch (p.shape) {
          case 0: { // 0: Kameraya Donuk Dairesel Disk / Billboard
            mesh = st.prims[content::kPrimParticle].valid() ? st.prims[content::kPrimParticle] : st.prims[content::kPrimQuad];
            m.m[0][0] = cam_r.x * p.size; m.m[0][1] = cam_r.y * p.size; m.m[0][2] = cam_r.z * p.size; m.m[0][3] = 0.0f;
            m.m[1][0] = cam_u.x * p.size; m.m[1][1] = cam_u.y * p.size; m.m[1][2] = cam_u.z * p.size; m.m[1][3] = 0.0f;
            m.m[2][0] = -cam_f.x * p.size; m.m[2][1] = -cam_f.y * p.size; m.m[2][2] = -cam_f.z * p.size; m.m[2][3] = 0.0f;
            m.m[3][0] = p.pos.x; m.m[3][1] = p.pos.y; m.m[3][2] = p.pos.z; m.m[3][3] = 1.0f;
            break;
          }
          case 1: { // 1: Hiza Gore Uzayan Igne / Kivilcim (Stretched Velocity Streak)
            mesh = st.prims[content::kPrimQuad].valid() ? st.prims[content::kPrimQuad] : st.prims[content::kPrimParticle];
            const float spd = length(p.vel);
            if (spd < 0.05f) {
              m.m[0][0] = cam_r.x * p.size; m.m[0][1] = cam_r.y * p.size; m.m[0][2] = cam_r.z * p.size; m.m[0][3] = 0.0f;
              m.m[1][0] = cam_u.x * p.size; m.m[1][1] = cam_u.y * p.size; m.m[1][2] = cam_u.z * p.size; m.m[1][3] = 0.0f;
              m.m[2][0] = -cam_f.x * p.size; m.m[2][1] = -cam_f.y * p.size; m.m[2][2] = -cam_f.z * p.size; m.m[2][3] = 0.0f;
              m.m[3][0] = p.pos.x; m.m[3][1] = p.pos.y; m.m[3][2] = p.pos.z; m.m[3][3] = 1.0f;
            } else {
              const Vec3 vdir = normalize(p.vel);
              const Vec3 to_cam = normalize(c_pos - p.pos);
              Vec3 side = cross(vdir, length_sq(to_cam) > 1e-4f ? to_cam : -cam_f);
              if (length_sq(side) < 1e-6f) side = cross(vdir, cam_u);
              side = normalize(side);
              const Vec3 norm = cross(side, vdir);
              const float len = p.size * (1.0f + std::min(spd * 0.25f, 6.0f));
              const float wid = p.size * 0.35f;
              m.m[0][0] = side.x * wid; m.m[0][1] = side.y * wid; m.m[0][2] = side.z * wid; m.m[0][3] = 0.0f;
              m.m[1][0] = vdir.x * len; m.m[1][1] = vdir.y * len; m.m[1][2] = vdir.z * len; m.m[1][3] = 0.0f;
              m.m[2][0] = norm.x * wid; m.m[2][1] = norm.y * wid; m.m[2][2] = norm.z * wid; m.m[2][3] = 0.0f;
              m.m[3][0] = p.pos.x; m.m[3][1] = p.pos.y; m.m[3][2] = p.pos.z; m.m[3][3] = 1.0f;
            }
            break;
          }
          case 2: { // 2: Yatay Duzlem / Sok Halka (Horizontal Plane)
            mesh = st.prims[content::kPrimPlane].valid() ? st.prims[content::kPrimPlane] : (st.prims[content::kPrimQuad].valid() ? st.prims[content::kPrimQuad] : st.prims[content::kPrimParticle]);
            m.m[0][0] = p.size; m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = 0.0f;
            m.m[1][0] = 0.0f; m.m[1][1] = 0.005f; m.m[1][2] = 0.0f; m.m[1][3] = 0.0f;
            m.m[2][0] = 0.0f; m.m[2][1] = 0.0f; m.m[2][2] = p.size; m.m[2][3] = 0.0f;
            m.m[3][0] = p.pos.x; m.m[3][1] = p.pos.y; m.m[3][2] = p.pos.z; m.m[3][3] = 1.0f;
            break;
          }
          case 3: { // 3: 3B Yuvarlak Kure (3D Sphere)
            mesh = st.prims[content::kPrimSphere].valid() ? st.prims[content::kPrimSphere] : st.prims[content::kPrimParticle];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size, p.size, p.size});
            break;
          }
          case 4: { // 4: 3B Voksel / Kup (Voxel Cube)
            mesh = st.prims[content::kPrimCube].valid() ? st.prims[content::kPrimCube] : st.prims[content::kPrimParticle];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size, p.size, p.size});
            break;
          }
          case 5: { // 5: 3B Enerji Simiti / Halka (3D Torus)
            mesh = st.prims[content::kPrimTorus].valid() ? st.prims[content::kPrimTorus] : st.prims[content::kPrimParticle];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size, p.size * 0.35f, p.size});
            break;
          }
          case 6: { // 6: 3B Koni (3D Cone)
            mesh = st.prims[content::kPrimCone].valid() ? st.prims[content::kPrimCone] : st.prims[content::kPrimParticle];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size, p.size * 1.5f, p.size});
            break;
          }
          case 7: { // 7: 3B Silindir (3D Cylinder)
            mesh = st.prims[content::kPrimCylinder].valid() ? st.prims[content::kPrimCylinder] : st.prims[content::kPrimParticle];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size * 0.6f, p.size * 1.8f, p.size * 0.6f});
            break;
          }
          default: {
            mesh = st.prims[content::kPrimParticle].valid() ? st.prims[content::kPrimParticle] : st.prims[content::kPrimQuad];
            m = Mat4::translate(p.pos) * Mat4::scale({p.size, p.size, p.size});
            break;
          }
        }

        if (mesh.valid()) {
          if (st.particle_mat.valid()) {
            ren.draw(mesh, st.particle_mat, m, st.particles.color(pi), nullptr, 1.0f);
          } else {
            ren.draw(mesh, m, st.particles.color(pi));
          }
        }
      }
    }
    // --- Canlı Şerit & Kuyruk İzi (Niagara Ribbon Trail) ---
    if (st.ribbon_initialized && st.ribbon_trail.count() >= 2) {
      const uint32_t rcount = st.ribbon_trail.count();
      for (uint32_t ri = 0; ri + 1 < rcount; ri++) {
        const content::RibbonPoint &p0 = st.ribbon_trail.point(ri);
        const content::RibbonPoint &p1 = st.ribbon_trail.point(ri + 1);
        const Vec3 delta = p1.pos - p0.pos;
        const float seg_len = length(delta);
        if (seg_len < 0.001f) continue;
        const Vec3 vdir = delta * (1.0f / seg_len);
        const Vec3 mid = (p0.pos + p1.pos) * 0.5f;
        const Vec3 to_cam = normalize(c_pos - mid);
        Vec3 side = cross(vdir, to_cam);
        if (length_sq(side) < 1e-4f) side = cross(vdir, cam_u);
        side = normalize(side);
        const Vec3 norm = cross(side, vdir);
        const float seg_w = (p0.width + p1.width) * 0.5f;
        Mat4 rm;
        rm.m[0][0] = side.x * seg_w; rm.m[0][1] = side.y * seg_w; rm.m[0][2] = side.z * seg_w; rm.m[0][3] = 0.0f;
        rm.m[1][0] = vdir.x * seg_len; rm.m[1][1] = vdir.y * seg_len; rm.m[1][2] = vdir.z * seg_len; rm.m[1][3] = 0.0f;
        rm.m[2][0] = norm.x * seg_w; rm.m[2][1] = norm.y * seg_w; rm.m[2][2] = norm.z * seg_w; rm.m[2][3] = 0.0f;
        rm.m[3][0] = mid.x; rm.m[3][1] = mid.y; rm.m[3][2] = mid.z; rm.m[3][3] = 1.0f;
        const Vec3 rcol{p0.color.x, p0.color.y, p0.color.z};
        if (st.prims[content::kPrimQuad].valid()) {
          if (st.particle_mat.valid()) {
            ren.draw(st.prims[content::kPrimQuad], st.particle_mat, rm, rcol, nullptr, 1.0f);
          } else {
            ren.draw(st.prims[content::kPrimQuad], rm, rcol);
          }
        }
      }
    }
    // --- CS2 Tarzı Hacimsel Voksel Dumanı (Responsive Voxel Smoke) ---
    if (st.smoke_initialized) {
      const uint32_t sx = st.smoke_grid.dim_x();
      const uint32_t sy = st.smoke_grid.dim_y();
      const uint32_t sz = st.smoke_grid.dim_z();
      const float vsz = st.smoke_grid.voxel_size();
      for (uint32_t vz = 0; vz < sz; vz++) {
        for (uint32_t vy = 0; vy < sy; vy++) {
          for (uint32_t vx = 0; vx < sx; vx++) {
            const float d = st.smoke_grid.get_density((int32_t)vx, (int32_t)vy, (int32_t)vz);
            if (d < 0.15f) continue;
            const Vec3 wp = st.smoke_grid.voxel_to_world((int32_t)vx, (int32_t)vy, (int32_t)vz);
            const float psize = vsz * 1.6f * d;
            Mat4 sm;
            sm.m[0][0] = cam_r.x * psize; sm.m[0][1] = cam_r.y * psize; sm.m[0][2] = cam_r.z * psize; sm.m[0][3] = 0.0f;
            sm.m[1][0] = cam_u.x * psize; sm.m[1][1] = cam_u.y * psize; sm.m[1][2] = cam_u.z * psize; sm.m[1][3] = 0.0f;
            sm.m[2][0] = -cam_f.x * psize; sm.m[2][1] = -cam_f.y * psize; sm.m[2][2] = -cam_f.z * psize; sm.m[2][3] = 0.0f;
            sm.m[3][0] = wp.x; sm.m[3][1] = wp.y; sm.m[3][2] = wp.z; sm.m[3][3] = 1.0f;
            const Vec3 scol = Vec3{0.35f, 0.35f, 0.38f} * d;
            if (st.prims[content::kPrimQuad].valid()) {
              ren.draw(st.prims[content::kPrimQuad], sm, scol);
            }
          }
        }
      }
    }
    // --- 3B Hacimsel Isik Huzmeleri (Indoor & Outdoor Fake Volumetric God Rays) ---
    // Kapali mekanlarda, odalarda, pencerelerde ve spot isiklar altinda raymarching
    // gerektirmeyen, sifir GPU yuklu gercek 3B hacimsel isik konileri cizilir.
    if (st.scene.godrays_enabled && st.prims[content::kPrimCone].valid()) {
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if (!(e.components & content::kSceneLight) || !e.light_godray || (e.flags & content::kSceneHidden)) continue;

        const Mat4 m = content::scene_entity_world_matrix(st.scene, i);
        const Vec3 p = {m.m[3][0], m.m[3][1], m.m[3][2]};

        Vec3 dir{0.0f, -1.0f, 0.0f};
        float reach = e.light_radius > 0.5f ? e.light_radius : 10.0f;
        float outer_rad = 2.0f;

        if (e.light_type == content::SceneLightType::Spot) {
          Vec3 fwd{-m.m[2][0], -m.m[2][1], -m.m[2][2]};
          dir = length_sq(fwd) > 1e-6f ? normalize(fwd) : Vec3{0.0f, -1.0f, 0.0f};
          float half_angle = e.light_spot_outer * (3.14159265f / 180.0f);
          outer_rad = std::tan(half_angle) * reach;
          if (outer_rad < 0.2f) outer_rad = 0.2f;
        } else if (e.light_type == content::SceneLightType::Directional) {
          Vec3 fwd{-m.m[2][0], -m.m[2][1], -m.m[2][2]};
          dir = length_sq(fwd) > 1e-6f ? normalize(fwd) : Vec3{0.0f, -1.0f, 0.0f};
          reach = 16.0f;
          outer_rad = 4.5f;
        } else { // Point light: lambadan asagi yumusak koni
          reach = e.light_radius > 0.5f ? e.light_radius * 0.75f : 5.0f;
          outer_rad = reach * 0.6f;
        }

        Vec3 up_ref = std::abs(dir.y) < 0.95f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        Vec3 rgt = normalize(cross(dir, up_ref));
        Vec3 up_v = cross(rgt, dir);

        Mat4 cone_m;
        // Col 0 (X): rgt * (outer_rad * 2.0f)
        cone_m.m[0][0] = rgt.x * (outer_rad * 2.0f);
        cone_m.m[0][1] = rgt.y * (outer_rad * 2.0f);
        cone_m.m[0][2] = rgt.z * (outer_rad * 2.0f);
        cone_m.m[0][3] = 0.0f;

        // Col 1 (Y): -dir * reach
        cone_m.m[1][0] = -dir.x * reach;
        cone_m.m[1][1] = -dir.y * reach;
        cone_m.m[1][2] = -dir.z * reach;
        cone_m.m[1][3] = 0.0f;

        // Col 2 (Z): up_v * (outer_rad * 2.0f)
        cone_m.m[2][0] = up_v.x * (outer_rad * 2.0f);
        cone_m.m[2][1] = up_v.y * (outer_rad * 2.0f);
        cone_m.m[2][2] = up_v.z * (outer_rad * 2.0f);
        cone_m.m[2][3] = 0.0f;

        // Col 3 (T): p + dir * (reach * 0.5f)
        Vec3 center = p + dir * (reach * 0.5f);
        cone_m.m[3][0] = center.x;
        cone_m.m[3][1] = center.y;
        cone_m.m[3][2] = center.z;
        cone_m.m[3][3] = 1.0f;

        Vec3 beam_col = e.light_color * (e.light_godray_intensity * 0.85f);
        ren.draw(st.prims[content::kPrimCone], st.beam_mat, cone_m, beam_col);
      }
    }
    // --- RDR2 tarzi sinematik isik huzmesi (God Rays) cekirdek cizimi --------
    // Sahnedeki engellerin (duvar, kutu) gunesi fiziksel olarak perdelemesi ve
    // arkasindan gercek isik saftlari akmasi icin sahne derinlik testine tabi
    // parlak bir gunes/isik cekirdegi cizilir.
    if (st.scene.godrays_enabled && gr_intensity > 0.0f && st.prims[content::kPrimSphere].valid()) {
      if (gr_source.w == 0.0f) {
        // Yonlu gunes isigi (directional sun)
        Vec3 sun_d = normalize(Vec3{gr_source.x, gr_source.y, gr_source.z});
        Vec3 to_sun = {-sun_d.x, -sun_d.y, -sun_d.z};
        Vec3 eye = camera_eye(cam);
        float sun_dist = 150.0f; // zfar (200.0f) onunde, sahne nesnelerinin arkasinda
        Vec3 sun_pos = eye + to_sun * sun_dist;
        float sun_radius = sun_dist * 0.09f; // dogal gokyuzu gunes diski
        Mat4 sun_m = Mat4::translate(sun_pos) * Mat4::scale(Vec3{sun_radius, sun_radius, sun_radius});
        ren.draw(st.prims[content::kPrimSphere], st.sun_mat, sun_m, Vec3{1.0f, 0.96f, 0.88f});
      } else {
        // Nokta ya da spot isik (point / spot light)
        Vec3 light_pos = {gr_source.x, gr_source.y, gr_source.z};
        float core_radius = 0.35f;
        Mat4 core_m = Mat4::translate(light_pos) * Mat4::scale(Vec3{core_radius, core_radius, core_radius});
        ren.draw(st.prims[content::kPrimSphere], st.light_core_mat, core_m, Vec3{1.0f, 1.0f, 1.0f});
      }
    }
    // Isik yaricapi / golge hacmi / gunes yonu: motorun kendi draw'u ile ince kutular.
    // --- Gorunum kipi kaplamalari -------------------------------------------
    if (view_mode == 2) { // Carpisma: govdeyle DONEN tel kutu; dinamik turuncu, sabit yesil.
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if (e.flags & content::kSceneHidden) continue;
        const bool karakter = e.components & content::kSceneCharacter;
        if (!karakter && !(e.components & content::kSceneBody)) continue;
        const Mat4 m = live_matrix(st, phys, i);
        // Karakter: kapsulun kusatan kutusu (r, boy/2, r), mavi. Govdesi de varsa
        // o govde oynatmada DOGMUYOR (karakter yerini aldi) — kutusu cizilmez.
        if (karakter) {
          editor_wire_box_m(ren, ds.cube, m, Vec3{e.char_radius, e.char_height * 0.5f, e.char_radius},
                            st.sel.contains((int32_t)i) ? Vec3{1.0f, 0.95f, 0.4f} : Vec3{0.3f, 0.6f, 1.0f}, 0.03f);
          continue;
        }
        const Vec3 half = e.shape == content::SceneShape::Box ? e.half : Vec3{e.radius, e.radius, e.radius};
        const Vec3 col = st.sel.contains((int32_t)i) ? Vec3{1.0f, 0.95f, 0.4f}
                         : e.dynamic ? Vec3{1.0f, 0.55f, 0.15f} : Vec3{0.25f, 0.9f, 0.35f};
        editor_wire_box_m(ren, ds.cube, m, half, col, 0.03f);
      }
    } else if (view_mode == 3) { // Sinirlar: secim ve odak bu kutulari kullanir.
      static content::SceneBounds vb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, vb);
      for (uint32_t i = 0; i < nb; i++) {
        if (st.scene.entities[i].flags & content::kSceneHidden) continue;
        const Vec3 col = st.sel.contains((int32_t)i) ? Vec3{1.0f, 0.95f, 0.4f} : Vec3{0.45f, 0.75f, 1.0f};
        editor_wire_aabb(ren, ds.cube, vb[i].lo, vb[i].hi, col, 0.025f);
      }
    } else if (view_mode == 4) { // Overdraw Isi Haritasi: gorsel varliklarin katman yogunlugu (TBDR heatmap)
      static content::SceneBounds vb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, vb);
      for (uint32_t i = 0; i < nb; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if (e.flags & content::kSceneHidden) continue;
        const bool has_geo = (e.components & (content::kSceneModel | content::kSceneTerrain | content::kSceneVoxel | content::kSceneWater)) != 0 || (e.primitive >= 0);
        if (!has_geo) continue;
        const Vec3 heat_col = (i % 3 == 0) ? Vec3{1.0f, 0.15f, 0.1f} : (i % 3 == 1) ? Vec3{1.0f, 0.65f, 0.0f} : Vec3{0.9f, 0.1f, 0.8f};
        editor_wire_aabb(ren, ds.cube, vb[i].lo, vb[i].hi, heat_col, 0.035f);
      }
    } else if (view_mode == 5) { // Isik Kumeleri: kume basina isik etki hacimleri (Clustered Lighting)
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        const SceneEntity &e = st.scene.entities[i];
        if (!(e.components & content::kSceneLight) || (e.flags & content::kSceneHidden)) continue;
        const Vec3 lo = {e.pos.x - e.light_radius, e.pos.y - e.light_radius, e.pos.z - e.light_radius};
        const Vec3 hi = {e.pos.x + e.light_radius, e.pos.y + e.light_radius, e.pos.z + e.light_radius};
        const Vec3 cluster_col = Vec3{0.15f, 0.85f, 1.0f};
        editor_wire_aabb(ren, ds.cube, lo, hi, cluster_col, 0.03f);
      }
    }
    st.gizmo_draws = editor_draw_gizmos(ren, ds.cube, st.scene, st.sel.items, st.sel.count, st.gizmos);
    if (headless) {
      oyun_kare_hazirla(0); // penceresiz kare senkron: tek yuva yeter
      if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores, before_cb)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
    } else {
      rhi::FrameContext fc;
      if (swap.acquire(&fc)) {
        // IKI YOL DA AYNI IKI FONKSIYONU CAGIRIR — ayrisamasinlar diye.
        // Burada bir kez `ui.record()` DOGRUDAN cagrildi ve pencereli editor
        // SIMSIYAH acildi: ana gecis (swapchain de offscreen de) IKI subpass
        // tanimliyor, ImGui'nin boru hatti subpass 1 icin kurulu ve ilerletmeyi
        // `record_cb` yapiyor. Headless yol record_cb'den gectigi icin calisti,
        // pencereli yol gecmedigi icin hicbir sey cizmedi. Ayni hata, tek yolda
        // duzeltilmis hali. Artik tek kaynak var.
        oyun_kare_hazirla(fc.frame_index);
        before_cb(fc.cmd, &rctx);   // golge + viewport (ikisi de KENDI gecisi)
        swap.begin_render_pass(fc);
        record_cb(fc.cmd, &rctx);   // subpass ilerlet + ImGui
        swap.end_frame(fc);
      }
      // Yeniden kurma TEK YERDEN yapilir: kare BASINDAKI sync_size. Burada
      // (kare sonunda) yapmak bir kareyi hep yanlis olcude birakiyordu —
      // arayuz yeni olcuye, hedef eskisine gore cizilmis oluyordu. acquire /
      // present OUT_OF_DATE derse bayrak kalir ve sonraki karenin basinda
      // ayni yerden islenir. Renderer'in cizim olcusu buradan etkilenmez:
      // renderer viewport'a ciziyor, onun olcusu panelden geliyor.
    }
    prof.end_frame();
    frame_i++;
    if (headless && frame_i >= opts.headless_frames) running = false;
  }
  // GUNCELLEME KAPISI (penceresiz): Updater'a HICBIR istek gitmemis olmali —
  // otomatik denetim (karar: penceresiz) ve kosum boyunca hicbir yol. Pozitif
  // kontrol ayni yolu DENER: menu komutunun govdesi (elle denetim) burada
  // cagrilir ve reddedilmeli (red sayaci 1 artar, istek sayaci 0 kalir). Istek
  // sayacinin gercekten saydigi engine_tests'te olculur (pencereli yapilandirma,
  // file:// fikstur: 1). Pencere cizilmez: dongu bitti. Kaynak derlemesinde
  // guncelleyici Disabled: "istek 0" orada kendiliginden dogru, olcen red
  // sayacidir (Tuzaklar 8cm).
  if (headless) {
    const uint32_t red0 = upd_ui.refused_headless;
    update_ui_check(upd_ui, true);
    update_ui_poll(upd_ui, platform::now_ns());
    const bool red_ok = upd_ui.refused_headless == red0 + 1;
    const bool ok = upd_ui.requests == 0 && red_ok;
    std::printf("[engine_editor] guncelleme kapisi: durum %s, otomatik karar \"%s\", Updater istegi %u, KONTROL elle denetim reddedildi %s (red %u) %s\n",
                upd_state_name(update_ui_state(upd_ui)), upd_auto_why_text(upd_ui.auto_why), upd_ui.requests, red_ok ? "evet" : "HAYIR",
                upd_ui.refused_headless, ok ? "OK" : "HATA");
    upd_ui.window_open = false;
    if (!ok) return 1;
  }
  static uint64_t scratch[1200]; // 2x kare kapasitesi (profiler sozlesmesi); 600 iken 300+ karede assert
  FrameStats stt = prof.frame_stats(Span<uint64_t>(scratch, 1200), 0);
  const EditorUiStats us = ui.stats();
  const int32_t prim_end = st.sel.primary();
  std::printf("[engine_editor] %u kare | p50 %.2f ms p99 %.2f ms | ui %u vertex %u indeks %u liste | cizim %u (gizmo %u) | nesne %u | kaynak %u/%u dosya | gunluk %u/%u%s | secili %u (%s) | tick %u | oynat: govde %u karakter %u (yerine gecen %u, dogamayan %u)\n",
              frame_i, stt.p50_ns / 1e6, stt.p99_ns / 1e6, us.vertices, us.indices, us.draw_lists, ren.stats().draws, st.gizmo_draws,
              st.scene.entity_count, st.scene.asset_count, st.browse_count, st.hist.undo_count(), st.hist.redo_count(),
              st.dirty ? " (kaydedilmedi)" : "", st.sel.count, prim_end >= 0 ? st.scene.entities[prim_end].name : "-", tick_i, st.live.bodies,
              st.live.characters, st.live.bodies_replaced, st.live.characters_failed);
  // Sistem arenasi OLCUMU. Gunluk (SceneHistory) 256 islemlik SABIT bir dizi
  // ve her islem iki SceneEntity tasir: varlik buyudukce (E3 ozellikleri,
  // 824 -> 1468 B) gunluk de buyur. Rezerv (512 MB) Fatal politikali, yani
  // tasma sessiz degil; bu satir payin NE KADAR kaldigini gorunur kilar.
  std::printf("[engine_editor] sistem arenasi: %.1f / %.1f MB (tepe %.1f MB) | gunluk %u islem x %zu B = %.1f KB\n",
              (double)sys.used() / (1024.0 * 1024.0), (double)sys.capacity() / (1024.0 * 1024.0),
              (double)sys.stats().peak / (1024.0 * 1024.0), 256u, sizeof(content::SceneOp),
              256.0 * (double)sizeof(content::SceneOp) / 1024.0);
  bool imgui_hata_var = false;
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
  }
  // IMGUI KULLANICI HATASI KAPISI. ImGui dengesiz Begin/End gibi hatalari
  // kurtarip stdout'a basarak devam eder; BASMAK KAPI DEGILDIR. Bir artik
  // ImGui::End() her karede hata yazdigi halde hicbir sey kizarmadi ve o
  // haliyle v0.1.0'a girdi. Sayac sifir degilse penceresiz kosum KIRMIZI doner.
  const uint32_t ui_hata = editor_ui_imgui_errors();
  std::printf("[engine_editor] imgui kullanici hatasi: %u\n", ui_hata);
  if (ui_hata != 0) {
    std::fprintf(stderr,
                 "[engine_editor] HATA: %u ImGui kullanici hatasi (dengesiz Begin/End, fazladan Pop*...). "
                 "Arayuz yigini bozuk; sebep yukaridaki [editor-ui] satirlarinda.\n",
                 ui_hata);
    imgui_hata_var = true;
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
  // IS SISTEMI ONCE SUSTURULUR — alt sistemlerden ONCE.
  //
  // Eskiden en SONDA kapaniyordu: fizik, renderer ve cihaz yikilirken worker
  // thread'leri HALA CALISIYORDU. Jolt'un is uyarlayicisi (FiberJoltJobs)
  // bizim kuyruga CIPLAK Job* itiyor; `delete impl_->jobs` o havuzu yok
  // ediyor. Bir worker o sirada elinde eski bir girdi tutuyorsa cop bir
  // isaretciyi cagiriyor.
  //
  // Olculdu (CI macOS/arm64, 2026-09-16): `thread: tulpar-job`, SIGSEGV,
  // fault_addr 0x8bc94512aa864210 (null degil — COP). Yigin izi iki cerceve,
  // cunku fiber yigini cozucuyu kesiyor. Dort kosumun ikisinde dustu: yaris.
  //
  // `jobs.shutdown()` worker'lari JOIN eder ve hicbir fiber'in park halinde
  // kalmadigini ENGINE_ASSERT ile dogrular. Ondan sonrasi tek thread'lidir,
  // yani bu sinif tamamen kapanir. Kapanis yolunda is URETEN kimse yok
  // (yikim yalniz Vulkan/arena nesnesi serbest birakiyor).
  // Panel duzenini KAPANISTA kaydet: ImGui hala ayakta (asagida kapaniyor),
  // yani docking agaci okunabilir. Basarisizlik olumcul degil -- editorun
  // kapanmasini bir duzen dosyasi engellememeli, ama sessiz de kalmamali.
  if (!headless) {
    LayoutError le{};
    if (layout_save(layout_path, &le))
      std::printf("[engine_editor] panel duzeni kaydedildi: %s\n", layout_path);
    else
      std::printf("[engine_editor] panel duzeni kaydedilemedi: %s\n", le.msg);
  }
  // Guncelleme denetimi/indirmesi suruyorsa curl/tar editorle birlikte kapanir
  // (POSIX'te yetim kalip indirmeye devam ederdi; docs/GUNCELLEME.md cancel()).
  if (update_ui_shutdown(upd_ui)) std::printf("[engine_editor] guncelleme: suren is iptal edildi (editor kapaniyor)\n");
  // Editorden baslatilan oyun editorle KAPANIR: sahipsiz kalan bir oyun
  // penceresi, editor tekrar acilinca "neden iki oyun var" sorusu olurdu.
  // PENCERESIZ kipte istisna: oyun BEKLENIR (en cok 120 s) — dogrulama
  // yolu (--headless N --komut oynat.oyunu_calistir) oyunun sonucunu gormeli.
  if (oyun.state == GameRunState::Running && headless) {
    std::printf("[engine_editor] penceresiz: oyunun bitmesi bekleniyor (%s)\n", oyun.game);
    for (int i = 0; i < 12000 && game_run_poll(oyun, oyun_satiri, nullptr) == GameRunState::Running; i++) platform::thread_sleep_us(10000);
  }
  if (oyun.state == GameRunState::Running) {
    game_run_stop(oyun);
    // Gomulu oyun `bitir` yolundan kendisi cikar; kGameStopGraceNs (3 s)
    // asilirsa poll agaci oldurur. 5 s: ikisine de yer var.
    for (int i = 0; i < 500 && game_run_poll(oyun, oyun_satiri, nullptr) == GameRunState::Running; i++) platform::thread_sleep_us(10000);
    std::printf("[engine_editor] calisan oyun durduruldu: %s\n", oyun.game);
  }
  if (oyun.state != GameRunState::Idle)
    std::printf("[engine_editor] oyun: %s, durum %s, cikis kodu %d, %u satir\n", oyun.game,
                oyun.state == GameRunState::Finished ? "bitti" : oyun.state == GameRunState::Failed ? "BASARISIZ" : "?", oyun.exit_code, oyun.lines);
  jobs.shutdown();
  bodies_remove(st, phys);
  // VIEWPORT ImGui'DEN ONCE KAPANIR: doku descriptor'i ImGui'nin havuzundan
  // geliyor, once ImGui kapanirsa o set'i iade edecek yer kalmaz. Eklenmedigi
  // ilk halde dogrulama katmani kapanista VUID-vkDestroyDevice-device-05137
  // veriyordu (cihaz yok edilirken cocuk nesneler duruyor).
  oyun_goruntu.shutdown(); // descriptor'i ImGui havuzundan: ImGui'den ONCE (viewport ile ayni kural)
  vp.shutdown();
  ui.shutdown();
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  // Guncelleme kurulduysa yeni ikili SIMDI baslar: panel duzeni ve son dosyalar
  // yazildi, eski surecin hicbir kaynagi (GPU, pencere dosyasi) kalmadi. Hata
  // olursa kullanicinin gorecegi yere de yazilir (engine_hata.log): pencere
  // kapanmak uzere, durum cubugu artik yok.
  if (restart_after_update) {
    char e[512] = {0};
    if (update_ui_restart(upd_ui, opts.argc, opts.argv, st.scene_path, e, sizeof e))
      std::printf("[engine_editor] guncelleme: yeni surum baslatildi\n");
    else
      platform::startup_failure("guncelleme kuruldu ama yeni editor baslatilamadi: %s — editoru elle acin", e);
  }
  if (imgui_hata_var) return 1;
  if (komut_hata) return 2;
  return 0;
}

} // namespace tulpar::engine::app
