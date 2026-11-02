#include "internal.h"

#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

static const uint64_t pow10_u64[20] = {
  1ull,
  10ull,
  100ull,
  1000ull,
  10000ull,
  100000ull,
  1000000ull,
  10000000ull,
  100000000ull,
  1000000000ull,
  10000000000ull,
  100000000000ull,
  1000000000000ull,
  10000000000000ull,
  100000000000000ull,
  1000000000000000ull,
  10000000000000000ull,
  100000000000000000ull,
  1000000000000000000ull,
  10000000000000000000ull,
};

// every power up to 1e22 is exact in a double.
static const double pow10_f64[23] = {
  1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
  1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

cima__int_status cima__num_magnitude(const cima__num *n, uint64_t *out) {
  if (n->mant == 0 && !n->inexact) {
    *out = 0;
    return CIMA__INT_OK;
  }
  if (n->inexact) return n->exp10 >= 0 ? CIMA__INT_RANGE : CIMA__INT_FRACTION;
  if (n->exp10 >= 0) {
    if (n->exp10 >= 20) return CIMA__INT_RANGE;
    return ckd_mul(out, n->mant, pow10_u64[n->exp10]) ? CIMA__INT_RANGE : CIMA__INT_OK;
  }
  if (n->exp10 <= -20) return CIMA__INT_FRACTION;
  uint64_t p = pow10_u64[-n->exp10];
  if (n->mant % p) return CIMA__INT_FRACTION;
  *out = n->mant / p;
  return CIMA__INT_OK;
}

static locale_t c_locale;
static pthread_once_t c_locale_once = PTHREAD_ONCE_INIT;
static void make_c_locale(void) { c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0); }

static locale_t the_c_locale(void) {
  pthread_once(&c_locale_once, make_c_locale);
  return c_locale;
}

static double slow_strtod(const char *tok, size_t len) {
  char small[64];
  char *buf = len < sizeof small ? small : cilo_malloc(len + 1);
  memcpy(buf, tok, len);
  buf[len] = '\0';
  double d = strtod_l(buf, nullptr, the_c_locale());
  if (buf != small) cilo_mfree(buf);
  return d;
}

double cima__num_double(const cima__num *n, const char *tok, size_t len) {
  if (n->mant == 0 && !n->inexact) return n->neg ? -0.0 : 0.0;
  // clinger's fast path: an exact mantissa and an exact power give one correctly rounded operation.
  if (!n->inexact && n->mant <= (1ull << 53)) {
    double m = (double)n->mant;
    int32_t e = n->exp10;
    double d;
    bool hit = true;
    if (e == 0) d = m;
    else if (e > 0 && e <= 22) d = m * pow10_f64[e];
    else if (e < 0 && e >= -22) d = m / pow10_f64[-e];
    else if (e > 22 && e <= 22 + 15 && m * pow10_f64[e - 22] <= 0x1p53) d = m * pow10_f64[e - 22] * 1e22;
    else hit = false;
    if (hit) return n->neg ? -d : d;
  }
  return slow_strtod(tok, len);
}

static const char digit_pairs[200] = {
  '0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0', '7', '0', '8', '0', '9', '1', '0', '1',
  '1', '1', '2', '1', '3', '1', '4', '1', '5', '1', '6', '1', '7', '1', '8', '1', '9', '2', '0', '2', '1', '2', '2',
  '2', '3', '2', '4', '2', '5', '2', '6', '2', '7', '2', '8', '2', '9', '3', '0', '3', '1', '3', '2', '3', '3', '3',
  '4', '3', '5', '3', '6', '3', '7', '3', '8', '3', '9', '4', '0', '4', '1', '4', '2', '4', '3', '4', '4', '4', '5',
  '4', '6', '4', '7', '4', '8', '4', '9', '5', '0', '5', '1', '5', '2', '5', '3', '5', '4', '5', '5', '5', '6', '5',
  '7', '5', '8', '5', '9', '6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6', '6', '6', '7', '6', '8',
  '6', '9', '7', '0', '7', '1', '7', '2', '7', '3', '7', '4', '7', '5', '7', '6', '7', '7', '7', '8', '7', '9', '8',
  '0', '8', '1', '8', '2', '8', '3', '8', '4', '8', '5', '8', '6', '8', '7', '8', '8', '8', '9', '9', '0', '9', '1',
  '9', '2', '9', '3', '9', '4', '9', '5', '9', '6', '9', '7', '9', '8', '9', '9',
};

size_t cima__fmt_u64(char *out, uint64_t v) {
  char tmp[20];
  char *p = tmp + 20;
  while (v >= 100) {
    uint64_t q = v / 100;
    p -= 2;
    memcpy(p, digit_pairs + (v - q * 100) * 2, 2);
    v = q;
  }
  if (v >= 10) {
    p -= 2;
    memcpy(p, digit_pairs + v * 2, 2);
  } else {
    *--p = (char)('0' + v);
  }
  size_t n = (size_t)(tmp + 20 - p);
  memcpy(out, p, n);
  return n;
}

// snprintf in the c locale, at the lowest precision that reads back the same double. 17 always does, and a
// precision that reads back means every higher one does too, so a binary search finds it.
static size_t slow_fmt_double(char *out, double v) {
  locale_t old = uselocale(the_c_locale());
  char buf[40];
  int lo = 1, hi = 17;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    snprintf(buf, sizeof buf, "%.*g", mid, v);
    if (strtod_l(buf, nullptr, the_c_locale()) == v) hi = mid;
    else lo = mid + 1;
  }
  snprintf(buf, sizeof buf, "%.*g", lo, v);
  uselocale(old);
  // `1e-07` reads better as `1e-7`, which is also what javascript prints.
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

size_t cima__fmt_double(char *out, double v) {
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
  // the fewest decimals k whose rounding r reads back as v. r and 10^k are exact, so r / 10^k is the correctly
  // rounded value of the decimal text, the same a parser gets.
  if (v >= 1e-5 && v < 1e15) {
    for (int k = 1; k <= 17; k++) {
      double s = v * pow10_f64[k];
      if (s >= 0x1p53) break;
      double r = nearbyint(s);
      if (r / pow10_f64[k] != v) continue;
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
  return n + slow_fmt_double(out + n, v);
}
