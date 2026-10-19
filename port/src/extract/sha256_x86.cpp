// SHA-256 blocks with the x86 SHA extensions (SHA-NI), several times faster
// than the portable rounds; the disc hashes are most of the extraction's time.
#include "sha256.hpp"

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace phantomcrash::extract {

#if defined(_M_X64) || defined(__x86_64__)

namespace {

alignas(16) constexpr std::uint32_t round_constants[64]{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

bool detect_sha_extensions() {
  int info[4]{};
#ifdef _MSC_VER
  __cpuid(info, 0);
  if (info[0] < 7) {
    return false;
  }
  __cpuidex(info, 7, 0);
  const bool sha = (info[1] & (1 << 29)) != 0;
  __cpuid(info, 1);
#else
  unsigned a{}, b{}, c{}, d{};
  if (__get_cpuid_max(0, nullptr) < 7) {
    return false;
  }
  __cpuid_count(7, 0, a, b, c, d);
  const bool sha = (b & (1u << 29)) != 0;
  __cpuid(1, a, b, c, d);
  info[2] = static_cast<int>(c);
#endif
  const bool sse41 = (info[2] & (1 << 19)) != 0;
  return sha && sse41;
}

} // namespace

bool sha256_hardware_available() {
  static const bool available = detect_sha_extensions();
  return available;
}

// The state words in the A..H order; data holds `count` 64-byte blocks.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sha,sse4.1")))
#endif
void sha256_hardware_blocks(std::array<std::uint32_t, 8> &state, const std::uint8_t *data,
                            std::size_t count) {
  const __m128i byte_swap = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
  __m128i tmp = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[0]));
  __m128i state1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[4]));
  tmp = _mm_shuffle_epi32(tmp, 0xB1);              // CDAB
  state1 = _mm_shuffle_epi32(state1, 0x1B);        // EFGH
  __m128i state0 = _mm_alignr_epi8(tmp, state1, 8); // ABEF
  state1 = _mm_blend_epi16(state1, tmp, 0xF0);     // CDGH

  for (; count > 0; --count, data += 64) {
    const __m128i abef = state0;
    const __m128i cdgh = state1;
    __m128i w[4];
    // Sixteen groups of four rounds; w is the message schedule's ring.
    for (int i = 0; i < 16; ++i) {
      __m128i &current = w[i % 4];
      if (i < 4) {
        current = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 16 * i)),
                                   byte_swap);
      }
      __m128i message = _mm_add_epi32(
          current, _mm_load_si128(reinterpret_cast<const __m128i *>(&round_constants[4 * i])));
      state1 = _mm_sha256rnds2_epu32(state1, state0, message);
      if (i >= 3 && i <= 14) {
        __m128i &next = w[(i + 1) % 4];
        next = _mm_add_epi32(next, _mm_alignr_epi8(current, w[(i + 3) % 4], 4));
        next = _mm_sha256msg2_epu32(next, current);
      }
      message = _mm_shuffle_epi32(message, 0x0E);
      state0 = _mm_sha256rnds2_epu32(state0, state1, message);
      if (i >= 1 && i <= 12) {
        __m128i &previous = w[(i + 3) % 4];
        previous = _mm_sha256msg1_epu32(previous, current);
      }
    }
    state0 = _mm_add_epi32(state0, abef);
    state1 = _mm_add_epi32(state1, cdgh);
  }

  tmp = _mm_shuffle_epi32(state0, 0x1B);       // FEBA
  state1 = _mm_shuffle_epi32(state1, 0xB1);    // DCHG
  state0 = _mm_blend_epi16(tmp, state1, 0xF0); // DCBA
  state1 = _mm_alignr_epi8(state1, tmp, 8);    // HGFE
  _mm_storeu_si128(reinterpret_cast<__m128i *>(&state[0]), state0);
  _mm_storeu_si128(reinterpret_cast<__m128i *>(&state[4]), state1);
}

#else

bool sha256_hardware_available() {
  return false;
}

void sha256_hardware_blocks(std::array<std::uint32_t, 8> &, const std::uint8_t *, std::size_t) {}

#endif

} // namespace phantomcrash::extract
