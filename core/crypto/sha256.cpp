#include "core/crypto/sha256.hpp"

#include <cstring>

namespace tulpar::engine {

namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t load_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Bir ya da daha cok 64 baytlik blok. Mesaj cizelgesi 16 kelimelik halka:
// 64 kelimelik tam dizi yerine yiginda 64 B (cerceve kapisi rahat, onbellek de).
void compress(uint32_t h[8], const uint8_t *p, size_t blocks) {
  for (; blocks; blocks--, p += 64) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = load_be32(p + i * 4);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t wi;
      if (i < 16) {
        wi = w[i];
      } else {
        const uint32_t w15 = w[(i - 15) & 15], w2 = w[(i - 2) & 15];
        const uint32_t s0 = rotr(w15, 7) ^ rotr(w15, 18) ^ (w15 >> 3);
        const uint32_t s1 = rotr(w2, 17) ^ rotr(w2, 19) ^ (w2 >> 10);
        wi = w[i & 15] = w[i & 15] + s0 + w[(i - 7) & 15] + s1;
      }
      const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + S1 + ch + kK[i] + wi;
      const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
}
} // namespace

void Sha256::init() {
  static constexpr uint32_t kH0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::memcpy(h, kH0, sizeof h);
  total = 0;
  buf_len = 0;
}

void Sha256::update(const void *data, size_t n) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  total += n;
  if (buf_len) {
    const size_t take = n < 64 - buf_len ? n : 64 - buf_len;
    std::memcpy(buf + buf_len, p, take);
    buf_len += (uint32_t)take;
    p += take;
    n -= take;
    if (buf_len < 64) return;
    compress(h, buf, 1);
    buf_len = 0;
  }
  if (n >= 64) {
    compress(h, p, n / 64);
    p += n & ~(size_t)63;
    n &= 63;
  }
  if (n) {
    std::memcpy(buf, p, n);
    buf_len = (uint32_t)n;
  }
}

void Sha256::final(uint8_t out[32]) {
  const uint64_t bits = total * 8;
  uint8_t pad[72] = {0x80};
  // 0x80 + sifirlar, uzunluk alani 56. bayta denk gelsin.
  const size_t pad_len = (buf_len < 56 ? 56 - buf_len : 120 - buf_len);
  for (int i = 0; i < 8; i++) pad[pad_len + i] = (uint8_t)(bits >> (56 - 8 * i));
  const uint64_t keep = total; // update() total'i ilerletir; son uzunluk zaten alindi
  update(pad, pad_len + 8);
  total = keep;
  for (int i = 0; i < 8; i++) {
    out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)h[i];
  }
}

void sha256(const void *data, size_t n, uint8_t out[32]) {
  Sha256 s;
  s.init();
  s.update(data, n);
  s.final(out);
}

void sha256_to_hex(const uint8_t sha[32], char out[65]) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = kHex[sha[i] >> 4];
    out[i * 2 + 1] = kHex[sha[i] & 15];
  }
  out[64] = 0;
}

bool sha256_from_hex(const char *hex, size_t len, uint8_t out[32]) {
  if (!hex || len != 64) return false;
  uint8_t tmp[32];
  for (int i = 0; i < 64; i++) {
    const char c = hex[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;
    if (i & 1) tmp[i / 2] = (uint8_t)(tmp[i / 2] | v);
    else tmp[i / 2] = (uint8_t)(v << 4);
  }
  std::memcpy(out, tmp, 32);
  return true;
}

} // namespace tulpar::engine
