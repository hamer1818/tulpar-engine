#include "platform/memory.hpp"

// L0 — ham sanal bellek. POSIX'te mmap/munmap/mprotect, Windows'ta
// VirtualAlloc/VirtualFree/VirtualProtect. Sozlesme ikisinde de ayni:
// os_reserve YAZILABILIR bellek dondurur (motorun arena'si ayirdigi ani
// kullanmaya baslar), os_protect_none erisimi tamamen kapatir (kanarya/
// tuzak sayfalari).
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h> // GetProcessMemoryInfo (os_resident_bytes)
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h> // task_info (os_resident_bytes)
#endif
#endif

namespace tulpar::engine::platform {

size_t os_page_size() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize ? (size_t)si.dwPageSize : 4096;
#else
  long p = sysconf(_SC_PAGESIZE);
  return p > 0 ? (size_t)p : 4096;
#endif
}

void *os_reserve(size_t bytes) {
#if defined(_WIN32)
  // RESERVE|COMMIT birlikte: POSIX'teki MAP_ANONYMOUS eslesmesi. Windows'ta
  // yalniz MEM_RESERVE yapip sayfa sayfa commit etmek daha "dogru" gorunur
  // ama arena hemen yazmaya basliyor; iki asamali kullanim sozlesmeyi
  // platforma gore ayirir. Commit edilen sayfalar sifirlanmis gelir (POSIX
  // anonim eslesme gibi).
  return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  void *p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
#endif
}

void os_release(void *p, size_t bytes) {
#if defined(_WIN32)
  // MEM_RELEASE boyut olarak SIFIR ister ve yalniz VirtualAlloc'un dondurdugu
  // TABAN adresle cagrilabilir — bytes bilerek kullanilmiyor.
  (void)bytes;
  if (p) VirtualFree(p, 0, MEM_RELEASE);
#else
  if (p) munmap(p, bytes);
#endif
}

bool os_protect_none(void *p, size_t bytes) {
#if defined(_WIN32)
  DWORD old = 0;
  return VirtualProtect(p, bytes, PAGE_NOACCESS, &old) != 0;
#else
  return mprotect(p, bytes, PROT_NONE) == 0;
#endif
}

size_t os_resident_bytes() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return (size_t)pmc.WorkingSetSize;
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
  return (size_t)info.resident_size;
#else
  // /proc/self/statm: "boyut yerlesik paylasilan ..." SAYFA cinsinden. Ilk
  // alan SANAL boyut — onu okumak rezerv edilen her bayti "kullanilan"
  // sayardi; ikinci alan yerlesik. fopen yok: FILE malloc ile ayriliyor.
  const int fd = open("/proc/self/statm", O_RDONLY);
  if (fd < 0) return 0;
  char buf[128];
  const ssize_t n = read(fd, buf, sizeof buf - 1);
  close(fd);
  if (n <= 0) return 0;
  buf[n] = 0;
  const char *p = buf;
  while (*p && *p != ' ') p++; // 1. alani (sanal boyut) atla
  while (*p == ' ') p++;
  size_t pages = 0;
  bool any = false;
  for (; *p >= '0' && *p <= '9'; p++) {
    pages = pages * 10 + (size_t)(*p - '0');
    any = true;
  }
  return any ? pages * os_page_size() : 0;
#endif
}

} // namespace tulpar::engine::platform
