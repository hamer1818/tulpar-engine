// L6 BRIDGE — TulparLang yerel eklenti yapistiricisi (K303, 2026-10-02).
//
// Motor Tulpar'a bir "yerel eklenti" olarak baglanir: derleyici
// tulpar-ext.json'u okuyup her eng_* cagrisini bildirilen C tipleriyle
// DOGRUDAN teng_*'e indirir (bkz. docs/KOPRU.md §1). Arada uretilmis bir
// VMValue katmani YOK — motor Tulpar'in deger tipini gormez.
//
// Tek istisna eng_init: motor -> Tulpar yonu (betik kancalari) icin betik
// VM'i motor acilmadan KURULMALI. Bu yapistirici o kurulumu yapar ve
// teng_init'i cagirir; bildirimde eng_init'in sembolu budur. VM'in kendisi
// TulparLang runtime'inin duz C yuzunden kurulur (tulpar_ext_func_lookup /
// tulpar_ext_call_f64) — VMValue'yu yine gormeden.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// teng_set_script_vm_v2 + teng_init. Imzasi teng_init ile AYNI (bildirimdeki
// eng_init); tulpar_kopru.cpp bunu derleme aninda kilitler.
int teng_tulpar_init(const char *title, int width, int height);

#ifdef __cplusplus
}
#endif
