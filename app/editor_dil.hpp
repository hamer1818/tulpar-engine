// L6 APP — editör arayüz dili (Türkçe / English). 2026-11-13.
//
// Kaynakta arayüz metinleri Türkçe sabitlerdir (binlerce, dağınık); satır satır sarmak yerine ÇEVİRİ
// ÇİZİM KATMANINDA yapılır: ImGui metin çizimi/ölçümü (third_party/imgui TULPAR YAMASI, imconfig.h
// GImGuiTulparCeviri) her metni sözlükte arar, bulursa İngilizcesini çizer/ölçer; bulamazsa Türkçesi kalır
// (nesne adları, dosya yolları, kullanıcı metni dokunulmaz). Türkçe iken kanca nullptr — bedelsiz.
//
// Sözlük: app/editor_dil_en.inc ("türkçe", "english") çiftleri. Eşleşme kuralları:
//  * Sayılar anahtarda '#' olur ve yerine geri konur: "# varlık" -> "# entities" ("9 varlık", "-3.5 m").
//  * Baştaki simge (Material PUA glifi) ve boşluklar, sondaki boşluklar korunur; yalnız aradaki metin aranır.
//  * Tam metin bulunamazsa " · " ile bölünüp parçalar ayrı çevrilir (durum çubuğu satırları).
// Eksik metinler: TULPAR_DIL_EKSIK=<dosya> ile çalışan editör çevrilemeyen metinleri o dosyaya yazar
// (çeviri turu için: sürücüyle ekranları gez, dosyadaki yeni satırları sözlüğe ekle).
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

enum class Dil : int { Tr = 0, En = 1 };

// Dili uygular (kancayı kurar/kaldırır). Bilinmeyen değer Tr sayılır.
void dil_ayarla(Dil d);
Dil dil_simdiki();
// "tr" / "en" (büyük küçük harf duyarsız, "en-US" gibi önek de olur) -> Dil; tanınmazsa false.
bool dil_ayristir(const char *metin, Dil *d);

// Çeviri çekirdeği (kanca bunu çağırır; testler doğrudan çağırır). Dil Tr iken de çalışır = sözlük araması
// (kanca yalnız En iken kurulur). Dönüş: çevrilmiş aralık (halka tampon, hemen tüketilmeli) ya da aynı aralık.
const char *dil_cevir(const char *bas, const char *son, const char **cevrilmis_son);
// Sözlükte kaç çift var ve kaç tanesi geçerli (anahtar tekrarı = hata; testin pozitif kontrolü).
uint32_t dil_sozluk_sayisi();
uint32_t dil_sozluk_tekrar(); // aynı (sayı normalleşmiş) anahtarın ikinci kez girmesi

} // namespace tulpar::engine::app
