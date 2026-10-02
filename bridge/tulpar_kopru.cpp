// L6 BRIDGE — TulparLang yerel eklenti yapistiricisi (bkz. tulpar_kopru.h).
//
// libengine_tulpar.a (masaustu) ve libtulpar_engine_android.a icinde yasar;
// motorun kendi ikilileri (engine_tests, editor, demo) bunu BAGLAMAZ, cunku
// asagidaki iki sembol TulparLang runtime'inda (libtulpar_runtime.a) — yalniz
// bir Tulpar oyununun linkinde cozulur.
#include "bridge/tulpar_kopru.h"

#include "bridge/engine_api.h"

// TulparLang runtime'inin eklentilere actigi DUZ C yuzu (src/vm/
// runtime_bindings.cpp). Motor Tulpar'in deger tipini (VMValue, ObjString)
// GORMEZ: eskiden uretilen VMValue bindingleri o yerlesime derleme aninda
// baglaniyordu ve TulparLang yerlesimi degistirince (2026-10-02: ObjString
// karakterleri nesnenin icine alindi) motor arsivi SESSIZCE yanlis okurdu.
//   lookup: Tulpar fonksiyonunu ADIYLA coz (cagirmadan, ayirmadan); NULL = yok,
//           *arity parametre sayisi (-1 = bilinmiyor).
//   call:   cozulmus fonksiyonu cagir; her arguman Tulpar'a float gider (tipli
//           `int` parametre kendi prologunda cevirir). 1 = cagrildi.
// Ikisinin imzasi TengScriptVm'in resolve/invoke alanlariyla BIREBIR ayni:
// tabloya ARADA katman olmadan konur. Olculdu (2026-10-02, Ryzen 7 9800X3D +
// RTX 5080, tools/kanca_olcumu.py, 200 bos kanca, 2000 kare): bu yol kanca
// basina 5.5-6.0 ns, eski uretilmis VMValue baglamasi 5.6-6.2 ns (ayni kosu
// oturumunda, sirayla). Arada katman varken (sarmalayici + runtime'da disari
// alinmis dagitim) 6.4-7.7 ns idi.
extern "C" void *tulpar_ext_func_lookup(const char *name, int *arity);
extern "C" int tulpar_ext_call_f64(void *fn, int arity, const double *args, int argc);

namespace {

// Yukleme aninda sorulur ("bu kanca var mi"): motor cevabi varlik basina
// saklar, eksik kanca her karede degil BIR KEZ bildirilir.
int vm_has(const char *fn) {
  if (!fn || !*fn) return 0;
  int arity = -1;
  return tulpar_ext_func_lookup(fn, &arity) != nullptr;
}

// Adla cagri (yavas yol; motor kancayi cozemezse ya da VM degistiyse).
int vm_call(const char *fn, const double *args, int argc) {
  if (!fn || !*fn) return 0;
  int arity = -1;
  void *p = tulpar_ext_func_lookup(fn, &arity);
  if (!p) return 0;
  return tulpar_ext_call_f64(p, arity, args, argc);
}

// HIZLI YOL (resolve/invoke): yuklemede bir kez coz, kare icinde isaretciyle
// cagir — ad kurma, hash, dizgi ayirma yok (engine_api.h TengScriptVm).
// Statik: teng_set_script_vm_v2 isaretciyi KOPYALAMAZ (engine_api.h).
const TengScriptVm kTulparVm = {vm_has, vm_call, tulpar_ext_func_lookup, tulpar_ext_call_f64};

}  // namespace

extern "C" int teng_tulpar_init(const char *title, int width, int height) {
  // VM kurulumu eng_init'in ICINDE: kancalar sahne yuklenirken cozuluyor ve
  // oyunun ayri bir cagriyi unutmasi mumkun OLMAMALI — unutulan kurulum,
  // sessizce calismayan betikler demek. _v2: dort alan (resolve/invoke).
  teng_set_script_vm_v2(&kTulparVm);
  return teng_init(title, width, height);
}

// ABI KILIDI (yapistirici): bildirimdeki eng_init imzasi ile bu fonksiyonun
// imzasi ayni mi. Kayarsa bu dosya DERLENMEZ. Geri kalan 207 fonksiyonun
// kilidi bridge/tulpar_abi.cpp'de (teng_*'e karsi).
namespace {
#define TENG_ABI(R, NAME, SYM, PARAMS) using Abi_##NAME = R(*) PARAMS;
#include "bridge/tulpar_ext_abi.inc"
#undef TENG_ABI
[[maybe_unused]] const Abi_eng_init kAbiTulparInit = &teng_tulpar_init;
}  // namespace
