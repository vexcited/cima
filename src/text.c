#include "internal.h"

static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static bool is_hostname(const char *s, size_t len, bool need_dot) {
  if (len == 0 || len > 253) return false;
  size_t label = 0, dots = 0;
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    if (c == '.') {
      if (label == 0 || s[i - 1] == '-') return false;
      label = 0;
      dots++;
      continue;
    }
    if (!is_alnum(c) && c != '-') return false;
    if (c == '-' && label == 0) return false;
    if (++label > 63) return false;
  }
  if (label == 0 || s[len - 1] == '-') return false;
  return !need_dot || dots > 0;
}

static bool is_email(const char *s, size_t len) {
  const char *at = memchr(s, '@', len);
  if (!at) return false;
  size_t local = (size_t)(at - s), domain = len - local - 1;
  if (local == 0 || local > 64 || s[0] == '.' || at[-1] == '.') return false;
  for (size_t i = 0; i < local; i++) {
    char c = s[i];
    if (c == '.' && s[i + 1] == '.') return false;
    if (!is_alnum(c) && !strchr(".!#$%&'*+/=?^_`{|}~-", c)) return false;
  }
  const char *d = at + 1;
  if (!is_hostname(d, domain, true)) return false;
  // a top level domain of letters only, two at least.
  const char *tld = d + domain;
  size_t tld_len = 0;
  while (tld > d && tld[-1] != '.') tld--, tld_len++;
  if (tld_len < 2) return false;
  for (size_t i = 0; i < tld_len; i++)
    if (!is_alpha(tld[i])) return false;
  return true;
}

static bool is_ipv4(const char *s, size_t len) {
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (part && (i >= len || s[i++] != '.')) return false;
    size_t start = i;
    unsigned v = 0;
    while (i < len && is_digit(s[i]) && i - start < 3) v = v * 10 + (unsigned)(s[i++] - '0');
    size_t n = i - start;
    if (n == 0 || v > 255 || (n > 1 && s[start] == '0')) return false;
  }
  return i == len;
}

static bool is_ipv6(const char *s, size_t len) {
  if (len < 2 || len > 45) return false;
  int groups = 0;
  bool gap = false;
  size_t i = 0;
  if (s[0] == ':') {
    if (s[1] != ':') return false;
    gap = true;
    i = 2;
    if (i == len) return true;
  }
  while (i < len) {
    size_t start = i;
    while (i < len && cima__hex((unsigned char)s[i]) >= 0 && i - start < 4) i++;
    size_t n = i - start;
    if (i < len && s[i] == '.') {
      // an embedded ipv4 address counts as two groups and ends the address.
      if (!is_ipv4(s + start, len - start)) return false;
      groups += 2;
      i = len;
      break;
    }
    if (n == 0) return false;
    groups++;
    if (i == len) break;
    if (s[i] != ':') return false;
    i++;
    if (i < len && s[i] == ':') {
      if (gap) return false;
      gap = true;
      i++;
      if (i == len) break;
    } else if (i == len) {
      return false;
    }
  }
  return gap ? groups < 8 : groups == 8;
}

static bool is_url(const char *s, size_t len) {
  size_t i = 0;
  if (len == 0 || !is_alpha(s[0])) return false;
  while (i < len && (is_alnum(s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')) i++;
  if (i == len || s[i] != ':' || i + 1 == len) return false;
  for (size_t j = i; j < len; j++)
    if ((unsigned char)s[j] <= ' ' || s[j] == '"' || s[j] == '<' || s[j] == '>' || s[j] == '\\' || s[j] == 0x7f)
      return false;
  i++;
  if (len - i >= 2 && s[i] == '/' && s[i + 1] == '/') {
    i += 2;
    size_t start = i;
    while (i < len && s[i] != '/' && s[i] != '?' && s[i] != '#') i++;
    const char *host = s + start;
    size_t host_len = i - start;
    const char *at = host_len ? memchr(host, '@', host_len) : nullptr;
    if (at) {
      host_len -= (size_t)(at + 1 - host);
      host = at + 1;
    }
    if (host_len && host[0] == '[') {
      const char *close = memchr(host, ']', host_len);
      return close && is_ipv6(host + 1, (size_t)(close - host - 1));
    }
    // the port, digits only.
    const char *colon = host_len ? memchr(host, ':', host_len) : nullptr;
    if (colon) {
      for (const char *p = colon + 1; p < host + host_len; p++)
        if (!is_digit(*p)) return false;
      host_len = (size_t)(colon - host);
    }
    return host_len > 0 && (is_ipv4(host, host_len) || is_hostname(host, host_len, false));
  }
  return true;
}

static bool is_uuid(const char *s, size_t len) {
  if (len != 36) return false;
  for (size_t i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (cima__hex((unsigned char)s[i]) < 0) {
      return false;
    }
  }
  return true;
}

static int num(const char *s, size_t n) {
  int v = 0;
  for (size_t i = 0; i < n; i++) {
    if (!is_digit(s[i])) return -1;
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

static int days_in_month(int y, int m) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : days[m - 1];
}

static bool parse_date(const char *s, size_t len, int *y, int *m, int *d) {
  if (len < 10 || s[4] != '-' || s[7] != '-') return false;
  *y = num(s, 4);
  *m = num(s + 5, 2);
  *d = num(s + 8, 2);
  return *y >= 0 && *m >= 1 && *m <= 12 && *d >= 1 && *d <= days_in_month(*y, *m);
}

// howard hinnant's days_from_civil.
static int64_t days_from_civil(int64_t y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int *m, int *d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = yoe + era * 400 + (*m <= 2);
}

// rfc 3339: a date, `T`, `t` or a space, hh:mm:ss, an optional fraction, then `Z` or an offset. with
// `need_zone` false a missing zone means utc.
static bool parse_datetime(const char *s, size_t len, bool need_zone, int64_t *ms) {
  int y, mo, d;
  if (!parse_date(s, len, &y, &mo, &d)) return false;
  int64_t days = days_from_civil(y, mo, d);
  if (len == 10) {
    if (need_zone) return false;
    *ms = days * 86400000;
    return true;
  }
  if (len < 19 || (s[10] != 'T' && s[10] != 't' && s[10] != ' ') || s[13] != ':' || s[16] != ':') return false;
  int h = num(s + 11, 2), mi = num(s + 14, 2), sec = num(s + 17, 2);
  // a leap second is accepted and lands on the next second.
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return false;
  size_t i = 19;
  int64_t frac = 0;
  if (i < len && s[i] == '.') {
    size_t start = ++i;
    while (i < len && is_digit(s[i])) {
      if (i - start < 3) frac = frac * 10 + (s[i] - '0');
      i++;
    }
    if (i == start) return false;
    for (size_t n = i - start; n < 3; n++) frac *= 10;
  }
  int64_t offset = 0;
  if (i == len) {
    if (need_zone) return false;
  } else if ((s[i] == 'Z' || s[i] == 'z') && i + 1 == len) {
  } else if ((s[i] == '+' || s[i] == '-') && len - i == 6 && s[i + 3] == ':') {
    int oh = num(s + i + 1, 2), om = num(s + i + 4, 2);
    if (oh < 0 || oh > 23 || om < 0 || om > 59) return false;
    offset = (oh * 60 + om) * 60000;
    if (s[i] == '-') offset = -offset;
  } else {
    return false;
  }
  *ms = ((days * 24 + h) * 60 + mi) * 60000 + sec * 1000 + frac - offset;
  return true;
}

bool cima__parse_timestamp(const char *s, size_t len, int64_t *ms) { return parse_datetime(s, len, false, ms); }

size_t cima__fmt_timestamp(char *out, int64_t ms) {
  int64_t days = ms / 86400000, rem = ms % 86400000;
  if (rem < 0) {
    rem += 86400000;
    days--;
  }
  int64_t y;
  int m, d;
  civil_from_days(days, &y, &m, &d);
  int h = (int)(rem / 3600000), mi = (int)(rem / 60000 % 60), s = (int)(rem / 1000 % 60), f = (int)(rem % 1000);
  if (y < 0 || y > 9999) {
    int n = snprintf(out, 32, "%04lld-%02d-%02dT%02d:%02d:%02d.%03dZ", (long long)y, m, d, h, mi, s, f);
    return (size_t)n;
  }
  char *p = out;
  int fields[] = {(int)y / 100, (int)y % 100, m, d, h, mi, s};
  static const char seps[] = "\0\0--T::";
  for (size_t i = 0; i < countof(fields); i++) {
    if (seps[i]) *p++ = seps[i];
    *p++ = (char)('0' + fields[i] / 10);
    *p++ = (char)('0' + fields[i] % 10);
  }
  p[0] = '.';
  p[1] = (char)('0' + f / 100);
  p[2] = (char)('0' + f / 10 % 10);
  p[3] = (char)('0' + f % 10);
  p[4] = 'Z';
  return (size_t)(p + 5 - out);
}

bool cima__check_format(cima_format format, const char *s, size_t len) {
  int64_t ms;
  int y, m, d;
  switch (format) {
  case CIMA_FORMAT_NONE: return true;
  case CIMA_FORMAT_EMAIL: return is_email(s, len);
  case CIMA_FORMAT_URL: return is_url(s, len);
  case CIMA_FORMAT_UUID: return is_uuid(s, len);
  case CIMA_FORMAT_DATE: return len == 10 && parse_date(s, len, &y, &m, &d);
  case CIMA_FORMAT_DATETIME: return parse_datetime(s, len, true, &ms);
  case CIMA_FORMAT_IPV4: return is_ipv4(s, len);
  case CIMA_FORMAT_IPV6: return is_ipv6(s, len);
  case CIMA_FORMAT_HOSTNAME: return is_hostname(s, len, false);
  }
  return true;
}

/* base64. */

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

bool cima__base64_decode(const char *s, size_t len, uint8_t *out, size_t *out_len) {
  while (len && s[len - 1] == '=') len--;
  if (len % 4 == 1) return false;
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < len; i++) {
    int v = b64_value((unsigned char)s[i]);
    if (v < 0) return false;
    acc = acc << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[n++] = (uint8_t)(acc >> bits);
    }
  }
  *out_len = n;
  return true;
}

size_t cima__base64_encode(const uint8_t *p, size_t len, char *out) {
  size_t n = 0, i = 0;
  for (; i + 3 <= len; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
    out[n++] = b64_chars[v >> 18];
    out[n++] = b64_chars[(v >> 12) & 63];
    out[n++] = b64_chars[(v >> 6) & 63];
    out[n++] = b64_chars[v & 63];
  }
  if (i < len) {
    uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < len ? (uint32_t)p[i + 1] << 8 : 0);
    out[n++] = b64_chars[v >> 18];
    out[n++] = b64_chars[(v >> 12) & 63];
    out[n++] = i + 1 < len ? b64_chars[(v >> 6) & 63] : '=';
    out[n++] = '=';
  }
  return n;
}
