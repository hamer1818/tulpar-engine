// L3 RENDERER — DERLENMIS RENDER GRAPH (ilk surum, plan §6).
//
// Gecisler VERIDIR: ad, tur, girdi/cikti hedefi, mip seviyesi. Tablo KURULUMDA
// bir kez uretilir (graph_build) ve bagimliliklar + layout gecisleri TABLODAN
// TURETILIR (graph_build icindeki ikinci gecis). Kayit aninda cozucu, arama ya
// da ayirma YOKTUR: Renderer::record_* diziyi bastan sona yurur.
//
// Neden boyle: "hangi gecis neyi okur" sorusunun cevabi calisma zamaninda
// aranirsa her kare CPU isi olur ve barrier'lar elle yazilir (unutulur). Burada
// okuyucu/yazici eslesmesi derleme-benzeri bir on adimda cikarilir; bir cikti
// sonradan orneklenmiyorsa graph_validate bunu HATA sayar (olu gecis) ve bir
// girdinin ureticisi yoksa yine hata verir. Gecis eklemek/cikarmak TEK yerde:
// graph_build.
//
// Vulkan tipi ICERMEZ (saf veri): layout niyeti GraphLayout ile tasinir,
// VkImageLayout'a ceviri Renderer'da yapilir.
#pragma once
#include <cstdint>

namespace tulpar::engine::renderer {

constexpr uint32_t kMaxBloomMips = 6;

// DIKKAT — AYNI ISIMDE IKINCI BIR SABIT VAR: renderer/graph_builder.hpp'deki
// dinamik DAG derleyicisi (RenderGraphBuilder) BASKA bir sistemdir ve kendi
// kapasitesini tasir. Eskiden o da `kMaxGraphPasses` adiniydi (deger 32) ve
// ayni ad alanindaydi: iki baslik bir arada include edilse yeniden tanimlama
// hatasi verirdi, edilmediginde de "kapasite 32" diye YANLIS okunuyordu. O
// sabit kMaxBuilderPasses olarak ayrildi. BURADAKI sayi, Renderer'in gecis
// tablosunun ve ona gore boyutlanan dizilerin (renderer.hpp: graph_,
// post_sets_, PostInfo::pass_name) tek kapasitesidir.
//
// kMaxGraphPasses asagida, GraphDesc'ten SONRA tanimlanir: artik elle
// sayilmaz, graph_pass_count()'tan TURETILIR (bkz. kGraphWidestTable).

enum class PassKind : uint8_t {
  Cull,    // GPU gorunurluk kumeleme (COMPUTE; cikti TAMPON, goruntu degil)
  Shadow,  // golge atlasi (yalniz derinlik, kendi gecisi)
  Motion,  // ekran uzayi hareket vektoru + reactive maske (kendi gecisi)
  Scene,   // sahne -> HDR renk hedefi (depth prepass + renk: 2 subpass)
  Bright,  // parlak gecis (esik): HDR -> down[0]
  Down,    // indirgeme: down[i-1] -> down[i]
  Up,      // toplamali yukari ornekleme: down[i] + (down|up)[i+1] -> up[i]
  Godray,  // screen-space godrays
  Compose, // tam ekran birlestirme (tone + bloom) -> CAGIRANIN hedefi
};

// Graph'in bildigi hedefler. Mip seviyesi ayri alanda tasinir.
enum GraphRes : uint8_t {
  kResShadow = 0, // golge atlasi (derinlik)
  kResHdr = 1,    // sahne HDR renk hedefi
  kResDown = 2,   // bloom indirgeme zinciri (mip'li)
  kResUp = 3,     // bloom yukari zinciri (mip'li)
  kResTarget = 4, // CAGIRANIN hedefi (swapchain/offscreen) — graph bunu yaratmaz
  kResMotion = 5, // hareket vektoru + reactive maske (RG = px kayma, A = maske)
  // Cull gecisinin ciktisi bir GORUNTU degil TAMPON'dur (dolayli komutlar +
  // gorunurluk listesi). Tablo bugun yalniz goruntu hedeflerinin layout'unu
  // turetiyor; bu kaynak orada yer alsin diye var ve tuketicisi (cizim
  // komutlari, surucunun dolayli okumasi) tablo DISINDA oldugu icin gecis
  // out_external isaretlidir — "olu gecis" denetimi onu atlar.
  kResCull = 6,
  kResGodray = 7, // godray hedefi
  kResNone = 0xFF,
};

// Ciktinin gecis sonundaki layout NIYETI (turetilir, elle yazilmaz).
enum class GraphLayout : uint8_t {
  ShaderRead, // sonraki bir gecis ORNEKLIYOR -> SHADER_READ_ONLY_OPTIMAL
  External,   // graph disina cikiyor (cagiranin gecisi ya da golge atlasi)
};

struct GraphPass {
  const char *name = "";
  PassKind kind = PassKind::Scene;
  uint8_t in0 = kResNone, in0_level = 0;
  uint8_t in1 = kResNone, in1_level = 0;
  uint8_t out = kResNone, out_level = 0;
  // --- TURETILEN alanlar (graph_build doldurur; kayitta hesaplanmaz) ---
  uint8_t producer0 = kResNone; // in0'i yazan gecis indeksi
  uint8_t producer1 = kResNone;
  bool out_sampled = false; // ciktisini sonraki bir gecis orneklyor mu
  GraphLayout out_layout = GraphLayout::External;
  // Cikti graph DISINDA tuketiliyor (upscaler / TAA / olcum): "olu gecis"
  // denetimi bunu atlar. Elle KURULUR (turetilemez: tuketici tabloda yok).
  // Bugun yalniz hareket vektoru hedefi boyle; bir upscaler gecisi tabloya
  // girdiginde onun in0'i olur ve bayrak dusmelidir.
  bool out_external = false;
};

struct GraphDesc {
  bool post = false;       // HDR hedefi + bloom zinciri + birlestirme
  bool shadow = true;      // golge gecisi tabloda mi
  bool motion = false;     // hareket vektoru gecisi tabloda mi (Faz 5)
  bool cull = false;       // GPU cull compute gecisi tabloda mi (Faz 9)
  bool godray = false;     // isik huzmesi gecisi tabloda mi
  uint32_t bloom_mips = 4; // post acikken 2..kMaxBloomMips
};

// --- Kapasite TEK YERDEN turetilir ----------------------------------------
// Gecis sayisinin formulu BIR kez yazilir (graph_pass_count) ve uc yerde
// kullanilir: (1) graph_build'in kapasite kontrolu, (2) graph_build'in
// "urettigim tablo beyan ettigimle AYNI mi" oz-denetimi, (3) asagidaki
// static_assert. Once formul graph_build'in icine gomuluydu ve kapasite (16)
// elle sayilmisti; godray gecisi eklenince en genis tablo 17 oldu, kapasite
// 16 kaldi ve graph_build 0 donerek post'u SESSIZCE kapatiyordu (bloom ve
// tonemap dahil). Bir daha olmasin diye sayi artik elle yazilmiyor.
constexpr uint32_t graph_clamp_mips(uint32_t m) {
  return m < 2u ? 2u : (m > kMaxBloomMips ? kMaxBloomMips : m);
}
// post acikken 2*mip = parlak(1) + indirgeme(mip-1) + yukari(mip-1) + birlestir(1).
constexpr uint32_t graph_pass_count(const GraphDesc &d) {
  return (d.cull ? 1u : 0u) + (d.shadow ? 1u : 0u) + (d.motion ? 1u : 0u) + 1u /*sahne*/ +
         (d.post ? (2u * graph_clamp_mips(d.bloom_mips) + (d.godray ? 1u : 0u)) : 0u);
}
// Butun anahtarlari acik, mip zinciri en derin tablo: graph_build'in
// uretebilecegi EN GENIS tablo. Yeni bir anahtar eklenince burasi da
// guncellenmeli — unutulursa graph_build'in n != need oz-denetimi yakalar.
constexpr GraphDesc graph_widest_desc() {
  GraphDesc d;
  d.post = true;
  d.shadow = true;
  d.motion = true;
  d.cull = true;
  d.godray = true;
  d.bloom_mips = kMaxBloomMips;
  return d;
}
constexpr uint32_t kGraphWidestTable = graph_pass_count(graph_widest_desc()); // 4 + 2*6 + 1 = 17

// Renderer'in gecis tablosu kapasitesi. Tam olarak en genis tabloya esit:
// buyugu bosa bellek, kucugu SESSIZ post kapanmasi demek.
constexpr uint32_t kMaxGraphPasses = kGraphWidestTable;
static_assert(kMaxGraphPasses >= kGraphWidestTable,
              "kMaxGraphPasses en genis tabloyu almiyor: graph_build 0 doner ve post SESSIZCE kapanir "
              "(bloom + tonemap dahil). Yeni gecis eklediysen graph_pass_count ve graph_widest_desc guncellenmeli.");

namespace detail {
// Ad dizgeleri sabit ve omurluk (tablo const char* tutar).
inline const char *down_name(uint32_t i) {
  static const char *k[kMaxBloomMips] = {"indirge0", "indirge1", "indirge2", "indirge3", "indirge4", "indirge5"};
  return k[i < kMaxBloomMips ? i : kMaxBloomMips - 1];
}
inline const char *up_name(uint32_t i) {
  static const char *k[kMaxBloomMips] = {"yukari0", "yukari1", "yukari2", "yukari3", "yukari4", "yukari5"};
  return k[i < kMaxBloomMips ? i : kMaxBloomMips - 1];
}
} // namespace detail

// Tabloyu uretir ve bagimlilik/layout alanlarini TURETIR. Donus: gecis sayisi
// (0 = kapasite yetmedi).
inline uint32_t graph_build(const GraphDesc &d, GraphPass *out, uint32_t cap) {
  const uint32_t mips = graph_clamp_mips(d.bloom_mips);
  const uint32_t need = graph_pass_count(d);
  if (cap < need || cap > kMaxGraphPasses) return 0;
  uint32_t n = 0;
  // Yazmalar SINIRLI: `need` ile asagidaki uretim AYRISIRSA (yeni bir gecis
  // eklenip graph_pass_count guncellenmezse) eskiden out[] sessizce TASARDI —
  // kapasite kontrolu need'e bakiyordu, uretime degil. Simdi tasma bayraga
  // dusuyor ve fonksiyon 0 donuyor; kurulum durur, sebep disari verilir.
  bool tasti = false;
  auto emit = [&](const GraphPass &p) {
    if (n >= cap) { tasti = true; return; }
    out[n++] = p;
  };
  if (d.cull) {
    // ILK gecis: hem golge hem sahne onun yazdigi dolayli komutlari okur.
    GraphPass p;
    p.name = "cull";
    p.kind = PassKind::Cull;
    p.out = kResCull;
    p.out_external = true; // tuketici cizim komutlari: tabloda gecis degil
    emit(p);
  }
  if (d.shadow) {
    GraphPass p;
    p.name = "golge";
    p.kind = PassKind::Shadow;
    p.out = kResShadow;
    emit(p);
  }
  if (d.motion) {
    // Kendi gecisi (kendi derinligi, transient): sahne gecisine ikinci bir renk
    // eki olarak eklenmedi cunku o zaman mesh.frag'in iki varyanti gerekirdi ve
    // hareket KAPALIYKEN bugunku boru hatlari degisirdi. Ayri gecis: ikinci bir
    // geometri gecisi (olculur, PLAN Faz 5 cihaz diliminde tartilir).
    GraphPass p;
    p.name = "hareket";
    p.kind = PassKind::Motion;
    p.out = kResMotion;
    p.out_external = true; // tuketici (upscaler/TAA) henuz tabloda degil
    emit(p);
  }
  {
    GraphPass p;
    p.name = d.post ? "sahne_hdr" : "sahne";
    p.kind = PassKind::Scene;
    p.in0 = d.shadow ? (uint8_t)kResShadow : (uint8_t)kResNone;
    p.out = d.post ? (uint8_t)kResHdr : (uint8_t)kResTarget;
    emit(p);
  }
  if (d.post) {
    {
      GraphPass p;
      p.name = "parlak";
      p.kind = PassKind::Bright;
      p.in0 = kResHdr;
      p.out = kResDown;
      p.out_level = 0;
      emit(p);
    }
    for (uint32_t i = 1; i < mips; i++) {
      GraphPass p;
      p.name = detail::down_name(i);
      p.kind = PassKind::Down;
      p.in0 = kResDown;
      p.in0_level = (uint8_t)(i - 1);
      p.out = kResDown;
      p.out_level = (uint8_t)i;
      emit(p);
    }
    // Yukari zincir: en kucuk mip'ten baslar. i = mips-2 icin alt kaynak
    // indirgeme zincirinin son mip'i, digerlerinde bir onceki yukari cikti.
    for (uint32_t k = 0; k < mips - 1; k++) {
      const uint32_t i = mips - 2 - k;
      GraphPass p;
      p.name = detail::up_name(i);
      p.kind = PassKind::Up;
      p.in0 = kResDown;
      p.in0_level = (uint8_t)i; // ayni seviye (keskin)
      p.in1 = (i + 1 == mips - 1) ? (uint8_t)kResDown : (uint8_t)kResUp;
      p.in1_level = (uint8_t)(i + 1); // alt seviye (yayilan)
      p.out = kResUp;
      p.out_level = (uint8_t)i;
      emit(p);
    }
    if (d.godray) {
      GraphPass p;
      p.name = "godray";
      p.kind = PassKind::Godray;
      p.in0 = kResDown;
      p.in0_level = 0;
      p.in1 = kResUp;
      p.in1_level = 0;
      p.out = kResGodray;
      p.out_level = 0;
      emit(p);
    }
    {
      GraphPass p;
      p.name = "birlestir";
      p.kind = PassKind::Compose;
      p.in0 = kResHdr;
      p.in1 = d.godray ? (uint8_t)kResGodray : (uint8_t)kResUp;
      p.in1_level = 0;
      p.out = kResTarget;
      emit(p);
    }
  }
  // OZ-DENETIM: beyan edilen sayi (need) ile gercekten uretilen tablo ayni
  // olmali. Ayrisma = graph_pass_count ile graph_build'in govdesi birbirinden
  // kaymis demektir; tabloyu kullanmak yerine 0 don (cagiran post'u kapatir ve
  // sebebi disari verir). Kapiyi tests/test_render_graph.cpp suruyor.
  if (tasti || n != need) return 0;
  // --- TURETME: uretici eslesmesi + layout ---------------------------------
  for (uint32_t j = 0; j < n; j++) {
    const uint8_t ins[2] = {out[j].in0, out[j].in1};
    const uint8_t lvls[2] = {out[j].in0_level, out[j].in1_level};
    for (uint32_t s = 0; s < 2; s++) {
      uint8_t prod = kResNone;
      if (ins[s] != kResNone)
        for (uint32_t i = j; i-- > 0;)
          if (out[i].out == ins[s] && out[i].out_level == lvls[s]) { prod = (uint8_t)i; break; }
      if (s == 0) out[j].producer0 = prod;
      else out[j].producer1 = prod;
      if (prod != kResNone) {
        out[prod].out_sampled = true;
        // Golge atlasi derinliktir ve kendi gecisi finalLayout'u zaten
        // SHADER_READ_ONLY birakir; renk hedefleri de oyle.
        out[prod].out_layout = GraphLayout::ShaderRead;
      }
    }
  }
  return n;
}

// Tutarlilik denetimi. nullptr = tutarli; aksi halde sebep (test kapisi bunu
// POZITIF KONTROL olarak bozuk tabloyla dener).
inline const char *graph_validate(const GraphPass *p, uint32_t n) {
  if (n == 0) return "bos tablo";
  if (n > kMaxGraphPasses) return "gecis sayisi kapasiteyi asiyor";
  for (uint32_t i = 0; i < n; i++) {
    if (p[i].out == kResNone) return "ciktisi olmayan gecis";
    if (p[i].in0 != kResNone && p[i].producer0 == kResNone && p[i].in0 != kResShadow)
      return "ureticisi olmayan girdi (in0)";
    if (p[i].in1 != kResNone && p[i].producer1 == kResNone && p[i].in1 != kResShadow)
      return "ureticisi olmayan girdi (in1)";
    if (p[i].producer0 != kResNone && p[i].producer0 >= i) return "girdi kendinden SONRAKI gecisten geliyor";
    if (p[i].producer1 != kResNone && p[i].producer1 >= i) return "girdi kendinden SONRAKI gecisten geliyor";
    // Cagiranin hedefine yazmayan bir gecisin ciktisi okunmuyorsa olu gecistir.
    // out_external: tuketici graph DISINDA (upscaler/olcum) — olu sayilmaz.
    if (p[i].out != kResTarget && !p[i].out_sampled && !p[i].out_external)
      return "ciktisi hic okunmayan gecis (olu)";
  }
  if (p[n - 1].out != kResTarget) return "son gecis cagiranin hedefine yazmiyor";
  return nullptr;
}

} // namespace tulpar::engine::renderer
