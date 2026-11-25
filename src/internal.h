#ifndef CIMA_INTERNAL_H
#define CIMA_INTERNAL_H

#include <cima/json.h>
#include <stdckdint.h>
#include <stdcountof.h>
#include <stdio.h>
#include <string.h>

#define LIKELY(x) CILO_LIKELY(x)
#define UNLIKELY(x) CILO_UNLIKELY(x)

static inline cilo_allocator *cima__heap(void) { return CILO_OF(&cilo_heap); }

static inline const cima_schema *cima__deref(const cima_schema *s) {
  while (s->kind == CIMA_KIND_REF) s = s->inner;
  return s;
}

/* ========================================================================================================== */
/* lexing, shared by the typed parser and the dom.                                                            */
/* ========================================================================================================== */

static inline const char *cima__ws(const char *p, const char *end) {
  while (p < end && (unsigned char)*p <= ' ' && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// the length of the utf-8 sequence at p, whose lead byte is past ascii, or zero when it is not valid: no overlong
// forms, no surrogates, nothing past U+10FFFF.
static inline size_t cima__utf8_seq(const unsigned char *p, const unsigned char *end) {
  unsigned c = p[0];
  size_t left = (size_t)(end - p);
  if (c < 0xc2) return 0;
  if (c < 0xe0) return left >= 2 && (p[1] & 0xc0) == 0x80 ? 2 : 0;
  if (c < 0xf0) {
    if (left < 3 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80) return 0;
    if ((c == 0xe0 && p[1] < 0xa0) || (c == 0xed && p[1] >= 0xa0)) return 0;
    return 3;
  }
  if (c < 0xf5) {
    if (left < 4 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80) return 0;
    if ((c == 0xf0 && p[1] < 0x90) || (c == 0xf4 && p[1] >= 0x90)) return 0;
    return 4;
  }
  return 0;
}

static inline uint64_t cima__load64(const char *p) {
  uint64_t v;
  memcpy(&v, p, 8);
  return v;
}

static inline uint32_t cima__load32(const char *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// short keys and strings compared and copied without a call: two overlapping words or blocks cover them and
// never touch a byte past `len`.
static inline bool cima__same(const char *a, const char *b, size_t len) {
  if (len > 32) return memcmp(a, b, len) == 0;
  if (len > 16) {
    uint64_t x = (cima__load64(a) ^ cima__load64(b)) | (cima__load64(a + 8) ^ cima__load64(b + 8));
    x |= cima__load64(a + len - 16) ^ cima__load64(b + len - 16);
    x |= cima__load64(a + len - 8) ^ cima__load64(b + len - 8);
    return !x;
  }
  if (len >= 8) return cima__load64(a) == cima__load64(b) && cima__load64(a + len - 8) == cima__load64(b + len - 8);
  if (len >= 4) return cima__load32(a) == cima__load32(b) && cima__load32(a + len - 4) == cima__load32(b + len - 4);
  return !len || (a[0] == b[0] && a[len / 2] == b[len / 2] && a[len - 1] == b[len - 1]);
}

[[gnu::always_inline]] static inline void cima__copy(char *dst, const char *src, size_t len) {
  if (len > 32) {
    memcpy(dst, src, len);
  } else if (len > 16) {
    // file names, urls and sentences: two overlapping blocks, still no call.
    __builtin_memcpy_inline(dst, src, 16);
    __builtin_memcpy_inline(dst + len - 16, src + len - 16, 16);
  } else if (len >= 8) {
    uint64_t head = cima__load64(src), tail = cima__load64(src + len - 8);
    memcpy(dst, &head, 8);
    memcpy(dst + len - 8, &tail, 8);
  } else if (len >= 4) {
    uint32_t head = cima__load32(src), tail = cima__load32(src + len - 4);
    memcpy(dst, &head, 4);
    memcpy(dst + len - 4, &tail, 4);
  } else if (len) {
    char first = src[0], middle = src[len / 2], last = src[len - 1];
    dst[0] = first;
    dst[len / 2] = middle;
    dst[len - 1] = last;
  }
}

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the string scanners read words little endian");

// vector paths per cpu, every one with a scalar twin that other cpus take and CIMA_NO_SIMD forces. code outside
// this file sees 16 byte blocks as cima__v16 through the helpers below, which each cpu provides: neon on arm64,
// and ssse3 on x86 where the build may count on it, -march=native or x86-64-v2 for instance. the lookups that
// check utf-8 need its byte shuffle.
#if defined(__ARM_NEON) && defined(__aarch64__) && !defined(CIMA_NO_SIMD)
#include <arm_neon.h>
#define CIMA__NEON 1
#else
#define CIMA__NEON 0
#endif
#if defined(__SSSE3__) && !CIMA__NEON && !defined(CIMA_NO_SIMD)
#include <immintrin.h>
#define CIMA__SSE 1
#else
#define CIMA__SSE 0
#endif
#define CIMA__SIMD (CIMA__NEON || CIMA__SSE)

constexpr uint64_t CIMA__ONES = 0x0101010101010101ull;
constexpr uint64_t CIMA__HIGHS = 0x8080808080808080ull;

// a high bit on the first byte of `v` that is a quote, a backslash, a control character or past ascii. only the
// lowest set bit is exact, borrows may mark bytes after it.
static inline uint64_t cima__special(uint64_t v) {
  uint64_t q = v ^ (CIMA__ONES * '"');
  uint64_t b = v ^ (CIMA__ONES * '\\');
  return ((q - CIMA__ONES) & ~q) | ((b - CIMA__ONES) & ~b) | ((v - CIMA__ONES * 0x20) & ~v) | v;
}

#if CIMA__NEON
typedef uint8x16_t cima__v16;

static inline cima__v16 cima__load16(const char *p) { return vld1q_u8((const uint8_t *)p); }
static inline void cima__store16(char *p, cima__v16 v) { vst1q_u8((uint8_t *)p, v); }
static inline bool cima__any16(cima__v16 v) { return vmaxvq_u8(v) != 0; }

// the first k bytes of v at p, k below 16, and nothing past them. returns p + k.
static inline char *cima__store_first16(char *p, cima__v16 v, unsigned k) {
  if (k & 8) {
    vst1_u8((uint8_t *)p, vget_low_u8(v));
    v = vextq_u8(v, v, 8);
    p += 8;
  }
  uint64_t x = vgetq_lane_u64(vreinterpretq_u64_u8(v), 0);
  if (k & 4) {
    uint32_t y = (uint32_t)x;
    memcpy(p, &y, 4);
    x >>= 32;
    p += 4;
  }
  if (k & 2) {
    uint16_t y = (uint16_t)x;
    memcpy(p, &y, 2);
    x >>= 16;
    p += 2;
  }
  if (k & 1) *p++ = (char)x;
  return p;
}

// the masks below hold a nibble a lane, the first lane lowest: cima__lane gives the first one set.
static inline uint64_t cima__nibbles(uint8x16_t m) {
  return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
}

static inline unsigned cima__lane(uint64_t mask) { return (unsigned)__builtin_ctzll(mask) >> 2; }

// cima__special of 16 bytes, exact on every lane: a signed compare takes control characters and bytes past ascii
// at once.
static inline uint64_t cima__special16(cima__v16 v) {
  uint8x16_t m = vorrq_u8(vceqq_u8(v, vdupq_n_u8('"')), vceqq_u8(v, vdupq_n_u8('\\')));
  return cima__nibbles(vorrq_u8(m, vcltq_s8(vreinterpretq_s8_u8(v), vdupq_n_s8(0x20))));
}

// what ends a run of text that utf-8 checks can go through: quotes, backslashes and control characters.
static inline uint64_t cima__stops16(cima__v16 v) {
  uint8x16_t m = vorrq_u8(vceqq_u8(v, vdupq_n_u8('"')), vceqq_u8(v, vdupq_n_u8('\\')));
  return cima__nibbles(vorrq_u8(m, vcltq_u8(v, vdupq_n_u8(0x20))));
}

// the first k bytes of v, the others zero.
static inline cima__v16 cima__first16(cima__v16 v, unsigned k) {
  static const uint8_t iota[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  return vandq_u8(v, vcgtq_u8(vdupq_n_u8((uint8_t)k), vld1q_u8(iota)));
}

// utf-8 checked 16 bytes at a time, after keiser and lemire: three lookups classify every byte with the one
// before it, and the bytes two and three back tell which continuations a lead asks for. bytes before the first
// block count as ascii.
typedef struct cima__utf8 {
  cima__v16 prev, error, incomplete;
} cima__utf8;

[[gnu::always_inline]] static inline void cima__utf8_next(cima__utf8 *s, cima__v16 in) {
  if (vmaxvq_u8(in) < 0x80) {
    s->error = vorrq_u8(s->error, s->incomplete);
    s->incomplete = vdupq_n_u8(0);
    s->prev = in;
    return;
  }
  enum : uint8_t {
    TOO_SHORT = 1, TOO_LONG = 2, OVERLONG_3 = 4, TOO_LARGE = 8, SURROGATE = 16, OVERLONG_2 = 32,
    TOO_LARGE_1000 = 64, OVERLONG_4 = 64, TWO_CONTS = 128, CARRY = TOO_SHORT | TOO_LONG | TWO_CONTS,
  };
  static const uint8_t high1[16] = {
    TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG,
    TWO_CONTS, TWO_CONTS, TWO_CONTS, TWO_CONTS,
    TOO_SHORT | OVERLONG_2, TOO_SHORT, TOO_SHORT | OVERLONG_3 | SURROGATE,
    TOO_SHORT | TOO_LARGE | TOO_LARGE_1000 | OVERLONG_4,
  };
  static const uint8_t low1[16] = {
    CARRY | OVERLONG_3 | OVERLONG_2 | OVERLONG_4, CARRY | OVERLONG_2, CARRY, CARRY,
    CARRY | TOO_LARGE, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000 | SURROGATE, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000,
  };
  static const uint8_t high2[16] = {
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE_1000 | OVERLONG_4,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
  };
  static const uint8_t last[16] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0xf0 - 1, 0xe0 - 1, 0xc0 - 1,
  };
  uint8x16_t prev1 = vextq_u8(s->prev, in, 15);
  uint8x16_t special = vandq_u8(vandq_u8(vqtbl1q_u8(vld1q_u8(high1), vshrq_n_u8(prev1, 4)),
                                         vqtbl1q_u8(vld1q_u8(low1), vandq_u8(prev1, vdupq_n_u8(0x0f)))),
                                vqtbl1q_u8(vld1q_u8(high2), vshrq_n_u8(in, 4)));
  // the second byte after a lead of three or four, and the third after a lead of four.
  uint8x16_t third = vqsubq_u8(vextq_u8(s->prev, in, 14), vdupq_n_u8(0xe0 - 0x80));
  uint8x16_t fourth = vqsubq_u8(vextq_u8(s->prev, in, 13), vdupq_n_u8(0xf0 - 0x80));
  uint8x16_t must = vandq_u8(vorrq_u8(third, fourth), vdupq_n_u8(0x80));
  s->error = vorrq_u8(s->error, veorq_u8(must, special));
  s->incomplete = vqsubq_u8(in, vld1q_u8(last));
  s->prev = in;
}

static inline bool cima__utf8_bad(const cima__utf8 *s) { return cima__any16(vorrq_u8(s->error, s->incomplete)); }
#elif CIMA__SSE
typedef __m128i cima__v16;

static inline cima__v16 cima__load16(const char *p) { return _mm_loadu_si128((const __m128i *)(const void *)p); }
static inline void cima__store16(char *p, cima__v16 v) { _mm_storeu_si128((__m128i *)(void *)p, v); }
static inline bool cima__any16(cima__v16 v) {
  return _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_setzero_si128())) != 0xffff;
}

// the first k bytes of v at p, k below 16, and nothing past them. returns p + k.
static inline char *cima__store_first16(char *p, cima__v16 v, unsigned k) {
  if (k & 8) {
    _mm_storel_epi64((__m128i *)(void *)p, v);
    v = _mm_srli_si128(v, 8);
    p += 8;
  }
  uint64_t x = (uint64_t)_mm_cvtsi128_si64(v);
  if (k & 4) {
    uint32_t y = (uint32_t)x;
    memcpy(p, &y, 4);
    x >>= 32;
    p += 4;
  }
  if (k & 2) {
    uint16_t y = (uint16_t)x;
    memcpy(p, &y, 2);
    x >>= 16;
    p += 2;
  }
  if (k & 1) *p++ = (char)x;
  return p;
}

// the masks below hold a bit a lane, the first lane lowest: cima__lane gives the first one set.
static inline unsigned cima__lane(uint64_t mask) { return (unsigned)__builtin_ctzll(mask); }

static inline __m128i cima__quotes16(cima__v16 v) {
  return _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8('"')), _mm_cmpeq_epi8(v, _mm_set1_epi8('\\')));
}

// cima__special of 16 bytes, exact on every lane: a signed compare takes control characters and bytes past ascii
// at once.
static inline uint64_t cima__special16(cima__v16 v) {
  __m128i m = _mm_or_si128(cima__quotes16(v), _mm_cmplt_epi8(v, _mm_set1_epi8(0x20)));
  return (uint64_t)(unsigned)_mm_movemask_epi8(m);
}

// what ends a run of text that utf-8 checks can go through: quotes, backslashes and control characters.
static inline uint64_t cima__stops16(cima__v16 v) {
  __m128i control = _mm_cmpeq_epi8(_mm_min_epu8(v, _mm_set1_epi8(0x1f)), v);
  return (uint64_t)(unsigned)_mm_movemask_epi8(_mm_or_si128(cima__quotes16(v), control));
}

// the first k bytes of v, the others zero.
static inline cima__v16 cima__first16(cima__v16 v, unsigned k) {
  const __m128i iota = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
  return _mm_and_si128(v, _mm_cmpgt_epi8(_mm_set1_epi8((char)k), iota));
}

// utf-8 checked 16 bytes at a time, after keiser and lemire, as the neon one above: its tables and its steps.
typedef struct cima__utf8 {
  cima__v16 prev, error, incomplete;
} cima__utf8;

[[gnu::always_inline]] static inline void cima__utf8_next(cima__utf8 *s, cima__v16 in) {
  if (!_mm_movemask_epi8(in)) {
    s->error = _mm_or_si128(s->error, s->incomplete);
    s->incomplete = _mm_setzero_si128();
    s->prev = in;
    return;
  }
  enum : uint8_t {
    TOO_SHORT = 1, TOO_LONG = 2, OVERLONG_3 = 4, TOO_LARGE = 8, SURROGATE = 16, OVERLONG_2 = 32,
    TOO_LARGE_1000 = 64, OVERLONG_4 = 64, TWO_CONTS = 128, CARRY = TOO_SHORT | TOO_LONG | TWO_CONTS,
  };
  alignas(16) static const uint8_t high1[16] = {
    TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG, TOO_LONG,
    TWO_CONTS, TWO_CONTS, TWO_CONTS, TWO_CONTS,
    TOO_SHORT | OVERLONG_2, TOO_SHORT, TOO_SHORT | OVERLONG_3 | SURROGATE,
    TOO_SHORT | TOO_LARGE | TOO_LARGE_1000 | OVERLONG_4,
  };
  alignas(16) static const uint8_t low1[16] = {
    CARRY | OVERLONG_3 | OVERLONG_2 | OVERLONG_4, CARRY | OVERLONG_2, CARRY, CARRY,
    CARRY | TOO_LARGE, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000 | SURROGATE, CARRY | TOO_LARGE | TOO_LARGE_1000,
    CARRY | TOO_LARGE | TOO_LARGE_1000,
  };
  alignas(16) static const uint8_t high2[16] = {
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE_1000 | OVERLONG_4,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | OVERLONG_3 | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_LONG | OVERLONG_2 | TWO_CONTS | SURROGATE | TOO_LARGE,
    TOO_SHORT, TOO_SHORT, TOO_SHORT, TOO_SHORT,
  };
  alignas(16) static const uint8_t last[16] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0xf0 - 1, 0xe0 - 1, 0xc0 - 1,
  };
  const __m128i nibble = _mm_set1_epi8(0x0f);
  // the byte before each one, the last of the block before first.
  __m128i prev1 = _mm_alignr_epi8(in, s->prev, 15);
  __m128i special = _mm_and_si128(
    _mm_and_si128(
      _mm_shuffle_epi8(_mm_load_si128((const __m128i *)(const void *)high1),
                       _mm_and_si128(_mm_srli_epi16(prev1, 4), nibble)),
      _mm_shuffle_epi8(_mm_load_si128((const __m128i *)(const void *)low1), _mm_and_si128(prev1, nibble))),
    _mm_shuffle_epi8(_mm_load_si128((const __m128i *)(const void *)high2),
                     _mm_and_si128(_mm_srli_epi16(in, 4), nibble)));
  // the second byte after a lead of three or four, and the third after a lead of four.
  __m128i third = _mm_subs_epu8(_mm_alignr_epi8(in, s->prev, 14), _mm_set1_epi8((char)(0xe0 - 0x80)));
  __m128i fourth = _mm_subs_epu8(_mm_alignr_epi8(in, s->prev, 13), _mm_set1_epi8((char)(0xf0 - 0x80)));
  __m128i must = _mm_and_si128(_mm_or_si128(third, fourth), _mm_set1_epi8((char)0x80));
  s->error = _mm_or_si128(s->error, _mm_xor_si128(must, special));
  s->incomplete = _mm_subs_epu8(in, _mm_load_si128((const __m128i *)(const void *)last));
  s->prev = in;
}

static inline bool cima__utf8_bad(const cima__utf8 *s) {
  return cima__any16(_mm_or_si128(s->error, s->incomplete));
}
#endif

// the first byte from p on that a string does not hold as it is, in text padded with nul bytes. for runs past
// their first bytes, which short strings settle with a branch a byte. words beat neon blocks here on an m4: most
// runs end within a few words, and a vector mask takes longer to reach a branch.
static inline const char *cima__special_run(const char *p) {
  for (;;) {
    uint64_t m = cima__special(cima__load64(p)) & CIMA__HIGHS;
    if (m) return p + (__builtin_ctzll(m) >> 3);
    p += 8;
  }
}

static inline int cima__hex(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c |= 0x20;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static inline int32_t cima__hex4(const char *p) {
  int a = cima__hex((unsigned char)p[0]), b = cima__hex((unsigned char)p[1]);
  int c = cima__hex((unsigned char)p[2]), d = cima__hex((unsigned char)p[3]);
  if ((a | b | c | d) < 0) return -1;
  return a << 12 | b << 8 | c << 4 | d;
}

typedef struct cima__str {
  // the raw body between the quotes.
  const char *ptr;
  size_t len;
  bool escaped;
} cima__str;

// the scan of a string that needs more than the inlined part, from `p` on. everything from `start` to `p` is plain.
const char *cima__scan_string_rest(const char *start, const char *p, const char *end, cima__str *out,
                                   const char **err);

// scans a string whose opening quote is at p. returns what follows the closing quote, or null with `*err` set.
// escapes, surrogate pairs and utf-8 are validated here, so decoding them later cannot fail.
static inline const char *cima__scan_string(const char *p, const char *end, cima__str *out, const char **err) {
  return cima__scan_string_rest(p + 1, p + 1, end, out, err);
}

// the same with plain ascii, most keys and strings, found eight bytes at a time without leaving the caller. it
// pays in the dom and the skipper, the typed parser is faster calling out. on an m4, neon compares lose to these
// words for short strings: moving a vector mask to a register costs more than the scan.
static inline const char *cima__scan_string_inline(const char *p, const char *end, cima__str *out,
                                                   const char **err) {
  const char *start = ++p;
  while (LIKELY(end - p >= 8)) {
    uint64_t m = cima__special(cima__load64(p)) & CIMA__HIGHS;
    if (m) {
      p += __builtin_ctzll(m) >> 3;
      if (LIKELY(*p == '"')) {
        *out = (cima__str){start, (size_t)(p - start), false};
        return p + 1;
      }
      break;
    }
    p += 8;
  }
  return cima__scan_string_rest(start, p, end, out, err);
}

static inline char *cima__put_utf8(char *d, uint32_t u) {
  if (u < 0x80) {
    *d++ = (char)u;
  } else if (u < 0x800) {
    *d++ = (char)(0xc0 | u >> 6);
    *d++ = (char)(0x80 | (u & 0x3f));
  } else if (u < 0x10000) {
    *d++ = (char)(0xe0 | u >> 12);
    *d++ = (char)(0x80 | ((u >> 6) & 0x3f));
    *d++ = (char)(0x80 | (u & 0x3f));
  } else {
    *d++ = (char)(0xf0 | u >> 18);
    *d++ = (char)(0x80 | ((u >> 12) & 0x3f));
    *d++ = (char)(0x80 | ((u >> 6) & 0x3f));
    *d++ = (char)(0x80 | (u & 0x3f));
  }
  return d;
}

// decodes a scanned body into `dst`, which holds `len` bytes: decoding never grows. returns the decoded length.
static inline size_t cima__unescape(char *dst, const char *src, size_t len) {
  const char *end = src + len;
  char *d = dst;
  while (src < end) {
    const char *bs = memchr(src, '\\', (size_t)(end - src));
    if (!bs) bs = end;
    memmove(d, src, (size_t)(bs - src));
    d += bs - src;
    src = bs;
    if (src >= end) break;
    char e = src[1];
    src += 2;
    switch (e) {
    case 'b': *d++ = '\b'; break;
    case 'f': *d++ = '\f'; break;
    case 'n': *d++ = '\n'; break;
    case 'r': *d++ = '\r'; break;
    case 't': *d++ = '\t'; break;
    case 'u': {
      uint32_t u = (uint32_t)cima__hex4(src);
      src += 4;
      if (u >= 0xd800 && u <= 0xdbff) {
        uint32_t lo = (uint32_t)cima__hex4(src + 2);
        src += 6;
        u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
      }
      d = cima__put_utf8(d, u);
      break;
    }
    default: *d++ = e; break;
    }
  }
  return (size_t)(d - dst);
}

/* numbers. */

typedef struct cima__num {
  // value = mant * 10^exp10, give or take the digits that did not fit when `inexact`.
  uint64_t mant;
  int32_t exp10;
  bool neg;
  // no fraction and no exponent in the text.
  bool plain;
  // nonzero digits were dropped.
  bool inexact;
} cima__num;

static inline bool cima__digit(const char *p, const char *end) { return p < end && *p >= '0' && *p <= '9'; }

// the count of leading digits in eight bytes and their value. a byte past ascii or below '0' borrows only into
// the bytes after it, so the first flagged byte is exact.
static inline uint32_t cima__lead_digits(uint64_t v, uint32_t *value) {
  uint64_t bad = ((v + CIMA__ONES * 0x46) | (v - CIMA__ONES * '0')) & CIMA__HIGHS;
  uint32_t k = bad ? (uint32_t)__builtin_ctzll(bad) >> 3 : 8;
  // the digits move to the top bytes, zeros before them read as leading zeros.
  v = k ? (v & CIMA__ONES * 0x0f) << (64 - 8 * k) : 0;
  v = v * 10 + (v >> 8);
  uint64_t lanes = 0x000000ff000000ffull;
  v = ((v & lanes) * (100 + (1000000ull << 32)) + ((v >> 16) & lanes) * (1 + (10000ull << 32))) >> 32;
  *value = (uint32_t)v;
  return k;
}

static const uint32_t cima__pow10_u32[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

// adds the digits at *pp to m a word at a time, while that cannot overflow and the words end before `end`.
// returns the byte after them when it was in the last word read, saving a load, otherwise -1 and the caller goes
// on a digit at a time.
static inline int cima__digits8(const char **pp, const char *end, uint64_t *m) {
  const char *p = *pp;
  int next = -1;
  // two words at once first: a fraction of up to sixteen digits then needs no load that waits on another. 1843 is
  // (2^64 - 1 - (10^16 - 1)) / 10^16.
  if (end - p >= 16 && *m <= 1843) {
    uint64_t v = cima__load64(p), w = cima__load64(p + 8);
    uint32_t a, b, ka = cima__lead_digits(v, &a), kb = cima__lead_digits(w, &b);
    if (ka < 8) {
      *m = *m * cima__pow10_u32[ka] + a;
      *pp = p + ka;
      return (uint8_t)(v >> 8 * ka);
    }
    *m = (*m * 100000000 + a) * cima__pow10_u32[kb] + b;
    p += 8 + kb;
    if (kb < 8) {
      *pp = p;
      return (uint8_t)(w >> 8 * kb);
    }
  }
  // (2^64 - 1 - 99999999) / 10^8.
  while (end - p >= 8 && *m <= 184467440736ull) {
    uint64_t v = cima__load64(p);
    uint32_t value, k = cima__lead_digits(v, &value);
    *m = *m * cima__pow10_u32[k] + value;
    p += k;
    if (k < 8) {
      next = (uint8_t)(v >> 8 * k);
      break;
    }
  }
  *pp = p;
  return next;
}

// m * 10 + d fits in 64 bits.
static inline bool cima__fits(uint64_t m, unsigned d) {
  return m < 1844674407370955161ull || (m == 1844674407370955161ull && d <= 5);
}

// scans a json number at p, returns its end or null.
static inline const char *cima__scan_number(const char *p, const char *end, cima__num *n) {
  uint64_t m = 0;
  int32_t drop = 0, frac = 0;
  bool full = false, inexact = false, neg = false, plain = true;
  if (p < end && *p == '-') {
    neg = true;
    p++;
  }
  if (p >= end) return nullptr;
  // the byte after what was read, -1 past the end.
  int c;
  if (*p == '0') {
    p++;
    c = p < end ? (uint8_t)*p : -1;
  } else if (*p >= '1' && *p <= '9') {
    c = cima__digits8(&p, end, &m);
    if (c < 0) {
      for (; cima__digit(p, end); p++) {
        unsigned d = (unsigned)(*p - '0');
        if (!full && cima__fits(m, d)) {
          m = m * 10 + d;
          continue;
        }
        full = true;
        drop++;
        inexact |= d != 0;
      }
      c = p < end ? (uint8_t)*p : -1;
    }
  } else {
    return nullptr;
  }
  if (c == '.') {
    plain = false;
    const char *digits = ++p;
    c = -1;
    if (!full) {
      c = cima__digits8(&p, end, &m);
      frac += (int32_t)(p - digits);
    }
    if (c < 0) {
      for (; cima__digit(p, end); p++) {
        unsigned d = (unsigned)(*p - '0');
        if (!full) {
          if (cima__fits(m, d)) {
            m = m * 10 + d;
            frac++;
            continue;
          }
          full = true;
        }
        inexact |= d != 0;
      }
      c = p < end ? (uint8_t)*p : -1;
    }
    if (p == digits) return nullptr;
  }
  int64_t e = 0;
  if ((c | 0x20) == 'e') {
    plain = false;
    p++;
    bool eneg = false;
    if (p < end && (*p == '+' || *p == '-')) eneg = *p++ == '-';
    if (!cima__digit(p, end)) return nullptr;
    while (cima__digit(p, end)) {
      if (e < 1000000000) e = e * 10 + (*p - '0');
      p++;
    }
    if (eneg) e = -e;
  }
  e += drop - frac;
  if (e > INT32_MAX / 2) e = INT32_MAX / 2;
  if (e < INT32_MIN / 2) e = INT32_MIN / 2;
  *n = (cima__num){.mant = m, .exp10 = (int32_t)e, .neg = neg, .plain = plain, .inexact = inexact};
  return p;
}

typedef struct cima__lex_error {
  const char *msg;
  const char *at;
} cima__lex_error;

// skips one value, checking its syntax, and returns what follows it or null with `err` set.
const char *cima__skip(const char *p, const char *end, uint32_t depth, cima__lex_error *err);
// the same over padded text, see CIMA__PAD.
const char *cima__pad_skip(const char *p, const char *end, uint32_t depth, cima__lex_error *err);
// cima__scan_string_rest over padded text: from p, past the plain start of the string at `body`.
const char *cima__pad_string_rest(const char *body, const char *p, const char *end, cima__str *out,
                                  const char **err);

typedef enum cima__int_status : uint8_t { CIMA__INT_OK, CIMA__INT_FRACTION, CIMA__INT_RANGE } cima__int_status;

// the magnitude of `n` when it is an integer, exactly.
cima__int_status cima__num_magnitude(const cima__num *n, uint64_t *out);
// correctly rounded, locale independent. `tok` is the text `n` came from.
double cima__num_double(const cima__num *n, const char *tok, size_t len);
// writes the digits of `v` into `out`, which holds 20 bytes, and returns their count.
size_t cima__fmt_u64(char *out, uint64_t v);
// the shortest text reading back as `v`, at most 32 bytes. `v` is finite.
size_t cima__fmt_double(char *out, double v);

/* ========================================================================================================== */
/* lexing text followed by nuls, CIMA__PAD of them: scans read whole words past the end without checks, a nul  */
/* is never valid where one stops. both parsers work on such a copy.                                          */
/* ========================================================================================================== */

constexpr size_t CIMA__PAD = 64;

// bytes a string holds as they are: printable ascii but the quote and the backslash.
static const bool cima__plain[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// whitespace. indented text puts a newline and a run of spaces between tokens: the spaces go a word at a time,
// and branches on them predict well since the depth of a document changes slowly.
[[gnu::always_inline]] static inline const char *cima__pad_ws(const char *p) {
  for (;;) {
    unsigned char c = (unsigned char)*p;
    if (LIKELY(c > ' ')) return p;
    if (c == ' ') {
      uint64_t x = cima__load64(p) ^ (CIMA__ONES * ' ');
      if (!x) {
        p += 8;
        continue;
      }
      p += (unsigned)__builtin_ctzll(x) >> 3;
      continue;
    }
    if (c != '\n' && c != '\r' && c != '\t') return p;
    p++;
  }
}

// whether the whitespace before the next child is `n` bytes, a newline and spaces or nothing, as before the
// last one. pretty printers indent every child of a container alike, so this guess holds and p moves on by a
// number known in advance: only the checks wait for the loads, and their branches predict.
[[gnu::always_inline]] static inline bool cima__indent_is(const char *p, uint32_t n) {
  if ((unsigned char)p[n] <= ' ') return false;
  if (!n) return true;
  if (p[0] != '\n') return false;
  const char *s = p + 1;
  uint64_t bad = 0;
  uint32_t k = n - 1;
  for (; k >= 8; k -= 8, s += 8) bad |= cima__load64(s) ^ (CIMA__ONES * ' ');
  if (k) bad |= (cima__load64(s) ^ (CIMA__ONES * ' ')) & ((1ull << (8 * k)) - 1);
  return !bad;
}

// the indentation guess from the whitespace between p and q.
static inline uint32_t cima__indent_of(const char *p, const char *q) { return q - p <= 33 ? (uint32_t)(q - p) : 0; }

/* ========================================================================================================== */
/* writer internals.                                                                                          */
/* ========================================================================================================== */

[[gnu::cold, gnu::noinline]] void cima__writer_grow(cima_json_writer *w, size_t need);

static inline char *cima__reserve(cima_json_writer *w, size_t n) {
  if (UNLIKELY(w->cap - w->len < n)) cima__writer_grow(w, n);
  return w->buf + w->len;
}

static inline void cima__put(cima_json_writer *w, const char *s, size_t n) {
  memcpy(cima__reserve(w, n), s, n);
  w->len += n;
}

static inline void cima__putc(cima_json_writer *w, char c) {
  *cima__reserve(w, 1) = c;
  w->len++;
}

void cima__newline(cima_json_writer *w);

// commas and indentation before a value or a key.
static inline void cima__pre(cima_json_writer *w) {
  if (w->after_key) {
    w->after_key = false;
    return;
  }
  if (w->comma) cima__putc(w, ',');
  if (UNLIKELY(w->indent) && w->depth) cima__newline(w);
}

// a quoted, escaped string with no comma handling.
void cima__write_quoted(cima_json_writer *w, const char *s, size_t len);

/* ========================================================================================================== */
/* schema side caches, built once and shared by every thread.                                                 */
/* ========================================================================================================== */

// a table that only grows, lookups are lock free. `replace` swaps the data of an existing entry, the old data
// is never freed since readers may still hold it. returns the data that won.
void *cima__cache_get(const void *k1, const void *k2, uint32_t tag);
void *cima__cache_put(const void *k1, const void *k2, uint32_t tag, void *data, bool replace);
enum { CIMA__TAG_OBJECT = 1, CIMA__TAG_REGEX, CIMA__TAG_DEFAULT };

typedef struct cima__object_info {
  const cima_field *fields;
  size_t count;
  cima_rename rename;
  // json keys after renaming, and `"key":` ready to copy.
  const char **names;
  uint32_t *lens;
  const char **quoted;
  uint32_t *quoted_lens;
  // field index + 1 by key hash, for objects past a few fields.
  uint32_t *slots;
  uint32_t mask;
  // the kind of each field that parses with no check, default or refinement, CIMA__FAST_OPTIONAL added when it
  // sits in an optional at `fast_offsets`, zero for the others.
  uint8_t *fast;
  uint32_t *fast_offsets;
} cima__object_info;

constexpr uint8_t CIMA__FAST_OPTIONAL = 0x80;

// a kind the parsers read in place: no check, no default and no refinement.
static inline bool cima__plain_kind(const cima_schema *f) {
  if (f->common.default_json || f->common.refinement) return false;
  switch (f->kind) {
  case CIMA_KIND_BOOL: return true;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32:
  case CIMA_KIND_U64: return !f->integer.has_min && !f->integer.has_max && !f->integer.multiple_of;
  case CIMA_KIND_NUMBER: return !f->number.has_min && !f->number.has_max && !f->number.multiple_of;
  case CIMA_KIND_STRING: {
    const cima_string_opts *o = &f->string;
    return !o->has_min && !o->has_max && !o->format && !o->pattern && !o->starts_with && !o->ends_with && !o->trim &&
           !o->lowercase;
  }
  default: return false;
  }
}

const cima__object_info *cima__object_info_of(const cima_schema *object);
// index of the field named `key` or -1. `hint` is the expected index, keys usually come in declaration order.
[[gnu::pure]] ptrdiff_t cima__find_field(const cima__object_info *info, const char *key, size_t len, size_t hint);
[[gnu::pure]] uint64_t cima__hash(const void *p, size_t len);

// the compiled pattern, null when it does not compile.
const void *cima__regex_of(const char *pattern);
bool cima__regex_match(const void *regex, const char *s);

/* ========================================================================================================== */
/* text formats.                                                                                              */
/* ========================================================================================================== */

[[gnu::pure]] bool cima__check_format(cima_format format, const char *s, size_t len);
// iso 8601 with a time zone, or a plain date, to milliseconds since the epoch.
bool cima__parse_timestamp(const char *s, size_t len, int64_t *ms);
// `2026-10-29T13:47:10.000Z`, at most 32 bytes.
size_t cima__fmt_timestamp(char *out, int64_t ms);
// standard or url safe alphabet, padding optional. `out` holds `len / 4 * 3 + 3` bytes.
bool cima__base64_decode(const char *s, size_t len, uint8_t *out, size_t *out_len);
// standard alphabet with padding, `out` holds `(len + 2) / 3 * 4` bytes.
size_t cima__base64_encode(const uint8_t *p, size_t len, char *out);

/* ========================================================================================================== */
/* values.                                                                                                    */
/* ========================================================================================================== */

// deep copy of a typed value, every pointer allocated again from `a`.
void cima__clone_value(const cima_schema *s, void *dst, const void *src, cilo_allocator *a);
[[gnu::pure]] bool cima__value_eq(const cima_schema *s, const void *a, const void *b);
[[gnu::pure]] uint64_t cima__value_hash(const cima_schema *s, const void *v);

// a list as cima lays them out, cap only on named lists.
typedef struct cima__list {
  char *items;
  size_t len;
  size_t cap;
} cima__list;

static inline bool cima__list_has_cap(const cima_schema *array) { return array->size >= sizeof(cima__list); }

static inline int64_t cima__read_int(cima_kind kind, const void *v) {
  switch (kind) {
  case CIMA_KIND_I8: return *(const int8_t *)v;
  case CIMA_KIND_I16: return *(const int16_t *)v;
  case CIMA_KIND_I32: return *(const int32_t *)v;
  case CIMA_KIND_U8: return *(const uint8_t *)v;
  case CIMA_KIND_U16: return *(const uint16_t *)v;
  case CIMA_KIND_U32: return *(const uint32_t *)v;
  default: return *(const int64_t *)v;
  }
}

// enums and tagged union kinds, read by the size of their c type.
static inline int64_t cima__read_enum(const void *v, size_t size) {
  switch (size) {
  case 1: return *(const int8_t *)v;
  case 2: return *(const int16_t *)v;
  case 8: return *(const int64_t *)v;
  default: return *(const int32_t *)v;
  }
}

static inline void cima__write_enum(void *v, size_t size, int64_t value) {
  switch (size) {
  case 1: *(int8_t *)v = (int8_t)value; break;
  case 2: *(int16_t *)v = (int16_t)value; break;
  case 8: *(int64_t *)v = value; break;
  default: *(int32_t *)v = (int32_t)value; break;
  }
}

// a tagged union keeps its kind in an enum with no size of its own in the schema, it is an int.
constexpr size_t CIMA__KIND_SIZE = sizeof(int);

#endif
