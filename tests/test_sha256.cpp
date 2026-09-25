// core/crypto/sha256 — FIPS 180-4 test vektorleri (NIST CSRC "SHA-256
// Examples") + blok sinirlari + akis API'sinin her parcalamada tek cagriyla
// ayni sonucu vermesi. Beklenen degerler NIST'in yayinladigi ozetler;
// sinir uzunluklari (55/56/63/64/119/120 'a') Python hashlib ile uretildi.
#include <cstdio>
#include <cstring>

#include "core/crypto/sha256.hpp"
#include "core/memory/arena.hpp"
#include "platform/time.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
bool hex_is(const uint8_t sha[32], const char *want) {
  char h[65];
  sha256_to_hex(sha, h);
  if (std::strcmp(h, want) == 0) return true;
  std::printf("    ozet   %s\n    beklenen %s\n", h, want);
  return false;
}
} // namespace

ENGINE_TEST(sha256_nist_vectors) {
  uint8_t d[32];
  sha256("", 0, d);
  CHECK(hex_is(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  sha256("abc", 3, d);
  CHECK(hex_is(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  // 448 bit: dolgu ikinci bir blok gerektirir (56 bayt >= 56).
  const char *m448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256(m448, std::strlen(m448), d);
  CHECK(hex_is(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
  // 896 bit.
  const char *m896 =
      "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  sha256(m896, std::strlen(m896), d);
  CHECK(hex_is(d, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
  // 1 000 000 x 'a': akis API'si 1000'lik parcalarla (tek tampon ayirmadan).
  char a1000[1000];
  std::memset(a1000, 'a', sizeof a1000);
  Sha256 s;
  s.init();
  for (int i = 0; i < 1000; i++) s.update(a1000, sizeof a1000);
  s.final(d);
  CHECK(hex_is(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

ENGINE_TEST(sha256_block_boundaries) {
  struct V { size_t n; const char *hex; };
  const V vs[] = {
      {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
      {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
      {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
      {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
      {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
      {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
  };
  char a[1000];
  std::memset(a, 'a', sizeof a);
  for (const V &v : vs) {
    uint8_t d[32];
    sha256(a, v.n, d);
    CHECK(hex_is(d, v.hex));
  }
}

// Akis: ayni veri HER parca boyuyla (1..130, bloklari her sekilde bolerek)
// tek cagriyla ayni ozeti vermeli. Guncelleyici dosyayi kare butcesine gore
// keyfi yerlerden boler; bir sinir hatasi yalniz belli boyda gorunurdu.
ENGINE_TEST(sha256_streaming_matches_one_shot) {
  uint8_t data[1537];
  uint32_t x = 0x12345678u;
  for (uint8_t &b : data) {
    x = x * 1664525u + 1013904223u;
    b = (uint8_t)(x >> 24);
  }
  uint8_t want[32];
  sha256(data, sizeof data, want);
  int bad = 0;
  for (size_t step = 1; step <= 130; step++) {
    Sha256 s;
    s.init();
    for (size_t off = 0; off < sizeof data; off += step) {
      const size_t n = sizeof data - off < step ? sizeof data - off : step;
      s.update(data + off, n);
    }
    uint8_t got[32];
    s.final(got);
    if (std::memcmp(got, want, 32) != 0) bad++;
  }
  CHECK(bad == 0);
  // Pozitif kontrol: tek bayt degisince ozet degismeli (karsilastirma olcuyor).
  data[700] ^= 1;
  uint8_t other[32];
  sha256(data, sizeof data, other);
  CHECK(std::memcmp(other, want, 32) != 0);
}

ENGINE_TEST(sha256_hex_roundtrip_and_rejects) {
  uint8_t d[32], back[32];
  sha256("abc", 3, d);
  char h[65];
  sha256_to_hex(d, h);
  CHECK(sha256_from_hex(h, 64, back) && std::memcmp(d, back, 32) == 0);
  // Buyuk harf de kabul (sha256sum kucuk yazar, elle yazilan buyuk olabilir).
  char up[65];
  for (int i = 0; i < 65; i++) up[i] = (h[i] >= 'a' && h[i] <= 'f') ? (char)(h[i] - 32) : h[i];
  CHECK(sha256_from_hex(up, 64, back) && std::memcmp(d, back, 32) == 0);
  uint8_t keep[32];
  std::memset(keep, 0xAB, 32);
  std::memcpy(back, keep, 32);
  CHECK(!sha256_from_hex(h, 63, back));      // kisa
  CHECK(!sha256_from_hex(h, 65, back));      // uzun
  char bad[65];
  std::memcpy(bad, h, 65);
  bad[10] = 'g';
  CHECK(!sha256_from_hex(bad, 64, back));    // hex degil
  CHECK(std::memcmp(back, keep, 32) == 0);   // basarisizlikta cikti degismez
}

// Olcum: tasinabilir yazimin hizi. Guncelleyicinin poll basina bayt butcesi
// (kUpdHashBudget) bu sayiyla secildi — docs/GUNCELLEME.md "Olcumler".
ENGINE_TEST(sha256_throughput_measured) {
  constexpr size_t kBuf = 8u << 20;
  SystemArena sys;
  CHECK(sys.reserve(kBuf + 4096, "sha256_olcum"));
  uint8_t *buf = sys.alloc_array<uint8_t>(kBuf);
  CHECK(buf != nullptr);
  if (!buf) return;
  for (size_t i = 0; i < kBuf; i++) buf[i] = (uint8_t)(i * 2654435761u >> 13);
  uint8_t d[32];
  sha256(buf, kBuf, d); // isinma
  const int reps = 4;
  const uint64_t t0 = platform::now_ns();
  for (int r = 0; r < reps; r++) sha256(buf, kBuf, d);
  const uint64_t dt = platform::now_ns() - t0;
  test::escape(d);
  const double mbs = (double)(kBuf * reps) / (1024.0 * 1024.0) / ((double)dt / 1e9);
  std::printf("    [olcum] sha256: %.0f MB/s (%d x %zu MB, tasinabilir yazim) -> 1 MB poll butcesi ~%.2f ms\n",
              mbs, reps, kBuf >> 20, 1000.0 / mbs);
  CHECK(mbs > 1.0); // yalniz "calisti" — hiz iddiasi yok, sayi basilir
}
