// L1 CORE — build_info.hpp'nin uygulamasi. TULPAR_SURUM yalniz BU dosyaya
// tanimlanir (set_source_files_properties): surum degisince tek bir nesne
// dosyasi yeniden derlenir, ccache'in geri kalan isabetleri bozulmaz.
#include "core/build_info.hpp"

#ifndef TULPAR_SURUM
#define TULPAR_SURUM ""
#endif

namespace tulpar::engine {

namespace {
// ISARET DIZGISI: surum ikilide "tulpar-engine-surum:<surum>" olarak durur.
// release.yml derlenen ikilide bu diziyi ARAR — "-D verildi ama gomulmedi"
// (yanlis dosyaya tanimlanmis makro, onbellekten gelmis eski nesne) sessiz
// kalmasin diye. build_version() on-ekten sonrasini dondurur.
constexpr char kPrefix[] = "tulpar-engine-surum:";
constexpr char kMarker[] = "tulpar-engine-surum:" TULPAR_SURUM;
} // namespace

const char *build_version() {
  // Isaretin bir tuketicisi olsun: dizi baglayicida atilmaz, grep onu bulur.
  return kMarker + (sizeof kPrefix - 1);
}

const char *build_platform() {
#if defined(__ANDROID__)
#if defined(__aarch64__)
  return "android-arm64";
#else
  return "bilinmiyor";
#endif
#elif defined(_WIN32)
#if defined(__x86_64__) || defined(_M_X64)
  return "windows-x86_64";
#else
  return "bilinmiyor";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
  return "macos-arm64";
#elif defined(__x86_64__)
  return "macos-x86_64";
#else
  return "bilinmiyor";
#endif
#elif defined(__linux__)
#if defined(__x86_64__)
  return "linux-x86_64";
#elif defined(__aarch64__)
  return "linux-aarch64";
#else
  return "bilinmiyor";
#endif
#else
  return "bilinmiyor";
#endif
}

} // namespace tulpar::engine
