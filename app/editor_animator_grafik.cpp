#include "app/editor_animator_grafik.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>

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
    ImGui::SetTooltip("%s → %s\n%s\n(tıkla: satıra git)", tr.from < 0 ? "herhangi" : d.states[tr.from].name, d.states[tr.to].name, k[ustteki].et);
    if (tik) { *git_satir = animator_tanim_satiri(metin, "gecis", nullptr, ustteki); git = *git_satir > 0; }
  }
  // --- Dugumler ---
  for (uint32_t i = 0; i < g.n; i++) {
    const AnimGrafikDugum &n = g.d[i];
    const ImVec2 p0(merkez[i].x - yari.x, merkez[i].y - yari.y), p1(merkez[i].x + yari.x, merkez[i].y + yari.y);
    const bool ustunde = pencere_ustu && fare.x >= p0.x && fare.x <= p1.x && fare.y >= p0.y && fare.y <= p1.y;
    const bool baslangic = n.durum >= 0 && n.durum == d.start;
    const ImU32 zemin = n.durum < 0 ? ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f * alfa) : ImGui::GetColorU32(ImGuiCol_FrameBgHovered, alfa);
    dl->AddRectFilled(p0, p1, zemin, fs * 0.35f);
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
      ImGui::SetTooltip("%s%s\n%s%s\n(tıkla: satıra git)", d.states[n.durum].name, baslangic ? " (başlangıç)" : "", oz,
                        n.ulasilamaz ? "\nUYARI: hiçbir geçiş bu duruma girmiyor" : "");
      if (tik) { *git_satir = animator_tanim_satiri(metin, "durum", d.states[n.durum].name, 0); git = *git_satir > 0; }
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

bool animator_grafik_ciz(const char *metin, float yukseklik, int *git_satir) {
  if (!metin || !git_satir) return false;
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
  if (g_ob.hatali) {
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(renk(Tone::Err)), ICON_MD_ERROR " satır %u: %s%s", g_ob.er.line, g_ob.er.msg,
                       g_ob.gecerli_var ? " — son geçerli hâli gösteriliyor" : "");
  }
  if (g_ob.gecerli_var) git = g_sekme == 0 ? grafik_ciz(metin, git_satir) : tablo_ciz(metin, git_satir);
  else if (!g_ob.hatali) ImGui::TextDisabled("durum yok");
  ImGui::EndChild();
  return git;
}

} // namespace tulpar::engine::app
