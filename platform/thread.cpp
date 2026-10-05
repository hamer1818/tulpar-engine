#include "platform/thread.hpp"

// MinGW-w64 winpthreads'i tasidigi icin thread/mutex/sched yolu Windows'ta da
// AYNI pthread kodudur (ayri bir Win32 uyarlamasi yazmiyoruz: ikinci bir kod
// yolu = ikinci bir hata yuzeyi). Platforma ozgu kalan tek sey cekirdek
// sayisi — sysconf POSIX'e ozgu.
#include <atomic>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h> // CreateToolhelp32Snapshot (os_thread_count)
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h> // task_threads (os_thread_count)
#else
#include <fcntl.h>
#include <string.h>
#endif
#endif

namespace tulpar::engine::platform {

namespace {
struct StartArg {
  ThreadFn fn;
  void *arg;
  const char *name;
  std::atomic<bool> copied;
};

void *thread_entry(void *p) {
  StartArg *src = static_cast<StartArg *>(p);
  StartArg a{src->fn, src->arg, src->name, {false}};
  // StartArg cagiran tarafin yiginindaydi; kopyalandi, artik serbest.
  src->copied.store(true, std::memory_order_release);
  if (a.name) {
#if defined(__APPLE__)
    pthread_setname_np(a.name);
#else
    pthread_setname_np(pthread_self(), a.name);
#endif
  }
  a.fn(a.arg);
  return nullptr;
}
} // namespace

bool thread_create(Thread &t, ThreadFn fn, void *arg, const char *name) {
  StartArg a{fn, arg, name, {false}};
  pthread_t h;
  if (pthread_create(&h, nullptr, thread_entry, &a) != 0) return false;
  // Kopyalanana kadar bekle: `a` bu cercevede yasiyor.
  while (!a.copied.load(std::memory_order_acquire)) thread_yield();
  t.handle = (unsigned long)h;
  t.valid = true;
  return true;
}

void thread_join(Thread &t) {
  if (!t.valid) return;
  pthread_join((pthread_t)t.handle, nullptr);
  t.valid = false;
}

void thread_yield() { sched_yield(); }

void thread_sleep_us(uint32_t us) {
  struct timespec ts;
  ts.tv_sec = us / 1000000u;
  ts.tv_nsec = (long)(us % 1000000u) * 1000L;
  nanosleep(&ts, nullptr);
}

uint32_t cpu_count() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors ? (uint32_t)si.dwNumberOfProcessors : 1u;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (uint32_t)n : 1u;
#endif
}

uint32_t os_thread_count() {
#if defined(_WIN32)
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  const DWORD pid = GetCurrentProcessId();
  THREADENTRY32 te;
  te.dwSize = sizeof te;
  uint32_t n = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID == pid) n++;
      te.dwSize = sizeof te;
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  return n;
#elif defined(__APPLE__)
  thread_act_array_t list = nullptr;
  mach_msg_type_number_t n = 0;
  if (task_threads(mach_task_self(), &list, &n) != KERN_SUCCESS) return 0;
  for (mach_msg_type_number_t i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), list[i]);
  vm_deallocate(mach_task_self(), (vm_address_t)list, (vm_size_t)n * sizeof(thread_act_t));
  return (uint32_t)n;
#else
  // /proc/self/status "Threads:\t<n>". fopen yok (FILE malloc ile ayrilir).
  const int fd = open("/proc/self/status", O_RDONLY);
  if (fd < 0) return 0;
  char buf[4096];
  const ssize_t n = read(fd, buf, sizeof buf - 1);
  close(fd);
  if (n <= 0) return 0;
  buf[n] = 0;
  const char *p = strstr(buf, "\nThreads:");
  if (!p) return 0;
  p += 9;
  while (*p == ' ' || *p == '\t') p++;
  uint32_t v = 0;
  bool any = false;
  for (; *p >= '0' && *p <= '9'; p++) {
    v = v * 10 + (uint32_t)(*p - '0');
    any = true;
  }
  return any ? v : 0;
#endif
}

} // namespace tulpar::engine::platform
