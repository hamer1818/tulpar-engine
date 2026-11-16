#include "app/editor_animator_grafik.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "app/editor_animator_metin.hpp"
#include "app/editor_ui.hpp"
#include <IconsMaterialDesign.h>

namespace tulpar::engine::app {

void animator_grafik_kur(const sim::AnimatorDef &def, AnimGrafik *out) {
  *out = AnimGrafik{};
  for (uint32_t i = 0; i < sim::kAnimMaxStates; i++) out->dugum_of[i] = -1;
  const uint32_t sn = std::min(def.state_count, sim::kAnimMaxStates);
  if (sn == 0) return;
  int32_t derinlik[sim::kAnimMaxStates];
  for (uint32_t i = 0; i < sn; i++) derinlik[i] = -1;
  int32_t kuyruk[sim::kAnimMaxStates];
  uint32_t bas = 0, son = 0;
  const int32_t start = (def.start >= 0 && (uint32_t)def.start < sn) ? def.start : 0;
  derinlik[start] = 0;
  kuyruk[son++] = start;
  bool herhangi_var = false;
  for (uint32_t t = 0; t < def.trans_count; t++) if (def.trans[t].from < 0) herhangi_var = true;
  // Genislik oncelikli; "herhangi" hedefleri baslangictan sonra 1. katmana girer (her durumdan ulasilir).
  auto yay = [&]() {
    while (bas < son) {
      const int32_t s = kuyruk[bas++];
      for (uint32_t t = 0; t < def.trans_count; t++) {
        const sim::AnimTransition &tr = def.trans[t];
        if (tr.from != s || tr.to < 0 || (uint32_t)tr.to >= sn || derinlik[tr.to] >= 0) continue;
        derinlik[tr.to] = derinlik[s] + 1;
        kuyruk[son++] = tr.to;
      }
    }
  };
  yay();
  if (herhangi_var) {
    for (uint32_t t = 0; t < def.trans_count; t++) {
      const sim::AnimTransition &tr = def.trans[t];
      if (tr.from >= 0 || tr.to < 0 || (uint32_t)tr.to >= sn || derinlik[tr.to] >= 0) continue;
      derinlik[tr.to] = 1;
      kuyruk[son++] = tr.to;
    }
    yay();
  }
  int32_t en_derin = 0;
  for (uint32_t i = 0; i < sn; i++) en_derin = std::max(en_derin, derinlik[i]);
  uint32_t sutun_dolu[kAnimGrafikMaxDugum + 1] = {0};
  if (herhangi_var) {   // 0. sutun: baslangic ustte, "herhangi" altinda (baslangic hep 0. satirda olsun diye once o)
    sutun_dolu[0] = 1;
  }
  for (uint32_t i = 0; i < sn; i++) {
    AnimGrafikDugum &d = out->d[out->n];
    d.durum = (int32_t)i;
    d.ulasilamaz = derinlik[i] < 0;
    d.sutun = d.ulasilamaz ? (uint32_t)(en_derin + 1) : (uint32_t)derinlik[i];
    if (d.ulasilamaz) out->ulasilamaz_n++;
    out->dugum_of[i] = (int32_t)out->n++;
  }
  // Satirlar: sutun icinde baslangic once, sonra durum sirasi (kararli).
  auto satir_ver = [&](uint32_t di) { AnimGrafikDugum &d = out->d[di]; d.satir = sutun_dolu[d.sutun]++; };
  satir_ver((uint32_t)out->dugum_of[start]);
  if (herhangi_var) out->d[out->dugum_of[start]].satir = 0;
  for (uint32_t i = 0; i < sn; i++) if ((int32_t)i != start) satir_ver((uint32_t)out->dugum_of[i]);
  if (herhangi_var) {
    AnimGrafikDugum &h = out->d[out->n];
    h.durum = -1;
    h.sutun = 0;
    h.satir = sutun_dolu[0]++;
    out->herhangi = (int32_t)out->n++;
  }
  for (uint32_t i = 0; i < out->n; i++) {
    out->sutun_n = std::max(out->sutun_n, out->d[i].sutun + 1);
    out->satir_n = std::max(out->satir_n, out->d[i].satir + 1);
  }
}

uint32_t animator_gecis_metni(const sim::AnimatorDef &def, uint32_t i, char *out, uint32_t cap) {
  if (!out || !cap) return 0;
  out[0] = 0;
  if (i >= def.trans_count) return 0;
  const sim::AnimTransition &t = def.trans[i];
  const char *pad = (t.param >= 0 && (uint32_t)t.param < def.param_count) ? def.params[t.param].name : "?";
  int n = 0;
  if (t.on_end) n = std::snprintf(out, cap, "bitince");
  else if (t.trigger) n = std::snprintf(out, cap, "tetik %s", pad);
  else {
    static const char *const kOp[] = {"?", ">", "<", ">=", "<=", "=="};
    n = std::snprintf(out, cap, "%s %s %g", pad, kOp[(uint32_t)t.op < 6 ? (uint32_t)t.op : 0], (double)t.value);
  }
  if (n < 0) n = 0;
  if ((uint32_t)n < cap) n += std::snprintf(out + n, cap - (uint32_t)n, " · %g s", (double)t.fade);
  return (uint32_t)std::min<int>(n, (int)cap - 1);
}

int animator_tanim_satiri(const char *metin, const char *anahtar, const char *ad, int sira) {
  if (!metin || !anahtar) return 0;
  const size_t al = std::strlen(anahtar), dl = ad ? std::strlen(ad) : 0;
  int satir = 1, say = 0;
  for (const char *p = metin; *p;) {
    const char *s = p;
    while (*s == ' ' || *s == '\t') s++;
    if (!std::strncmp(s, anahtar, al) && (s[al] == ' ' || s[al] == '\t')) {
      const char *a = s + al;
      while (*a == ' ' || *a == '\t') a++;
      if (ad) {
        if (!std::strncmp(a, ad, dl) && (a[dl] == ' ' || a[dl] == '\t' || a[dl] == '\n' || a[dl] == '\r' || a[dl] == '#' || !a[dl])) return satir;
      } else if (say++ == sira) {
        return satir;
      }
    }
    const char *nl = std::strchr(p, '\n');
    if (!nl) break;
    p = nl + 1;
    satir++;
  }
  return 0;
}

namespace {

// Son ayristirma (metin ozeti degismedikce tekrar yok). Hatada son GECERLI tanim tutulur.
struct Onbellek {
  uint64_t ozet = 0;
  bool dolu = false;
  bool gecerli_var = false;
  bool hatali = false;
  sim::AnimatorDef def;
  AnimGrafik graf;
  sim::AnimatorError er;
};
Onbellek g_ob;
int g_sekme = 0;   // 0 grafik, 1 tablo
const char *g_etkin = nullptr;   // A8: bu cizimde vurgulanacak durum adi (animator_grafik_ciz suresince)
bool g_duzen = false;            // A8b: bu cizimde grafik duzenleyebilir (sag tik menuleri)
int32_t g_sag_dugum = -1, g_sag_kenar = -1;   // sag tik: menu acilacak durum / gecis (bir sonraki adimda OpenPopup)

uint64_t fnv(const char *s) {
  uint64_t h = 1469598103934665603ull;
  for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
  return h;
}

void yenile(const char *metin) {
  const uint64_t oz = fnv(metin);
  if (g_ob.dolu && oz == g_ob.ozet) return;
  g_ob.ozet = oz;
  g_ob.dolu = true;
  static sim::AnimatorDef yeni;
  yeni = sim::AnimatorDef{};
  g_ob.er = sim::AnimatorError{};
  if (sim::animator_parse(metin, std::strlen(metin), &yeni, &g_ob.er)) {
    g_ob.def = yeni;
    animator_grafik_kur(g_ob.def, &g_ob.graf);
    g_ob.gecerli_var = true;
    g_ob.hatali = false;
  } else {
    g_ob.hatali = true;
  }
}

ImU32 renk(Tone t, float alfa = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], c[3] * alfa));
}

// Kutu kenarinda, merkezden hedefe dogru cikis noktasi.
ImVec2 kenar(ImVec2 m, ImVec2 yari, ImVec2 yon) {
  const float ax = std::fabs(yon.x), ay = std::fabs(yon.y);
  float k = 1e9f;
  if (ax > 1e-5f) k = std::min(k, yari.x / ax);
  if (ay > 1e-5f) k = std::min(k, yari.y / ay);
  if (k > 1e8f) k = 0;
  return ImVec2(m.x + yon.x * k, m.y + yon.y * k);
}

void ok_ucu(ImDrawList *dl, ImVec2 uc, ImVec2 yon, ImU32 c, float boy) {
  const ImVec2 dik(-yon.y, yon.x);
  dl->AddTriangleFilled(uc, ImVec2(uc.x - yon.x * boy + dik.x * boy * 0.5f, uc.y - yon.y * boy + dik.y * boy * 0.5f),
                        ImVec2(uc.x - yon.x * boy - dik.x * boy * 0.5f, uc.y - yon.y * boy - dik.y * boy * 0.5f), c);
}

void durum_ozeti(const sim::AnimatorDef &d, const sim::AnimState &s, char *out, size_t cap) {
  int n = 0;
  if (s.blend) {
    const char *pad = (s.param >= 0 && (uint32_t)s.param < d.param_count) ? d.params[s.param].name : "?";
    n = std::snprintf(out, cap, "karışım (%s):", pad);
    for (uint32_t k = 0; k < s.point_count && n > 0 && (size_t)n < cap; k++)
      n += std::snprintf(out + n, cap - (size_t)n, " %g→%s", (double)s.points[k].threshold, s.points[k].clip_name);
  } else {
    n = std::snprintf(out, cap, "klip %s", s.point_count ? s.points[0].clip_name : "?");
  }
  if (n > 0 && (size_t)n < cap) n += std::snprintf(out + n, cap - (size_t)n, "%s%s", s.loop ? " · döngü" : " · tek", s.root_motion ? " · kök" : "");
}

bool grafik_ciz(const char *metin, int *git_satir) {
  const sim::AnimatorDef &d = g_ob.def;
  const AnimGrafik &g = g_ob.graf;
  bool git = false;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float fs = ImGui::GetFontSize();
  const float alfa = g_ob.hatali ? 0.45f : 1.0f;
  // Kutu boyu: en uzun ad + simge. Sutun araligi koşul etiketine yer birakir.
  float kutu_w = fs * 6.0f;
  for (uint32_t i = 0; i < d.state_count; i++) kutu_w = std::max(kutu_w, ImGui::CalcTextSize(d.states[i].name).x + fs * 2.6f);
  const float kutu_h = fs * 2.2f, ara_x = fs * 9.0f, ara_y = fs * 2.6f, kenar_bosluk = fs * 0.8f;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const ImVec2 icerik(kenar_bosluk * 2 + g.sutun_n * kutu_w + (g.sutun_n ? g.sutun_n - 1 : 0) * ara_x,
                      kenar_bosluk * 2 + g.satir_n * kutu_h + (g.satir_n ? g.satir_n - 1 : 0) * ara_y);
  ImVec2 merkez[kAnimGrafikMaxDugum];
  for (uint32_t i = 0; i < g.n; i++)
    merkez[i] = ImVec2(o.x + kenar_bosluk + g.d[i].sutun * (kutu_w + ara_x) + kutu_w * 0.5f, o.y + kenar_bosluk + g.d[i].satir * (kutu_h + ara_y) + kutu_h * 0.5f);
  const ImVec2 yari(kutu_w * 0.5f, kutu_h * 0.5f);
  const ImVec2 fare = ImGui::GetIO().MousePos;
  const bool pencere_ustu = ImGui::IsWindowHovered();
  const bool tik = pencere_ustu && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  // --- Gecisler: once geometri + etiket yeri (cakismasin), sonra cizgiler, en son etiketler (cizgi etiketin ustune binmesin) ---
  const ImU32 cizgi = renk(Tone::TextDim, alfa), vurgu = renk(Tone::Accent, alfa);
  struct Kenar { bool var, ilmek; ImVec2 p0, p1, y, e0, e1; char et[96]; };
  static Kenar k[sim::kAnimMaxTrans];
  ImVec2 dolu0[sim::kAnimMaxTrans + kAnimGrafikMaxDugum], dolu1[sim::kAnimMaxTrans + kAnimGrafikMaxDugum];
  uint32_t dolu_n = 0;
  for (uint32_t i = 0; i < g.n; i++) { dolu0[dolu_n] = ImVec2(merkez[i].x - yari.x, merkez[i].y - yari.y); dolu1[dolu_n++] = ImVec2(merkez[i].x + yari.x, merkez[i].y + yari.y); }
  auto ortusme = [&](ImVec2 a0, ImVec2 a1) {   // dolu dikdortgenlerle kesisim alani (0 = bos yer)
    float toplam = 0;
    for (uint32_t i = 0; i < dolu_n; i++) {
      const float w = std::min(a1.x, dolu1[i].x) - std::max(a0.x, dolu0[i].x), hh = std::min(a1.y, dolu1[i].y) - std::max(a0.y, dolu0[i].y);
      if (w > 0 && hh > 0) toplam += w * hh;
    }
    return toplam;
  };
  int32_t ustteki = -1;
  for (uint32_t t = 0; t < d.trans_count; t++) {
    Kenar &e = k[t];
    e.var = false;
    const sim::AnimTransition &tr = d.trans[t];
    const int32_t a = tr.from < 0 ? g.herhangi : g.dugum_of[tr.from];
    const int32_t b = (tr.to >= 0 && (uint32_t)tr.to < d.state_count) ? g.dugum_of[tr.to] : -1;
    if (a < 0 || b < 0) continue;
    e.var = true;
    animator_gecis_metni(d, t, e.et, sizeof e.et);
    const ImVec2 esz = ImGui::CalcTextSize(e.et);
    const ImVec2 h(esz.x * 0.5f + 3, esz.y * 0.5f + 1);
    e.ilmek = a == b;
    ImVec2 em;
    if (e.ilmek) {   // kendine gecis: kutunun ustunde kucuk ilmek (calisma zamaninda etkisiz: animator_update atlar)
      const ImVec2 m = merkez[a];
      e.p0 = ImVec2(m.x - yari.x * 0.4f, m.y - yari.y);
      e.p1 = ImVec2(m.x + yari.x * 0.4f, m.y - yari.y);
      em = ImVec2(m.x, m.y - yari.y - fs * 2.0f);
    } else {
      const ImVec2 ma = merkez[a], mb = merkez[b];
      ImVec2 y(mb.x - ma.x, mb.y - ma.y);
      const float l = std::sqrt(y.x * y.x + y.y * y.y);
      e.y = y = ImVec2(y.x / l, y.y / l);
      // Karsi yonde de gecis varsa iki ok yan yana: dik yonde kaydir (ust uste binmesin).
      bool karsi = false;
      for (uint32_t u = 0; u < d.trans_count; u++)
        if (d.trans[u].from == tr.to && d.trans[u].to == tr.from) karsi = true;
      const ImVec2 dik(-y.y, y.x);
      const float kay = karsi ? fs * 0.35f : 0.0f;
      e.p0 = kenar(ImVec2(ma.x + dik.x * kay, ma.y + dik.y * kay), yari, y);
      e.p1 = kenar(ImVec2(mb.x + dik.x * kay, mb.y + dik.y * kay), yari, ImVec2(-y.x, -y.y));
      // Etiket: cizginin yaninda. Aday yerler: cizgi boyunca (0.5, 0.3, 0.7, ...) x iki yan x iki uzaklik; ilk BOS aday,
      // hic yoksa en az ortusen. (Tek aday + "dolu ise ilkine don" denendi: kisa capraz kenarda hepsi doluydu, etiketler ust uste.)
      static const float kT[] = {0.5f, 0.3f, 0.7f, 0.2f, 0.8f, 0.4f, 0.6f};
      float en_az = 1e30f;
      em = ImVec2(0, 0);
      for (int uzak = 0; uzak < 2 && en_az > 0; uzak++)
        for (float tt : kT) {
          for (int yon = 0; yon < 2; yon++) {
            // once kaydirilan yan (karsi gecisin cizgisinden uzak); obur yanda karsi cizginin de otesine
            const float yan = yon == 0 ? (h.y + 2.0f + uzak * h.y * 2.2f) : -(kay * 2 + h.y + 2.0f + uzak * h.y * 2.2f);
            const ImVec2 c(e.p0.x + (e.p1.x - e.p0.x) * tt + dik.x * yan, e.p0.y + (e.p1.y - e.p0.y) * tt + dik.y * yan);
            const float o2 = ortusme(ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y));
            if (o2 < en_az) { en_az = o2; em = c; }
            if (en_az <= 0) break;
          }
          if (en_az <= 0) break;
        }
    }
    e.e0 = ImVec2(em.x - h.x, em.y - h.y);
    e.e1 = ImVec2(em.x + h.x, em.y + h.y);
    dolu0[dolu_n] = e.e0; dolu1[dolu_n++] = e.e1;
    if (pencere_ustu && fare.x >= e.e0.x && fare.x <= e.e1.x && fare.y >= e.e0.y && fare.y <= e.e1.y) ustteki = (int32_t)t;
  }
  for (uint32_t t = 0; t < d.trans_count; t++) {
    const Kenar &e = k[t];
    if (!e.var) continue;
    const bool u = (int32_t)t == ustteki;
    const ImU32 c = u ? vurgu : cizgi;
    if (e.ilmek) {
      dl->AddBezierCubic(e.p0, ImVec2(e.p0.x, e.p0.y - fs * 2), ImVec2(e.p1.x, e.p1.y - fs * 2), e.p1, c, u ? 2.5f : 1.5f);
      ok_ucu(dl, e.p1, ImVec2(0, 1), c, fs * 0.5f);
    } else {
      dl->AddLine(e.p0, e.p1, c, u ? 2.5f : 1.5f);
      ok_ucu(dl, e.p1, e.y, c, fs * 0.55f);
    }
  }
  for (uint32_t t = 0; t < d.trans_count; t++) {
    const Kenar &e = k[t];
    if (!e.var) continue;
    const bool u = (int32_t)t == ustteki;
    dl->AddRectFilled(e.e0, e.e1, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.92f), 3.0f);
    if (u) dl->AddRect(e.e0, e.e1, vurgu, 3.0f);
    dl->AddText(ImVec2(e.e0.x + 3, e.e0.y + 1), renk(u ? Tone::Accent : Tone::Text, alfa), e.et);
  }
  if (ustteki >= 0) {
    const sim::AnimTransition &tr = d.trans[ustteki];
    ImGui::SetTooltip("%s → %s\n%s\n(tıkla: satıra git%s)", tr.from < 0 ? "herhangi" : d.states[tr.from].name, d.states[tr.to].name, k[ustteki].et,
                      g_duzen ? " · sağ tık: sil" : "");
    if (tik) { *git_satir = animator_tanim_satiri(metin, "gecis", nullptr, ustteki); git = *git_satir > 0; }
    if (g_duzen && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) g_sag_kenar = ustteki;
  }
  // --- Dugumler ---
  for (uint32_t i = 0; i < g.n; i++) {
    const AnimGrafikDugum &n = g.d[i];
    const ImVec2 p0(merkez[i].x - yari.x, merkez[i].y - yari.y), p1(merkez[i].x + yari.x, merkez[i].y + yari.y);
    const bool ustunde = pencere_ustu && fare.x >= p0.x && fare.x <= p1.x && fare.y >= p0.y && fare.y <= p1.y;
    const bool baslangic = n.durum >= 0 && n.durum == d.start;
    const bool etkin = n.durum >= 0 && g_etkin && !std::strcmp(g_etkin, d.states[n.durum].name);   // A8: oyunda/önizlemede şu anki durum
    const ImU32 zemin = n.durum < 0 ? ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f * alfa)
                        : etkin     ? renk(Tone::Ok, 0.35f * alfa)
                                    : ImGui::GetColorU32(ImGuiCol_FrameBgHovered, alfa);
    dl->AddRectFilled(p0, p1, zemin, fs * 0.35f);
    if (etkin) dl->AddRect(ImVec2(p0.x - 3, p0.y - 3), ImVec2(p1.x + 3, p1.y + 3), renk(Tone::Ok, alfa), fs * 0.45f, 2.0f);
    const ImU32 cerceve = ustunde ? renk(Tone::Accent, alfa) : baslangic ? renk(Tone::Ok, alfa) : n.ulasilamaz ? renk(Tone::Warn, alfa) : renk(Tone::TextDim, alfa);
    dl->AddRect(p0, p1, cerceve, fs * 0.35f, (ustunde || baslangic) ? 2.5f : 1.2f);
    char ad[64];
    if (n.durum < 0) std::snprintf(ad, sizeof ad, "herhangi");
    else std::snprintf(ad, sizeof ad, "%s%s", baslangic ? ICON_MD_PLAY_ARROW " " : (d.states[n.durum].blend ? ICON_MD_TUNE " " : ""), d.states[n.durum].name);
    const ImVec2 asz = ImGui::CalcTextSize(ad);
    dl->AddText(ImVec2(merkez[i].x - asz.x * 0.5f, merkez[i].y - asz.y * 0.5f), renk(n.durum < 0 ? Tone::TextDim : Tone::Text, alfa), ad);
    if (ustunde && n.durum >= 0) {
      char oz[256];
      durum_ozeti(d, d.states[n.durum], oz, sizeof oz);
      ImGui::SetTooltip("%s%s\n%s%s\n(tıkla: satıra git%s)", d.states[n.durum].name, baslangic ? " (başlangıç)" : "", oz,
                        n.ulasilamaz ? "\nUYARI: hiçbir geçiş bu duruma girmiyor" : "", g_duzen ? " · sağ tık: başlangıç yap / klip / sil" : "");
      if (tik) { *git_satir = animator_tanim_satiri(metin, "durum", d.states[n.durum].name, 0); git = *git_satir > 0; }
      if (g_duzen && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) g_sag_dugum = n.durum;
    } else if (ustunde) {
      ImGui::SetTooltip("herhangi: bu geçişler her durumdan alınır");
    }
  }
  ImGui::Dummy(icerik);
  return git;
}

bool tablo_ciz(const char *metin, int *git_satir) {
  const sim::AnimatorDef &d = g_ob.def;
  bool git = false;
  if (ImGui::BeginTable("##anim_gecis", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Kaynak");
    ImGui::TableSetupColumn("Hedef");
    ImGui::TableSetupColumn("Koşul");
    ImGui::TableSetupColumn("Durum içeriği");
    ImGui::TableHeadersRow();
    for (uint32_t t = 0; t < d.trans_count; t++) {
      const sim::AnimTransition &tr = d.trans[t];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      char sat[64];
      std::snprintf(sat, sizeof sat, "%s##gt%u", tr.from < 0 ? "herhangi" : d.states[tr.from].name, t);
      if (ImGui::Selectable(sat, false, ImGuiSelectableFlags_SpanAllColumns)) {
        *git_satir = animator_tanim_satiri(metin, "gecis", nullptr, (int)t);
        git = *git_satir > 0;
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(d.states[tr.to].name);
      ImGui::TableNextColumn();
      char et[96];
      animator_gecis_metni(d, t, et, sizeof et);
      ImGui::TextUnformatted(et);
      ImGui::TableNextColumn();
      char oz[256];
      durum_ozeti(d, d.states[tr.to], oz, sizeof oz);
      ImGui::TextDisabled("%s", oz);
    }
    ImGui::EndTable();
  }
  return git;
}

} // namespace

namespace {
char g_canli_yol[1024] = {0};
char g_canli_durum[sim::kAnimNameLen] = {0};
uint32_t g_dosya_nesli = 0;
// A8b: klip listesi (editor verir) ve geri al halkasi (dosyaya bagli; animator dosyalari kucuk: 16 KB'tan buyuk metin halkaya girmez,
// "Geri al" o islem icin kapali kalir — sessiz degil, dugme ipucu soyler).
char g_klip_yol[1024] = {0};
char g_klip_ad[64][64];
uint32_t g_klip_n = 0;
constexpr uint32_t kGeriN = 8, kGeriBoy = 16 * 1024;
char g_geri_yol[1024] = {0};
char g_geri[kGeriN][kGeriBoy];
uint32_t g_geri_bas = 0, g_geri_n = 0;
char g_islem_hata[200] = {0};
// Acilir pencere alanlari
char g_d_ad[32] = {0}, g_d_klip[64] = {0};
int g_d_klip_sec = 0;
bool g_d_dongu = true;
char g_d_duzenle[32] = {0};   // dolu: "Klibi degistir" kipi (bu durumun klibi)
int g_g_kaynak = 0, g_g_hedef = 0, g_g_kosul = 0, g_g_param = 0;
char g_g_tetik[32] = "zipla";
float g_g_deger = 0.5f, g_g_sure = 0.2f;
bool klip_listesi_var(const char *yol) { return yol && g_klip_n && !std::strcmp(yol, g_klip_yol); }

// Islem uygula: once metni geri al halkasina koy, sonra donusum; basarisizsa halka geri sarilir, metin degismemistir.
template <class F> void uygula(AnimGrafikBaglam *b, const char *aciklama, F &&islem) {
  g_islem_hata[0] = 0;
  if (!b->yol || std::strcmp(b->yol, g_geri_yol)) { std::snprintf(g_geri_yol, sizeof g_geri_yol, "%s", b->yol ? b->yol : ""); g_geri_n = 0; }
  const size_t n = std::strlen(b->metin_yaz);
  const bool kayit = n + 1 <= kGeriBoy;
  const uint32_t yuva = (g_geri_bas + g_geri_n) % kGeriN;
  if (kayit) std::memcpy(g_geri[yuva], b->metin_yaz, n + 1);
  char err[200];
  if (islem(err, (uint32_t)sizeof err)) {
    if (kayit) { if (g_geri_n < kGeriN) g_geri_n++; else g_geri_bas = (g_geri_bas + 1) % kGeriN; }
    b->degisti = true;
    std::snprintf(b->mesaj, sizeof b->mesaj, "%s", aciklama);
  } else {
    std::snprintf(g_islem_hata, sizeof g_islem_hata, "%s", err);
  }
}

// "+ Durum" / "Klibi degistir" penceresi: ad, klip (modelin listesinden; liste yoksa elle), dongu.
void durum_penceresi(AnimGrafikBaglam *b) {
  if (!ImGui::BeginPopup("##anim_durum_ekle")) return;
  const bool duzenle = g_d_duzenle[0] != 0;
  ImGui::TextUnformatted(duzenle ? "Klibi değiştir" : "Yeni durum");
  ImGui::Separator();
  if (duzenle) ImGui::Text("Durum: %s", g_d_duzenle);
  else ImGui::InputText("Ad##anim_d_ad", g_d_ad, sizeof g_d_ad, ImGuiInputTextFlags_CharsNoBlank);
  char jeton[40] = {0};
  if (klip_listesi_var(b->yol)) {
    const char *on = g_d_klip_sec >= 0 && (uint32_t)g_d_klip_sec < g_klip_n ? g_klip_ad[g_d_klip_sec] : "";
    if (ImGui::BeginCombo("Klip##anim_d_klip", on)) {
      for (uint32_t c = 0; c < g_klip_n; c++) {
        char et[96];
        std::snprintf(et, sizeof et, "%u: %s", c, g_klip_ad[c]);
        if (ImGui::Selectable(et, (int)c == g_d_klip_sec)) g_d_klip_sec = (int)c;
      }
      ImGui::EndCombo();
    }
    if (g_d_klip_sec >= 0 && (uint32_t)g_d_klip_sec < g_klip_n) anim_klip_jetonu(g_klip_ad[g_d_klip_sec], (uint32_t)g_d_klip_sec, jeton, sizeof jeton);
  } else {
    ImGui::InputTextWithHint("Klip##anim_d_klip_el", "klip adı ya da #0", g_d_klip, sizeof g_d_klip, ImGuiInputTextFlags_CharsNoBlank);
    std::snprintf(jeton, sizeof jeton, "%s", g_d_klip);
    ImGui::TextDisabled("(bu dosyayı kullanan model sahnede yok: klip adını yaz)");
  }
  ImGui::Checkbox("Döngü (kapalıysa bir kez oynar, sonda durur)##anim_d_dongu", &g_d_dongu);
  if (ImGui::Button(duzenle ? "Klibi uygula" : "Durumu ekle")) {
    char ad[32];
    std::snprintf(ad, sizeof ad, "%s", duzenle ? g_d_duzenle : g_d_ad);
    char ac[120];
    std::snprintf(ac, sizeof ac, duzenle ? "durum '%s' klibi: %s" : "durum eklendi: %s (%s)", ad, jeton);
    uygula(b, ac, [&](char *err, uint32_t ecap) {
      return duzenle ? anim_metin_durum_klip(b->metin_yaz, b->cap, ad, jeton, g_d_dongu, err, ecap)
                     : anim_metin_durum_ekle(b->metin_yaz, b->cap, ad, jeton, g_d_dongu, err, ecap);
    });
    if (!g_islem_hata[0]) ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Vazgeç##anim_d")) ImGui::CloseCurrentPopup();
  if (g_islem_hata[0]) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Err)), "%s", g_islem_hata);
  ImGui::EndPopup();
}

// "+ Gecis" penceresi: kaynak (herhangi dahil), hedef, kosul turu, parametre/deger ya da tetik adi, sure.
void gecis_penceresi(AnimGrafikBaglam *b) {
  if (!ImGui::BeginPopup("##anim_gecis_ekle")) return;
  const sim::AnimatorDef &d = g_ob.def;
  ImGui::TextUnformatted("Yeni geçiş");
  ImGui::Separator();
  // Kaynak: 0 = herhangi, 1.. = durumlar
  static char kaynaklar[(sim::kAnimMaxStates + 1) * sim::kAnimNameLen + 64], hedefler[sim::kAnimMaxStates * sim::kAnimNameLen + 8];
  size_t kn = (size_t)std::snprintf(kaynaklar, sizeof kaynaklar, "herhangi (her durumdan)") + 1, hn = 0;
  for (uint32_t i = 0; i < d.state_count; i++) {
    kn += (size_t)std::snprintf(kaynaklar + kn, sizeof kaynaklar - kn - 1, "%s", d.states[i].name) + 1;
    hn += (size_t)std::snprintf(hedefler + hn, sizeof hedefler - hn - 1, "%s", d.states[i].name) + 1;
  }
  kaynaklar[kn] = 0; hedefler[hn] = 0;
  if (g_g_kaynak > (int)d.state_count) g_g_kaynak = 0;
  if (g_g_hedef >= (int)d.state_count) g_g_hedef = 0;
  ImGui::Combo("Nereden##anim_g_k", &g_g_kaynak, kaynaklar);
  ImGui::Combo("Nereye##anim_g_h", &g_g_hedef, hedefler);
  ImGui::Combo("Ne zaman##anim_g_kosul", &g_g_kosul, "hız şundan büyükse\0hız şundan küçükse\0tetik gelince (betikten)\0klip bitince\0");
  // Sayi parametreleri: tanimdakiler + otomatik olanlar (hiz, dikey) — yoksa eklenir.
  const char *sayilar[sim::kAnimMaxParams + 2];
  uint32_t sn = 0;
  for (uint32_t i = 0; i < d.param_count; i++) if (!d.params[i].trigger) sayilar[sn++] = d.params[i].name;
  auto ekle_yoksa = [&](const char *a) { for (uint32_t i = 0; i < sn; i++) if (!std::strcmp(sayilar[i], a)) return; sayilar[sn++] = a; };
  ekle_yoksa(sim::kAnimAutoSpeed);
  ekle_yoksa(sim::kAnimAutoVertical);
  if (g_g_param >= (int)sn) g_g_param = 0;
  if (g_g_kosul <= 1) {
    if (ImGui::BeginCombo("Değer##anim_g_p", sayilar[g_g_param])) {
      for (uint32_t i = 0; i < sn; i++) if (ImGui::Selectable(sayilar[i], (int)i == g_g_param)) g_g_param = (int)i;
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("hiz / dikey oyunda nesnenin hareketinden kendiliğinden gelir (m/s)");
    ImGui::DragFloat("Eşik##anim_g_v", &g_g_deger, 0.05f, -50.0f, 50.0f, "%.2f");
  } else if (g_g_kosul == 2) {
    ImGui::InputText("Tetik adı##anim_g_t", g_g_tetik, sizeof g_g_tetik, ImGuiInputTextFlags_CharsNoBlank);
    ImGui::TextDisabled("Betikte: sahne_animator_tetik(sahne_bul(\"nesne\"), \"%s\")", g_g_tetik);
  }
  ImGui::DragFloat("Geçiş süresi##anim_g_s", &g_g_sure, 0.01f, 0.0f, 2.0f, "%.2f s");
  if (ImGui::Button("Geçişi ekle") && d.state_count) {
    AnimGecisIstek g;
    std::snprintf(g.kaynak, sizeof g.kaynak, "%s", g_g_kaynak == 0 ? "herhangi" : d.states[g_g_kaynak - 1].name);
    std::snprintf(g.hedef, sizeof g.hedef, "%s", d.states[g_g_hedef].name);
    g.kosul = (AnimKosul)g_g_kosul;
    std::snprintf(g.param, sizeof g.param, "%s", g_g_kosul <= 1 ? sayilar[g_g_param] : g_g_tetik);
    g.deger = g_g_deger;
    g.sure = g_g_sure;
    char ac[120];
    std::snprintf(ac, sizeof ac, "geçiş eklendi: %s → %s", g.kaynak, g.hedef);
    uygula(b, ac, [&](char *err, uint32_t ecap) { return anim_metin_gecis_ekle(b->metin_yaz, b->cap, g, err, ecap); });
    if (!g_islem_hata[0]) ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Vazgeç##anim_g")) ImGui::CloseCurrentPopup();
  if (g_islem_hata[0]) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Err)), "%s", g_islem_hata);
  ImGui::EndPopup();
}

// Tek seferlik klip adi mi (saldiri, olum, zipla...): yeni durum varsayilan "Tek" gelir.
bool tek_seferlik(const char *ad) {
  char k[64];
  uint32_t i = 0;
  for (; ad[i] && i + 1 < sizeof k; i++) k[i] = (ad[i] >= 'A' && ad[i] <= 'Z') ? (char)(ad[i] - 'A' + 'a') : ad[i];
  k[i] = 0;
  static const char *const kAlt[] = {"saldir", "vur", "attack", "hit", "die", "death", "dead", "olum", "jump", "zipla", "zafer", "win",
                                     "punch", "kick", "shoot", "ates", nullptr};
  for (uint32_t j = 0; kAlt[j]; j++) if (std::strstr(k, kAlt[j])) return true;
  return !std::strcmp(k, "ol");
}

void durum_penceresi_ac(const char *metin, const char *duzenle_ad) {
  g_islem_hata[0] = 0;
  std::snprintf(g_d_duzenle, sizeof g_d_duzenle, "%s", duzenle_ad ? duzenle_ad : "");
  g_d_dongu = true;
  if (!duzenle_ad) {
    // Varsayilan: animatorde HENUZ KULLANILMAYAN ilk klip ve onun adi (gecerli ad ise) — en sik durumda tek tik "Durumu ekle".
    anim_yeni_ad(metin, "durum", g_d_ad, sizeof g_d_ad);
    g_d_klip_sec = 0;
    for (uint32_t c = 0; c < g_klip_n; c++) {
      char j[40];
      anim_klip_jetonu(g_klip_ad[c], c, j, sizeof j);
      bool kullaniliyor = false;
      for (uint32_t i = 0; i < g_ob.def.state_count && !kullaniliyor; i++)
        for (uint32_t k = 0; k < g_ob.def.states[i].point_count; k++)
          if (!std::strcmp(g_ob.def.states[i].points[k].clip_name, j)) kullaniliyor = true;
      if (kullaniliyor) continue;
      g_d_klip_sec = (int)c;
      if (anim_ad_gecerli(g_klip_ad[c])) anim_yeni_ad(metin, g_klip_ad[c], g_d_ad, sizeof g_d_ad);
      g_d_dongu = !tek_seferlik(g_klip_ad[c]);
      break;
    }
  }
  ImGui::OpenPopup("##anim_durum_ekle");
}
void gecis_penceresi_ac(int kaynak) {
  g_islem_hata[0] = 0;
  g_g_kaynak = kaynak;
  ImGui::OpenPopup("##anim_gecis_ekle");
}

// Arac cubugu + sag tik menuleri + pencereler (yalniz duzenlenebilirken).
void duzen_arayuzu(const char *metin, AnimGrafikBaglam *b) {
  if (ImGui::Button(ICON_MD_ADD " Durum ekle##anim_arac")) durum_penceresi_ac(metin, nullptr);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Yeni durum: bir klip oynatır (ör. saldır, düş)");
  ImGui::SameLine();
  ImGui::BeginDisabled(!g_ob.gecerli_var || g_ob.def.state_count < 1);
  if (ImGui::Button(ICON_MD_ARROW_FORWARD " Geçiş ekle##anim_arac")) gecis_penceresi_ac(0);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("İki durum arasında geçiş: hıza göre, tetikle ya da klip bitince");
  ImGui::SameLine();
  const bool geri_var = g_geri_n > 0 && b->yol && !std::strcmp(b->yol, g_geri_yol);
  ImGui::BeginDisabled(!geri_var);
  if (ImGui::Button(ICON_MD_UNDO " Geri al##anim_arac")) {
    const uint32_t yuva = (g_geri_bas + g_geri_n - 1) % kGeriN;
    const size_t n = std::strlen(g_geri[yuva]);
    if (n + 1 <= b->cap) {
      std::memcpy(b->metin_yaz, g_geri[yuva], n + 1);
      g_geri_n--;
      b->degisti = true;
      std::snprintf(b->mesaj, sizeof b->mesaj, "grafikteki son değişiklik geri alındı");
    }
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(geri_var ? "Grafikten yapılan son değişikliği geri alır (%u adım)" : "Grafikten yapılmış değişiklik yok", g_geri_n);
  if (g_islem_hata[0] && !ImGui::IsPopupOpen("##anim_durum_ekle") && !ImGui::IsPopupOpen("##anim_gecis_ekle")) {
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Err)), ICON_MD_ERROR " %s", g_islem_hata);
  }
}

// Sag tik menuleri: cizimden SONRA (grafik_ciz g_sag_* isaretler).
void sag_tik_menuleri(const char *metin, AnimGrafikBaglam *b) {
  const sim::AnimatorDef &d = g_ob.def;
  if (g_sag_dugum >= 0) { ImGui::OpenPopup("##anim_dugum"); }
  static int32_t dugum = -1, kenar = -1;
  if (g_sag_dugum >= 0) { dugum = g_sag_dugum; g_sag_dugum = -1; }
  if (g_sag_kenar >= 0) { kenar = g_sag_kenar; g_sag_kenar = -1; ImGui::OpenPopup("##anim_kenar"); }
  bool durum_ac = false, gecis_ac = false;
  if (ImGui::BeginPopup("##anim_dugum")) {
    if (dugum >= 0 && (uint32_t)dugum < d.state_count) {
      const char *ad = d.states[dugum].name;
      ImGui::TextDisabled("%s", ad);
      ImGui::Separator();
      if (ImGui::MenuItem("Başlangıç yap", nullptr, false, d.start != dugum)) {
        char ac[96];
        std::snprintf(ac, sizeof ac, "başlangıç: %s", ad);
        uygula(b, ac, [&](char *err, uint32_t ecap) { return anim_metin_baslangic(b->metin_yaz, b->cap, ad, err, ecap); });
      }
      if (ImGui::MenuItem("Buradan geçiş ekle…")) gecis_ac = true;
      if (ImGui::MenuItem("Klibi değiştir…", nullptr, false, !d.states[dugum].blend)) durum_ac = true;
      if (ImGui::MenuItem("Sil (geçişleriyle)", nullptr, false, d.state_count > 1)) {
        char ac[96];
        std::snprintf(ac, sizeof ac, "durum silindi: %s", ad);
        uygula(b, ac, [&](char *err, uint32_t ecap) { return anim_metin_durum_sil(b->metin_yaz, b->cap, ad, err, ecap); });
      }
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("##anim_kenar")) {
    if (kenar >= 0 && (uint32_t)kenar < d.trans_count) {
      char et[96];
      animator_gecis_metni(d, (uint32_t)kenar, et, sizeof et);
      ImGui::TextDisabled("%s → %s · %s", d.trans[kenar].from < 0 ? "herhangi" : d.states[d.trans[kenar].from].name, d.states[d.trans[kenar].to].name, et);
      ImGui::Separator();
      if (ImGui::MenuItem("Geçişi sil")) {
        const uint32_t sira = (uint32_t)kenar;
        uygula(b, "geçiş silindi", [&](char *err, uint32_t ecap) { return anim_metin_gecis_sil(b->metin_yaz, b->cap, sira, err, ecap); });
      }
    }
    ImGui::EndPopup();
  }
  // Menuden acilan pencereler: menu kapandiktan SONRA (ic ice acilir pencere degil)
  if (durum_ac && dugum >= 0) {
    durum_penceresi_ac(metin, d.states[dugum].name);
    const int32_t c = d.states[dugum].points[0].clip;
    g_d_klip_sec = c >= 0 ? c : 0;
    g_d_dongu = d.states[dugum].loop;
  }
  if (gecis_ac && dugum >= 0) gecis_penceresi_ac(dugum + 1);
  durum_penceresi(b);
  gecis_penceresi(b);
}
} // namespace

void animator_canli_ayarla(const char *tam_yol, const char *durum) {
  if (!tam_yol || !durum || !durum[0]) { g_canli_yol[0] = 0; g_canli_durum[0] = 0; return; }
  std::snprintf(g_canli_yol, sizeof g_canli_yol, "%s", tam_yol);
  std::snprintf(g_canli_durum, sizeof g_canli_durum, "%s", durum);
}
const char *animator_canli_durum(const char *tam_yol) {
  return (tam_yol && g_canli_yol[0] && !std::strcmp(tam_yol, g_canli_yol)) ? g_canli_durum : nullptr;
}
void animator_dosya_kaydedildi() { g_dosya_nesli++; }
uint32_t animator_dosya_nesli() { return g_dosya_nesli; }

void animator_canli_klipler(const char *tam_yol, const char *const *adlar, uint32_t n) {
  if (!tam_yol || !adlar || !n) { g_klip_n = 0; g_klip_yol[0] = 0; return; }
  std::snprintf(g_klip_yol, sizeof g_klip_yol, "%s", tam_yol);
  g_klip_n = n < 64 ? n : 64;
  for (uint32_t i = 0; i < g_klip_n; i++) std::snprintf(g_klip_ad[i], sizeof g_klip_ad[i], "%s", adlar[i] ? adlar[i] : "");
}

bool animator_grafik_ciz(const char *metin, float yukseklik, int *git_satir, AnimGrafikBaglam *b) {
  if (!metin || !git_satir) return false;
  g_etkin = b ? b->etkin_durum : nullptr;
  g_duzen = b && b->metin_yaz && b->cap;
  yenile(metin);
  bool git = false;
  ImGui::BeginChild("##anim_grafik", ImVec2(-FLT_MIN, yukseklik), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  if (ImGui::RadioButton("Grafik", g_sekme == 0)) g_sekme = 0;
  ImGui::SameLine();
  if (ImGui::RadioButton("Tablo", g_sekme == 1)) g_sekme = 1;
  ImGui::SameLine();
  if (g_ob.gecerli_var) {
    const sim::AnimatorDef &d = g_ob.def;
    ImGui::TextDisabled("%u durum · %u geçiş · %u parametre", d.state_count, d.trans_count, d.param_count);
    if (g_ob.graf.ulasilamaz_n) {
      ImGui::SameLine();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Warn)), ICON_MD_WARNING " %u durum ulaşılamaz", g_ob.graf.ulasilamaz_n);
    }
  }
  if (g_etkin && g_etkin[0]) {
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Ok)), ICON_MD_PLAY_CIRCLE " şu an: %s", g_etkin);
  }
  if (g_ob.hatali) {
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Err)), ICON_MD_ERROR " satır %u: %s%s", g_ob.er.line, g_ob.er.msg,
                       g_ob.gecerli_var ? " — son geçerli hâli gösteriliyor" : "");
  }
  if (g_duzen) duzen_arayuzu(metin, b);
  if (g_ob.gecerli_var) git = g_sekme == 0 ? grafik_ciz(metin, git_satir) : tablo_ciz(metin, git_satir);
  else if (!g_ob.hatali) ImGui::TextDisabled("durum yok");
  if (g_duzen) sag_tik_menuleri(metin, b);
  ImGui::EndChild();
  g_etkin = nullptr;
  g_duzen = false;
  return git;
}

} // namespace tulpar::engine::app
