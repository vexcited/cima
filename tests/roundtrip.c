#include "test.h"

#include <locale.h>
#include <math.h>
#include <stdlib.h>

enumeration(Status, draft, published, archived);

object(Point, number() x, number() y);
object(Square, number(gt(0)) side);
tagged_union(Figure, "kind", (Point) point, (Square) square);

object(Doc,
  u64()                       id,
  i64()                       delta,
  string()                    title,
  optional(string())          subtitle,
  nullable(i32())             rank,
  boolean()                   pinned,
  number()                    ratio,
  bytes()                     thumb,
  timestamp()                 created_at,
  unknown()                   meta,
  (Status)                    status,
  array(Figure)               figures,
  array(array(i16()))         grid,
  optional(Point)             origin,
  options(rename_all(CAMEL)),
);

static const char doc_json[] =
    "{\"id\": 18446744073709551615, \"delta\": -9223372036854775808, \"title\": \"tab\\there \\\"quoted\\\" "
    "\\u00e9\\ud83d\\ude00 \\u0001\\u001f\\u007f\", \"rank\": null, \"pinned\": true, \"ratio\": 0.1, "
    "\"thumb\": \"AAEC/w==\", \"createdAt\": \"2026-10-29T13:47:10.250Z\", \"meta\": {\"a\": [1, 2.5, null]}, "
    "\"status\": \"published\", \"figures\": [{\"kind\": \"point\", \"x\": -1.5, \"y\": 1e-7}, {\"side\": 3, "
    "\"kind\": \"square\"}], \"grid\": [[1, -2], [], [32767]]}";

static void typed(void) {
  with_arena(a) {
    Doc doc;
    cima_result r = CIMA_PARSE(Doc, doc_json, &doc, .arena = a);
    CHECK_STR(first_issue(a, r), "ok");
    size_t len;
    char *text = cima_to_json(&Doc_schema, &doc, &(cima_options){.arena = a}, &len);
    CHECK(strlen(text) == len);
    CHECK_STR(text, "{\"id\":18446744073709551615,\"delta\":-9223372036854775808,\"title\":\"tab\\there \\\"quoted\\\" "
                    "\xc3\xa9\xf0\x9f\x98\x80 \\u0001\\u001f\x7f\",\"rank\":null,\"pinned\":true,\"ratio\":0.1,"
                    "\"thumb\":\"AAEC/w==\",\"createdAt\":\"2026-10-29T13:47:10.250Z\","
                    "\"meta\":{\"a\": [1, 2.5, null]},"
                    "\"status\":\"published\",\"figures\":[{\"kind\":\"point\",\"x\":-1.5,\"y\":1e-7},{\"kind\":"
                    "\"square\",\"side\":3}],\"grid\":[[1,-2],[],[32767]]}");
    Doc again;
    CHECK(CIMA_PARSE(Doc, text, &again, .arena = a).ok);
    char *twice = cima_to_json(&Doc_schema, &again, &(cima_options){.arena = a}, nullptr);
    CHECK_STR(twice, text);
    // the dom reads what the encoder writes, and writes it back the same.
    cima_json *dom = json_parse(a, text, len, nullptr);
    CHECK(dom && json_or(json_get(dom, "id"), 0ull) == UINT64_MAX);
    CHECK(json_or(json_at(dom, "/figures/1/side"), 0) == 3);
    CHECK_STR(json_text(a, dom), "{\"id\":18446744073709551615,\"delta\":-9223372036854775808,\"title\":\"tab\\there "
                                 "\\\"quoted\\\" \xc3\xa9\xf0\x9f\x98\x80 \\u0001\\u001f\x7f\",\"rank\":null,"
                                 "\"pinned\":"
                                 "true,\"ratio\":0.1,\"thumb\":\"AAEC/w==\",\"createdAt\":\"2026-10-29T13:47:10.250Z\","
                                 "\"meta\":{\"a\":[1,2.5,null]},\"status\":\"published\",\"figures\":[{\"kind\":"
                                 "\"point\",\"x\":-1.5,\"y\":1e-7},{\"kind\":\"square\",\"side\":3}],\"grid\":[[1,-2],"
                                 "[],[32767]]}");
    // pretty output parses to the same value.
    char *pretty = cima_json_pretty(CILO_OF(a), dom, nullptr);
    CHECK(json_eq(json_parse(a, pretty, strlen(pretty), nullptr), dom));
  }
}

static uint64_t rng = 0x243f6a8885a308d3ull;
static uint64_t next(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

static size_t significant(const char *s) {
  char d[40];
  size_t n = 0, start = 0;
  for (; *s && *s != 'e'; s++)
    if (*s >= '0' && *s <= '9') d[n++] = *s;
  while (start < n && d[start] == '0') start++;
  while (n > start && d[n - 1] == '0') n--;
  return n - start;
}

static void doubles(void) {
  static const struct {
    double value;
    const char *text;
  } known[] = {
    {0.1, "0.1"},
    {1.0 / 3, "0.3333333333333333"},
    {2.0 / 3, "0.6666666666666666"},
    {100, "100"},
    {-0.0, "-0"},
    {1e21, "1e+21"},
    {1e-7, "1e-7"},
    {123456.789, "123456.789"},
    {5e-324, "5e-324"},
    {1.7976931348623157e308, "1.7976931348623157e+308"},
    {0.000123, "0.000123"},
    {9007199254740993.0, "9007199254740992"},
    {3.14159, "3.14159"},
    {-2.5e-5, "-0.000025"},
  };
  cima_json_writer w = {};
  for (size_t i = 0; i < sizeof known / sizeof *known; i++) {
    cima_json_write_double(&w, known[i].value);
    char *text = cima_json_writer_finish(&w, nullptr);
    CHECK_STR(text, known[i].text);
    cilo_mfree(text);
  }
  cima_json_write_double(&w, NAN);
  char *text = cima_json_writer_finish(&w, nullptr);
  CHECK_STR(text, "null");
  cilo_mfree(text);

  // random bit patterns and random short decimals read back exactly, through cima and through strtod.
  size_t bad = 0, longer = 0;
  with_arena(a) {
    for (int i = 0; i < 200000; i++) {
      double v;
      if (i % 2) {
        uint64_t bits = next();
        memcpy(&v, &bits, 8);
        if (!isfinite(v)) continue;
      } else {
        v = (double)(int64_t)(next() % 2000000001 - 1000000000) / pow(10, (double)(next() % 12));
      }
      rewinding(a) {
        cima_json_writer out = json_writer(a);
        cima_json_write_double(&out, v);
        char *s = cima_json_writer_finish(&out, nullptr);
        double back;
        bool ok = CIMA_PARSE(number(), s, &back, .arena = a).ok;
        double libc = strtod(s, nullptr);
        if (!ok || memcmp(&back, &v, 8) != 0 || memcmp(&libc, &v, 8) != 0) bad++;
        char shortest[40];
        for (int p = 1; p <= 17; p++) {
          snprintf(shortest, sizeof shortest, "%.*g", p, v);
          if (strtod(shortest, nullptr) == v) break;
        }
        // never more significant digits than the shortest form needs.
        if (significant(s) > significant(shortest)) longer++;
      }
    }
  }
  if (bad || longer) fprintf(stderr, "doubles: %zu wrong, %zu longer than needed\n", bad, longer);
  CHECK(bad == 0);
  CHECK(longer == 0);
}

static void integers(void) {
  with_arena(a) {
    static const char *const texts[] = {"0", "-1", "9223372036854775807", "-9223372036854775808"};
    for (size_t i = 0; i < 4; i++) {
      int64_t v;
      CHECK(CIMA_PARSE(i64(), texts[i], &v, .arena = a).ok);
      CHECK_STR(CIMA_TO_JSON(i64(), &v, .arena = a), texts[i]);
    }
    for (int i = 0; i < 100000; i++) {
      uint64_t u = next() >> (next() % 64);
      rewinding(a) {
        char *s = CIMA_TO_JSON(u64(), &u, .arena = a);
        uint64_t back;
        if (!CIMA_PARSE(u64(), s, &back, .arena = a).ok || back != u) CHECK(!"u64 round trip");
        int64_t si = (int64_t)u;
        s = CIMA_TO_JSON(i64(), &si, .arena = a);
        int64_t sback;
        if (!CIMA_PARSE(i64(), s, &sback, .arena = a).ok || sback != si) CHECK(!"i64 round trip");
      }
    }
  }
}

static void strings(void) {
  with_arena(a) {
    char all[256];
    for (int i = 1; i < 128; i++) all[i - 1] = (char)i;
    memcpy(all + 127, "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 9);
    all[136] = '\0';
    cima_string s = all;
    char *text = CIMA_TO_JSON(string(), &s, .arena = a);
    cima_string back;
    CHECK(CIMA_PARSE(string(), text, &back, .arena = a).ok);
    CHECK_STR(back, all);
    // invalid utf-8 from c code still gives valid json.
    s = "a\xff\xc3(b";
    text = CIMA_TO_JSON(string(), &s, .arena = a);
    CHECK_STR(text, "\"a\xef\xbf\xbd\xef\xbf\xbd(b\"");
    CHECK(cima_json_valid(text, strlen(text)));
  }
}

// a locale writing 0,5 changes nothing, on the fast paths or the slow ones.
static void locales(void) {
  const char *names[] = {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR"};
  bool set = false;
  for (size_t i = 0; i < 4 && !set; i++) set = setlocale(LC_ALL, names[i]) != nullptr;
  if (!set) return;
  with_arena(a) {
    double values[] = {0.5, 1e-7, 1.7976931348623157e308, 123456.789, 5e-324};
    for (size_t i = 0; i < 5; i++) {
      char *text = CIMA_TO_JSON(number(), &values[i], .arena = a);
      CHECK(strchr(text, ',') == nullptr);
      double back;
      CHECK(CIMA_PARSE(number(), text, &back, .arena = a).ok && back == values[i]);
    }
    double d;
    CHECK(CIMA_PARSE(number(), "0.12345678901234567890123", &d, .arena = a).ok && d == 0.12345678901234568);
  }
  setlocale(LC_ALL, "C");
}

int main(void) {
  locales();
  typed();
  doubles();
  integers();
  strings();
  return report_failures();
}
