// L0 PLATFORM — BASLATMA hatasini kullanicinin GOREBILECEGI bir yere yazar.
//
// NEDEN VAR (olculdu 2026-09-20, yayinlanmis v0.1.0 Windows zip'i):
// engine_editor.exe pencereyi acamayinca stderr'e tek satir basip 1 ile
// cikiyordu. Terminalden calistiran gelistirici sebebi goruyor; .exe'ye CIFT
// TIKLAYAN kullanici ise konsol penceresiyle birlikte o satiri da kaybediyor
// ve ekraninda hicbir sey olmuyor — "program acilmiyor" algisi tam olarak bu.
//
// Cozum hatayi SUSTURMAK degil, IKI YERE birden yazmak:
//   1. stderr — bugunku davranis, bayti bayina ayni (betikler/CI bozulmaz),
//   2. <calisan ikilinin dizini>/engine_hata.log — konsol kapansa bile
//      kullanicinin acip okuyabilecegi, paketin YANINDA duran kalici iz.
// Dizin salt-okunursa (Program Files) 2. adim SESSIZCE atlanir: hata zaten
// 1. adimda verildi, gunluk yazamamak yeni bir ariza degildir.
//
// Paket ayrica her ikili icin bir `.bat` baslatici tasir (tools/package.sh):
// o da hata halinde `pause` ile konsolu ACIK tutar. Ikisi birbirinin yedegi.
#pragma once

namespace tulpar::engine::platform {

// Mesaji stderr'e ve <ikili dizini>/engine_hata.log'a yazar. GERI DONER
// (fatal degil): cagiran kendi cikis kodunu kendisi verir.
void startup_failure(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace tulpar::engine::platform
