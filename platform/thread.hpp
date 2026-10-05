// L0 PLATFORM — is parcacigi ilkelleri. Job sistemi (L1) bunun ustune kurulu;
// motorun geri kalani thread'e dogrudan DOKUNMAZ, job kullanir.
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

struct Thread {
  unsigned long handle = 0; // pthread_t; L0 disina sizmasin diye opak
  bool valid = false;
};

using ThreadFn = void (*)(void *arg);

bool thread_create(Thread &t, ThreadFn fn, void *arg, const char *name);
void thread_join(Thread &t);
void thread_yield();
void thread_sleep_us(uint32_t us);
uint32_t cpu_count();
// OLCUM: surecin canli is parcacigi sayisi (cagiran dahil). "Dusen kurulum
// worker'larini birakti mi" sorusunun aleti (tests/test_bridge.cpp, Tuzaklar
// 8cq). Linux/Android /proc/self/status "Threads:", macOS task_threads,
// Windows Toolhelp32 (TH32CS_SNAPTHREAD). Olculemezse 0.
uint32_t os_thread_count();

} // namespace tulpar::engine::platform
