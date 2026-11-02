#ifndef CIMA_INTERNAL_H
#define CIMA_INTERNAL_H

#include <cima/json.h>
#include <stdckdint.h>
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

// short keys and strings compared and copied without a call: two overlapping words cover up to 16 bytes and
// never touch a byte past `len`.
static inline bool cima__same(const char *a, const char *b, size_t len) {
  if (len > 16) return memcmp(a, b, len) == 0;
  if (len >= 8) return cima__load64(a) == cima__load64(b) && cima__load64(a + len - 8) == cima__load64(b + len - 8);
  if (len >= 4) return cima__load32(a) == cima__load32(b) && cima__load32(a + len - 4) == cima__load32(b + len - 4);
  return !len || (a[0] == b[0] && a[len / 2] == b[len / 2] && a[len - 1] == b[len - 1]);
}

static inline void cima__copy(char *dst, const char *src, size_t len) {
  if (len > 16) {
    memcpy(dst, src, len);
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

constexpr uint64_t CIMA__ONES = 0x0101010101010101ull;
constexpr uint64_t CIMA__HIGHS = 0x8080808080808080ull;

// a high bit on the first byte of `v` that is a quote, a backslash, a control character or past ascii. only the
// lowest set bit is exact, borrows may mark bytes after it.
static inline uint64_t cima__special(uint64_t v) {
  uint64_t q = v ^ (CIMA__ONES * '"');
  uint64_t b = v ^ (CIMA__ONES * '\\');
  return ((q - CIMA__ONES) & ~q) | ((b - CIMA__ONES) & ~b) | ((v - CIMA__ONES * 0x20) & ~v) | v;
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
  if (*p == '0') {
    p++;
  } else if (*p >= '1' && *p <= '9') {
    // up to 19 digits never overflow, the common case pays no check.
    const char *fast = end - p > 19 ? p + 19 : end;
    while (p < fast && *p >= '0' && *p <= '9') m = m * 10 + (uint64_t)(*p++ - '0');
    while (cima__digit(p, end)) {
      unsigned d = (unsigned)(*p++ - '0');
      uint64_t t;
      if (!full && !ckd_mul(&t, m, 10) && !ckd_add(&t, t, d)) {
        m = t;
        continue;
      }
      full = true;
      drop++;
      inexact |= d != 0;
    }
  } else {
    return nullptr;
  }
  if (p < end && *p == '.') {
    plain = false;
    p++;
    if (!cima__digit(p, end)) return nullptr;
    while (cima__digit(p, end)) {
      unsigned d = (unsigned)(*p++ - '0');
      if (!full) {
        uint64_t t;
        if (!ckd_mul(&t, m, 10) && !ckd_add(&t, t, d)) {
          m = t;
          frac++;
          continue;
        }
        full = true;
      }
      inexact |= d != 0;
    }
  }
  int64_t e = 0;
  if (p < end && (*p == 'e' || *p == 'E')) {
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
} cima__object_info;

const cima__object_info *cima__object_info_of(const cima_schema *object);
// index of the field named `key` or -1. `hint` is the expected index, keys usually come in declaration order.
ptrdiff_t cima__find_field(const cima__object_info *info, const char *key, size_t len, size_t hint);
uint64_t cima__hash(const void *p, size_t len);

// the compiled pattern, null when it does not compile.
const void *cima__regex_of(const char *pattern);
bool cima__regex_match(const void *regex, const char *s);

/* ========================================================================================================== */
/* text formats.                                                                                              */
/* ========================================================================================================== */

bool cima__check_format(cima_format format, const char *s, size_t len);
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
bool cima__value_eq(const cima_schema *s, const void *a, const void *b);
uint64_t cima__value_hash(const cima_schema *s, const void *v);

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
