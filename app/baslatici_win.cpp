// L6 APP — Windows baslaticisi "Tulpar Editor.exe" (docs/EDITOR-UYGULAMA-PLANI.md U7b).
//
// NEDEN: engine_editor.exe bir KONSOL uygulamasi (penceresiz kip, --guncelleme, --yeni-oyun, CI ve
// testler stdout'unu okur). Baslat menusunden ya da .sahne'ye cift tiklayarak acilinca yaninda
// siyah bir konsol penceresi belirirdi — "normal uygulama" degil. engine_editor'u GUI alt sistemine
// (-mwindows) cevirmek stdout'a dayanan her seyi riske atardi; bunun yerine bu kucuk GUI-alt-sistemli
// ikili engine_editor.exe'yi CREATE_NO_WINDOW ile, ayni argumanlarla baslatir ve cikar.
// Editorun kendi cocuklari (derleyici, oyun) zaten CREATE_NO_WINDOW ile acilir (platform/process).
// Kurucunun (tools/kurulum/windows.iss) kisayollari ve .sahne iliskilendirmesi BUNU cagirir.
#if defined(_WIN32)
#include <windows.h>

#include <cwchar>

namespace {
// Komut satirindan ilk argumani (bu programin yolu) atla; geri kalan AYNEN aktarilir (tirnaklar dahil).
const wchar_t *argumanlar(const wchar_t *c) {
  if (!c) return L"";
  if (*c == L'"') {
    c++;
    while (*c && *c != L'"') c++;
    if (*c == L'"') c++;
  } else {
    while (*c && *c != L' ' && *c != L'\t') c++;
  }
  while (*c == L' ' || *c == L'\t') c++;
  return c;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t yol[32768];
  const DWORD n = GetModuleFileNameW(nullptr, yol, 32768);
  if (n == 0 || n >= 32768) return 1;
  wchar_t *sl = wcsrchr(yol, L'\\');
  if (!sl) return 1;
  *sl = 0;
  static wchar_t exe[32768], komut[65536];
  _snwprintf(exe, 32768, L"%ls\\engine_editor.exe", yol);
  exe[32767] = 0;
  _snwprintf(komut, 65536, L"\"%ls\" %ls", exe, argumanlar(GetCommandLineW()));
  komut[65535] = 0;
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof pi);
  if (!CreateProcessW(exe, komut, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    wchar_t m[33000];
    _snwprintf(m, 33000, L"Tulpar Edit\u00f6r ba\u015flat\u0131lamad\u0131 (hata %lu):\n%ls", GetLastError(), exe);
    m[32999] = 0;
    MessageBoxW(nullptr, m, L"Tulpar Edit\u00f6r", MB_ICONERROR | MB_OK);
    return 1;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}
#endif
