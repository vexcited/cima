#include "test.h"

enumeration(Sort, newest, oldest);

object(Search,
  string(trim(), at_least(2))                      q,
  u32(at_least(1), fallback(1))                    page,
  u32(at_most(100), fallback(20))                  per_page,
  optional(boolean())                              archived,
  optional(Sort)                                   sort,
  array(string(lowercase()), unique(), fallback([]))  tags,
  optional(array(u64()))                           ids,
  optional(timestamp())                            since,
);

object(Strict, u8() a, options(strict()));

#define PAIRS(...) ((const cima_pair[]){__VA_ARGS__}), sizeof((const cima_pair[]){__VA_ARGS__}) / sizeof(cima_pair)

static void query(void) {
  with_arena(a) {
    Search s;
    cima_result r = cima_parse_pairs(&Search_schema,
                                     PAIRS({"q", " cats "}, {"page", "3"}, {"archived", "on"}, {"sort", "oldest"},
                                           {"tags", "Fluffy"}, {"tags[]", "Small"}, {"ids", "1"}, {"ids", "42"},
                                           {"since", "2026-01-01"}, {"utm_source", "x"}),
                                     &s, &(cima_options){.arena = a, .coerce = true});
    CHECK_STR(first_issue(a, r), "ok");
    CHECK_STR(s.q, "cats");
    CHECK(s.page == 3 && s.per_page == 20 && s.archived.some && s.archived.value);
    CHECK(s.sort.some && s.sort.value == Sort_oldest);
    CHECK(s.tags.len == 2 && strcmp(s.tags.items[0], "fluffy") == 0 && strcmp(s.tags.items[1], "small") == 0);
    CHECK(s.ids.some && s.ids.value.len == 2 && s.ids.value.items[1] == 42);
    CHECK(s.since.some && s.since.value == 1767225600000);

    // empty inputs count as left out, except for text.
    r = cima_parse_pairs(&Search_schema, PAIRS({"q", "ab"}, {"page", ""}, {"archived", ""}),
                         &s, &(cima_options){.arena = a, .coerce = true});
    CHECK_STR(first_issue(a, r), "ok");
    CHECK(s.page == 1 && !s.archived.some && s.tags.len == 0 && !s.ids.some);

    r = cima_parse_pairs(&Search_schema,
                         PAIRS({"q", "a"}, {"page", "0"}, {"per_page", "x"}, {"archived", "maybe"}, {"tags", "a"},
                               {"tags", "A"}, {"ids", "-1"}, {"q", "b"}),
                         &s, &(cima_options){.arena = a, .coerce = true});
    CHECK(!r.ok && r.issues.len == 6);
    const char *want[][2] = {
      {"/q", "expected a single value"},
      {"/page", "must be at least 1"},
      {"/per_page", "expected an integer"},
      {"/archived", "expected a boolean"},
      {"/tags/1", "repeats item 0"},
      {"/ids/0", "must be at least 0"},
    };
    for (size_t i = 0; i < r.issues.len && i < 6; i++) {
      CHECK_STR(r.issues.items[i].path, want[i][0]);
      CHECK_STR(r.issues.items[i].message, want[i][1]);
    }
    Strict st;
    r = cima_parse_pairs(&Strict_schema, PAIRS({"a", "1"}, {"b", "2"}), &st, &(cima_options){.arena = a, .coerce = true});
    CHECK_STR(first_issue(a, r), "/b: unknown key");
  }
}

static void text(void) {
  with_arena(a) {
    cima_options o = {.arena = a};
    uint64_t id;
    CHECK(CIMA_PARSE(u64(), "1", &id, .arena = a).ok);
    CHECK(cima_parse_text(CIMA_SCHEMA(u64(at_least(1))), "18446744073709551615", &id, &o).ok && id == UINT64_MAX);
    CHECK_STR(first_issue(a, cima_parse_text(CIMA_SCHEMA(u64(at_least(1))), "0", &id, &o)), ": must be at least 1");
    CHECK_STR(first_issue(a, cima_parse_text(CIMA_SCHEMA(u64()), "12abc", &id, &o)), ": expected an integer");
    double d;
    CHECK(cima_parse_text(&cima_number_schema, "-2.5e3", &d, &o).ok && d == -2500);
    Sort sort;
    CHECK(cima_parse_text(&Sort_schema, "newest", &sort, &o).ok && sort == Sort_newest);
    CHECK_STR(first_issue(a, cima_parse_text(&Sort_schema, "random", &sort, &o)), ": must be one of newest, oldest");
    // headers repeat themselves with commas.
    cima_u32_list list;
    CHECK(cima_parse_text(CIMA_SCHEMA(array(u32())), "1, 2 ,3", &list, &o).ok && list.len == 3 && list.items[2] == 3);
    typeof(CIMA_CTYPE(optional(string(uuid())))) key;
    CHECK(cima_parse_text(CIMA_SCHEMA(optional(string(uuid()))), "123e4567-e89b-12d3-a456-426614174000", &key, &o).ok);
    CHECK(key.some);
    CHECK_STR(first_issue(a, cima_parse_text(CIMA_SCHEMA(optional(string(uuid()))), "nope", &key, &o)),
              ": must be a valid uuid");
    typeof(CIMA_CTYPE(nullable(i32()))) maybe;
    CHECK(cima_parse_text(CIMA_SCHEMA(nullable(i32())), "null", &maybe, &o).ok && !maybe.some);
    cima_unknown raw;
    CHECK(cima_parse_text(&cima_unknown_schema, "[1,2]", &raw, &o).ok);
    CHECK_STR(raw, "[1,2]");
    CHECK(cima_parse_text(&cima_unknown_schema, "plain \"text\"", &raw, &o).ok);
    CHECK_STR(raw, "\"plain \\\"text\\\"\"");
    // an object from text is json.
    Strict st;
    CHECK(cima_parse_text(&Strict_schema, "{\"a\": 4}", &st, &o).ok && st.a == 4);
    CHECK_STR(first_issue(a, cima_parse_text(&Strict_schema, "{\"a\": 4", &st, &o)),
              ": expected ',' or '}' at offset 7");
    CHECK_STR(first_issue(a, cima_parse_text(&Strict_schema, "a=4", &st, &o)), ": expected an object");
    bool b;
    CHECK(cima_parse_text(&cima_bool_schema, "yes", &b, &o).ok && b);
    CHECK(cima_parse_text(&cima_bool_schema, "0", &b, &o).ok && !b);
  }
}

int main(void) {
  query();
  text();
  return report_failures();
}
