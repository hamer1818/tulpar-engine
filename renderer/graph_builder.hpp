// L3 RENDERER — Dinamik Render Graph Derleyicisi (graph_builder.hpp)
//
// ESİNLENME VE MİMARİ:
// 1. Dagor Engine (GaijinEntertainment/DagorEngine prog/gameLibs/render/daBfg/)
//    daBfg'nin DAG (Yönlendirilmiş Döngüsüz Çizge) mimarisi esas alınmıştır.
//    Geçişler (Render Passes) okudukları ve yazdıkları kaynakları (Texture/Buffer)
//    bağımsız olarak beyan eder.
// 2. Kahn Topolojik Sıralama Algoritması (Topological Sort):
//    Geçişler arası bağımlılık çizgesi analiz edilerek deterministik ve optimal bir
//    yürütme sırası elde edilir; döngüsel bağımlılıklar (circular dependency) anında yakalanır.
// 3. Ölü Geçiş Budama (Pass Culling / Dead-Code Elimination):
//    Nihai ekrana (Backbuffer) veya yan etkiye (side-effect) ulaşmayan kullanılmayan
//    ara geçişler GPU'da yürütülmeden önce çizgeden otomatik olarak ayıklanır.
// 4. Geçici Kaynak Ömür Analizi (Transient Resource Lifetime):
//    Her kaynağın ilk okunduğu ve son bırakıldığı geçiş indeksleri saptanarak
//    bellek alias (yeniden kullanım) ve bariyer optimizasyonu sağlanır.
//
// SIFIR TAHSİS (ZERO-ALLOC) SÖZLEŞMESİ:
// - STL konteynerleri (std::vector, std::map, std::string) içermez.
// - Sabit kapasiteli (32 geçiş, 64 kaynak) yerel bellek blokları ile çalışır.
// - Çalışma zamanında sıfır dinamik heap ayırması.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tulpar::engine::renderer {

constexpr uint32_t kMaxBuilderPasses = 32;
constexpr uint32_t kMaxGraphResources = 64;
constexpr uint32_t kMaxPassIo = 8;
constexpr uint32_t kMaxNameLength = 32;

struct ResourceLifetime {
  char name[kMaxNameLength] = {0};
  int32_t first_pass = -1;  // İlk erişildiği (yazıldığı/okunduğu) geçiş
  int32_t last_pass = -1;   // Son kez okunduğu geçiş (bu geçişten sonra bellek alias yapılabilir)
  int32_t writer_pass = -1; // Bu kaynağı üreten geçiş indeksi (-1 = dışarıdan import edilmiş)
  bool referenced = false;
};

struct RenderPassNode {
  char name[kMaxNameLength] = {0};
  uint32_t reads[kMaxPassIo] = {0};
  uint32_t read_count = 0;
  uint32_t writes[kMaxPassIo] = {0};
  uint32_t write_count = 0;
  bool has_side_effects = false; // Ekrana çizim / Backbuffer gibi budanamaz geçişler
  bool culled = false;           // Analiz sonrası kullanılmadığı için budandı mı?
};

class RenderGraphBuilder {
public:
  RenderGraphBuilder() = default;

  // Yeni bir grafik kaynağı (Doku/Buffer) kaydeder
  int32_t register_resource(const char *name) {
    if (m_resource_count >= kMaxGraphResources) return -1;
    uint32_t id = m_resource_count++;
    std::strncpy(m_resources[id].name, name, kMaxNameLength - 1);
    m_resources[id].first_pass = -1;
    m_resources[id].last_pass = -1;
    m_resources[id].writer_pass = -1;
    m_resources[id].referenced = false;
    return static_cast<int32_t>(id);
  }

  // Yeni bir render geçişi (Pass) ekler
  int32_t add_pass(const char *name, bool has_side_effects = false) {
    if (m_pass_count >= kMaxBuilderPasses) return -1;
    uint32_t idx = m_pass_count++;
    RenderPassNode &node = m_passes[idx];
    std::strncpy(node.name, name, kMaxNameLength - 1);
    node.read_count = 0;
    node.write_count = 0;
    node.has_side_effects = has_side_effects;
    node.culled = false;
    return static_cast<int32_t>(idx);
  }

  // Geçişin bir kaynağı okuduğunu belirtir
  bool pass_reads(uint32_t pass_idx, uint32_t resource_id) {
    if (pass_idx >= m_pass_count || resource_id >= m_resource_count) return false;
    RenderPassNode &node = m_passes[pass_idx];
    if (node.read_count >= kMaxPassIo) return false;
    node.reads[node.read_count++] = resource_id;
    return true;
  }

  // Geçişin bir kaynağa yazdığını / ürettiğini belirtir
  bool pass_writes(uint32_t pass_idx, uint32_t resource_id) {
    if (pass_idx >= m_pass_count || resource_id >= m_resource_count) return false;
    RenderPassNode &node = m_passes[pass_idx];
    if (node.write_count >= kMaxPassIo) return false;
    node.writes[node.write_count++] = resource_id;
    m_resources[resource_id].writer_pass = static_cast<int32_t>(pass_idx);
    return true;
  }

  // Çıktısı hiçbir geçiş tarafından tüketilmeyen ölü geçişleri budar (Cull Unused Passes)
  void cull_unused_passes() {
    bool needed_passes[kMaxBuilderPasses] = {false};
    bool needed_resources[kMaxGraphResources] = {false};

    // 1. Yan etkisi olan (ör. Swapchain/Backbuffer sunumu) tüm geçişleri işaretle
    for (uint32_t i = 0; i < m_pass_count; i++) {
      if (m_passes[i].has_side_effects) {
        needed_passes[i] = true;
      }
    }

    // 2. Çizgede geriye doğru yayılma (Reverse Reachability)
    bool changed = true;
    while (changed) {
      changed = false;
      for (int32_t i = static_cast<int32_t>(m_pass_count) - 1; i >= 0; i--) {
        if (!needed_passes[i]) {
          // Bu geçişin yazdığı herhangi bir kaynak bir sonraki geçiş tarafından isteniyor mu?
          for (uint32_t w = 0; w < m_passes[i].write_count; w++) {
            uint32_t res_id = m_passes[i].writes[w];
            if (needed_resources[res_id]) {
              needed_passes[i] = true;
              changed = true;
              break;
            }
          }
        }

        if (needed_passes[i]) {
          // Bu geçiş gerekiyorsa, okuduğu tüm kaynakları da 'gerekli' olarak işaretle
          for (uint32_t r = 0; r < m_passes[i].read_count; r++) {
            uint32_t res_id = m_passes[i].reads[r];
            if (!needed_resources[res_id]) {
              needed_resources[res_id] = true;
              changed = true;
            }
          }
        }
      }
    }

    // 3. İhtiyaç duyulmayan geçişleri 'culled' olarak işaretle
    for (uint32_t i = 0; i < m_pass_count; i++) {
      m_passes[i].culled = !needed_passes[i];
    }
  }

  // Kahn Algoritması ile topolojik sıralama ve kaynak ömürlerini hesaplar
  bool compile() {
    // Önce ölü geçişleri buda
    cull_unused_passes();

    // Geçişler arası bağımlılık matrisi ve giriş dereceleri (in-degrees)
    // adj[A][B] == true: Pass A, Pass B'den önce yürütülmelidir (B, A'nın çıktısını okuyor)
    bool adj[kMaxBuilderPasses][kMaxBuilderPasses] = {{false}};
    uint32_t in_degree[kMaxBuilderPasses] = {0};

    for (uint32_t b = 0; b < m_pass_count; b++) {
      if (m_passes[b].culled) continue;
      for (uint32_t r = 0; r < m_passes[b].read_count; r++) {
        uint32_t res_id = m_passes[b].reads[r];
        int32_t a = m_resources[res_id].writer_pass;
        if (a >= 0 && a != static_cast<int32_t>(b) && !m_passes[a].culled) {
          if (!adj[a][b]) {
            adj[a][b] = true;
            in_degree[b]++;
          }
        }
      }
    }

    // Kahn Algoritması Sırası
    uint32_t queue[kMaxBuilderPasses];
    uint32_t q_head = 0;
    uint32_t q_tail = 0;

    for (uint32_t i = 0; i < m_pass_count; i++) {
      if (!m_passes[i].culled && in_degree[i] == 0) {
        queue[q_tail++] = i;
      }
    }

    m_sorted_count = 0;
    while (q_head < q_tail) {
      uint32_t u = queue[q_head++];
      m_sorted_passes[m_sorted_count++] = u;

      for (uint32_t v = 0; v < m_pass_count; v++) {
        if (adj[u][v]) {
          in_degree[v]--;
          if (in_degree[v] == 0) {
            queue[q_tail++] = v;
          }
        }
      }
    }

    // Döngü kontrolü: Sıralanan aktif geçiş sayısı toplam aktif geçişe eşit olmalı
    uint32_t active_passes = 0;
    for (uint32_t i = 0; i < m_pass_count; i++) {
      if (!m_passes[i].culled) active_passes++;
    }

    if (m_sorted_count != active_passes) {
      // Döngüsel bağımlılık (Circular Dependency) tespit edildi!
      return false;
    }

    // Kaynak ömürlerini sıralı geçişlere göre hesapla
    for (uint32_t res_id = 0; res_id < m_resource_count; res_id++) {
      m_resources[res_id].first_pass = -1;
      m_resources[res_id].last_pass = -1;
      m_resources[res_id].referenced = false;
    }

    for (uint32_t order_idx = 0; order_idx < m_sorted_count; order_idx++) {
      uint32_t pass_idx = m_sorted_passes[order_idx];
      const RenderPassNode &node = m_passes[pass_idx];

      // Yazılan kaynaklar
      for (uint32_t w = 0; w < node.write_count; w++) {
        uint32_t rid = node.writes[w];
        m_resources[rid].referenced = true;
        if (m_resources[rid].first_pass < 0) {
          m_resources[rid].first_pass = static_cast<int32_t>(order_idx);
        }
        m_resources[rid].last_pass = static_cast<int32_t>(order_idx);
      }

      // Okunan kaynaklar
      for (uint32_t r = 0; r < node.read_count; r++) {
        uint32_t rid = node.reads[r];
        m_resources[rid].referenced = true;
        if (m_resources[rid].first_pass < 0) {
          m_resources[rid].first_pass = static_cast<int32_t>(order_idx);
        }
        m_resources[rid].last_pass = static_cast<int32_t>(order_idx);
      }
    }

    return true;
  }

  // Derleme sonrası sorgu fonksiyonları
  uint32_t pass_count() const { return m_pass_count; }
  uint32_t resource_count() const { return m_resource_count; }
  uint32_t sorted_count() const { return m_sorted_count; }
  uint32_t sorted_pass_index(uint32_t order) const { return m_sorted_passes[order]; }
  const RenderPassNode &pass(uint32_t idx) const { return m_passes[idx]; }
  const ResourceLifetime &resource(uint32_t id) const { return m_resources[id]; }

private:
  RenderPassNode m_passes[kMaxBuilderPasses];
  uint32_t m_pass_count = 0;

  ResourceLifetime m_resources[kMaxGraphResources];
  uint32_t m_resource_count = 0;

  uint32_t m_sorted_passes[kMaxBuilderPasses] = {0};
  uint32_t m_sorted_count = 0;
};

} // namespace tulpar::engine::renderer
