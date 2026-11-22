// L6 CONTENT — İkili USD (crate, "PXR-USDC") -> USDA metni (2026-11-22).
//
// NEDEN METNE ÇEVİRİ: USDA okuyucusu (content/mesh_import.cpp usd_isle) Mesh / Xform / primvar / leftHanded / upAxis /
// metersPerUnit mantığının TEK kaynağı ve Blender sahnesiyle glTF'e karşı kapılı. İkili okuyucu aynı mantığı ikinci kez yazsaydı iki
// doğruluk kaynağı olurdu; bunun yerine crate ayrıştırılır, okuyucunun anladığı USDA alt kümesi bellekte yazılır ve okuyucuya verilir.
// Kapı: aynı sahnenin usdc'si usda'sıyla BİREBİR aynı modeli vermeli (tests/test_khronos_ornekleri.cpp).
//
// Biçim (OpenUSD pxr/usd/sdf/crateFile.cpp, sürüm >= 0.4.0): önyükleme (8 bayt kimlik + 8 bayt sürüm + TOC ofseti), TOC (16 baytlık
// ad + başlangıç + boy), bölümler TOKENS (LZ4'lü, sıfırla ayrılmış), STRINGS, FIELDS (sıkıştırılmış token indeksleri + LZ4'lü
// ValueRep'ler), FIELDSETS, PATHS (sıkıştırılmış ağaç: yol / öğe tokenı / atlama), SPECS. Tamsayı sıkıştırma Usd_IntegerCompression
// (ortak değer + 2 bitlik kodlar + değişken boylu farklar, sonra TfFastCompression = LZ4 blok). LZ4 burada yazılı (vendor yok).
// Okunan: def/over/class hiyerarşisi, tip adı, özelliklerin `default`u (skaler, vektör, matris, quat, token, dizi; sıkıştırılmış int /
// float dizileri), `variability` (uniform), `interpolation`, sahne `upAxis` / `metersPerUnit`. Atlanan (SAYILIR, sessiz değil):
// referans / payload / variant / inherits, metne dökülemeyen tip (half vb.). Zaman örnekli özellik `.timeSamples` olarak yazılır,
// USDA okuyucusu onu kendi sayacıyla atlar. İlişki (rel) USDA okuyucusundaki gibi sessizce yok sayılır: aynı sahnenin iki biçimi
// aynı notu vermeli.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::content {

// b[0..n) crate. Dönüş: malloc'lu USDA metni (çağıran free eder), *metin_n uzunluk; nullptr = hata (err dolu). *atlanan: metne
// dökülmeyen öğe sayısı.
char *usdc_usda_metni(const unsigned char *b, size_t n, size_t *metin_n, uint32_t *atlanan, char *err, size_t err_cap);

// Test için saf parçalar.
// LZ4 blok çözücü. Dönüş: yazılan bayt, -1 = bozuk girdi / taşma.
long long usdc_lz4_coz(const unsigned char *src, size_t sn, unsigned char *dst, size_t dn);

} // namespace tulpar::engine::content
