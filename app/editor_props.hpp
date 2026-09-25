// L6 APP — editor_props: nesne OZELLIKLERI denetcisi (E5).
//
// Tasarimci ayni betigi on dusmana verir ve her birine editorde KENDI
// degerini yazar (can = 250, hiz = 5, devriye noktasi) — kod yazmadan. Sahne
// yalniz USTUNE YAZILANLARI tasir (E3, content/scene.hpp); varsayilan betigin
// KODUNDA yasar. Editorun "bu betik hangi ozellikleri okuyor, varsayilanlari
// ne" sorusunun cevabi bu yuzden betigin METNINDEN gelir:
//
//     int can = ozellik_tam(i, "can", 100);      // bildirimin KENDISI
//
// Uc parca, ucu de ayri olculur:
//   1. prop_scan    — saf tarayici (ImGui yok, ayirma yok, dosya yok).
//   2. PropCache    — betik yolu -> tarama; damga (mtime ns + boyut) degisince
//                     yeniden taranir. Disk yalniz denetci karti cizilirken ve
//                     en cok kPropRestatFrames karede bir sorulur.
//   3. props_panel  — "Tulpar Betik" kartindaki Ozellikler bolumu (ImGui).
//
// Tarayici GRAMERI (Tulpar'in kendi sozcukleyicisini izler — TulparLang
// src/lexer/lexer.cpp):
//   * `//` satir sonuna, `/* */` IC ICE GIRMEZ (Tulpar'daki gibi); kapanmayan
//     blok dosya sonuna kadar yorumdur.
//   * "..." dize (\ kacisi), t"...{ifade}..." sablon dize (ic ice {} ve ic
//     dizeler). Yorumun ve dizenin ICINDEKI cagri BILDIRIM DEGILDIR. Sablonun
//     {ifade}'sindeki bir cagri da sayilmaz (sablon bir butun olarak atlanir;
//     deger oyunda yine okunur, yalniz denetcide listelenmez).
//   * Cagri: 8 addan biri (ozellik_sayi|_tam|_bayrak|_nokta, prop_num|_int|
//     _flag|_point) TAM tanimlayici olarak (UTF-8 bayti da tanimlayici
//     karakteridir: `ozellik_sayiğ` baska bir ad), ardindan `(`. Onunde `.`
//     (baska bir nesnenin uyesi) ya da func/fonksiyon/fonk/islev/işlev
//     (TANIM, cagri degil — tulpar/engine.tpr'nin kendisi) varsa sayilmaz.
//   * 1. arguman herhangi bir DENGELI ifade (parantez/koseli/susluye bakilir).
//   * 2. arguman TEK bir dize literali ve ardindan `,`. Degilse (degisken,
//     birlestirme, eksik) ad bilinemez: `dynamic_name` sayilir, LISTELENEMEZ.
//     Literal ama [a-z0-9_]{1,23} degilse (scene_prop_name_ok) `bad_name`:
//     editor boyle bir ad yazamaz, listelenmez.
//   * 3. arguman ve hemen `)`:
//       sayi / tam: [-|+] SAYI — onluk (`_` ayiraci, `.`, `e±us`), 0x / 0b.
//                   `tam` tamsayi ve |v| <= 2^24 olmali (float'ta tam temsil).
//       bayrak:     true | false | dogru | yanlis | doğru | yanlış
//       nokta:      v3(s, s, s)  ya da  { x: s, y: s, z: s } (anahtar sirasi serbest)
//     Baska her sey (degisken, ifade, `(3)`) LITERAL DEGIL: bildirim YINE
//     LISTELENIR (tur ve ad bilindigi icin tasarimci ustune yazabilir) ama
//     varsayilani BILINMEZ (kPropDeclDefaultUnknown, def = 0) ve
//     `bad_default` sayilir. Listelememek, sahnedeki gecerli bir ustune
//     yazmayi "betik bunu okumuyor" (yetim) diye yanlis gosterirdi.
//   * Ayni ad ikinci kez: tur, bayrak ve varsayilan (bit-tam) AYNIYSA tekrar
//     (bir kez listelenir); degilse `conflicts` — ILKI listelenir.
//   * cap'e sigmayan bildirim yazilmaz, `overflow` sayilir (tekrar eden bir
//     tasma adi her gorunuste sayilir: sayac "sigmayan CAGRI"dir).
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/editor_widgets.hpp"
#include "content/scene.hpp"

namespace tulpar::engine::app {

enum PropDeclFlags : uint32_t {
  kPropDeclDefaultUnknown = 1u << 0, // varsayilan literal degil / turune uymuyor: def = 0, GOSTERILMEZ
};

struct PropDecl {
  char name[content::kScenePropNameLen] = {0}; // scene_prop_name_ok'tan gecmis
  uint32_t type = 0;                            // content::ScenePropType
  float def[3] = {0, 0, 0};                     // tura gore; kullanilmayanlar 0
  uint32_t line = 0;                            // ilk goruldugu satir (1 tabanli)
  uint32_t flags = 0;                           // PropDeclFlags
};

struct PropScanResult {
  uint32_t count = 0;          // out'a yazilan TEKIL bildirim
  uint32_t calls = 0;          // bulunan cagri (tekrar ve hatalilar dahil)
  uint32_t overflow = 0;       // cap'e SIGMAYAN bildirim: yazilmadi, SAYILDI
  uint32_t bad_default = 0;    // varsayilan literal degil / turune uymuyor: listelendi, varsayilan bilinmiyor
  uint32_t bad_name = 0;       // literal ad gecersiz ([a-z0-9_]{1,23} degil): listelenmedi
  uint32_t dynamic_name = 0;   // ad literal degil ya da eksik: listelenemez
  uint32_t conflicts = 0;      // ayni ad farkli tur/varsayilanla: ilki listelendi
  uint32_t first_bad_line = 0; // yukaridaki sorunlardan (tasma dahil) ilkinin satiri; 0 = sorun yok
  uint32_t problems() const { return overflow + bad_default + bad_name + dynamic_name + conflicts; }
};

// Metni tara. `text` NUL ile bitmek ZORUNDA DEGIL (len esastir). Ayirma yok,
// out cagiranin. out == nullptr / cap == 0: yalniz sayar (her bildirim tasma).
PropScanResult prop_scan(const char *text, size_t len, PropDecl *out, uint32_t cap);

// Ada gore bildirim; yoksa nullptr.
const PropDecl *prop_decl_find(const PropDecl *d, uint32_t n, const char *name);

// YETIM ustune yazmalar: varlikta olup betigin bildirmedigi (ad yok) ya da
// FARKLI turle bildirdigi (kopru tur uyusmazligini hata sayar ve varsayilani
// doner — yani deger oyunda etkisiz) ozellikler. idx_out: e.props indeksleri.
// Donus: yetim sayisi (cap'i asan da sayilir, yazilmaz).
uint32_t prop_orphans(const content::SceneEntity &e, const PropDecl *d, uint32_t n, uint32_t *idx_out, uint32_t cap);

// --- Gorunum isaretleri ------------------------------------------------------
// Secili varligin `nokta` ozellikleri DUNYA uzayinda: bildirilenler (ustune
// yazilmissa o deger, degilse BILINEN varsayilan) + yetim nokta ustune
// yazmalari. Donusum content::scene_prop_point_world — kopru ve derleyici ile
// AYNI kural (yazar pozu; olcek yok).
enum PropMarkerKind : uint32_t { kPropMarkerDefault = 0, kPropMarkerOverride = 1, kPropMarkerOrphan = 2 };
struct PropMarker {
  float world[3] = {0, 0, 0};
  uint32_t kind = kPropMarkerDefault;
  char name[content::kScenePropNameLen] = {0};
};
// scanned = false (betik yok / taranamadi): bildirim bilinmez, varlikta
// yazili her nokta Override olarak doner (yetim diyemeyiz).
// Donus: nokta sayisi (cap'i asan da sayilir). Gecersiz indeks: 0.
uint32_t prop_marker_points(const content::SceneDesc &s, uint32_t ent, const PropDecl *d, uint32_t n, bool scanned, PropMarker *out,
                            uint32_t cap);

// --- Nokta duzenleme kipi (E6) -------------------------------------------------
// Denetcideki ✥ ya da gorunumde bir isaretin eskenar dortgeni, secili varligin
// BIR noktasini kipe alir: editorun TEK gizmosu varligin yerine noktanin DUNYA
// konumunda durur (yalniz tasima), her surukleme karesi yerel ofsete geri
// cevrilir (content::scene_prop_point_local, point_world'un tersi) ve
// birakinca gunluge TEK islem girer. Kural denetci satiriyla AYNI:
// suruklenebilir nokta = denetcide DUZENLENEBILIR nokta satiri. Kip durumu ve
// gizmo tesisati editor_app'te; buradakiler saf (ImGui yok) ve test_editor_props
// olcuyor.
enum class PropEditState : uint8_t {
  Ok = 0,
  NoEntity,    // indeks gecersiz / ad bos
  Locked,      // varlik kilitli (kSceneLocked): gizmo gibi nokta da degismez
  NotScanned,  // betik yok / taranamadi: bildirim bilinmiyor (satirlar salt okunur)
  NotDeclared, // betik bu adi `nokta` olarak okumuyor (yetim, tur farkli ya da hic yok)
  NoPosition,  // varsayilan kodda hesaplaniyor, ustune yazma yok: konum bilinmiyor
  Full,        // nokta varsayilanda ve varlikta 16 ozellik dolu: ustune yazma yaratilamaz
};
const char *prop_edit_state_text(PropEditState s);
// `name` noktasi bu varlikta suruklenebilir mi. Ok ise out_local = su anki
// yerel deger (ustune yazma, yoksa betigin varsayilani) ve *is_override.
// out_local / is_override nullptr olabilir.
PropEditState prop_edit_state_entity(const content::SceneEntity &e, const char *name, const PropDecl *d, uint32_t n, bool scanned,
                                     float out_local[3], bool *is_override);
// Ayni kural, sahne indeksiyle (gecersiz indeks: NoEntity).
PropEditState prop_edit_state(const content::SceneDesc &s, int32_t ent, const char *name, const PropDecl *d, uint32_t n, bool scanned,
                              float out_local[3], bool *is_override);

// Surukleme karesi: dunya konumunu yerel ofsete cevirip `name`e yazar (ustune
// yazma yoksa YARATIR — varsayilandan surukleme). false: indeks gecersiz ya da
// scene_prop_set reddetti (tavan dolu, sonlu olmayan deger); varlik DEGISMEZ.
// out_local nullptr olabilir. Ebeveyn zinciri ve olcek kurali point_local'in.
bool prop_point_set_world(content::SceneDesc &s, uint32_t ent, const char *name, const float world[3], float out_local[3]);

// Gorunumde isaret secimi (ekran uzayi). Eskenar dortgen bir L1 topudur:
// |dx| + |dy| <= r tam olarak cizilen sekildir. Aday: visible && editable.
// Birden cok aday: EN YAKINI; esitlikte SONRA cizilen (ustte gorunen).
// Donus: indeks, yoksa -1.
struct PropMarkerScreen {
  float x = 0, y = 0;     // ekran konumu (gorunum paneli dahil, ImGui uzayi)
  bool visible = false;   // kameranin onunde (izdusum gecerli)
  bool editable = false;  // prop_edit_state == Ok
};
// Isabet yaricapi: buyuk isaretin yaricapi (6 px, editor_app kPropMarkerR) +
// 3 px pay. Kucuk (varsayilan) isaret de ayni payla tutulur: 4.5 px'lik bir
// hedefe tiklamak imlecle zor.
constexpr float kPropMarkerHitR = 9.0f;
int32_t prop_marker_hit(const PropMarkerScreen *m, uint32_t n, float mx, float my, float r);

// --- Onbellek ---------------------------------------------------------------
// Bir betik kac bildirim tasiyabilir: 32. Varlik basina ozellik tavani 16
// (kSceneMaxProps); betik 16'dan fazlasini BILDIREBILIR (hepsine ayni anda
// deger verilemez ama her biri ayri ayri ustune yazilabilir), 2 kat pay.
constexpr uint32_t kPropDeclMax = 32;
// Ayni anda taranmis tutulan betik: 8. Denetci tek varligi gosterir; 8,
// secim gezinirken ayni birkac betige tekrar tekrar donmeyi diske gitmeden
// karsilar. Dolunca en uzun suredir kullanilmayan cikar (SAYILIR).
constexpr uint32_t kPropCacheMax = 8;
// Diske en sik bu kadar karede bir sorulur (60 Hz'de 0.5 s): betigi kod
// editorunde kaydedip editore donen tasarimci degisikligi yarim saniyede gorur,
// duran bir denetci her kare stat cagirmaz.
constexpr uint32_t kPropRestatFrames = 30;
// Taranan betigin tavani. Olculdu 2026-09-25: depodaki en buyuk .tpr
// (tulpar/examples/engine_aksiyon.tpr) ~44 KB, davranis betikleri < 4 KB.
// Asan dosya YARIM taranmaz (yarim metin "bildirim yok" derdi): durum TooBig.
constexpr uint32_t kPropScanTextMax = 256 * 1024;

enum class PropCacheState : uint8_t {
  Missing, // dosya yok / damga alinamadi
  TooBig,  // kPropScanTextMax'i asiyor: taranmadi
  ReadFail,// acilamadi ya da okuma yarida kaldi
  Ok,      // tarandi (res gecerli)
};
const char *prop_cache_state_text(PropCacheState s);

struct PropCacheEntry {
  char path[1024] = {0}; // COZULMUS yol (anahtar)
  int64_t mtime_ns = 0, size = -1;
  PropCacheState state = PropCacheState::Missing;
  PropDecl decls[kPropDeclMax];
  PropScanResult res;
  uint32_t stat_frame = 0; // son damga sorusu
  uint32_t use_frame = 0;  // son kullanim (LRU)
  uint32_t loads = 0;      // bu yola atandigindan beri kac kez okunup tarandi
  bool used = false;
};
struct PropCache {
  PropCacheEntry e[kPropCacheMax];
  uint32_t evictions = 0; // yer acmak icin cikarilan girdi
  uint32_t stats = 0;     // toplam damga sorusu (kapilar: kisitlama CALISIYOR MU)
  uint32_t loads = 0;     // toplam okuma + tarama
  char text[kPropScanTextMax + 1];
};

// `path`in taramasi. may_io: bu cagri diske gidebilir mi (denetci karti
// cizilirken true; gorunum isaretleri false — yalniz onbellege bakar).
//   * Onbellekte ve may_io: son sorudan >= kPropRestatFrames kare gectiyse
//     damga sorulur; degistiyse (ya da durum degistiyse) yeniden okunur.
//   * Onbellekte degil: may_io ise okunur/taranir (yer yoksa LRU cikar),
//     degilse nullptr.
// `frame`: tekduze artan kare sayaci.
const PropCacheEntry *prop_cache_get(PropCache &c, const char *path, uint32_t frame, bool may_io);
// Yalniz bak (disk yok): prop_cache_get(c, path, frame, false) ile ayni.
const PropCacheEntry *prop_cache_peek(PropCache &c, const char *path, uint32_t frame);
void prop_cache_clear(PropCache &c);

// --- Denetci bolumu (ImGui) ----------------------------------------------------
// "Tulpar Betik" kartinin icinde (ya da betiksiz ama ozellikli varlikta tek
// basina) cizilir. Degeri SAHNEYE YAZMAZ, niyet dondurur — gunluk editorun:
//   * surekli widget'lar (surukleme, metin): `e` YERINDE degisir ve her
//     widget'in PropItem'i on_item'e verilir (editor_app: track_edit) —
//     diger alanlarla ayni tek-islem sozlesmesi;
//   * ayrik eylemler (bayrak, sifirla ↺, yetimi sil): `after = e` uzerinde
//     yapilir ve commit = true doner (editor_app: commit(st, si, after)).
// Ikisi de coklu secimde editor_multiedit uzerinden AYNI betigi tasiyan
// digerlerine yayilir (ada gore fark).
struct PropsPanelInput {
  const PropDecl *decls = nullptr;     // tarama (yoksa nullptr)
  uint32_t decl_count = 0;
  const PropScanResult *scan = nullptr; // nullptr: betik TARANAMADI (note sebebi soyler)
  const char *note = nullptr;           // taranamadiysa gorunur sebep ("betik dosyasi yok: ...")
  bool has_script = true;               // false: betik bileseni yok, ozellikler HAM gosterilir
  const char *point_edit = nullptr;     // E6: kipteki nokta (bu varligin); ✥ basili cizilir. nullptr = kip kapali
};
struct PropsPanelResult {
  bool commit = false;          // ayrik eylem: `after` yeni varlik
  uint32_t rows_declared = 0;   // cizilen bildirim satiri
  uint32_t rows_overridden = 0; // bunlarin ustune yazilmis olani
  uint32_t rows_orphan = 0;     // cizilen yetim / ham satir
  bool diagnostics = false;     // tarama sorunu satiri cizildi
  // E6: bir nokta satirinin ✥ dugmesine basildi — kip bu ad icin ac/kapa
  // (karari editor verir; panel yalniz niyet dondurur).
  bool point_toggle = false;
  char point_name[content::kScenePropNameLen] = {0};
  uint32_t point_buttons = 0;   // cizilen ✥ (nokta satiri) — kapali olanlar dahil
  uint32_t point_enabled = 0;   // bunlarin tiklanabilir olani (prop_edit_state Ok)
  WidgetRect point_rect;        // son cizilen ✥ (kapilar tiklar)
};
PropsPanelResult props_panel(content::SceneEntity &e, content::SceneEntity &after, const PropsPanelInput &in,
                             void (*on_item)(void *user, const PropItem &it), void *user);

} // namespace tulpar::engine::app
