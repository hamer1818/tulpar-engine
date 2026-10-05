// L0 PLATFORM — isletim sisteminden bellek. YALNIZ acilista cagrilir
// (SystemArena rezervi, fiber yiginlari). Kare icinde cagrilmaz (A2).
#pragma once
#include <cstddef>

namespace tulpar::engine::platform {

size_t os_page_size();
// Okunur-yazilir anonim bellek; sayfa hizali. Basarisizlikta nullptr.
void *os_reserve(size_t bytes);
void os_release(void *p, size_t bytes);
// Bir sayfa araligini erisilmez yapar (fiber yigini tasma bekcisi).
bool os_protect_none(void *p, size_t bytes);

// OLCUM (yukaridaki "yalniz acilista" kuralinin DISINDA): surecin YERLESIK
// bellegi (RSS), bayt. Sanal boyut DEGIL — rezerv edilip dokunulmamis sayfa
// sayilmaz (test_memory.cpp bunu ayirt ediyor). Linux/Android
// /proc/self/statm (2. alan), macOS/iOS task_info, Windows
// GetProcessMemoryInfo (WorkingSetSize). Ayirma yapmaz (yigin tamponu, ham
// open/read): kare icinde cagrilabilir. Olculemezse 0.
size_t os_resident_bytes();
// OLCUM: surecin SANAL boyutu, bayt — rezerv edilip dokunulmamis sayfa da
// SAYILIR (RSS'in tersi). 512 MB'lik SystemArena rezervi RSS'te gorunmez,
// burada gorunur: "yarim kurulum arenayi geri birakti mi" sorusunun aleti
// (tests/test_bridge.cpp, Tuzaklar 8cq). Linux/Android /proc/self/statm
// (1. alan), macOS task_info virtual_size, Windows GetProcessMemoryInfo
// PrivateUsage (commit yuku; os_reserve MEM_COMMIT ile ayirdigi icin rezerv
// orada da sayilir). Ayirma yapmaz. Olculemezse 0.
size_t os_virtual_bytes();

} // namespace tulpar::engine::platform
