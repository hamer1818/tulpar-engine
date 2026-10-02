// L6 BRIDGE — Tulpar yerel eklenti bildiriminin ABI KILIDI (K303, 2026-10-02).
//
// TulparLang derleyicisi eng_* cagrisini tulpar-ext.json'daki C tipleriyle
// DOGRUDAN teng_*'e indirir; statik arsivde tip bilgisi olmadigi icin
// bildirim ile C arasindaki bir kaymayi GOREMEZ (int yerine double okunur,
// hata yok). Bu dosya o kaymayi motorun KENDI derlemesinde yakalar:
// bridge/tulpar_ext_abi.inc (SPEC'ten uretilir, bildirimle ayni kaynak) her
// imzayi teng_*'in gercek bildirimine (engine_api.h) tipli bir isaretci
// olarak atar. C++'ta farkli islev isaretcisi tipleri arasinda ortuk cevrim
// yoktur: tip kayarsa motor DERLENMEZ.
//
// Ikinci olcu: tablo bir teste baglanir (tests/test_bridge.cpp
// bridge_tulpar_abi_*), yani bildirimdeki her sembol arsivde TANIMLI olmak
// zorunda — yoksa engine_tests LINKLENMEZ.
//
// POZITIF KONTROL: CMake yapilandirmasi bu dosyayi TULPAR_ABI_KILIDI_BOZ ile
// bir kez derlemeyi dener ve DUSMESINI bekler (CMakeLists.txt); derlenirse
// kilit bir sey olcmuyor demektir ve yapilandirma durur.
#include "bridge/engine_api.h"

#include <cstdint>

namespace tulpar::engine::bridge {

using TulparAbiFn = void (*)(void);

namespace {
#define TENG_ABI(R, NAME, SYM, PARAMS) [[maybe_unused]] R(*const kAbi_##NAME) PARAMS = &SYM;
#include "bridge/tulpar_ext_abi.inc"
#undef TENG_ABI
#ifdef TULPAR_ABI_KILIDI_BOZ
// Kasitli kayma: teng_init `int(const char *, int, int)`; burada double.
[[maybe_unused]] int (*const kAbiBoz)(const char *, double, int) = &teng_init;
#endif
}  // namespace

#define TENG_ABI(R, NAME, SYM, PARAMS) reinterpret_cast<TulparAbiFn>(kAbi_##NAME),
extern const TulparAbiFn kTulparAbiTable[] = {
#include "bridge/tulpar_ext_abi.inc"
};
#undef TENG_ABI
extern const int kTulparAbiCount = TENG_ABI_COUNT;
static_assert(sizeof(kTulparAbiTable) / sizeof(kTulparAbiTable[0]) == TENG_ABI_COUNT,
              "ABI tablosu bildirimdeki fonksiyon sayisiyla ayni olmali");

}  // namespace tulpar::engine::bridge
