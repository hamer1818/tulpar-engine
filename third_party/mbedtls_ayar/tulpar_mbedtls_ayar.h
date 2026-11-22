/* Tulpar Engine — mbedTLS kullanici yapilandirmasi (MBEDTLS_USER_CONFIG_FILE, CMakeLists.txt engine_mbedtls PUBLIC).
 * Vendor edilmis third_party/mbedtls/include/mbedtls/mbedtls_config.h DOKUNULMAZ (tarball ozeti CMakeLists.txt'te).
 *
 * THREADING (2026-11-22): PSA anahtar deposu ve RNG, MBEDTLS_THREADING_C olmadan iki is parcacigindan eszamanli
 * kullanilamaz. HTTPS kapisi (tests/test_http.cpp) ayni surecte istemci + sunucu el sikismasini PARALEL kosturuyor;
 * Windows UCRT64 CI'da (koşum 45050933452) sunucu her baglantida ssl_tls13_calc_finished_core'da PSA_ERROR_BAD_STATE
 * (-0x6C00) ile dustu, Linux / macOS / MINGW64'te zamanlama sansiyla geciyordu. Urunde kopru tek HTTP is parcacigi kullanir,
 * ama editor guncelleyicisi + oyun HTTP'si gibi ikinci bir kullanici eklendigi gun ayni sinif urune tasinir. Mutex'ler
 * pthread (MinGW: winpthreads, Android/macOS/Linux: sistem).
 */
#ifndef TULPAR_MBEDTLS_AYAR_H
#define TULPAR_MBEDTLS_AYAR_H
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
#endif
