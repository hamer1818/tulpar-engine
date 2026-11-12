// L6 APP — BAGIMSIZ KOD PENCERESI (kullanici, 2026-11-12: "sekmeyi ayirip kendi basina gorebiliyorsun; editor kapaninca
// o da kapanmasin, normal tarayici sekmesi gibi").
//
// Kod sekmesi editorun icinde "Gorunum"un yaninda acilir. "Ayri pencerede ac" onu KENDI isletim sistemi penceresine ayirir:
// ayni program `engine_editor --kod <dosya>` ile AYRI SUREC olarak (process_start_detached: kendi oturum/grup, editor
// kapansa da yasar) baslar ve yalniz Kod panelini tam pencerede gosterir (sahne, Oynat, paneller yok). Ayni surec icinde bir
// ImGui pencere olsaydi (multi-viewport) editor kapaninca o da kapanirdi; istenen bu degildi.
//
// Kaydedilmemis degisiklik: pencere kapatma istegi iptal edilir, "Kaydet / Kaydetmeden kapat / Vazgec" sorulur.
// Dosyayi editorle birlikte acik tutmak guvenli: panel ayni dosyayi ~0.5 s'de bir yoklar ve disarida degisince yeniler.
#pragma once
#include <cstddef>
#include <cstdint>

#include "app/editor_app.hpp"
#include "app/editor_ui.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"

namespace tulpar::engine::app {

// Cocuk surecin argumanlari (saf, testli): [exe, --kod, dosya, (--proje p), (--kutuphane k), --size 1100x800, ek...].
// `ek`: bosluklarla ayrik EK arguman dizgisi (test icin TULPAR_KOD_PENCERESI_EK: penceresiz kosum + --out). Donus: argv sayisi
// (null dahil degil), 0 = sigmadi. argv NULL ile biter; dizgiler `tampon`a kopyalanir.
size_t kod_penceresi_argv(const char *exe, const char *dosya, const char *proje, const char *kutuphane, const char *ek, const char **argv,
                          size_t argv_cap, char *tampon, size_t tampon_cap);
// Bagimsiz sureci baslatir (detached). false: err'de sebep (ornegin exe bulunamadi).
bool kod_penceresi_baslat(const char *dosya, const char *proje, const char *kutuphane, char *err, size_t err_cap);

// --- Geri birlestirme (pencere -> editor sekmesi) ------------------------------------------------------------------
// Ayri surec editorle konusamaz; ortak ayar dizinine ISTEK DOSYASI birakir: "istek_<pid>_<n>.txt" = unix saniyesi + dosya yolu.
// Calisan editor dizini yoklar (kod_istekleri_al), yolu sekme olarak acar ve dosyayi SILER; pencere dosyanin yok olmasini bekler.
// Editor yoksa dosya tuketilmez: pencere zaman asiminda onu geri alir ve acik kalir (kayip yok). 60 sn'den eski istek yok sayilir.
bool kod_istegi_yaz(const char *dizin, const char *dosya_yolu, char *istek_yolu, size_t cap);
// Her gecerli istek icin fn(yol, user): true donerse istek dosyasi silinir (tuketildi), false: kalir (editor su an acamiyor).
// Donus: tuketilen istek sayisi.
typedef bool (*KodIstekFn)(const char *yol, void *user);
uint32_t kod_istekleri_al(const char *dizin, KodIstekFn fn, void *user);
// Istek dizini (ayar dizini/kod_istekleri). false: ayar dizini kurulamadi.
bool kod_istek_dizini(char *out, size_t cap);

// `engine_editor --kod` giris noktasi: editor_run'in cihaz/swapchain/ui kurulumundan SONRA cagrilir. Donus: cikis kodu.
int kod_penceresi_calistir(rhi::Device &dev, rhi::Swapchain *swap, rhi::OffscreenTarget *off, const rhi::OffscreenConfig &oc,
                           rhi::OffscreenResult &ores, EditorUi &ui, const EditorHost *host, const EditorOptions &opts, bool headless);

} // namespace tulpar::engine::app
