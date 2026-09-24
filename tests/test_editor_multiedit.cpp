// editor_multiedit kapilari: coklu secimde alan duzenlemesi DOGRU yaprak
// duzeyinde yayiliyor mu. Her biri, yanlis bir tasarimin (butun struct'i ya da
// butun kelimeyi kopyalamak) GERCEKTEN bozacagi bir durumu olcer.
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_multiedit.hpp"

using namespace tulpar::engine;
using content::SceneEntity;

namespace {
SceneEntity make(float x, float y, float z) {
  SceneEntity e{};
  e.pos = Vec3{x, y, z};
  return e;
}
} // namespace

// pos.x yazmak digerlerinin pos.y/pos.z'sine DOKUNMAMALI. Butun Vec3'u
// kopyalayan bir tasarim secimi tek noktaya yigardi.
ENGINE_TEST(multiedit_vec_component_is_its_own_leaf) {
  const SceneEntity before = make(1, 2, 3);
  SceneEntity after = before;
  after.pos.x = 9;
  SceneEntity target = make(5, 6, 7);
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK(target.pos.x == 9 && target.pos.y == 6 && target.pos.z == 7);
}

// Bilesen eklemek hedefin KENDI bilesenlerini silmemeli. Butun kelimeyi
// kopyalamak, Model'e Isik eklerken secimdeki Govdeli nesnenin Govdesini
// kaldirirdi.
ENGINE_TEST(multiedit_component_bits_are_merged_not_copied) {
  SceneEntity before{};
  before.components = content::kSceneModel;
  SceneEntity after = before;
  after.components |= content::kSceneLight; // ana secilide Isik eklendi
  SceneEntity target{};
  target.components = content::kSceneBody;  // hedefin kendi bileseni
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK((target.components & content::kSceneLight) != 0); // eklenen geldi
  CHECK((target.components & content::kSceneBody) != 0);  // kendisininki KALDI
  CHECK((target.components & content::kSceneModel) == 0); // ana secilinin eskisi TASINMADI

  // Kaldirma da yalniz kaldirilan biti etkiler.
  SceneEntity b2 = after, a2 = after;
  a2.components &= ~content::kSceneLight;
  CHECK(app::multiedit_apply(b2, a2, &target));
  CHECK((target.components & content::kSceneLight) == 0);
  CHECK((target.components & content::kSceneBody) != 0);
}

// Ad yayilmaz: 20 nesneyi secip birinin adini degistirmek 20 ayni ad uretmemeli.
ENGINE_TEST(multiedit_name_is_never_propagated) {
  SceneEntity before{};
  std::strcpy(before.name, "a");
  SceneEntity after = before;
  std::strcpy(after.name, "yeni_ad");
  SceneEntity target{};
  std::strcpy(target.name, "b");
  CHECK(!app::multiedit_apply(before, after, &target)); // baska hicbir sey degismedi
  CHECK(std::strcmp(target.name, "b") == 0);
}

// Metin alani (dosya yolu) BUTUN olarak gider: yarim dizi kopyalamak iki yolun
// karisimini uretirdi.
ENGINE_TEST(multiedit_string_field_is_copied_whole) {
  SceneEntity before{};
  std::strcpy(before.audio_clip, "kisa.wav");
  SceneEntity after = before;
  std::strcpy(after.audio_clip, "cok_daha_uzun_bir_ad.wav");
  SceneEntity target{};
  // Hedefin kendi adi KAYNAKTAN UZUN olmali: kopya "butun" degilse kuyruk kalir.
  // Ama alan char[kSceneNameLen] ve o sabit "NUL dahil" demek — 32 karakterlik
  // bir literal 33 bayt yazar ve BIR BAYT tasar. Olculdu 2026-09-20: yerelde
  // sessiz gecti, Ubuntu CI'da glibc _FORTIFY_SOURCE bunu SIGABRT'a cevirdi
  // ("*** buffer overflow detected ***"), yani paketin ozet satiri hic basilmadi.
  // Bu yuzden uzunluk artik DERLEME ZAMANINDA kapiya bagli: bir sonraki sefere
  // cokme degil, derleme hatasi olsun.
  static constexpr char kHedefAd[] = "hedefin_kendi_cok_uzun_adii.wav";
  static_assert(sizeof kHedefAd <= content::kSceneNameLen,
                "hedef adi audio_clip alanina sigmiyor (NUL dahil)");
  static_assert(sizeof kHedefAd > sizeof "cok_daha_uzun_bir_ad.wav",
                "hedef adi kaynaktan UZUN olmali, yoksa test kuyruk kalmasini olcmez");
  std::strcpy(target.audio_clip, kHedefAd);
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK(std::strcmp(target.audio_clip, "cok_daha_uzun_bir_ad.wav") == 0);
}

// Hicbir sey degismediyse hedef de degismez ve fonksiyon bunu soyler:
// gunluge bos islem dusmemeli.
ENGINE_TEST(multiedit_no_change_means_no_change) {
  const SceneEntity e = make(1, 2, 3);
  SceneEntity target = make(4, 5, 6);
  const SceneEntity copy = target;
  CHECK(!app::multiedit_apply(e, e, &target));
  CHECK(std::memcmp(&target, &copy, sizeof target) == 0);
}

// Tablo bos degil ve makul buyuklukte (tools/scene_check.py kapsamini ayrica olcer).
ENGINE_TEST(multiedit_leaf_table_is_populated) {
  CHECK(app::multiedit_leaf_count() > 60);
}

// --- Nesne ozellikleri (E3): ADA GORE birlestirme, yalniz AYNI betik --------
namespace {
bool prop1(SceneEntity &e, const char *name, uint32_t type, float x) {
  const float v[3] = {x, 0.0f, 0.0f};
  return content::scene_prop_set(e, name, type, v);
}
SceneEntity scripted(const char *script) {
  SceneEntity e{};
  e.components = content::kSceneScript;
  std::snprintf(e.script_file, sizeof e.script_file, "%s", script);
  return e;
}
float val(const SceneEntity &e, const char *name) {
  const content::SceneProp *p = content::scene_prop_find(e, name);
  return p ? p->v[0] : -12345.0f;
}
} // namespace

// Ana secilide `can` 100 -> 250 ve yeni `hiz` eklendi. Ayni betikli hedef
// ikisini ALIR ve kendi `kalkan`ini KORUR (bayt kopyasi onu silerdi: hedefin
// props[0]'i "kalkan", ana secilinin "can"). Baska betikli hedef HICBIR SEY
// almaz ve bu sayilir (sessiz degil).
ENGINE_TEST(multiedit_props_merge_by_name_only_into_same_script) {
  SceneEntity before = scripted("davranis/dusman.tpr");
  CHECK(prop1(before, "can", content::kScenePropTam, 100));
  SceneEntity after = before;
  CHECK(prop1(after, "can", content::kScenePropTam, 250));
  CHECK(prop1(after, "hiz", content::kScenePropSayi, 5));

  SceneEntity ayni = scripted("davranis/dusman.tpr");
  CHECK(prop1(ayni, "can", content::kScenePropTam, 100) && prop1(ayni, "kalkan", content::kScenePropBayrak, 1));
  SceneEntity baska = scripted("davranis/kapi.tpr");
  CHECK(prop1(baska, "can", content::kScenePropTam, 100));
  const SceneEntity baska0 = baska;

  app::MultieditStats st;
  CHECK(app::multiedit_apply(before, after, &ayni, &st));
  CHECK(val(ayni, "can") == 250.0f && val(ayni, "hiz") == 5.0f && val(ayni, "kalkan") == 1.0f && ayni.prop_count == 3);
  CHECK(st.props_applied == 2 && st.props_script_skipped == 0 && st.props_overflow == 0);

  CHECK(!app::multiedit_apply(before, after, &baska, &st)); // betik farkli: DEGISMEDI
  CHECK(std::memcmp(&baska, &baska0, sizeof baska) == 0);
  CHECK(st.props_script_skipped == 1);

  // KONTROL: ozellik farki olmayan bir duzenleme (konum) betigi farkli hedefe
  // de gider — atlama yalniz OZELLIKLERE ozgu, butun hedefe degil.
  SceneEntity b2 = before, a2 = before;
  a2.pos.x = 3;
  CHECK(app::multiedit_apply(b2, a2, &baska, &st));
  CHECK(baska.pos.x == 3 && val(baska, "can") == 100.0f && st.props_script_skipped == 1);
  std::printf("    [bilgi] ozellik yayma: uygulanan %u, betik farkli atlanan %u, tasma %u\n", st.props_applied,
              st.props_script_skipped, st.props_overflow);
}

// Kaldirma da ada gore yayilir: ana secilide `hiz` silindi -> hedefteki `hiz`
// silinir, hedefin kendi `x`i kalir.
ENGINE_TEST(multiedit_props_removal_propagates) {
  SceneEntity before = scripted("a.tpr");
  CHECK(prop1(before, "can", content::kScenePropTam, 1) && prop1(before, "hiz", content::kScenePropSayi, 2));
  SceneEntity after = before;
  CHECK(content::scene_prop_remove(after, "hiz"));
  SceneEntity t = scripted("a.tpr");
  CHECK(prop1(t, "hiz", content::kScenePropSayi, 9) && prop1(t, "x", content::kScenePropSayi, 7));
  app::MultieditStats st;
  CHECK(app::multiedit_apply(before, after, &t, &st));
  CHECK(content::scene_prop_find(t, "hiz") == nullptr && val(t, "x") == 7.0f && t.prop_count == 1);
  CHECK(st.props_applied == 1);
  // Hedefte zaten yoksa: degisiklik yok, yanlis "degisti" raporu yok.
  SceneEntity u = scripted("a.tpr");
  CHECK(!app::multiedit_apply(before, after, &u, &st));
}

// Hedef doluysa (16 ozellik) yeni ad REDDEDILIR ve SAYILIR; hedefin baytlari
// degismez. KONTROL: 15 ozellikli hedef ayni degisikligi alir.
ENGINE_TEST(multiedit_props_overflow_is_counted_not_silent) {
  SceneEntity before = scripted("a.tpr");
  SceneEntity after = before;
  CHECK(prop1(after, "yeni", content::kScenePropSayi, 1));
  SceneEntity dolu = scripted("a.tpr");
  char ad[16];
  for (uint32_t i = 0; i < content::kSceneMaxProps; i++) {
    std::snprintf(ad, sizeof ad, "p%02u", i);
    CHECK(prop1(dolu, ad, content::kScenePropSayi, (float)i));
  }
  const SceneEntity dolu0 = dolu;
  app::MultieditStats st;
  CHECK(!app::multiedit_apply(before, after, &dolu, &st));
  CHECK(std::memcmp(&dolu, &dolu0, sizeof dolu) == 0 && st.props_overflow == 1);
  SceneEntity bir_eksik = dolu0;
  CHECK(content::scene_prop_remove(bir_eksik, "p00"));
  CHECK(app::multiedit_apply(before, after, &bir_eksik, &st));
  CHECK(val(bir_eksik, "yeni") == 1.0f && st.props_overflow == 1 && st.props_applied == 1);
}
