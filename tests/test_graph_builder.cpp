// L3 RENDERER — Render Graph Builder Birim Testleri (test_graph_builder.cpp)
// Dagor daBfg esinlenmeli topolojik sıralama, ölü geçiş budama ve kaynak ömrü doğrulaması.

#include "renderer/graph_builder.hpp"
#include "tests/test.hpp"
#include <cstring>

using namespace tulpar::engine::renderer;

ENGINE_TEST(graph_builder_topological_sort_and_culling) {
  RenderGraphBuilder gb;

  // Kaynaklar
  int32_t r_shadow = gb.register_resource("ShadowMap");
  int32_t r_depth  = gb.register_resource("DepthBuffer");
  int32_t r_hdr    = gb.register_resource("HdrTarget");
  int32_t r_godray = gb.register_resource("GodrayShafts");
  int32_t r_unused = gb.register_resource("UnusedTexture");

  // Geçişler
  // 1. Gölge haritası
  int32_t p_shadow = gb.add_pass("ShadowPass");
  gb.pass_writes(p_shadow, r_shadow);

  // 2. Derinlik prepass
  int32_t p_depth = gb.add_pass("DepthPrepass");
  gb.pass_writes(p_depth, r_depth);

  // 3. Ana Işıklandırma
  int32_t p_lighting = gb.add_pass("LightingPass");
  gb.pass_reads(p_lighting, r_shadow);
  gb.pass_reads(p_lighting, r_depth);
  gb.pass_writes(p_lighting, r_hdr);

  // 4. Işık Hüzmeleri (Godrays)
  int32_t p_godray = gb.add_pass("GodrayPass");
  gb.pass_reads(p_godray, r_depth);
  gb.pass_writes(p_godray, r_godray);

  // 5. PostProcess / Ekrana Çizim (Side Effect)
  int32_t p_post = gb.add_pass("PostProcess", true);
  gb.pass_reads(p_post, r_hdr);
  gb.pass_reads(p_post, r_godray);

  // 6. Ölü Geçiş (Dead Pass - çıktısını kimse okumuyor)
  int32_t p_dead = gb.add_pass("DeadPass");
  gb.pass_writes(p_dead, r_unused);

  // Derle
  bool ok = gb.compile();
  CHECK(ok == true);

  // Ölü geçiş budandı mı?
  CHECK(gb.pass(p_dead).culled == true);
  CHECK(gb.pass(p_shadow).culled == false);
  CHECK(gb.pass(p_depth).culled == false);
  CHECK(gb.pass(p_lighting).culled == false);
  CHECK(gb.pass(p_godray).culled == false);
  CHECK(gb.pass(p_post).culled == false);

  // 5 aktif geçiş sıralanmış olmalı
  CHECK(gb.sorted_count() == 5);

  // Sıralama doğrulaması:
  // PostProcess son geçiş olmalı
  CHECK(gb.sorted_pass_index(4) == static_cast<uint32_t>(p_post));

  // Kaynak ömürleri kontrolü
  const ResourceLifetime &res_depth = gb.resource(r_depth);
  CHECK(res_depth.referenced == true);
  CHECK(res_depth.first_pass <= res_depth.last_pass);
}

ENGINE_TEST(graph_builder_detects_circular_dependency) {
  RenderGraphBuilder gb;

  int32_t r_a = gb.register_resource("ResA");
  int32_t r_b = gb.register_resource("ResB");

  int32_t p1 = gb.add_pass("Pass1", true);
  gb.pass_reads(p1, r_b);
  gb.pass_writes(p1, r_a);

  int32_t p2 = gb.add_pass("Pass2", true);
  gb.pass_reads(p2, r_a);
  gb.pass_writes(p2, r_b);

  // Döngüsel bağımlılık compile() == false dönmeli
  bool ok = gb.compile();
  CHECK(ok == false);
}
