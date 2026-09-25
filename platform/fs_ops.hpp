// L0 PLATFORM — dosya/dizin ISLEMLERI (yarat, sil, tasi, gez, oku).
//
// NEDEN AYRI: platform/fs.hpp motorun sicak yolundaki farklari (mkdir, esleme,
// damga) tutuyor ve A-API/CRT ile calisiyor. Bu dosya editor ici
// guncelleyicinin (app/updater) ihtiyaci: bir KURULUM AGACINI guvenle
// degistirmek. Iki kural onu ayri kiliyor:
//
// 1. YOLLAR UTF-8, Windows'ta W-API. Paket kullanicinin indirdigi yerde durur:
//    `C:\Users\<Turkce kullanici adi>\Downloads\tulpar-engine-...` (c-cedilla,
//    yumusak g, noktasiz i). A-API'ler (CreateFileA,
//    MoveFileA, FindFirstFileA) yolu SISTEM KOD SAYFASINDA yorumlar; UTF-8
//    bayt dizisi orada baska harflere (ya da '?'ye) doner ve dosya "yok"
//    gorunur. ASCII yolda iki yol ayni sonucu verir — yani ASCII gecici
//    dizinde kosan bir test farki HIC gormez (Tuzaklar 8cl). Burada her cagri
//    UTF-8 -> UTF-16 cevirip W-API kullanir; uzun yol (>= 240) `\\?\` onekiyle
//    MAX_PATH sinirini asar.
//
// 2. TASIMA HEDEFI EZMEZ. fs_move hedef VARSA basarisiz olur (Windows:
//    MoveFileExW, REPLACE_EXISTING YOK; POSIX: lstat + rename — POSIX rename
//    hedefi SESSIZCE ezerdi). Guncelleyicinin geri alma gunlugu "her tasima
//    tersine cevrilebilir" varsayimina dayanir; ezilen bir dosya geri
//    getirilemezdi. Ayni birim sarti: kopya yok (MOVEFILE_COPY_ALLOWED yok).
//    Windows calisan bir .exe'nin (ve yuklu bir DLL'in) USTUNE yazdirmaz ama
//    ADINI DEGISTIRTIR — guncelleme tasarimi buna dayanir.
//
// Donus sozlesmesi: bool = basari. Hata ayrintisi (errno / GetLastError)
// fs_last_error()'dan okunur (thread-yerel degil: tek thread'li cagiran).
#pragma once

#include <cstddef>
#include <cstdint>

namespace tulpar::engine::platform {

enum class FsKind : uint8_t { None, File, Dir, Other };

// Sembolik bag IZLENMEZ (lstat / reparse noktasi): bag Other doner.
FsKind fs_kind(const char *path);
inline bool fs_exists(const char *path) { return fs_kind(path) != FsKind::None; }
inline bool fs_is_dir(const char *path) { return fs_kind(path) == FsKind::Dir; }
// Dosya boyutu (bayt). Yoksa / dizinse false.
bool fs_size(const char *path, uint64_t *out);

// mkdir -p: eksik her ust dizini yaratir; zaten VAR OLAN dizin basaridir.
// Yolda dosya olan bir bilesen varsa false.
bool fs_mkdir_p(const char *path);
// Tek bos dizin sil.
bool fs_rmdir(const char *path);
// Tek dosya sil (Windows: salt-okunur bit once kaldirilir).
bool fs_remove_file(const char *path);
// Ozyinelemeli sil. Yol yoksa BASARI. Bag izlenmez (bagin kendisi silinir,
// hedefi degil). Silinemeyen bir girdi olursa (Windows'ta acik dosya) gerisini
// silmeye devam eder ve false doner.
bool fs_remove_tree(const char *path);
// Tasi/yeniden adlandir. Hedef VARSA false (yukariya bak). Dosya ya da dizin.
bool fs_move(const char *from, const char *to);

// Dizin gezme: `.` ve `..` verilmez. fn false donerse gezme durur.
using FsDirFn = bool (*)(const char *name, FsKind kind, void *user);
bool fs_list_dir(const char *path, FsDirFn fn, void *user);

// AYIRMASIZ dosya okuma (POSIX open/read, Windows CreateFileW/ReadFile):
// guncelleyici kare basina bayt butcesiyle ozetlerken fopen'in malloc'u
// kare yoluna girmesin.
struct FsFile {
  intptr_t h = -1;
};
bool fs_open_read(FsFile &f, const char *path);
// Okunan bayt; 0 = dosya sonu; < 0 hata.
int64_t fs_read(FsFile &f, void *buf, size_t n);
void fs_close(FsFile &f);

// Butun dosyayi `buf`a oku (en cok cap-1 bayt, NUL eklenir). Donus: okunan
// bayt; -1 = acilamadi/okunamadi. *truncated: dosya cap-1'den buyuktu.
int64_t fs_read_all(const char *path, char *buf, size_t cap, bool *truncated);
// Dosyayi yarat/kes ve yaz.
bool fs_write_all(const char *path, const void *data, size_t n);

// Calisan ikilinin dizini, UTF-8 (Windows: GetModuleFileNameW). platform::
// exe_dir() Windows'ta A-API ile SISTEM KOD SAYFASINDA doner; W-API'ye
// verilecek yol icin BU kullanilir. Donus: uzunluk, 0 = bulunamadi.
size_t fs_exe_dir_utf8(char *buf, size_t n);

// Sistem araci: Windows'ta `%SystemRoot%\System32\<name>.exe` (TAM yol,
// PATH'e BAKILMAZ); POSIX'te PATH'te aranir. Yoksa false.
bool fs_system_tool(const char *name, char *out, size_t cap);

// Salt-okunur isareti (POSIX: yazma bitleri; Windows: READONLY ozniteligi).
// Paketler salt-okunur dosya tasiyabilir (macOS libglfw.3.dylib 0444);
// testler bu durumu yeniden uretmek icin kullanir.
bool fs_set_readonly(const char *path, bool readonly);

// Mutlak yol (UTF-8; POSIX realpath — yol VAR olmali; Windows
// GetFullPathNameW). Testlerin file:// adresi kurmasi icin.
bool fs_abs_path(const char *path, char *out, size_t cap);

// Son basarisiz cagrinin isletim sistemi hata kodu (errno / GetLastError).
int fs_last_error();

} // namespace tulpar::engine::platform
