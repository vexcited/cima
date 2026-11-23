#include "test.h"

#include <stdlib.h>

// discord's ids: a u64 in c, a string of digits on the wire, though some payloads send a number.
static bool snowflake_decode(const cima_json *in, void *out, cima_ctx *ctx) {
  uint64_t *id = out;
  if (json_type(in) == CIMA_JSON_INT && in->i >= 0) {
    *id = (uint64_t)in->i;
    return true;
  }
  if (json_type(in) == CIMA_JSON_STRING && in->len && in->len <= 20) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < in->len; i++) {
      unsigned d = (unsigned)(in->str[i] - '0');
      if (d > 9 || ckd_mul(&v, v, 10) || ckd_add(&v, v, d)) {
        cima_report(ctx, "", "must be a snowflake");
        return false;
      }
    }
    *id = v;
    return true;
  }
  cima_report(ctx, "", "must be a snowflake");
  return false;
}

static void snowflake_encode(cima_json_writer *w, const void *value) {
  char digits[CIMA_JSON_NUMBER_MAX];
  size_t n = cima_json_format_uint(digits, *(const uint64_t *)value);
  cima_json_write_string_n(w, digits, n);
}

static const cima_codec snowflake_codec = {
    .decode = snowflake_decode,
    .encode = snowflake_encode,
    .json_schema = literal({"type": "string", "pattern": "^[0-9]+$"}),
};

custom(Snowflake, uint64_t, &snowflake_codec, describe("a discord id"));

// a codec keeping memory: "#rrggbb" read into a struct with a name copied out of the input.
typedef struct Color {
  uint8_t r, g, b;
  const char *text;
} Color;

static bool color_decode(const cima_json *in, void *out, cima_ctx *ctx) {
  Color *c = out;
  const char *s = json_or(in, (const char *)nullptr);
  unsigned r, g, b;
  if (!s || strlen(s) != 7 || sscanf(s, "#%2x%2x%2x", &r, &g, &b) != 3) return false;
  *c = (Color){(uint8_t)r, (uint8_t)g, (uint8_t)b, cima_strdup(ctx, s, 7)};
  return true;
}

static void color_encode(cima_json_writer *w, const void *value) {
  const Color *c = value;
  char text[8];
  snprintf(text, sizeof text, "#%02x%02x%02x", c->r, c->g, c->b);
  json_put(w, (const char *)text);
}

static void color_dispose(void *value, cilo_allocator *alloc) {
  Color *c = value;
  if (c->text) cilo_free(alloc, (void *)c->text, 8);
}

static const cima_codec color_codec = {
    .decode = color_decode, .encode = color_encode, .dispose = color_dispose,
    .json_schema = literal({"type": "string", "pattern": "^#[0-9a-f]{6}$"})};

custom(Paint, Color, &color_codec);

object(Point, i32() x, i32() y);

// a codec over another schema: a point written as [x, y] or as {"x": .., "y": ..}.
static bool point_decode(const cima_json *in, void *out, cima_ctx *ctx) {
  Point *p = out;
  if (json_type(in) == CIMA_JSON_ARRAY && json_len(in) == 2) {
    *p = (Point){(int32_t)json_or(json_index(in, 0), 0), (int32_t)json_or(json_index(in, 1), 0)};
    return true;
  }
  return cima_decode(ctx, &Point_schema, in, out);
}

static void point_encode(cima_json_writer *w, const void *value) {
  const Point *p = value;
  json_begin_array(w);
  json_put(w, p->x);
  json_put(w, p->y);
  json_end_array(w);
}

static const cima_codec point_codec = {.decode = point_decode, .encode = point_encode};

object(Member,
  (Snowflake)                                 id,
  array(Snowflake)                            roles,
  optional(Snowflake)                         nick_by,
  with((uint64_t), &snowflake_codec)          guild,
  optional(Paint)                             color,
  with((Point), &point_codec)                 at,
);

static void decoding(void) {
  with_arena(a) {
    Member m;
    cima_result r = CIMA_PARSE(Member, "{\"id\": \"175928847299117063\", \"roles\": [\"1\", 2, \"30\"], "
                                       "\"guild\": \"81384788765712384\", \"color\": \"#ff8800\", \"at\": [3, -4]}",
                               &m, .arena = a);
    CHECK(r.ok);
    CHECK(m.id == 175928847299117063u && m.roles.len == 3 && m.roles.items[1] == 2 && m.roles.items[2] == 30);
    CHECK(!m.nick_by.some && m.guild == 81384788765712384u);
    CHECK(m.color.some && m.color.value.r == 0xff && m.color.value.g == 0x88 && m.color.value.b == 0);
    CHECK_STR(m.color.value.text, "#ff8800");
    CHECK(m.at.x == 3 && m.at.y == -4);

    r = CIMA_PARSE(Member, "{\"id\": 1, \"roles\": [], \"guild\": 2, \"at\": {\"x\": 5, \"y\": 6}}", &m, .arena = a);
    CHECK(r.ok && m.at.x == 5 && m.at.y == 6);

    // issues sit at the value, the codec's own message or a generic one.
    r = CIMA_PARSE(Member, "{\"id\": \"12a\", \"roles\": [\"1\", true], \"guild\": 1, \"color\": \"red\", \"at\": "
                           "{\"x\": 1}}",
                   &m, .arena = a);
    CHECK(!r.ok && r.issues.len == 4);
    CHECK_STR(r.issues.items[0].path, "/id");
    CHECK_STR(r.issues.items[0].message, "must be a snowflake");
    CHECK_STR(r.issues.items[1].path, "/roles/1");
    CHECK_STR(r.issues.items[2].path, "/color");
    CHECK_STR(r.issues.items[2].message, "is not valid");
    CHECK_STR(r.issues.items[3].path, "/at/y");
    CHECK(m.id == 0);

    // text and pairs hand the codec a string.
    Snowflake id;
    CHECK(cima_parse_text(&Snowflake_schema, "42", &id, &(cima_options){.arena = a}).ok && id == 42);
    cima_pair pairs[] = {{"id", "7"}, {"roles", "8"}, {"roles", "9"}, {"guild", "1"}, {"at", "{\"x\":1,\"y\":2}"}};
    r = cima_parse_pairs(&Member_schema, pairs, countof(pairs), &m, &(cima_options){.arena = a});
    CHECK(r.ok && m.id == 7 && m.roles.len == 2 && m.roles.items[1] == 9);
  }
}

static void encoding(void) {
  with_arena(a) {
    Snowflake roles[] = {1, 18446744073709551615u};
    Member m = {
        .id = 175928847299117063u,
        .roles = {roles, countof(roles), 0},
        .guild = 9,
        .color = {true, {0x12, 0x34, 0x56, nullptr}},
        .at = {1, 2},
    };
    CHECK_STR(CIMA_TO_JSON(Member, &m, .arena = a),
              "{\"id\":\"175928847299117063\",\"roles\":[\"1\",\"18446744073709551615\"],\"guild\":\"9\","
              "\"color\":\"#123456\",\"at\":[1,2]}");
    CHECK_STR(json_text(a, json_obj("member", json_typed(Member, &m), "id", json_typed(Snowflake, &m.id))),
              "{\"member\":{\"id\":\"175928847299117063\",\"roles\":[\"1\",\"18446744073709551615\"],\"guild\":\"9\","
              "\"color\":\"#123456\",\"at\":[1,2]},\"id\":\"175928847299117063\"}");
    CHECK(CIMA_CHECK(Member, &m).ok);
  }
}

static void schemas(void) {
  with_arena(a) {
    cima_json *s = json_parse(a, CIMA_JSON_SCHEMA(Member, .arena = a), strlen(CIMA_JSON_SCHEMA(Member, .arena = a)),
                              nullptr);
    CHECK_STR(json_or(json_at(s, "/$defs/Member/properties/id/$ref"), ""), "#/$defs/Snowflake");
    CHECK_STR(json_or(json_at(s, "/$defs/Snowflake/type"), ""), "string");
    CHECK_STR(json_or(json_at(s, "/$defs/Snowflake/pattern"), ""), "^[0-9]+$");
    CHECK_STR(json_or(json_at(s, "/$defs/Snowflake/description"), ""), "a discord id");
    CHECK_STR(json_or(json_at(s, "/$defs/Member/properties/guild/pattern"), ""), "^[0-9]+$");
    cima_json *at = json_at(s, "/$defs/Member/properties/at");
    CHECK(json_type(at) == CIMA_JSON_OBJECT && json_len(at) == 0);
  }
}

static void heap(void) {
  Member m;
  cima_result r = CIMA_PARSE(Member, "{\"id\": \"1\", \"roles\": [\"2\"], \"guild\": 3, \"color\": \"#000000\", "
                                     "\"at\": [0, 0]}",
                             &m);
  CHECK(r.ok && m.color.some && m.color.value.text);
  // the codec gives back the text it kept, the leak checker of asan would say otherwise.
  Member_dispose(&m);
  CHECK(!m.color.some && !m.roles.items);
  cima_result_dispose(&r);
}

int main(void) {
  decoding();
  encoding();
  schemas();
  heap();
  return report_failures();
}
