// Editör arayüz dili (app/editor_dil.hpp). 2026-11-13.
#include "tests/test.hpp"

#include <cstring>

#include "app/editor_dil.hpp"
#include <imgui.h>

using namespace tulpar::engine::app;

namespace {
// Çeviri çekirdeği: metni çevirip std::strcmp ile karşılaştırılabilir tampona koyar.
const char *cev(const char *m, char *out, unsigned cap) {
  const char *son;
  const char *b = dil_cevir(m, m + std::strlen(m), &son);
  const unsigned n = (unsigned)(son - b);
  if (n + 1 > cap) return "<TASTI>";
  std::memcpy(out, b, n);
  out[n] = 0;
  return out;
}
} // namespace

ENGINE_TEST(dil_sozluk_tekrarsiz_ve_dolu) {
  CHECK(dil_sozluk_sayisi() > 0);
  CHECK(dil_sozluk_tekrar() == 0); // ayni (sayi normallesmis) anahtar iki kez girmis olmamali
}

ENGINE_TEST(dil_cekirdek_sayi_simge_ve_parca) {
  char o[512];
  CHECK(!std::strcmp(cev("Dosya", o, sizeof o), "File"));
  CHECK(!std::strcmp(cev("9 varlık", o, sizeof o), "9 entities"));          // sayi geri konur
  CHECK(!std::strcmp(cev("-3.5 varlık", o, sizeof o), "-3.5 entities"));    // eksi + ondalik tek sayi
  CHECK(!std::strcmp(cev("\xEE\xAE\x9B  Dosya  ", o, sizeof o), "\xEE\xAE\x9B  File  ")); // simge + bosluklar korunur
  CHECK(!std::strcmp(cev("Dosya · 12 varlık", o, sizeof o), "File \xC2\xB7 12 entities"));  // " · " parcalari
  CHECK(!std::strcmp(cev("1. Dosya > Başlangıç ekranı > Yeni oyun: oyunun adını yazın, Oluştur'a basın.", o, sizeof o),
                      "1. File > Start screen > New game: type the game's name and press Create."));
  CHECK(!std::strcmp(cev("arena.sahne  (9 varlık)", o, sizeof o), "arena.sahne (9 entities)") ||
        !std::strcmp(cev("arena.sahne  (9 varlık)", o, sizeof o), "arena.sahne  (9 entities)")); // "ad (# varlik)" kalibi
  CHECK(!std::strcmp(cev("yüklendi: editor.sahne", o, sizeof o), "loaded: editor.sahne"));            // "onek: degisken" kalibi
  // Pozitif kontrol: sozlukte olmayan metin AYNEN doner (nesne adi, yol, kullanici metni).
  CHECK(!std::strcmp(cev("kure_1", o, sizeof o), "kure_1"));
  CHECK(!std::strcmp(cev("Sözlükte Yok Bu Cümle", o, sizeof o), "Sözlükte Yok Bu Cümle"));
}

ENGINE_TEST(dil_imgui_kancasi_olcum_ve_ayar) {
  // Yama (imconfig.h GImGuiTulparCeviri) imgui'de DURUYOR mu: En'de "Dosya" ölçüsü "File" ölçüsüne eşit,
  // Tr'de değil (kanca kapalıyken Türkçe ölçülür). Yama kaybolursa bu test kırmızı olur.
  ImGuiContext *ctx = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(200, 200);
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  ImGui::NewFrame();
  dil_ayarla(Dil::Tr);
  const float tr_dosya = ImGui::CalcTextSize("Dosya").x, tr_file = ImGui::CalcTextSize("File").x;
  dil_ayarla(Dil::En);
  const float en_dosya = ImGui::CalcTextSize("Dosya").x;
  dil_ayarla(Dil::Tr);
  CHECK(tr_dosya != tr_file);          // kontrol: iki metin zaten farkli genislikte
  CHECK(en_dosya == tr_file);          // En: "Dosya" olculurken "File" olculdu
  CHECK(dil_simdiki() == Dil::Tr);
  ImGui::EndFrame();
  ImGui::DestroyContext(ctx);
}

ENGINE_TEST(dil_ayristir_ve_ayar_kancasi) {
  Dil d = Dil::Tr;
  CHECK(dil_ayristir("en", &d) && d == Dil::En);
  CHECK(dil_ayristir("EN-us", &d) && d == Dil::En);
  CHECK(dil_ayristir("tr_TR", &d) && d == Dil::Tr);
  CHECK(!dil_ayristir("de", &d));
  CHECK(!dil_ayristir(nullptr, &d));
}
