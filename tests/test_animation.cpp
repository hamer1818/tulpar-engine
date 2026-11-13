// Faz 2: animasyon — sikistirma hata sinirlari, sabit iz eleme, ornekleme,
// dongu, model uzayi zinciri, 0 ayirma, paralel/seri bit esitligi.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/jobs/job_system.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "sim/animation.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
constexpr uint32_t J = 8, S = 60;

struct Fixture {
  SystemArena sys;
  Joint joints[J];
  Vec3 t[J][S];
  Quat r[J][S];
  Vec3 s[J][S];
  RawTrack tracks[J];
  RawClip raw;
  const ClipHeader *clip = nullptr;
  ClipBuildStats st;

  bool init() {
    if (!sys.reserve(16u << 20, "anim")) return false;
    for (uint32_t j = 0; j < J; j++) {
      joints[j].parent = j == 0 ? -1 : (int16_t)(j - 1);
      for (uint32_t k = 0; k < S; k++) {
        float ph = (float)k / S * 2 * kPi;
        // Kok: sabit ceviri; digerleri: 1 birim x'te (zincir), 3. eklem asagi yukari
        t[j][k] = j == 0 ? Vec3{0, 1, 0} : Vec3{1, j == 3 ? 0.25f * std::sin(ph) : 0.0f, 0};
        // Donus: her eklem farkli eksende sallanir; eklem 5 sabit
        float ang = j == 5 ? 0.3f : 0.5f * std::sin(ph + j);
        Vec3 axis = j % 3 == 0 ? Vec3{0, 1, 0} : j % 3 == 1 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        r[j][k] = Quat::axis_angle(axis, ang);
        s[j][k] = Vec3{1, 1, 1};
      }
      tracks[j] = RawTrack{t[j], r[j], s[j]};
    }
    raw = RawClip{tracks, J, S, 30.0f};
    clip = ClipBuilder::build(sys, raw, 1e-5f, &st);
    return clip != nullptr;
  }
};
Fixture *fx() {
  static Fixture f;
  static bool ok = f.init();
  return ok ? &f : nullptr;
}
float rot_err_deg(Quat a, Quat b) {
  float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  if (d > 1) d = 1;
  return 2.0f * std::acos(d) * 180.0f / kPi;
}
} // namespace

ENGINE_TEST(anim_compression_bounds_and_constant_elimination) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  // Sabit izler: kok ceviri (1) + eklem 1,2,4,5,6,7 ceviri (6) + eklem 5 donus (1) + tum olcekler (8) = 16
  CHECK(f->st.const_tracks == 16);
  CHECK(f->st.max_rot_error_deg < 0.05f);
  CHECK(f->st.max_pos_error < 0.001f);
  CHECK(f->st.compressed_bytes * 3 < f->st.raw_bytes);
  std::printf("    [bilgi] ham %zu B -> sikistirilmis %zu B (%.1fx); sabit iz %u/24; hata donus %.4f°, ceviri %.5f\n",
              f->st.raw_bytes, f->st.compressed_bytes, (double)f->st.raw_bytes / f->st.compressed_bytes,
              f->st.const_tracks, f->st.max_rot_error_deg, f->st.max_pos_error);
}

ENGINE_TEST(anim_sample_matches_raw_and_loops) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  Vec3 t[J], s[J];
  Quat r[J];
  LocalPose pose{t, r, s, J};
  ClipSampler::sample(f->clip, 0.0f, pose);
  bool ok = true;
  for (uint32_t j = 0; j < J; j++) {
    ok = ok && nearly_equal(t[j], f->t[j][0], 1e-3f) && rot_err_deg(r[j], f->r[j][0]) < 0.05f;
  }
  CHECK(ok);
  // Kare 10 = 10/30 s
  ClipSampler::sample(f->clip, 10.0f / 30.0f, pose);
  CHECK(nearly_equal(t[3], f->t[3][10], 1e-3f));
  // Yarim kare: iki komsunun arasinda
  ClipSampler::sample(f->clip, 10.5f / 30.0f, pose);
  float lo = f->t[3][10].y < f->t[3][11].y ? f->t[3][10].y : f->t[3][11].y;
  float hi = f->t[3][10].y < f->t[3][11].y ? f->t[3][11].y : f->t[3][10].y;
  CHECK(t[3].y >= lo - 1e-3f && t[3].y <= hi + 1e-3f);
  // Dongu: sure sonu = baslangic
  Vec3 t0[J], s0[J];
  Quat r0[J];
  LocalPose p0{t0, r0, s0, J};
  ClipSampler::sample(f->clip, 0.0f, p0);
  ClipSampler::sample(f->clip, f->clip->duration_s, pose);
  CHECK(std::memcmp(t, t0, sizeof t) == 0 && std::memcmp(r, r0, sizeof r) == 0);
}

ENGINE_TEST(anim_to_model_chains_parents) {
  Joint js[3];
  js[0].parent = -1; js[1].parent = 0; js[2].parent = 1;
  Skeleton sk{js, 3};
  Vec3 t[3] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 0}};
  Quat r[3] = {Quat::axis_angle({0, 1, 0}, kPi * 0.5f), Quat::identity(), Quat::identity()};
  Vec3 s[3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
  LocalPose pose{t, r, s, 3};
  Mat4 m[3];
  ClipSampler::to_model(sk, pose, m);
  // Kok 90° y donusu: cocuklar x ekseninde 1,2 -> dunyada z ekseninde -1,-2
  CHECK(nearly_equal(transform_point(m[1], {0, 0, 0}), {0, 0, -1}, 1e-4f));
  CHECK(nearly_equal(transform_point(m[2], {0, 0, 0}), {0, 0, -2}, 1e-4f));
}

ENGINE_TEST(anim_sampling_allocates_nothing) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  Vec3 t[J], s[J];
  Quat r[J];
  LocalPose pose{t, r, s, J};
  Skeleton sk{f->joints, J};
  Mat4 m[J];
  AllocGate::begin_frame();
  for (int i = 0; i < 1000; i++) {
    ClipSampler::sample(f->clip, i * 0.0137f, pose);
    ClipSampler::to_model(sk, pose, m);
  }
  CHECK(AllocGate::end_frame() == 0);
}

namespace {
struct Inst {
  const ClipHeader *clip;
  float time;
  Vec3 t[J], s[J];
  Quat r[J];
};
void eval_job(void *p) {
  Inst *in = static_cast<Inst *>(p);
  LocalPose pose{in->t, in->r, in->s, J};
  ClipSampler::sample(in->clip, in->time, pose);
}
} // namespace

ENGINE_TEST(anim_parallel_eval_equals_serial) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  static SystemArena jsys;
  CHECK(jsys.reserve(8u << 20, "anim-js"));
  JobSystem js;
  CHECK(js.init(jsys, JobSystemConfig{}));
  static Inst par[64], ser[64];
  JobDecl decls[64];
  for (int i = 0; i < 64; i++) {
    par[i] = Inst{f->clip, i * 0.031f, {}, {}, {}};
    ser[i] = par[i];
    decls[i] = JobDecl{eval_job, &par[i], "anim_eval"};
  }
  Counter c;
  js.run(decls, 64, &c);
  js.wait(c);
  for (int i = 0; i < 64; i++) eval_job(&ser[i]);
  bool same = true;
  for (int i = 0; i < 64; i++)
    same = same && std::memcmp(par[i].t, ser[i].t, sizeof par[i].t) == 0 &&
           std::memcmp(par[i].r, ser[i].r, sizeof par[i].r) == 0;
  CHECK(same);
  js.shutdown();
}

// ---- Karakter bütçesi ölçümü (2026-11-13, docs/MODEL-ANIMASYON-PLANI.md §6 madde 4) --------------------------------
// "Ekranda kaç iskeletli karakter taşır?" sorusunun CPU yarısı: bir karakter için her kare yapılan iş =
// ClipSampler::sample + to_model + skin_matrices (sahne çalışma zamanının yaptığı üçlü). Burada N karakter × J eklem
// bu üçlünün süresi (tek iş parçacığı, ısınmadan sonra en iyi 5 turun medyanı) basılır. SAYI cihaza özgüdür: bu
// makinede (masaüstü) ölçülür ve BASILIR, mobil için İDDİA EDİLMEZ (Mali-G72 ölçümü cihaz ister).
// Kapı iddiası yalnız bizim kodumuz için: süre N ile orantılı artar (kontrolün kendisi: ölçüm gerçekten iş yapıyor) ve
// kare içinde 0 ayırma. Mutlak ms için eşik YOK.
namespace {
constexpr uint32_t BJ = 128, BS = 60;
struct BenchFx {
  SystemArena sys;
  Joint joints[BJ];
  Mat4 inv[BJ];
  Vec3 t[BJ][BS], s[BJ][BS];
  Quat r[BJ][BS];
  RawTrack tracks[BJ];
  const ClipHeader *clip = nullptr, *clip60 = nullptr;   // 128 ve 60 eklemli iki klip (sample, eklem sayısı eşit ister)
  bool init() {
    if (!sys.reserve(32u << 20, "anim_bench")) return false;
    for (uint32_t j = 0; j < BJ; j++) {
      joints[j].parent = j == 0 ? -1 : (int16_t)((j - 1) / 2);   // ikili ağaç: gerçek iskelete benzer derinlik (~7)
      inv[j] = Mat4::identity();
      for (uint32_t k = 0; k < BS; k++) {
        float ph = (float)k / BS * 2 * kPi;
        t[j][k] = j == 0 ? Vec3{0, 1, 0} : Vec3{0.1f, 0.2f, 0};
        r[j][k] = Quat::axis_angle(j % 3 == 0 ? Vec3{0, 1, 0} : j % 3 == 1 ? Vec3{1, 0, 0} : Vec3{0, 0, 1}, 0.4f * std::sin(ph + j));
        s[j][k] = Vec3{1, 1, 1};
      }
      tracks[j] = RawTrack{t[j], r[j], s[j]};
    }
    RawClip raw{tracks, BJ, BS, 30.0f};
    ClipBuildStats st;
    clip = ClipBuilder::build(sys, raw, 1e-5f, &st);
    RawClip raw60{tracks, 60, BS, 30.0f};
    clip60 = ClipBuilder::build(sys, raw60, 1e-5f, &st);
    return clip != nullptr && clip60 != nullptr;
  }
};
BenchFx *bfx() {
  static BenchFx f;
  static bool ok = f.init();
  return ok ? &f : nullptr;
}
double tur_ms(BenchFx *f, uint32_t joints, uint32_t chars, float t0) {
  static Vec3 t[BJ], s[BJ];
  static Quat r[BJ];
  static Mat4 model[BJ], skin[BJ];
  LocalPose pose{t, r, s, joints};
  Skeleton sk{f->joints, joints};
  double best[5];
  for (int tur = 0; tur < 7; tur++) {                      // 2 ısınma + 5 ölçüm
    auto a = std::chrono::steady_clock::now();
    float acc = 0;
    for (uint32_t c = 0; c < chars; c++) {
      ClipSampler::sample(joints == 60 ? f->clip60 : f->clip, t0 + c * 0.0173f, pose);
      ClipSampler::to_model(sk, pose, model);
      ClipSampler::skin_matrices(model, f->inv, joints, skin);
      acc += skin[joints - 1].m[3][0];                       // sonucu kullan: derleyici işi silemesin
    }
    auto b = std::chrono::steady_clock::now();
    test::escape(&acc);
    if (tur >= 2) best[tur - 2] = std::chrono::duration<double, std::milli>(b - a).count();
  }
  std::sort(best, best + 5);
  return best[2];
}
} // namespace

ENGINE_TEST(anim_olcum_karakter_butcesi) {
  BenchFx *f = bfx();
  CHECK(f != nullptr);
  if (!f) return;
  const uint32_t eklemler[] = {60, 128};
  const uint32_t karakterler[] = {10, 50, 100, 200};
  double ilk60 = 0, son60 = 0;
  for (uint32_t j : eklemler) {
    std::printf("    [olcum] %3u eklem:", j);
    for (uint32_t n : karakterler) {
      const double ms = tur_ms(f, j, n, 0.1f);
      std::printf("  %3u karakter %.3f ms", n, ms);
      if (j == 60 && n == 10) ilk60 = ms;
      if (j == 60 && n == 200) son60 = ms;
    }
    std::printf("\n");
  }
  {
    // A2: gecis sirasinda her karakter IKI klip ornekler ve eklem basina slerp yapar (model_pose_blend'in yaptigi is).
    Vec3 t1[BJ], s1[BJ], t2[BJ], s2[BJ];
    Quat r1[BJ], r2[BJ];
    Mat4 model[BJ], skin[BJ];
    LocalPose pa{t1, r1, s1, 60}, pb{t2, r2, s2, 60};
    Skeleton sk{f->joints, 60};
    double en_iyi = 1e9;
    for (int tur = 0; tur < 7; tur++) {
      auto a0 = std::chrono::steady_clock::now();
      float acc = 0;
      for (uint32_t c = 0; c < 200; c++) {
        ClipSampler::sample(f->clip60, 0.1f + c * 0.0173f, pa);
        ClipSampler::sample(f->clip60, 0.9f + c * 0.0131f, pb);
        for (uint32_t j = 0; j < 60; j++) {
          pa.t[j] = pa.t[j] + (pb.t[j] - pa.t[j]) * 0.4f;
          pa.s[j] = pa.s[j] + (pb.s[j] - pa.s[j]) * 0.4f;
          pa.r[j] = slerp(pa.r[j], pb.r[j], 0.4f);
        }
        ClipSampler::to_model(sk, pa, model);
        ClipSampler::skin_matrices(model, f->inv, 60, skin);
        acc += skin[59].m[3][0];
      }
      auto b0 = std::chrono::steady_clock::now();
      test::escape(&acc);
      const double ms = std::chrono::duration<double, std::milli>(b0 - a0).count();
      if (tur >= 2 && ms < en_iyi) en_iyi = ms;
    }
    std::printf("    [olcum]  60 eklem: 200 karakter GECIS SIRASINDA (2 klip + slerp) %.3f ms\n", en_iyi);
  }
  std::printf("    [olcum] (masaustu CPU, tek parcacik, sample+to_model+skin; mobil icin iddia YOK)\n");
  CHECK(ilk60 > 0.0 && son60 > ilk60 * 8.0);               // 20x iş > 8x süre: ölçüm gerçekten ölçeklenir (kontrol)
  Vec3 t[BJ], s[BJ];
  Quat r[BJ];
  Mat4 model[BJ], skin[BJ];
  LocalPose pose{t, r, s, 60};
  Skeleton sk{f->joints, 60};
  AllocGate::begin_frame();
  for (int i = 0; i < 200; i++) {
    ClipSampler::sample(f->clip60, i * 0.0137f, pose);
    ClipSampler::to_model(sk, pose, model);
    ClipSampler::skin_matrices(model, f->inv, 60, skin);
  }
  CHECK(AllocGate::end_frame() == 0);
}
