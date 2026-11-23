#include "../src/internal.h"
#include "test.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

// the scanner and the formatter cima had before eisel-lemire and schubfach, kept as references: the new ones must
// agree with them on every input.

static const char *ref_scan_number(const char *p, const char *end, cima__num *n) {
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

static const double ref_pow10[18] = {1e0, 1e1, 1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,
                                     1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17};

static size_t ref_slow_fmt(char *out, double v) {
  char buf[320];
  int lo = 1, hi = 17;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    snprintf(buf, sizeof buf, "%.*g", mid, v);
    if (strtod(buf, nullptr) == v) hi = mid;
    else lo = mid + 1;
  }
  snprintf(buf, sizeof buf, "%.*g", lo, v);
  size_t n = 0;
  for (const char *p = buf; *p;) {
    out[n++] = *p;
    if (*p++ == 'e') {
      if (*p == '+' || *p == '-') out[n++] = *p++;
      while (*p == '0' && p[1]) p++;
    }
  }
  return n;
}

static size_t ref_fmt_double(char *out, double v) {
  size_t n = 0;
  if (signbit(v)) {
    out[n++] = '-';
    v = -v;
  }
  if (v == 0) {
    out[n++] = '0';
    return n;
  }
  if (v < 0x1p53 && v == (double)(uint64_t)v) return n + cima__fmt_u64(out + n, (uint64_t)v);
  if (v >= 1e-5 && v < 1e15) {
    for (int k = 1; k <= 17; k++) {
      double s = v * ref_pow10[k];
      if (s >= 0x1p53) break;
      double r = nearbyint(s);
      if (r / ref_pow10[k] != v) continue;
      char digits[20];
      size_t d = cima__fmt_u64(digits, (uint64_t)r);
      if (d <= (size_t)k) {
        out[n++] = '0';
        out[n++] = '.';
        for (size_t z = d; z < (size_t)k; z++) out[n++] = '0';
        memcpy(out + n, digits, d);
        return n + d;
      }
      memcpy(out + n, digits, d - (size_t)k);
      n += d - (size_t)k;
      out[n++] = '.';
      memcpy(out + n, digits + d - (size_t)k, (size_t)k);
      return n + (size_t)k;
    }
  }
  return n + ref_slow_fmt(out + n, v);
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rng64(void) {
  uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

static uint32_t rng(uint32_t n) { return (uint32_t)(rng64() % n); }

static double from_bits(uint64_t b) {
  double d;
  memcpy(&d, &b, 8);
  return d;
}

static uint64_t to_bits(double d) {
  uint64_t b;
  memcpy(&b, &d, 8);
  return b;
}

// CIMA_TEST_NUMBERS scales the random rounds, CIMA_TEST_SEED changes them.
static uint64_t scale = 1;

static int shown;

// the text in a buffer of its exact length, so a read past it is caught by the sanitizers.
static double parse_exact(const char *text, size_t len, const char **stop) {
  char *buf = malloc(len ? len : 1);
  memcpy(buf, text, len);
  cima__num n;
  const char *q = cima__scan_number(buf, buf + len, &n);
  double d = q ? cima__num_double(&n, buf, (size_t)(q - buf)) : NAN;
  if (stop) *stop = q ? text + (q - buf) : nullptr;
  free(buf);
  return d;
}

static void check_format(double v) {
  if (!isfinite(v)) return;
  char got[CIMA_JSON_NUMBER_MAX], want[64];
  memset(got, 0x7f, sizeof got);
  size_t n = cima__fmt_double(got, v);
  got[n] = '\0';
  want[ref_fmt_double(want, v)] = '\0';
  bool same = n < CIMA_JSON_NUMBER_MAX && strcmp(got, want) == 0;
  const char *stop;
  double back = parse_exact(got, n, &stop);
  bool reads = stop == got + n && to_bits(back) == to_bits(v) && to_bits(strtod(got, nullptr)) == to_bits(v);
  if ((!same || !reads) && shown++ < 20) fprintf(stderr, "format %a: got %s, want %s\n", v, got, want);
  CHECK(same && reads);
}

static void check_parse(const char *text, size_t len) {
  char z[1024];
  if (len >= sizeof z) return;
  memcpy(z, text, len);
  z[len] = '\0';
  char *zend;
  double want = strtod(z, &zend);
  const char *stop;
  double got = parse_exact(text, len, &stop);
  bool same = stop == text + len && zend == z + len && to_bits(got) == to_bits(want);
  if (!same && shown++ < 20) fprintf(stderr, "parse %s: got %a, want %a\n", z, got, want);
  CHECK(same);
}

static void check_parse_str(const char *text) { check_parse(text, strlen(text)); }

static void format_edges(void) {
  static const double fixed[] = {
    0.0, 1.0, 0.1, 0.5, 1.5, 1e-7, 1.5e-7, 1e-5, 1e-4, 0.001, 1e15, 1e16, 1e17, 1e21, 1e22, 1e23, 1e300, 5e-324,
    DBL_MIN, DBL_MAX, DBL_TRUE_MIN, DBL_EPSILON, 0x1p53, 0x1p53 + 2, 0x1p54, 0x1p63, 0x1p64, 0x1.fffffffffffffp52,
    9007199254740993.0, 123456789012345.67, 0.000123456789012345, 0.0000123456789012345, 1.2345678901234e-5,
    1.23456789012345e-5, 9.999999999999999e-6, 9.999999999999999e14, 999999999999999.9, 1e15 + 0.5, 4.35, 0.3,
    2.2250738585072011e-308, 2.2250738585072014e-308, 1.7976931348623157e308, 4.9406564584124654e-324, 1.0 / 3,
    2.0 / 3, M_PI, M_E, 100.0 / 3,
  };
  for (size_t i = 0; i < countof(fixed); i++) {
    check_format(fixed[i]);
    check_format(-fixed[i]);
  }
  static const struct {
    double v;
    const char *text;
  } known[] = {
    {1e300, "1e+300"},      {1.5e-7, "1.5e-7"},     {5e-324, "5e-324"},   {-0.0, "-0"},
    {0.1, "0.1"},           {1e21, "1e+21"},        {0x1p53, "9007199254740992"},
    {1.234567890123e-5, "0.00001234567890123"},  {1.2345678901234e-5, "1.2345678901234e-5"},
    {DBL_MAX, "1.7976931348623157e+308"},          {123456789012345.67, "123456789012345.67"},
  };
  for (size_t i = 0; i < countof(known); i++) {
    char out[CIMA_JSON_NUMBER_MAX];
    out[cima__fmt_double(out, known[i].v)] = '\0';
    CHECK_STR(out, known[i].text);
  }
  // every power of two and its neighbours, where the gap below is half the gap above.
  for (uint64_t e = 0; e < 2047; e++) {
    uint64_t b = e << 52;
    for (int d = -3; d <= 3; d++) {
      if ((int64_t)b + d > 0 && b + d < 0x7ffull << 52) check_format(from_bits(b + (uint64_t)d));
    }
    check_format(from_bits(e << 52 | 1ull << 51));
  }
  // every power of ten and its neighbours, every short multiple around the layout thresholds.
  for (int e = -325; e <= 308; e++) {
    char text[16];
    snprintf(text, sizeof text, "1e%d", e);
    double v = strtod(text, nullptr);
    for (int d = -3; d <= 3; d++) check_format(from_bits(to_bits(v) + (uint64_t)d));
  }
  for (int e = -7; e <= 17; e++) {
    for (int digits = 1; digits <= 17; digits++) {
      for (int i = 0; i < 20; i++) {
        char text[40];
        uint64_t m = rng64() % 100000000000000000ull;
        snprintf(text, sizeof text, "%llue%d", (unsigned long long)m, e - digits);
        check_format(strtod(text, nullptr));
        snprintf(text, sizeof text, "%.*e", digits - 1, i == 0 ? pow(10, e) : (double)(i + 1) * pow(10, e));
        check_format(strtod(text, nullptr));
      }
    }
  }
  // subnormals of every width.
  for (int bitsn = 1; bitsn <= 52; bitsn++) {
    for (int i = 0; i < 40; i++) check_format(from_bits(rng64() >> (64 - bitsn) | 1ull << (bitsn - 1)));
  }
  // ties: doubles halfway between two short decimals.
  for (int j = 1; j <= 60; j++) {
    for (int i = 0; i < 40; i++) check_format(ldexp((double)(2 * rng(1u << 20) + 1), -j));
  }
}

static void format_random(void) {
  uint64_t rounds = 200000 * scale;
  for (uint64_t i = 0; i < rounds; i++) {
    uint64_t b = rng64();
    switch (i & 3) {
    case 0: break;
    // most of what json carries sits around one.
    case 1: b = (b & 0x800fffffffffffffull) | (uint64_t)(1023 - 30 + rng(90)) << 52; break;
    case 2: {
      char text[40];
      uint64_t m = rng64() % (uint64_t)pow(10, 1 + rng(17));
      snprintf(text, sizeof text, "%llue%d", (unsigned long long)m, (int)rng(60) - 40);
      b = to_bits(strtod(text, nullptr));
      break;
    }
    case 3: b = (b & 0x800fffffffffffffull) >> rng(53) | (uint64_t)rng(2047) << 52; break;
    }
    check_format(from_bits(b));
  }
}

// the new scanner returns what the old one did, end and fields, with nothing readable past the text.
static void check_scan(const char *text, size_t len) {
  char *buf = malloc(len);
  memcpy(buf, text, len);
  cima__num a = {}, b = {};
  const char *qa = cima__scan_number(buf, buf + len, &a);
  const char *qb = ref_scan_number(buf, buf + len, &b);
  bool same = qa == qb && (!qa || (a.mant == b.mant && a.exp10 == b.exp10 && a.neg == b.neg && a.plain == b.plain &&
                                   a.inexact == b.inexact));
  if (!same && shown++ < 20) fprintf(stderr, "scan %.*s\n", (int)len, text);
  CHECK(same);
  free(buf);
}

static void scanning(void) {
  // any text cut anywhere.
  static const char alphabet[] = "0000123456789999..eE+-x ";
  uint64_t rounds = 300000 * scale;
  char text[64];
  for (uint64_t i = 0; i < rounds; i++) {
    size_t len = 1 + rng(48);
    // long runs of digits half of the time, they reach the words read eight and sixteen at a time.
    uint32_t odds = i & 1 ? 4 : 40;
    for (size_t j = 0; j < len; j++) {
      text[j] = rng(odds) ? (char)('0' + rng(10)) : alphabet[rng(sizeof alphabet - 1)];
    }
    if (rng(2)) text[0] = rng(2) ? '-' : (char)('1' + rng(9));
    check_scan(text, len);
  }
}

// the exact decimal digits of m * 2^e, in base 1e9 limbs, written as `digits` then an exponent of ten.
static size_t exact_decimal(char *out, uint64_t m, int e) {
  static uint32_t limbs[160];
  size_t count = 0;
  for (uint64_t x = m; x; x /= 1000000000) limbs[count++] = (uint32_t)(x % 1000000000);
  int e10 = 0;
  // m * 2^-k is m * 5^k / 10^k.
  uint32_t step = e > 0 ? 1u << 29 : 1220703125u;
  int stepped = e > 0 ? 29 : 13;
  for (int left = e > 0 ? e : -e; left > 0; left -= stepped) {
    uint64_t f = left >= stepped ? step : (e > 0 ? 1ull << left : (uint64_t)pow(5, left));
    uint64_t carry = 0;
    for (size_t i = 0; i < count; i++) {
      uint64_t x = limbs[i] * f + carry;
      limbs[i] = (uint32_t)(x % 1000000000);
      carry = x / 1000000000;
    }
    while (carry) limbs[count++] = (uint32_t)(carry % 1000000000), carry /= 1000000000;
  }
  if (e < 0) e10 = e;
  size_t n = (size_t)snprintf(out, 16, "%u", limbs[count - 1]);
  for (size_t i = count - 1; i-- > 0;) n += (size_t)snprintf(out + n, 16, "%09u", limbs[i]);
  return n + (size_t)snprintf(out + n, 16, "e%d", e10);
}

static void parse_edges(void) {
  static const char *const cases[] = {
    "0", "-0", "0.0", "-0.0e5", "1", "-1", "0e999999999999", "1e-99999999999", "1e99999999999", "1e309",
    "1e308", "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308", "1e-324", "1e-323",
    "2.4703282292062327e-324", "2.4703282292062328e-324", "4.9406564584124654e-324", "2.2250738585072011e-308",
    "2.2250738585072012e-308", "2.2250738585072014e-308", "9007199254740993", "9007199254740993.0000000001",
    "9007199254740992.5", "9007199254740994.9999", "7.2057594037927933e16", "1.00000000000000011102230246251565404",
    "1.00000000000000011102230246251565405", "18446744073709551615", "18446744073709551616", "18446744073709551615e-20",
    "0.000000000000000000000000000000000000000000000000000000000000000000000000000001",
    "123456789012345678901234567890",
    "0.1", "0.3", "1e23", "8.98846567431158e307", "1.0e-307", "4.4501477170144023e-308", "3.4028236692093846e38",
    "0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
  };
  for (size_t i = 0; i < countof(cases); i++) check_parse_str(cases[i]);
  // integer parts around every bound the scanner switches at, then fractions of every length.
  static const char *const bounds[] = {
    "1842", "1843", "1844", "1845", "184467440735", "184467440736", "184467440737", "184467440738",
    "1844674407370955160", "1844674407370955161", "1844674407370955162", "18446744073709551615",
    "18446744073709551616", "0", "9", "99999999", "9999999999999999", "100000000",
  };
  for (size_t i = 0; i < countof(bounds); i++) {
    for (int digits = 0; digits <= 30; digits++) {
      for (int round = 0; round < 8; round++) {
        char text[96];
        size_t n = (size_t)snprintf(text, sizeof text, "%s%s", round & 1 ? "-" : "", bounds[i]);
        if (digits) text[n++] = '.';
        for (int d = 0; d < digits; d++) text[n++] = round < 4 ? '9' : (char)('0' + rng(10));
        if (round & 2) n += (size_t)snprintf(text + n, 16, "e%d", (int)rng(40) - 20);
        check_parse(text, n);
        check_scan(text, n);
        // and inside a longer text, where the scanner reads whole words past the number.
        memcpy(text + n, ",                       ", 24);
        check_scan(text, n + 24);
      }
    }
  }
  // halfway points between neighbouring doubles, exact, and cut short or nudged either way.
  char text[1200];
  for (int i = 0; i < (int)(4000 * scale); i++) {
    uint64_t c = (rng64() >> 11 | 1ull << 52) >> rng(i & 1 ? 53 : 4);
    if (!c) c = 1;
    c >>= __builtin_ctzll(c) * rng(2);
    int q = (int)rng(2099) - 1075;
    if (i % 3 == 0) q = (int)rng(160) - 80;
    size_t n = exact_decimal(text, 2 * c + 1, q - 1);
    check_parse(text, n);
    size_t digits = (size_t)(strchr(text, 'e') - text);
    int e10 = atoi(text + digits + 1);
    for (size_t keep = 15; keep <= 26 && keep < digits; keep++) {
      n = exact_decimal(text, 2 * c + 1, q - 1);
      n = keep + (size_t)snprintf(text + keep, 16, "e%d", e10 + (int)(digits - keep));
      check_parse(text, n);
      char save = text[keep - 1];
      if (save < '9') {
        text[keep - 1] = (char)(save + 1);
        check_parse(text, n);
      }
      if (save > '0') {
        text[keep - 1] = (char)(save - 1);
        check_parse(text, n);
      }
      text[keep - 1] = save;
    }
  }
}

static void parse_random(void) {
  uint64_t rounds = 1000000 * scale;
  char text[96];
  for (uint64_t i = 0; i < rounds; i++) {
    size_t n = 0;
    if (rng(2)) text[n++] = '-';
    unsigned digits = 1 + rng(25);
    unsigned point = rng(digits + 2);
    bool lead = rng(4) == 0;
    if (lead) {
      text[n++] = '0';
      text[n++] = '.';
      for (unsigned z = rng(12); z; z--) text[n++] = '0';
    }
    for (unsigned d = 0; d < digits; d++) {
      if (!lead && d == point && d > 0) text[n++] = '.';
      text[n++] = (char)((d == 0 && !lead ? '1' : '0') + rng(d == 0 && !lead ? 9 : 10));
    }
    if (rng(4)) {
      text[n++] = "eE"[rng(2)];
      unsigned sign = rng(3);
      if (sign) text[n++] = sign == 1 ? '+' : '-';
      int e = (int)rng(661) - 350;
      if (sign == 1 && e < 0) e = -e;
      n += (size_t)snprintf(text + n, 16, "%d", sign == 2 ? (e < 0 ? -e : e) : e);
    }
    check_parse(text, n);
  }
  // every double written long and short reads back.
  for (uint64_t i = 0; i < rounds / 4; i++) {
    double v = from_bits((rng64() & ~(0x7ffull << 52)) | (uint64_t)rng(2047) << 52);
    int n = i & 1 ? snprintf(text, sizeof text, "%.17g", v) : snprintf(text, sizeof text, "%.*e", (int)rng(25), v);
    check_parse(text, (size_t)n);
  }
}

int main(void) {
  const char *s = getenv("CIMA_TEST_NUMBERS");
  if (s && atoll(s) > 0) scale = (uint64_t)atoll(s);
  s = getenv("CIMA_TEST_SEED");
  if (s) rng_state ^= (uint64_t)atoll(s) * 0x2545f4914f6cdd1dull;
  format_edges();
  format_random();
  scanning();
  parse_edges();
  parse_random();
  return report_failures();
}
